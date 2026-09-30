#ifdef _WIN32

#include "CandidateWindow.h"

#include <algorithm>

namespace zhuyin::windowsime {

CandidateWindow::CandidateWindow() : hwnd_(nullptr), instance_(nullptr) {}

CandidateWindow::~CandidateWindow() { Destroy(); }

bool CandidateWindow::EnsureCreated(HINSTANCE instance) {
  if (hwnd_ != nullptr) {
    return true;
  }

  instance_ = instance;

  WNDCLASSEXW window_class = {};
  window_class.cbSize = sizeof(window_class);
  window_class.lpfnWndProc = &CandidateWindow::WindowProc;
  window_class.hInstance = instance_;
  window_class.lpszClassName = kWindowClassName;
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  window_class.style = CS_HREDRAW | CS_VREDRAW;

  RegisterClassExW(&window_class);

  hwnd_ = CreateWindowExW(
      WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, kWindowClassName,
      L"", WS_POPUP | WS_BORDER, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
      CW_USEDEFAULT, nullptr, nullptr, instance_, this);

  return hwnd_ != nullptr;
}

void CandidateWindow::Destroy() {
  if (hwnd_ != nullptr) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
}

void CandidateWindow::Hide() {
  if (hwnd_ != nullptr) {
    ShowWindow(hwnd_, SW_HIDE);
  }
}

bool CandidateWindow::IsVisible() const {
  return hwnd_ != nullptr && IsWindowVisible(hwnd_) != FALSE;
}

void CandidateWindow::Show(
    const RECT& anchor_rect,
    const std::vector<std::wstring>& numbered_candidates) {
  numbered_candidates_ = numbered_candidates;
  if (!EnsureCreated(instance_ != nullptr ? instance_ : GetModuleHandleW(nullptr))) {
    return;
  }

  HDC dc = GetDC(hwnd_);
  const SIZE size = MeasureWindow(dc);
  ReleaseDC(hwnd_, dc);

  const int x = anchor_rect.left;
  const int y = anchor_rect.bottom + 2;
  SetWindowPos(hwnd_, HWND_TOPMOST, x, y, size.cx, size.cy,
               SWP_NOACTIVATE | SWP_SHOWWINDOW);
  InvalidateRect(hwnd_, nullptr, TRUE);
}

LRESULT CALLBACK CandidateWindow::WindowProc(HWND hwnd, UINT message,
                                             WPARAM wParam, LPARAM lParam) {
  CandidateWindow* window = nullptr;
  if (message == WM_NCCREATE) {
    auto* create_struct = reinterpret_cast<CREATESTRUCTW*>(lParam);
    window = static_cast<CandidateWindow*>(create_struct->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(window));
    window->hwnd_ = hwnd;
  } else {
    window = reinterpret_cast<CandidateWindow*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }

  if (window != nullptr) {
    return window->HandleMessage(message, wParam, lParam);
  }

  return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT CandidateWindow::HandleMessage(UINT message, WPARAM wParam,
                                       LPARAM lParam) {
  switch (message) {
    case WM_PAINT: {
      PAINTSTRUCT paint = {};
      HDC dc = BeginPaint(hwnd_, &paint);
      Paint(dc);
      EndPaint(hwnd_, &paint);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    default:
      return DefWindowProcW(hwnd_, message, wParam, lParam);
  }
}

void CandidateWindow::Paint(HDC dc) {
  RECT client = {};
  GetClientRect(hwnd_, &client);

  HBRUSH background = CreateSolidBrush(RGB(255, 255, 255));
  FillRect(dc, &client, background);
  DeleteObject(background);

  HPEN border = CreatePen(PS_SOLID, 1, RGB(160, 160, 160));
  HGDIOBJ old_pen = SelectObject(dc, border);
  HGDIOBJ old_brush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
  Rectangle(dc, client.left, client.top, client.right, client.bottom);
  SelectObject(dc, old_brush);
  SelectObject(dc, old_pen);
  DeleteObject(border);

  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB(30, 30, 30));

  RECT line_rect = client;
  line_rect.left += 8;
  line_rect.top += 6;
  line_rect.right -= 8;

  TEXTMETRICW metrics = {};
  GetTextMetricsW(dc, &metrics);
  const int line_height = metrics.tmHeight + 6;

  for (size_t i = 0; i < numbered_candidates_.size(); ++i) {
    RECT text_rect = line_rect;
    text_rect.top += static_cast<LONG>(i * line_height);
    text_rect.bottom = text_rect.top + line_height;
    DrawTextW(dc, numbered_candidates_[i].c_str(), -1, &text_rect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  }
}

SIZE CandidateWindow::MeasureWindow(HDC dc) const {
  SIZE size = {180, 24};
  if (dc == nullptr) {
    return size;
  }

  TEXTMETRICW metrics = {};
  GetTextMetricsW(dc, &metrics);
  const int line_height = metrics.tmHeight + 6;
  int width = 140;
  for (const auto& line : numbered_candidates_) {
    SIZE line_size = {};
    if (GetTextExtentPoint32W(dc, line.c_str(),
                              static_cast<int>(line.size()), &line_size)) {
      width = std::max(width, line_size.cx + 16);
    }
  }

  size.cx = width;
  size.cy = std::max(24, static_cast<int>(numbered_candidates_.size()) * line_height + 12);
  return size;
}

}  // namespace zhuyin::windowsime

#endif  // _WIN32
