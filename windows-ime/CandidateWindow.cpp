#ifdef _WIN32

#include "CandidateWindow.h"

#include <algorithm>

namespace zhuyin::windowsime {

CandidateWindow::CandidateWindow()
    : hwnd_(nullptr),
      instance_(nullptr),
      highlighted_index_(static_cast<size_t>(-1)),
      font_(nullptr) {}

CandidateWindow::~CandidateWindow() {
  Destroy();
  if (font_ != nullptr) {
    DeleteObject(font_);
    font_ = nullptr;
  }
}

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
      L"", WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
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
    const std::vector<std::wstring>& numbered_candidates,
    size_t highlighted_index) {
  numbered_candidates_ = numbered_candidates;
  highlighted_index_ = highlighted_index;
  if (!EnsureCreated(instance_ != nullptr ? instance_ : GetModuleHandleW(nullptr))) {
    return;
  }

  HDC dc = GetDC(hwnd_);
  HFONT font = EnsureFont();
  HGDIOBJ old_font = SelectObject(dc, font);
  const SIZE size = MeasureWindow(dc);
  SelectObject(dc, old_font);
  ReleaseDC(hwnd_, dc);

  const int x = anchor_rect.left;
  const int y = anchor_rect.bottom + 2;
  SetWindowPos(hwnd_, HWND_TOPMOST, x, y, size.cx, size.cy,
               SWP_NOACTIVATE | SWP_SHOWWINDOW);

  // Rounded-corner silhouette so the popup looks less like a plain 1990s
  // dialog box.
  HRGN region = CreateRoundRectRgn(0, 0, size.cx + 1, size.cy + 1,
                                   kCornerRadius, kCornerRadius);
  SetWindowRgn(hwnd_, region, TRUE);  // hwnd_ takes ownership of region

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

  // Soft light-gray background with a slightly darker, rounded border reads
  // as a modern popup rather than a bare system dialog.
  HBRUSH background = CreateSolidBrush(RGB(250, 250, 250));
  FillRect(dc, &client, background);
  DeleteObject(background);

  HPEN border = CreatePen(PS_SOLID, 1, RGB(200, 200, 200));
  HGDIOBJ old_pen = SelectObject(dc, border);
  HGDIOBJ old_brush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
  RoundRect(dc, client.left, client.top, client.right - 1, client.bottom - 1,
           kCornerRadius, kCornerRadius);
  SelectObject(dc, old_brush);
  SelectObject(dc, old_pen);
  DeleteObject(border);

  HFONT font = EnsureFont();
  HGDIOBJ old_font = SelectObject(dc, font);
  SetBkMode(dc, TRANSPARENT);

  TEXTMETRICW metrics = {};
  GetTextMetricsW(dc, &metrics);
  const int line_height = metrics.tmHeight + 10;
  const int padding_x = 10;

  for (size_t i = 0; i < numbered_candidates_.size(); ++i) {
    RECT row_rect = client;
    row_rect.top = client.top + 4 + static_cast<LONG>(i * line_height);
    row_rect.bottom = row_rect.top + line_height;
    row_rect.left += 2;
    row_rect.right -= 2;

    if (i == highlighted_index_) {
      // Rounded highlight pill behind the selected row, similar to how
      // Microsoft New Phonetic highlights the arrow-key-selected candidate.
      HBRUSH highlight_brush = CreateSolidBrush(RGB(51, 122, 230));
      HGDIOBJ old_highlight_brush = SelectObject(dc, highlight_brush);
      HPEN highlight_pen = CreatePen(PS_SOLID, 1, RGB(51, 122, 230));
      HGDIOBJ old_highlight_pen = SelectObject(dc, highlight_pen);
      RoundRect(dc, row_rect.left, row_rect.top, row_rect.right, row_rect.bottom,
               6, 6);
      SelectObject(dc, old_highlight_pen);
      SelectObject(dc, old_highlight_brush);
      DeleteObject(highlight_pen);
      DeleteObject(highlight_brush);
      SetTextColor(dc, RGB(255, 255, 255));
    } else {
      SetTextColor(dc, RGB(40, 40, 40));
    }

    RECT text_rect = row_rect;
    text_rect.left += padding_x;
    text_rect.right -= padding_x;
    DrawTextW(dc, numbered_candidates_[i].c_str(), -1, &text_rect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  }

  SelectObject(dc, old_font);
}

HFONT CandidateWindow::EnsureFont() const {
  if (font_ == nullptr) {
    font_ = CreateFontW(
        -16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft JhengHei UI");
  }
  return font_;
}

SIZE CandidateWindow::MeasureWindow(HDC dc) const {
  SIZE size = {180, 24};
  if (dc == nullptr) {
    return size;
  }

  TEXTMETRICW metrics = {};
  GetTextMetricsW(dc, &metrics);
  const int line_height = metrics.tmHeight + 10;
  int width = 150;
  for (const auto& line : numbered_candidates_) {
    SIZE line_size = {};
    if (GetTextExtentPoint32W(dc, line.c_str(),
                              static_cast<int>(line.size()), &line_size)) {
      width = std::max(width, static_cast<int>(line_size.cx) + 28);
    }
  }

  size.cx = width;
  size.cy = std::max(24, static_cast<int>(numbered_candidates_.size()) * line_height + 8);
  return size;
}

}  // namespace zhuyin::windowsime

#endif  // _WIN32
