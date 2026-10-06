#pragma once

#ifdef _WIN32

#include <Windows.h>
#include <ctffunc.h>
#include <msctf.h>

#include <memory>
#include <string>
#include <vector>

#include "CandidateWindow.h"
#include <zhuyin/engine.h>

namespace zhuyin::windowsime {

class TextService final : public ITfTextInputProcessorEx,
                          public ITfKeyEventSink,
                          public ITfCompositionSink,
                          public ITfFunctionProvider,
                          public ITfFnReconversion {
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

  // ITfFunctionProvider: lets host applications discover that this text
  // service offers ITfFnReconversion (see below).
  STDMETHODIMP GetType(GUID* guid) override;
  STDMETHODIMP GetDescription(BSTR* description) override;
  STDMETHODIMP GetFunction(REFGUID guid_service, REFIID riid,
                           IUnknown** function) override;

  // ITfFunction (base of ITfFnReconversion).
  STDMETHODIMP GetDisplayName(BSTR* name) override;

  // ITfFnReconversion: drives the Windows-provided "Reconversion" command on
  // edit controls (e.g. right-click a previously committed character and
  // choose "重新轉換") -- the post-commit equivalent of picking a candidate
  // while still composing. QueryRange reports which already-committed range
  // is convertible (the current selection, or the single character before
  // an empty caret); Reconvert looks up a plausible original reading for
  // that already-committed text and reopens our normal candidate-selection
  // flow directly on it.
  STDMETHODIMP QueryRange(ITfRange* range, ITfRange** new_range,
                          BOOL* convertible) override;
  STDMETHODIMP GetReconversion(ITfRange* range,
                               ITfCandidateList** candidate_list) override;
  STDMETHODIMP Reconvert(ITfRange* range) override;

  HRESULT EnsureCompositionText(TfEditCookie edit_cookie, ITfContext* context,
                                const std::wstring& text, bool commit_text,
                                bool cancel);
  std::wstring BuildPreeditText() const;
  std::wstring BuildCommittedText() const;
  void ResetState();

  // Invoked (via a small internal ITfEditSession) from QueryRange/Reconvert
  // above, where the actual TSF range manipulation needs an edit cookie that
  // the ITfFnReconversion methods themselves aren't handed directly.
  HRESULT ExtendReconversionRange(TfEditCookie edit_cookie, ITfRange* range,
                                  ITfRange** new_range, BOOL* convertible);
  HRESULT PerformReconversion(TfEditCookie edit_cookie, ITfContext* context,
                              ITfRange* range);

 private:
  ~TextService();

  struct FixedSegment {
    std::wstring text;
    std::vector<std::string> syllables;
  };

  // One already-decided-or-still-auto-decoded unit of the current
  // composition, used for Left/Right navigation to target an individual
  // character/phrase for reselection before the whole thing is committed.
  struct LogicalSegment {
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
  bool HandleArrowKey(ITfContext* context, bool move_right);
  bool HandleCandidateSelection(ITfContext* context, size_t one_based_index);
  bool CommitCurrentSyllable();
  bool RestorePreviousFixedSegment();
  void RefreshVisibleCandidates();
  std::wstring BuildDecodedPendingText() const;
  std::vector<std::string> AllSyllables() const;
  std::vector<LogicalSegment> BuildLogicalSegments() const;
  size_t SegmentSyllableOffset(const std::vector<LogicalSegment>& segments,
                              int segment_index) const;
  HRESULT RequestEditSession(ITfContext* context, bool commit_text,
                             bool cancel_text);
  HRESULT EnsureCompositionRange(TfEditCookie edit_cookie, ITfContext* context,
                                 ITfRange** range_out);
  bool ComputeNavSegmentCharRange(size_t* offset, size_t* length) const;
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

  // -1 = normal typing (append-only) mode. >= 0 = Left/Right has targeted
  // logical segment `nav_segment_index_` (see BuildLogicalSegments) for
  // review/reselection instead of appending new input at the end.
  int nav_segment_index_;
};

}  // namespace zhuyin::windowsime

#endif  // _WIN32
