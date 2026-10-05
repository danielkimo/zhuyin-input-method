#ifdef _WIN32

#include "TextService.h"

#include <Windows.h>
#include <msctf.h>

#include <algorithm>
#include <cstddef>
#include <cwctype>
#include <memory>
#include <new>
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

}  // namespace

TextService::TextService()
    : ref_count_(1),
      activate_flags_(0),
      client_id_(TF_CLIENTID_NULL),
      thread_mgr_(nullptr),
      composition_(nullptr),
      engine_loaded_(false),
      engine_attempted_(false) {
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

  if (HasCompositionState()) {
    switch (wParam) {
      case VK_BACK:
      case VK_ESCAPE:
      case VK_RETURN:
      case VK_SPACE:
        return true;
      default:
        break;
    }

    if ((wParam >= '1' && wParam <= '9') || (wParam >= VK_NUMPAD1 && wParam <= VK_NUMPAD9)) {
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
    if (wParam == VK_RETURN || wParam == VK_SPACE) {
      return HandleCommit(context);
    }
    if (wParam >= '1' && wParam <= '9') {
      return HandleCandidateSelection(context, static_cast<size_t>(wParam - '0'));
    }
    if (wParam >= VK_NUMPAD1 && wParam <= VK_NUMPAD9) {
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

bool TextService::HandleCandidateSelection(ITfContext* context,
                                           size_t one_based_index) {
  RefreshVisibleCandidates();
  if (one_based_index == 0 || one_based_index > visible_candidates_.size() ||
      pending_syllables_.empty()) {
    return true;
  }

  const zhuyin::Candidate selected = visible_candidates_[one_based_index - 1];
  if (selected.syllable_count == 0 ||
      selected.syllable_count > pending_syllables_.size()) {
    return true;
  }
  FixedSegment fixed_segment;
  fixed_segment.text = Utf8ToWide(selected.text);
  fixed_segment.syllables.assign(pending_syllables_.begin(),
                                 pending_syllables_.begin() +
                                     static_cast<std::ptrdiff_t>(selected.syllable_count));
  fixed_segments_.push_back(std::move(fixed_segment));
  pending_syllables_.erase(
      pending_syllables_.begin(),
      pending_syllables_.begin() + static_cast<std::ptrdiff_t>(selected.syllable_count));

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
  if (pending_syllables_.empty() || decoder_ == nullptr) {
    return;
  }
  visible_candidates_ = decoder_->GetCandidatesAt(pending_syllables_, 0);
  if (visible_candidates_.size() > 9) {
    visible_candidates_.resize(9);
  }
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

  ITfContextView* context_view = nullptr;
  if (FAILED(context->GetActiveView(&context_view)) || context_view == nullptr) {
    HideCandidateWindow();
    return;
  }

  RECT rect = {};
  BOOL clipped = FALSE;
  if (FAILED(context_view->GetTextExt(edit_cookie, range, &rect, &clipped))) {
    context_view->Release();
    HideCandidateWindow();
    return;
  }
  context_view->Release();

  if (clipped) {
    HideCandidateWindow();
    return;
  }

  if (!candidate_window_.EnsureCreated(g_hInstance)) {
    return;
  }
  candidate_window_.Show(rect, lines);
}

void TextService::HideCandidateWindow() { candidate_window_.Hide(); }

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

}  // namespace zhuyin::windowsime

#endif  // _WIN32
