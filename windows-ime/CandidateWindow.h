#pragma once

#ifdef _WIN32

#include <Windows.h>

#include <string>
#include <vector>

namespace zhuyin::windowsime {

class CandidateWindow {
 public:
  CandidateWindow();
  ~CandidateWindow();

  bool EnsureCreated(HINSTANCE instance);
  void Destroy();
  void Hide();
  bool IsVisible() const;
  // `highlighted_index` is the zero-based row currently selected via
  // Up/Down navigation (drawn with a highlighted background), or SIZE_MAX
  // for no highlight.
  void Show(const RECT& anchor_rect,
            const std::vector<std::wstring>& numbered_candidates,
            size_t highlighted_index);

 private:
  static constexpr wchar_t kWindowClassName[] = L"ZhuyinAICandidateWindow";
  static constexpr int kCornerRadius = 8;

  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam,
                                     LPARAM lParam);
  LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
  void Paint(HDC dc);
  SIZE MeasureWindow(HDC dc) const;
  HFONT EnsureFont() const;

  HWND hwnd_;
  HINSTANCE instance_;
  std::vector<std::wstring> numbered_candidates_;
  size_t highlighted_index_;
  mutable HFONT font_;
};

}  // namespace zhuyin::windowsime

#endif  // _WIN32
