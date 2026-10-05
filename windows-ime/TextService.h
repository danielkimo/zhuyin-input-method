#pragma once

#ifdef _WIN32

#include <Windows.h>
#include <msctf.h>

#include <memory>
#include <string>
#include <vector>

#include "CandidateWindow.h"
#include <zhuyin/engine.h>

namespace zhuyin::windowsime {

class TextService final : public ITfTextInputProcessorEx,
                          public ITfKeyEventSink,
                          public ITfCompositionSink {
 public:
  TextService();

  STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) override;
  STDMETHODIMP_(ULONG) AddRef() override;
  STDMETHODIMP_(ULONG) Release() override;

  STDMETHODIMP Activate(ITfThreadMgr* thread_mgr, TfClientId client_id) override;
  STDMETHODIMP ActivateEx(ITfThreadMgr* thread_mgr, TfClientId client_id,
                          DWORD flags) override;
  STDMETHODIMP Deactivate() override;

  STDMETHODIMP OnSetFocus(BOOL foreground) override;
  STDMETHODIMP OnTestKeyDown(ITfContext* context, WPARAM wParam, LPARAM lParam,
                             BOOL* eaten) override;
  STDMETHODIMP OnKeyDown(ITfContext* context, WPARAM wParam, LPARAM lParam,
                         BOOL* eaten) override;
  STDMETHODIMP OnTestKeyUp(ITfContext* context, WPARAM wParam, LPARAM lParam,
                           BOOL* eaten) override;
  STDMETHODIMP OnKeyUp(ITfContext* context, WPARAM wParam, LPARAM lParam,
                       BOOL* eaten) override;
  STDMETHODIMP OnPreservedKey(ITfContext* context, REFGUID guid,
                              BOOL* eaten) override;
  STDMETHODIMP OnReleaseContext(ITfContext* context);

  STDMETHODIMP OnCompositionTerminated(TfEditCookie edit_cookie,
                                       ITfComposition* composition) override;

  HRESULT EnsureCompositionText(TfEditCookie edit_cookie, ITfContext* context,
                                const std::wstring& text, bool commit_text,
                                bool cancel);
  std::wstring BuildPreeditText() const;
  std::wstring BuildCommittedText() const;
  void ResetState();

 private:
  ~TextService();

  struct FixedSegment {
    std::wstring text;
    std::vector<std::string> syllables;
  };

  HRESULT AdviseKeyEventSink();
  void UnadviseKeyEventSink();
  HRESULT EnsureEngineLoaded();
  HRESULT GetFocusedContext(ITfContext** context) const;
  bool HasCompositionState() const;
  bool ShouldHandleKey(WPARAM wParam) const;
  bool ShouldInterceptMappedKey(WPARAM wParam) const;
  bool ShouldPassThroughAsLiteralPunctuation(WPARAM wParam) const;
  bool HasUnsupportedModifierState() const;
  bool HandleKeyDown(ITfContext* context, WPARAM wParam);
  bool HandleMappedSymbolKey(ITfContext* context, char ascii_key);
  bool HandleBackspace(ITfContext* context);
  bool HandleCommit(ITfContext* context);
  bool HandleEscape(ITfContext* context);
  bool HandleCandidateSelection(ITfContext* context, size_t one_based_index);
  bool CommitCurrentSyllable();
  bool RestorePreviousFixedSegment();
  void RefreshVisibleCandidates();
  std::wstring BuildDecodedPendingText() const;
  HRESULT RequestEditSession(ITfContext* context, bool commit_text,
                             bool cancel_text);
  HRESULT EnsureCompositionRange(TfEditCookie edit_cookie, ITfContext* context,
                                 ITfRange** range_out);
  void UpdateCandidateWindow(TfEditCookie edit_cookie, ITfContext* context,
                             ITfRange* range);
  void HideCandidateWindow();
  char VirtualKeyToAscii(WPARAM wParam) const;

  LONG ref_count_;
  DWORD activate_flags_;
  TfClientId client_id_;
  ITfThreadMgr* thread_mgr_;
  ITfComposition* composition_;
  CandidateWindow candidate_window_;

  zhuyin::Dictionary dictionary_;
  std::unique_ptr<zhuyin::Decoder> decoder_;
  bool engine_loaded_;
  bool engine_attempted_;

  std::vector<FixedSegment> fixed_segments_;
  std::vector<std::string> pending_syllables_;
  zhuyin::SyllableComposer syllable_composer_;
  std::vector<zhuyin::Candidate> visible_candidates_;
};

}  // namespace zhuyin::windowsime

#endif  // _WIN32
