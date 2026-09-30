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
  void Show(const RECT& anchor_rect,
            const std::vector<std::wstring>& numbered_candidates);

 private:
  static constexpr wchar_t kWindowClassName[] = L"ZhuyinAICandidateWindow";

  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam,
                                     LPARAM lParam);
  LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
  void Paint(HDC dc);
  SIZE MeasureWindow(HDC dc) const;

  HWND hwnd_;
  HINSTANCE instance_;
  std::vector<std::wstring> numbered_candidates_;
};

}  // namespace zhuyin::windowsime

#endif  // _WIN32
