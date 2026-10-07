#ifdef _WIN32

#include "TextService.h"

#include <Windows.h>
#include <ctffunc.h>
#include <msctf.h>

#include <algorithm>
#include <cstddef>
#include <cwctype>
#include <memory>
#include <new>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "Globals.h"

namespace zhuyin::windowsime {
namespace {

std::wstring Utf8ToWide(const std::string& utf8) {
  if (utf8.empty()) {
    return std::wstring();
  }
  const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                       static_cast<int>(utf8.size()), nullptr, 0);
  if (size <= 0) {
    return std::wstring();
  }
  std::wstring wide(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
                      wide.data(), size);
  return wide;
}

std::string WideToUtf8(const std::wstring& wide) {
  if (wide.empty()) {
    return std::string();
  }
  const int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                       static_cast<int>(wide.size()), nullptr, 0,
                                       nullptr, nullptr);
  if (size <= 0) {
    return std::string();
  }
  std::string utf8(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                      utf8.data(), size, nullptr, nullptr);
  return utf8;
}

std::wstring JoinCandidateTexts(const std::vector<zhuyin::Candidate>& candidates) {
  std::wstring joined;
  for (const auto& candidate : candidates) {
    joined += Utf8ToWide(candidate.text);
  }
  return joined;
}

std::wstring GetModuleDirectory() {
  wchar_t module_path[MAX_PATH] = {};
  GetModuleFileNameW(g_hInstance, module_path,
                     static_cast<DWORD>(_countof(module_path)));
  std::wstring path = module_path;
  const size_t slash = path.find_last_of(L"\\/");
  if (slash == std::wstring::npos) {
    return std::wstring();
  }
  return path.substr(0, slash);
}

class CompositionEditSession final : public ITfEditSession {
 public:
  CompositionEditSession(TextService* service, ITfContext* context,
                         bool commit_text, bool cancel_text)
      : ref_count_(1),
        service_(service),
        context_(context),
        commit_text_(commit_text),
        cancel_text_(cancel_text) {
    if (service_ != nullptr) {
      service_->AddRef();
    }
    if (context_ != nullptr) {
      context_->AddRef();
    }
  }

  STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) override {
    if (ppvObject == nullptr) {
      return E_INVALIDARG;
    }
    *ppvObject = nullptr;

    if (riid == IID_IUnknown || riid == IID_ITfEditSession) {
      *ppvObject = static_cast<ITfEditSession*>(this);
    } else {
      return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
  }

  STDMETHODIMP_(ULONG) AddRef() override {
    return static_cast<ULONG>(InterlockedIncrement(&ref_count_));
  }

  STDMETHODIMP_(ULONG) Release() override {
    const ULONG remaining = static_cast<ULONG>(InterlockedDecrement(&ref_count_));
    if (remaining == 0) {
      delete this;
    }
    return remaining;
  }

  STDMETHODIMP DoEditSession(TfEditCookie edit_cookie) override {
    const std::wstring text = commit_text_ ? service_->BuildCommittedText()
                                           : service_->BuildPreeditText();
    return service_->EnsureCompositionText(edit_cookie, context_, text,
                                           commit_text_, cancel_text_);
  }

 private:
  ~CompositionEditSession() {
    if (context_ != nullptr) {
      context_->Release();
    }
    if (service_ != nullptr) {
      service_->Release();
    }
  }

  LONG ref_count_;
  TextService* service_;
  ITfContext* context_;
  bool commit_text_;
  bool cancel_text_;
};

// Mirrors CompositionEditSession's pattern (call back into public TextService
// methods only) for the ITfFnReconversion::Reconvert path: reads the
// caller-supplied already-committed range and starts a fresh composition
// directly on it.
class ReconvertEditSession final : public ITfEditSession {
 public:
  ReconvertEditSession(TextService* service, ITfContext* context, ITfRange* range)
      : ref_count_(1), service_(service), context_(context), range_(range) {
    if (service_ != nullptr) {
      service_->AddRef();
    }
    if (context_ != nullptr) {
      context_->AddRef();
    }
    if (range_ != nullptr) {
      range_->AddRef();
    }
  }

  STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) override {
    if (ppvObject == nullptr) {
      return E_INVALIDARG;
    }
    *ppvObject = nullptr;

    if (riid == IID_IUnknown || riid == IID_ITfEditSession) {
      *ppvObject = static_cast<ITfEditSession*>(this);
    } else {
      return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
  }

  STDMETHODIMP_(ULONG) AddRef() override {
    return static_cast<ULONG>(InterlockedIncrement(&ref_count_));
  }

  STDMETHODIMP_(ULONG) Release() override {
    const ULONG remaining = static_cast<ULONG>(InterlockedDecrement(&ref_count_));
    if (remaining == 0) {
      delete this;
    }
    return remaining;
  }

  STDMETHODIMP DoEditSession(TfEditCookie edit_cookie) override {
    return service_->PerformReconversion(edit_cookie, context_, range_);
  }

 private:
  ~ReconvertEditSession() {
    if (range_ != nullptr) {
      range_->Release();
    }
    if (context_ != nullptr) {
      context_->Release();
    }
    if (service_ != nullptr) {
      service_->Release();
    }
  }

  LONG ref_count_;
  TextService* service_;
  ITfContext* context_;
  ITfRange* range_;
};

// Backs ITfFnReconversion::QueryRange, which (unlike Reconvert) is not handed
// an edit cookie directly -- the implementation has to open its own
// ITfEditSession to safely inspect/clone the caller-supplied range.
class QueryRangeEditSession final : public ITfEditSession {
 public:
  QueryRangeEditSession(TextService* service, ITfRange* range,
                        ITfRange** new_range, BOOL* convertible)
      : ref_count_(1),
        service_(service),
        range_(range),
        new_range_(new_range),
        convertible_(convertible) {
    if (service_ != nullptr) {
      service_->AddRef();
    }
    if (range_ != nullptr) {
      range_->AddRef();
    }
  }

  STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) override {
    if (ppvObject == nullptr) {
      return E_INVALIDARG;
    }
    *ppvObject = nullptr;

    if (riid == IID_IUnknown || riid == IID_ITfEditSession) {
      *ppvObject = static_cast<ITfEditSession*>(this);
    } else {
      return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
  }

  STDMETHODIMP_(ULONG) AddRef() override {
    return static_cast<ULONG>(InterlockedIncrement(&ref_count_));
  }

  STDMETHODIMP_(ULONG) Release() override {
    const ULONG remaining = static_cast<ULONG>(InterlockedDecrement(&ref_count_));
    if (remaining == 0) {
      delete this;
    }
    return remaining;
  }

  STDMETHODIMP DoEditSession(TfEditCookie edit_cookie) override {
    return service_->ExtendReconversionRange(edit_cookie, range_, new_range_,
                                             convertible_);
  }

 private:
  ~QueryRangeEditSession() {
    if (range_ != nullptr) {
      range_->Release();
    }
    if (service_ != nullptr) {
      service_->Release();
    }
  }

  LONG ref_count_;
  TextService* service_;
  ITfRange* range_;
  ITfRange** new_range_;
  BOOL* convertible_;
};

}  // namespace

TextService::TextService()
    : ref_count_(1),
      activate_flags_(0),
      client_id_(TF_CLIENTID_NULL),
      thread_mgr_(nullptr),
      composition_(nullptr),
      engine_loaded_(false),
      engine_attempted_(false),
      nav_segment_index_(-1),
      highlighted_candidate_index_(0),
      english_mode_(false) {
  DllAddRef();
}

TextService::~TextService() {
  HideCandidateWindow();
  if (composition_ != nullptr) {
    composition_->Release();
    composition_ = nullptr;
  }
  if (thread_mgr_ != nullptr) {
    thread_mgr_->Release();
    thread_mgr_ = nullptr;
  }
  DllRelease();
}

STDMETHODIMP TextService::QueryInterface(REFIID riid, void** ppvObject) {
  if (ppvObject == nullptr) {
    return E_INVALIDARG;
  }
  *ppvObject = nullptr;

  if (riid == IID_IUnknown || riid == IID_ITfTextInputProcessor ||
      riid == IID_ITfTextInputProcessorEx) {
    *ppvObject = static_cast<ITfTextInputProcessorEx*>(this);
  } else if (riid == IID_ITfKeyEventSink) {
    *ppvObject = static_cast<ITfKeyEventSink*>(this);
  } else if (riid == IID_ITfCompositionSink) {
    *ppvObject = static_cast<ITfCompositionSink*>(this);
  } else if (riid == IID_ITfFunctionProvider) {
    *ppvObject = static_cast<ITfFunctionProvider*>(this);
  } else if (riid == IID_ITfFnReconversion) {
    *ppvObject = static_cast<ITfFnReconversion*>(this);
  } else if (riid == IID_ITfFunction) {
    *ppvObject = static_cast<ITfFunction*>(this);
  } else {
    return E_NOINTERFACE;
  }

  AddRef();
  return S_OK;
}

STDMETHODIMP_(ULONG) TextService::AddRef() {
  return static_cast<ULONG>(InterlockedIncrement(&ref_count_));
}

STDMETHODIMP_(ULONG) TextService::Release() {
  const ULONG remaining = static_cast<ULONG>(InterlockedDecrement(&ref_count_));
  if (remaining == 0) {
    delete this;
  }
  return remaining;
}

STDMETHODIMP TextService::Activate(ITfThreadMgr* thread_mgr,
                                   TfClientId client_id) {
  return ActivateEx(thread_mgr, client_id, 0);
}

STDMETHODIMP TextService::ActivateEx(ITfThreadMgr* thread_mgr,
                                     TfClientId client_id, DWORD flags) {
  if (thread_mgr == nullptr) {
    return E_INVALIDARG;
  }

  activate_flags_ = flags;
  client_id_ = client_id;
  thread_mgr_ = thread_mgr;
  thread_mgr_->AddRef();

  return AdviseKeyEventSink();
}

STDMETHODIMP TextService::Deactivate() {
  if (thread_mgr_ != nullptr) {
    if (ITfContext* context = nullptr; SUCCEEDED(GetFocusedContext(&context))) {
      RequestEditSession(context, false, true);
      context->Release();
    }
  }

  UnadviseKeyEventSink();
  ResetState();

  if (thread_mgr_ != nullptr) {
    thread_mgr_->Release();
    thread_mgr_ = nullptr;
  }
  client_id_ = TF_CLIENTID_NULL;
  activate_flags_ = 0;
  return S_OK;
}

STDMETHODIMP TextService::OnSetFocus(BOOL foreground) {
  if (!foreground) {
    HideCandidateWindow();
  }
  return S_OK;
}

STDMETHODIMP TextService::OnTestKeyDown(ITfContext* context, WPARAM wParam,
                                        LPARAM lParam, BOOL* eaten) {
  if (eaten == nullptr) {
    return E_INVALIDARG;
  }
  *eaten = ShouldHandleKey(wParam) ? TRUE : FALSE;
  return S_OK;
}

STDMETHODIMP TextService::OnKeyDown(ITfContext* context, WPARAM wParam,
                                    LPARAM lParam, BOOL* eaten) {
  if (eaten == nullptr) {
    return E_INVALIDARG;
  }
  *eaten = HandleKeyDown(context, wParam) ? TRUE : FALSE;
  return S_OK;
}

STDMETHODIMP TextService::OnTestKeyUp(ITfContext* context, WPARAM wParam,
                                      LPARAM lParam, BOOL* eaten) {
  if (eaten == nullptr) {
    return E_INVALIDARG;
  }
  *eaten = FALSE;
  return S_OK;
}

STDMETHODIMP TextService::OnKeyUp(ITfContext* context, WPARAM wParam,
                                  LPARAM lParam, BOOL* eaten) {
  if (eaten == nullptr) {
    return E_INVALIDARG;
  }
  *eaten = FALSE;
  return S_OK;
}

STDMETHODIMP TextService::OnPreservedKey(ITfContext* context, REFGUID guid,
                                         BOOL* eaten) {
  if (eaten == nullptr) {
    return E_INVALIDARG;
  }
  *eaten = FALSE;
  return S_OK;
}

STDMETHODIMP TextService::OnReleaseContext(ITfContext* context) {
  return S_OK;
}

STDMETHODIMP TextService::OnCompositionTerminated(TfEditCookie edit_cookie,
                                                  ITfComposition* composition) {
  if (composition_ != nullptr) {
    composition_->Release();
    composition_ = nullptr;
  }
  ResetState();
  return S_OK;
}

HRESULT TextService::EnsureCompositionText(TfEditCookie edit_cookie,
                                           ITfContext* context,
                                           const std::wstring& text,
                                           bool commit_text, bool cancel) {
  if (context == nullptr) {
    return E_INVALIDARG;
  }

  if (cancel) {
    if (composition_ != nullptr) {
      ITfRange* range = nullptr;
      if (SUCCEEDED(composition_->GetRange(&range)) && range != nullptr) {
        range->SetText(edit_cookie, 0, L"", 0);
        range->Release();
      }
      ITfComposition* active_composition = composition_;
      composition_ = nullptr;
      active_composition->EndComposition(edit_cookie);
      active_composition->Release();
    }
    HideCandidateWindow();
    return S_OK;
  }

  if (text.empty() && !commit_text) {
    if (composition_ != nullptr) {
      ITfRange* range = nullptr;
      if (SUCCEEDED(composition_->GetRange(&range)) && range != nullptr) {
        range->SetText(edit_cookie, 0, L"", 0);
        range->Release();
      }
      ITfComposition* active_composition = composition_;
      composition_ = nullptr;
      active_composition->EndComposition(edit_cookie);
      active_composition->Release();
    }
    HideCandidateWindow();
    return S_OK;
  }

  ITfRange* range = nullptr;
  HRESULT hr = EnsureCompositionRange(edit_cookie, context, &range);
  if (FAILED(hr)) {
    return hr;
  }

  hr = range->SetText(edit_cookie, 0, text.c_str(), static_cast<LONG>(text.size()));
  if (SUCCEEDED(hr)) {
    UpdateCandidateWindow(edit_cookie, context, range);
  }

  if (commit_text && composition_ != nullptr) {
    ITfComposition* active_composition = composition_;
    composition_ = nullptr;
    active_composition->EndComposition(edit_cookie);
    active_composition->Release();
    HideCandidateWindow();
  }

  range->Release();
  return hr;
}

std::wstring TextService::BuildPreeditText() const {
  std::wstring text;
  for (const auto& fixed : fixed_segments_) {
    text += fixed.text;
  }
  text += BuildDecodedPendingText();
  text += Utf8ToWide(syllable_composer_.Display());
  return text;
}

std::wstring TextService::BuildCommittedText() const {
  std::wstring text;
  for (const auto& fixed : fixed_segments_) {
    text += fixed.text;
  }
  text += BuildDecodedPendingText();
  if (!syllable_composer_.Empty()) {
    text += Utf8ToWide(syllable_composer_.Display());
  }
  return text;
}

void TextService::ResetState() {
  syllable_composer_.Clear();
  pending_syllables_.clear();
  fixed_segments_.clear();
  visible_candidates_.clear();
  nav_segment_index_ = -1;
  highlighted_candidate_index_ = 0;
  HideCandidateWindow();
}

HRESULT TextService::AdviseKeyEventSink() {
  if (thread_mgr_ == nullptr) {
    return E_UNEXPECTED;
  }

  ITfKeystrokeMgr* keystroke_mgr = nullptr;
  HRESULT hr = thread_mgr_->QueryInterface(IID_ITfKeystrokeMgr,
                                           reinterpret_cast<void**>(&keystroke_mgr));
  if (FAILED(hr)) {
    return hr;
  }

  hr = keystroke_mgr->AdviseKeyEventSink(client_id_, this, TRUE);
  keystroke_mgr->Release();
  return hr;
}

void TextService::UnadviseKeyEventSink() {
  if (thread_mgr_ == nullptr) {
    return;
  }

  ITfKeystrokeMgr* keystroke_mgr = nullptr;
  if (SUCCEEDED(thread_mgr_->QueryInterface(IID_ITfKeystrokeMgr,
                                            reinterpret_cast<void**>(&keystroke_mgr)))) {
    keystroke_mgr->UnadviseKeyEventSink(client_id_);
    keystroke_mgr->Release();
  }
}

HRESULT TextService::EnsureEngineLoaded() {
  if (engine_attempted_) {
    return engine_loaded_ ? S_OK : HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
  }
  engine_attempted_ = true;

  const std::wstring module_dir = GetModuleDirectory();
  const std::wstring candidates[] = {
      module_dir + L"\\dictionary.tsv",
      module_dir + L"\\data\\dictionary.tsv",
      module_dir + L"\\..\\share\\zhuyin\\dictionary.tsv",
  };

  for (const auto& path : candidates) {
    if (path.empty()) {
      continue;
    }
    if (dictionary_.LoadFromFile(WideToUtf8(path))) {
      decoder_ = std::make_unique<zhuyin::Decoder>(dictionary_);
      engine_loaded_ = true;
      return S_OK;
    }
  }

  return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
}

HRESULT TextService::GetFocusedContext(ITfContext** context) const {
  if (context == nullptr) {
    return E_INVALIDARG;
  }
  *context = nullptr;

  if (thread_mgr_ == nullptr) {
    return E_UNEXPECTED;
  }

  ITfDocumentMgr* document_mgr = nullptr;
  HRESULT hr = thread_mgr_->GetFocus(&document_mgr);
  if (FAILED(hr) || document_mgr == nullptr) {
    return FAILED(hr) ? hr : E_FAIL;
  }

  hr = document_mgr->GetTop(context);
  document_mgr->Release();
  return hr;
}

bool TextService::HasCompositionState() const {
  return !fixed_segments_.empty() || !pending_syllables_.empty() ||
         !syllable_composer_.Empty() || composition_ != nullptr;
}

bool TextService::ShouldHandleKey(WPARAM wParam) const {
  if (HasUnsupportedModifierState()) {
    return false;
  }

  // Dedicated Chinese/English toggle key. Microsoft New Phonetic uses Shift
  // for this, but a bare Shift press is easy to trigger by accident (e.g.
  // while reaching for a capital letter or symbol), so this IME uses the
  // backtick/grave key instead -- it isn't mapped to any Bopomofo symbol, so
  // there's no ambiguity with normal typing.
  if (wParam == VK_OEM_3) {
    return true;
  }

  if (english_mode_) {
    return false;
  }

  if (HasCompositionState()) {
    switch (wParam) {
      case VK_BACK:
      case VK_ESCAPE:
      case VK_RETURN:
      case VK_SPACE:
      case VK_LEFT:
      case VK_RIGHT:
        return true;
      default:
        break;
    }

    // Up/Down move the highlighted row in the candidate popup (like
    // Microsoft New Phonetic); only meaningful while candidates are showing.
    if ((wParam == VK_UP || wParam == VK_DOWN) && !visible_candidates_.empty()) {
      return true;
    }

    // Digits double as both candidate-selection shortcuts and Bopomofo
    // symbol/tone keys on the Dachen layout. Only treat them as candidate
    // selection once the current syllable is fully composed (no in-progress
    // initial/medial/final waiting for its tone); otherwise let them fall
    // through to the normal mapped-key handling below so e.g. a tone key
    // typed mid-syllable isn't swallowed with no effect.
    if (syllable_composer_.Empty() &&
        ((wParam >= '1' && wParam <= '9') ||
         (wParam >= VK_NUMPAD1 && wParam <= VK_NUMPAD9))) {
      return true;
    }
  }

  return ShouldInterceptMappedKey(wParam);
}

bool TextService::ShouldInterceptMappedKey(WPARAM wParam) const {
  if (!HasCompositionState() && ShouldPassThroughAsLiteralPunctuation(wParam)) {
    return false;
  }
  return VirtualKeyToAscii(wParam) != '\0';
}

bool TextService::ShouldPassThroughAsLiteralPunctuation(WPARAM wParam) const {
  // Minimal punctuation fallback: when no Zhuyin composition is active, let a
  // couple of common punctuation keys insert literal ASCII characters. A more
  // complete symbol table can be layered on later without changing the core
  // TSF composition flow.
  return wParam == VK_OEM_PERIOD || wParam == VK_OEM_COMMA;
}

bool TextService::HasUnsupportedModifierState() const {
  return (GetKeyState(VK_CONTROL) & 0x8000) != 0 ||
         (GetKeyState(VK_MENU) & 0x8000) != 0 ||
         (GetKeyState(VK_LWIN) & 0x8000) != 0 ||
         (GetKeyState(VK_RWIN) & 0x8000) != 0 ||
         (GetKeyState(VK_SHIFT) & 0x8000) != 0;
}

bool TextService::HandleKeyDown(ITfContext* context, WPARAM wParam) {
  if (context == nullptr || HasUnsupportedModifierState()) {
    return false;
  }

  if (wParam == VK_OEM_3) {
    return HandleToggleEnglishMode(context);
  }

  if (english_mode_) {
    return false;
  }

  if (!HasCompositionState() && ShouldPassThroughAsLiteralPunctuation(wParam)) {
    return false;
  }

  if (HasCompositionState()) {
    if (wParam == VK_BACK) {
      return HandleBackspace(context);
    }
    if (wParam == VK_ESCAPE) {
      return HandleEscape(context);
    }
    if (wParam == VK_RETURN) {
      // If the user has navigated the candidate popup with Up/Down, Enter
      // confirms whichever row is highlighted (matching Microsoft New
      // Phonetic); otherwise it commits the whole composed phrase as-is.
      if (!visible_candidates_.empty() && highlighted_candidate_index_ != 0) {
        return HandleCandidateSelection(
            context, highlighted_candidate_index_ + 1);
      }
      return HandleCommit(context);
    }
    if (wParam == VK_SPACE) {
      // Space is overloaded: while a syllable is still being composed (no
      // tone picked yet), it is the explicit first-tone symbol and should
      // just finish that syllable so typing can continue into a longer
      // phrase (e.g. ㄕㄨ + space -> 輸, then continue with ㄖㄨˋ ㄈㄚˇ to
      // get 輸入法 instead of committing "書" to the document early).
      // Only once there is no partial syllable left does space fall back to
      // committing the whole composed phrase, matching Enter.
      if (!syllable_composer_.Empty()) {
        const char ascii_key = VirtualKeyToAscii(wParam);
        return ascii_key != '\0' && HandleMappedSymbolKey(context, ascii_key);
      }
      if (!visible_candidates_.empty() && highlighted_candidate_index_ != 0) {
        return HandleCandidateSelection(
            context, highlighted_candidate_index_ + 1);
      }
      return HandleCommit(context);
    }
    if (wParam == VK_LEFT) {
      return HandleArrowKey(context, false);
    }
    if (wParam == VK_RIGHT) {
      return HandleArrowKey(context, true);
    }
    if (wParam == VK_UP) {
      return HandleCandidateHighlightKey(context, false);
    }
    if (wParam == VK_DOWN) {
      return HandleCandidateHighlightKey(context, true);
    }
    if (syllable_composer_.Empty() && wParam >= '1' && wParam <= '9') {
      return HandleCandidateSelection(context, static_cast<size_t>(wParam - '0'));
    }
    if (syllable_composer_.Empty() && wParam >= VK_NUMPAD1 && wParam <= VK_NUMPAD9) {
      return HandleCandidateSelection(context,
                                      static_cast<size_t>(wParam - VK_NUMPAD0));
    }
  }

  const char ascii_key = VirtualKeyToAscii(wParam);
  if (ascii_key == '\0') {
    return false;
  }
  return HandleMappedSymbolKey(context, ascii_key);
}

bool TextService::HandleMappedSymbolKey(ITfContext* context, char ascii_key) {
  const std::string symbol = zhuyin::BopomofoKeyboard::KeyToSymbol(ascii_key);
  if (symbol.empty()) {
    return false;
  }

  EnsureEngineLoaded();
  nav_segment_index_ = -1;  // typing exits segment-navigation mode

  if (syllable_composer_.HasFinal() && zhuyin::BopomofoKeyboard::IsInitialSymbol(symbol)) {
    CommitCurrentSyllable();
  }

  const auto result = syllable_composer_.AddSymbol(symbol);
  if (result == zhuyin::SyllableComposer::AddResult::kRejected) {
    if (syllable_composer_.HasFinal() && zhuyin::BopomofoKeyboard::IsInitialSymbol(symbol)) {
      CommitCurrentSyllable();
      if (syllable_composer_.AddSymbol(symbol) ==
          zhuyin::SyllableComposer::AddResult::kRejected) {
        return false;
      }
    } else {
      return false;
    }
  }

  if (zhuyin::BopomofoKeyboard::IsToneSymbol(symbol) && syllable_composer_.HasFinal()) {
    CommitCurrentSyllable();
  }

  RefreshVisibleCandidates();
  return SUCCEEDED(RequestEditSession(context, false, false));
}

bool TextService::HandleBackspace(ITfContext* context) {
  nav_segment_index_ = -1;  // typing/deleting exits segment-navigation mode
  bool changed = false;
  if (!syllable_composer_.Empty()) {
    changed = syllable_composer_.BackspaceSymbol();
  } else if (!pending_syllables_.empty()) {
    pending_syllables_.pop_back();
    changed = true;
  } else {
    changed = RestorePreviousFixedSegment();
  }

  if (!changed) {
    return false;
  }

  RefreshVisibleCandidates();
  if (!HasCompositionState()) {
    ResetState();
    return SUCCEEDED(RequestEditSession(context, false, true));
  }
  return SUCCEEDED(RequestEditSession(context, false, false));
}

bool TextService::HandleCommit(ITfContext* context) {
  if (syllable_composer_.HasFinal()) {
    CommitCurrentSyllable();
  }

  const std::wstring commit_text = BuildCommittedText();
  if (commit_text.empty()) {
    return false;
  }

  const HRESULT hr = RequestEditSession(context, true, false);
  if (SUCCEEDED(hr)) {
    ResetState();
  }
  return SUCCEEDED(hr);
}

bool TextService::HandleEscape(ITfContext* context) {
  const HRESULT hr = RequestEditSession(context, false, true);
  ResetState();
  return SUCCEEDED(hr);
}

bool TextService::HandleToggleEnglishMode(ITfContext* context) {
  // Commit (or discard, if nothing decodable yet) any in-progress Zhuyin
  // composition before switching modes, the same way switching away from
  // the IME entirely would behave.
  if (HasCompositionState()) {
    if (syllable_composer_.HasFinal() || !pending_syllables_.empty() ||
        !fixed_segments_.empty()) {
      HandleCommit(context);
    }
    if (HasCompositionState()) {
      RequestEditSession(context, false, true);
      ResetState();
    }
  }

  english_mode_ = !english_mode_;
  return true;
}

bool TextService::HandleArrowKey(ITfContext* context, bool move_right) {
  if (!syllable_composer_.Empty()) {
    if (syllable_composer_.HasFinal()) {
      CommitCurrentSyllable();
    } else {
      // An incomplete syllable is still being typed; let the keystroke fall
      // through rather than starting navigation over a partial segment.
      return false;
    }
  }

  const std::vector<LogicalSegment> segments = BuildLogicalSegments();
  if (segments.empty()) {
    return false;
  }

  if (!move_right) {
    if (nav_segment_index_ < 0) {
      nav_segment_index_ = static_cast<int>(segments.size()) - 1;
    } else if (nav_segment_index_ > 0) {
      --nav_segment_index_;
    }
  } else {
    if (nav_segment_index_ < 0) {
      return false;
    }
    if (nav_segment_index_ < static_cast<int>(segments.size()) - 1) {
      ++nav_segment_index_;
    } else {
      nav_segment_index_ = -1;  // moved past the last segment; back to normal typing
    }
  }

  RefreshVisibleCandidates();
  return SUCCEEDED(RequestEditSession(context, false, false));
}

bool TextService::HandleCandidateHighlightKey(ITfContext* context,
                                              bool move_down) {
  if (visible_candidates_.empty()) {
    return false;
  }

  const size_t count = visible_candidates_.size();
  if (move_down) {
    highlighted_candidate_index_ = (highlighted_candidate_index_ + 1) % count;
  } else {
    highlighted_candidate_index_ =
        (highlighted_candidate_index_ == 0) ? count - 1
                                            : highlighted_candidate_index_ - 1;
  }

  // Only the popup's highlight changes; the composed text itself is
  // untouched until the user actually confirms a candidate, so just redraw.
  return SUCCEEDED(RequestEditSession(context, false, false));
}

bool TextService::HandleCandidateSelection(ITfContext* context,
                                           size_t one_based_index) {
  RefreshVisibleCandidates();
  if (one_based_index == 0 || one_based_index > visible_candidates_.size()) {
    return true;
  }
  const zhuyin::Candidate selected = visible_candidates_[one_based_index - 1];

  if (nav_segment_index_ < 0) {
    if (pending_syllables_.empty() || selected.syllable_count == 0 ||
        selected.syllable_count > pending_syllables_.size()) {
      return true;
    }
    FixedSegment fixed_segment;
    fixed_segment.text = Utf8ToWide(selected.text);
    fixed_segment.syllables.assign(
        pending_syllables_.begin(),
        pending_syllables_.begin() +
            static_cast<std::ptrdiff_t>(selected.syllable_count));
    fixed_segments_.push_back(std::move(fixed_segment));
    pending_syllables_.erase(pending_syllables_.begin(),
                             pending_syllables_.begin() +
                                 static_cast<std::ptrdiff_t>(selected.syllable_count));
  } else {
    // Reselecting a specific logical segment while navigating: lock in every
    // segment up to and including the targeted one (converting any
    // auto-decoded segments before it into fixed segments too, so the
    // targeted choice doesn't get re-decoded differently next refresh), and
    // re-slice whatever syllables remain after it back into pending_syllables_.
    const std::vector<LogicalSegment> segments = BuildLogicalSegments();
    if (nav_segment_index_ >= static_cast<int>(segments.size())) {
      nav_segment_index_ = -1;
      RefreshVisibleCandidates();
      return true;
    }
    const std::vector<std::string> all_syllables = AllSyllables();
    const size_t offset = SegmentSyllableOffset(segments, nav_segment_index_);
    if (selected.syllable_count == 0 ||
        offset + selected.syllable_count > all_syllables.size()) {
      return true;
    }

    std::vector<FixedSegment> new_fixed;
    new_fixed.reserve(static_cast<size_t>(nav_segment_index_) + 1);
    for (int i = 0; i < nav_segment_index_; ++i) {
      new_fixed.push_back({segments[i].text, segments[i].syllables});
    }
    FixedSegment target;
    target.text = Utf8ToWide(selected.text);
    target.syllables.assign(
        all_syllables.begin() + static_cast<std::ptrdiff_t>(offset),
        all_syllables.begin() +
            static_cast<std::ptrdiff_t>(offset + selected.syllable_count));
    new_fixed.push_back(std::move(target));

    std::vector<std::string> remaining(
        all_syllables.begin() +
            static_cast<std::ptrdiff_t>(offset + selected.syllable_count),
        all_syllables.end());

    fixed_segments_ = std::move(new_fixed);
    pending_syllables_ = std::move(remaining);
    nav_segment_index_ = -1;  // back to normal append mode after reselection
  }

  RefreshVisibleCandidates();
  return SUCCEEDED(RequestEditSession(context, false, false));
}

bool TextService::CommitCurrentSyllable() {
  if (!syllable_composer_.HasFinal()) {
    return false;
  }
  pending_syllables_.push_back(syllable_composer_.Reading());
  syllable_composer_.Clear();
  return true;
}

bool TextService::RestorePreviousFixedSegment() {
  if (fixed_segments_.empty()) {
    return false;
  }
  FixedSegment segment = fixed_segments_.back();
  fixed_segments_.pop_back();
  pending_syllables_.insert(pending_syllables_.begin(), segment.syllables.begin(),
                            segment.syllables.end());
  return true;
}

void TextService::RefreshVisibleCandidates() {
  visible_candidates_.clear();
  highlighted_candidate_index_ = 0;
  if (decoder_ == nullptr) {
    return;
  }

  if (nav_segment_index_ < 0) {
    if (pending_syllables_.empty()) {
      return;
    }
    visible_candidates_ = decoder_->GetCandidatesAt(pending_syllables_, 0);
  } else {
    const std::vector<LogicalSegment> segments = BuildLogicalSegments();
    if (segments.empty() || nav_segment_index_ >= static_cast<int>(segments.size())) {
      nav_segment_index_ = -1;
      return;
    }
    const std::vector<std::string> all_syllables = AllSyllables();
    const size_t offset = SegmentSyllableOffset(segments, nav_segment_index_);
    if (offset >= all_syllables.size()) {
      nav_segment_index_ = -1;
      return;
    }
    visible_candidates_ = decoder_->GetCandidatesAt(all_syllables, offset);
  }

  if (visible_candidates_.size() > 9) {
    visible_candidates_.resize(9);
  }
}

std::vector<std::string> TextService::AllSyllables() const {
  std::vector<std::string> all;
  for (const auto& fixed : fixed_segments_) {
    all.insert(all.end(), fixed.syllables.begin(), fixed.syllables.end());
  }
  all.insert(all.end(), pending_syllables_.begin(), pending_syllables_.end());
  return all;
}

std::vector<TextService::LogicalSegment> TextService::BuildLogicalSegments() const {
  std::vector<LogicalSegment> segments;
  segments.reserve(fixed_segments_.size() + pending_syllables_.size());
  for (const auto& fixed : fixed_segments_) {
    segments.push_back({fixed.text, fixed.syllables});
  }

  if (pending_syllables_.empty()) {
    return segments;
  }

  if (decoder_ == nullptr) {
    for (const auto& syllable : pending_syllables_) {
      segments.push_back({Utf8ToWide(syllable), {syllable}});
    }
    return segments;
  }

  const std::vector<zhuyin::Candidate> sentence =
      decoder_->ComposeBestSentence(pending_syllables_);
  size_t offset = 0;
  for (const auto& candidate : sentence) {
    size_t count = candidate.syllable_count;
    if (count == 0 || offset + count > pending_syllables_.size()) {
      // Defensive fallback in case the decoder ever returns an inconsistent
      // syllable count; fall back to a single-syllable segment so navigation
      // never reads out of bounds.
      count = 1;
    }
    count = std::min(count, pending_syllables_.size() - offset);
    LogicalSegment segment;
    segment.text = Utf8ToWide(candidate.text);
    segment.syllables.assign(pending_syllables_.begin() + static_cast<std::ptrdiff_t>(offset),
                             pending_syllables_.begin() +
                                 static_cast<std::ptrdiff_t>(offset + count));
    segments.push_back(std::move(segment));
    offset += count;
  }
  return segments;
}

size_t TextService::SegmentSyllableOffset(const std::vector<LogicalSegment>& segments,
                                          int segment_index) const {
  size_t offset = 0;
  for (int i = 0; i < segment_index && i < static_cast<int>(segments.size()); ++i) {
    offset += segments[i].syllables.size();
  }
  return offset;
}

std::wstring TextService::BuildDecodedPendingText() const {
  if (pending_syllables_.empty()) {
    return std::wstring();
  }
  if (decoder_ == nullptr) {
    std::wstring readings;
    for (const auto& syllable : pending_syllables_) {
      readings += Utf8ToWide(syllable);
    }
    return readings;
  }
  return JoinCandidateTexts(decoder_->ComposeBestSentence(pending_syllables_));
}

HRESULT TextService::RequestEditSession(ITfContext* context, bool commit_text,
                                        bool cancel_text) {
  if (context == nullptr) {
    return E_INVALIDARG;
  }

  auto* edit_session = new (std::nothrow)
      CompositionEditSession(this, context, commit_text, cancel_text);
  if (edit_session == nullptr) {
    return E_OUTOFMEMORY;
  }

  HRESULT session_hr = E_FAIL;
  HRESULT hr = context->RequestEditSession(client_id_, edit_session,
                                           TF_ES_SYNC | TF_ES_READWRITE,
                                           &session_hr);
  edit_session->Release();
  if (FAILED(hr)) {
    return hr;
  }
  return session_hr;
}

HRESULT TextService::EnsureCompositionRange(TfEditCookie edit_cookie,
                                            ITfContext* context,
                                            ITfRange** range_out) {
  if (range_out == nullptr) {
    return E_INVALIDARG;
  }
  *range_out = nullptr;

  HRESULT hr = S_OK;
  if (composition_ == nullptr) {
    ITfInsertAtSelection* insert_at_selection = nullptr;
    hr = context->QueryInterface(IID_ITfInsertAtSelection,
                                 reinterpret_cast<void**>(&insert_at_selection));
    if (FAILED(hr)) {
      return hr;
    }

    ITfRange* insertion_range = nullptr;
    hr = insert_at_selection->InsertTextAtSelection(edit_cookie, TF_IAS_QUERYONLY,
                                                    L"", 0, &insertion_range);
    insert_at_selection->Release();
    if (FAILED(hr)) {
      return hr;
    }

    ITfContextComposition* context_composition = nullptr;
    hr = context->QueryInterface(IID_ITfContextComposition,
                                 reinterpret_cast<void**>(&context_composition));
    if (FAILED(hr)) {
      insertion_range->Release();
      return hr;
    }

    hr = context_composition->StartComposition(edit_cookie, insertion_range, this,
                                               &composition_);
    context_composition->Release();
    insertion_range->Release();
    if (FAILED(hr)) {
      return hr;
    }
  }

  return composition_->GetRange(range_out);
}

void TextService::UpdateCandidateWindow(TfEditCookie edit_cookie,
                                        ITfContext* context, ITfRange* range) {
  if (range == nullptr || visible_candidates_.empty()) {
    HideCandidateWindow();
    return;
  }

  std::vector<std::wstring> lines;
  lines.reserve(visible_candidates_.size());
  for (size_t i = 0; i < visible_candidates_.size(); ++i) {
    lines.push_back(std::to_wstring(i + 1) + L". " +
                    Utf8ToWide(visible_candidates_[i].text));
  }

  // While navigating a specific logical segment (Left/Right arrows), position
  // the popup over just that segment instead of the whole composition.
  ITfRange* measure_range = range;
  ITfRange* segment_range = nullptr;
  size_t char_offset = 0;
  size_t char_length = 0;
  if (nav_segment_index_ >= 0 &&
      ComputeNavSegmentCharRange(&char_offset, &char_length) && char_length > 0) {
    if (SUCCEEDED(range->Clone(&segment_range)) && segment_range != nullptr) {
      segment_range->Collapse(edit_cookie, TF_ANCHOR_START);
      LONG shifted = 0;
      segment_range->ShiftEnd(edit_cookie,
                             static_cast<LONG>(char_offset + char_length), &shifted,
                             nullptr);
      segment_range->ShiftStart(edit_cookie, static_cast<LONG>(char_offset), &shifted,
                                nullptr);
      measure_range = segment_range;
    }
  }

  ITfContextView* context_view = nullptr;
  if (FAILED(context->GetActiveView(&context_view)) || context_view == nullptr) {
    if (segment_range != nullptr) {
      segment_range->Release();
    }
    HideCandidateWindow();
    return;
  }

  RECT rect = {};
  BOOL clipped = FALSE;
  const HRESULT hr = context_view->GetTextExt(edit_cookie, measure_range, &rect, &clipped);
  context_view->Release();
  if (segment_range != nullptr) {
    segment_range->Release();
  }
  if (FAILED(hr)) {
    HideCandidateWindow();
    return;
  }

  if (clipped) {
    HideCandidateWindow();
    return;
  }

  if (!candidate_window_.EnsureCreated(g_hInstance)) {
    return;
  }
  candidate_window_.Show(rect, lines, highlighted_candidate_index_);
}

void TextService::HideCandidateWindow() { candidate_window_.Hide(); }

bool TextService::ComputeNavSegmentCharRange(size_t* offset, size_t* length) const {
  if (nav_segment_index_ < 0) {
    return false;
  }
  const std::vector<LogicalSegment> segments = BuildLogicalSegments();
  if (nav_segment_index_ >= static_cast<int>(segments.size())) {
    return false;
  }
  size_t char_offset = 0;
  for (int i = 0; i < nav_segment_index_; ++i) {
    char_offset += segments[i].text.size();
  }
  if (offset != nullptr) {
    *offset = char_offset;
  }
  if (length != nullptr) {
    *length = segments[nav_segment_index_].text.size();
  }
  return true;
}

char TextService::VirtualKeyToAscii(WPARAM wParam) const {
  if (wParam >= 'A' && wParam <= 'Z') {
    return static_cast<char>(std::towlower(static_cast<wchar_t>(wParam)));
  }
  if (wParam >= '0' && wParam <= '9') {
    return static_cast<char>(wParam);
  }
  switch (wParam) {
    case VK_OEM_COMMA:
      return ',';
    case VK_OEM_PERIOD:
      return '.';
    case VK_OEM_1:
      return ';';
    case VK_OEM_2:
      return '/';
    case VK_OEM_MINUS:
      return '-';
    default:
      return '\0';
  }
}

STDMETHODIMP TextService::GetType(GUID* guid) {
  if (guid == nullptr) {
    return E_INVALIDARG;
  }
  *guid = kTextServiceClsid;
  return S_OK;
}

STDMETHODIMP TextService::GetDescription(BSTR* description) {
  if (description == nullptr) {
    return E_INVALIDARG;
  }
  *description = SysAllocString(kTextServiceDescription);
  return (*description != nullptr) ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP TextService::GetFunction(REFGUID /*guid_service*/, REFIID riid,
                                     IUnknown** function) {
  if (function == nullptr) {
    return E_INVALIDARG;
  }
  *function = nullptr;
  if (riid != IID_ITfFnReconversion && riid != IID_ITfFunction) {
    return E_NOINTERFACE;
  }
  return QueryInterface(riid, reinterpret_cast<void**>(function));
}

STDMETHODIMP TextService::GetDisplayName(BSTR* name) {
  if (name == nullptr) {
    return E_INVALIDARG;
  }
  *name = SysAllocString(kTextServiceDisplayName);
  return (*name != nullptr) ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP TextService::QueryRange(ITfRange* range, ITfRange** new_range,
                                    BOOL* convertible) {
  if (range == nullptr || new_range == nullptr || convertible == nullptr) {
    return E_INVALIDARG;
  }
  *new_range = nullptr;
  *convertible = FALSE;

  ITfContext* context = nullptr;
  HRESULT hr = range->GetContext(&context);
  if (FAILED(hr) || context == nullptr) {
    return FAILED(hr) ? hr : E_FAIL;
  }

  auto* edit_session = new (std::nothrow) QueryRangeEditSession(
      this, range, new_range, convertible);
  if (edit_session == nullptr) {
    context->Release();
    return E_OUTOFMEMORY;
  }

  HRESULT session_hr = E_FAIL;
  hr = context->RequestEditSession(client_id_, edit_session,
                                   TF_ES_SYNC | TF_ES_READ, &session_hr);
  edit_session->Release();
  context->Release();
  if (FAILED(hr)) {
    return hr;
  }
  return session_hr;
}

STDMETHODIMP TextService::GetReconversion(ITfRange* /*range*/,
                                         ITfCandidateList** candidate_list) {
  // Only Reconvert() (invoked by the OS's built-in "Reconversion" command) is
  // implemented; a host that instead drives its own candidate UI via this
  // alternate path isn't supported yet.
  if (candidate_list != nullptr) {
    *candidate_list = nullptr;
  }
  return E_NOTIMPL;
}

STDMETHODIMP TextService::Reconvert(ITfRange* range) {
  if (range == nullptr) {
    return E_INVALIDARG;
  }

  ITfContext* context = nullptr;
  HRESULT hr = range->GetContext(&context);
  if (FAILED(hr) || context == nullptr) {
    return FAILED(hr) ? hr : E_FAIL;
  }

  auto* edit_session = new (std::nothrow) ReconvertEditSession(this, context, range);
  if (edit_session == nullptr) {
    context->Release();
    return E_OUTOFMEMORY;
  }

  HRESULT session_hr = E_FAIL;
  hr = context->RequestEditSession(client_id_, edit_session,
                                   TF_ES_SYNC | TF_ES_READWRITE, &session_hr);
  edit_session->Release();
  context->Release();
  if (FAILED(hr)) {
    return hr;
  }
  return session_hr;
}

HRESULT TextService::ExtendReconversionRange(TfEditCookie edit_cookie,
                                             ITfRange* range,
                                             ITfRange** new_range,
                                             BOOL* convertible) {
  if (range == nullptr || new_range == nullptr || convertible == nullptr) {
    return E_INVALIDARG;
  }
  *new_range = nullptr;
  *convertible = FALSE;

  EnsureEngineLoaded();

  ITfRange* clone = nullptr;
  HRESULT hr = range->Clone(&clone);
  if (FAILED(hr) || clone == nullptr) {
    return FAILED(hr) ? hr : E_FAIL;
  }

  BOOL is_empty = FALSE;
  hr = clone->IsEmpty(edit_cookie, &is_empty);
  if (FAILED(hr)) {
    clone->Release();
    return hr;
  }

  if (is_empty) {
    // No selection: offer the single character immediately before an empty
    // caret, matching the New Phonetic "reconvert the last character" UX.
    LONG shifted = 0;
    hr = clone->ShiftStart(edit_cookie, -1, &shifted, nullptr);
    if (FAILED(hr) || shifted == 0) {
      clone->Release();
      return S_OK;  // nothing to the left of the caret; *convertible stays FALSE
    }
  }

  WCHAR buffer[256];
  ULONG fetched = 0;
  hr = clone->GetText(edit_cookie, 0, buffer, _countof(buffer) - 1, &fetched);
  if (FAILED(hr) || fetched == 0) {
    clone->Release();
    return S_OK;
  }
  buffer[fetched] = L'\0';

  if (dictionary_.ReadingsForText(WideToUtf8(std::wstring(buffer, fetched))).empty()) {
    // Not a phrase/character our dictionary recognizes a reading for; report
    // not-convertible rather than offering a reconversion we can't fulfill.
    clone->Release();
    return S_OK;
  }

  *new_range = clone;  // ownership transferred to the caller
  *convertible = TRUE;
  return S_OK;
}

HRESULT TextService::PerformReconversion(TfEditCookie edit_cookie,
                                         ITfContext* context, ITfRange* range) {
  if (context == nullptr || range == nullptr) {
    return E_INVALIDARG;
  }

  EnsureEngineLoaded();
  if (decoder_ == nullptr) {
    return E_FAIL;
  }

  WCHAR buffer[256];
  ULONG fetched = 0;
  HRESULT hr = range->GetText(edit_cookie, 0, buffer, _countof(buffer) - 1, &fetched);
  if (FAILED(hr) || fetched == 0) {
    return FAILED(hr) ? hr : E_FAIL;
  }
  buffer[fetched] = L'\0';
  const std::string text = WideToUtf8(std::wstring(buffer, fetched));

  const std::vector<std::string> readings = dictionary_.ReadingsForText(text);
  if (readings.empty()) {
    return E_FAIL;
  }

  // Multiple readings are possible for an ambiguous polyphone; the first
  // (by the dictionary's own sort order) is used to seed candidates --
  // the user can still pick a different homophone from the reopened list.
  std::vector<std::string> syllables;
  std::istringstream reading_stream(readings.front());
  std::string syllable;
  while (reading_stream >> syllable) {
    syllables.push_back(syllable);
  }
  if (syllables.empty()) {
    return E_FAIL;
  }

  // Abandon any in-progress typing composition state; reconversion always
  // starts a fresh composition directly on the caller-supplied range.
  ResetState();
  pending_syllables_ = std::move(syllables);

  ITfContextComposition* context_composition = nullptr;
  hr = context->QueryInterface(IID_ITfContextComposition,
                               reinterpret_cast<void**>(&context_composition));
  if (FAILED(hr)) {
    pending_syllables_.clear();
    return hr;
  }

  hr = context_composition->StartComposition(edit_cookie, range, this, &composition_);
  context_composition->Release();
  if (FAILED(hr)) {
    pending_syllables_.clear();
    return hr;
  }

  RefreshVisibleCandidates();
  UpdateCandidateWindow(edit_cookie, context, range);
  return S_OK;
}

}  // namespace zhuyin::windowsime

#endif  // _WIN32
