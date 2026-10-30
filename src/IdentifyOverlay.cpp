#include "IdentifyOverlay.h"

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <string_view>

#include "Win32.h"

namespace monitone {
namespace {

constexpr wchar_t kOverlayClass[] = L"Monitone.Identify";
constexpr UINT_PTR kCloseTimer = 1;
constexpr UINT kVisibleMs = 2500;
constexpr BYTE kOpacity = 230;
constexpr COLORREF kBackground = RGB(0x4F, 0x46, 0xE5);  // indigo from the app icon
constexpr COLORREF kForeground = RGB(0xFF, 0xFF, 0xFF);

struct OverlayContent {
  std::wstring number;
  std::wstring name;
};

void DeleteFontHandle(HFONT font) noexcept { ::DeleteObject(font); }
void DeleteBrushHandle(HBRUSH brush) noexcept { ::DeleteObject(brush); }
using UniqueFont = UniqueResource<detail::NullableTraits<HFONT, DeleteFontHandle>>;
using UniqueBrush = UniqueResource<detail::NullableTraits<HBRUSH, DeleteBrushHandle>>;

UniqueFont MakeFont(int height, int weight) {
  return UniqueFont{::CreateFontW(-height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI")};
}

void Paint(HWND window, const OverlayContent& content) {
  PAINTSTRUCT paint{};
  const HDC dc = ::BeginPaint(window, &paint);
  RECT client{};
  ::GetClientRect(window, &client);

  const UniqueBrush background{::CreateSolidBrush(kBackground)};
  ::FillRect(dc, &client, background.Get());
  ::SetBkMode(dc, TRANSPARENT);
  ::SetTextColor(dc, kForeground);

  const int height = client.bottom - client.top;
  RECT numberRect = client;
  numberRect.bottom = client.top + height * 3 / 4;
  RECT nameRect = client;
  nameRect.top = numberRect.bottom - height / 12;

  const UniqueFont numberFont = MakeFont(height / 2, FW_SEMIBOLD);
  const UniqueFont nameFont = MakeFont(height / 12, FW_NORMAL);
  const HGDIOBJ previous = ::SelectObject(dc, numberFont.Get());
  ::DrawTextW(dc, content.number.c_str(), -1, &numberRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  ::SelectObject(dc, nameFont.Get());
  ::DrawTextW(dc, content.name.c_str(), -1, &nameRect, DT_CENTER | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
  ::SelectObject(dc, previous);
  ::EndPaint(window, &paint);
}

LRESULT CALLBACK OverlayProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_NCCREATE: {
      const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
      ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
      break;
    }
    case WM_PAINT:
      if (const auto* content = reinterpret_cast<const OverlayContent*>(::GetWindowLongPtrW(window, GWLP_USERDATA)))
        Paint(window, *content);
      return 0;
    case WM_TIMER:
    case WM_LBUTTONDOWN:
      ::DestroyWindow(window);
      return 0;
    case WM_NCDESTROY:
      // Ownership of the content was handed to the window in ShowDisplayNumbers.
      std::unique_ptr<OverlayContent>{reinterpret_cast<OverlayContent*>(::SetWindowLongPtrW(window, GWLP_USERDATA, 0))};
      break;
    default:
      break;
  }
  return ::DefWindowProcW(window, message, wParam, lParam);
}

void RegisterOverlayClass(HINSTANCE instance) {
  WNDCLASSEXW existing{};
  existing.cbSize = sizeof(existing);
  if (::GetClassInfoExW(instance, kOverlayClass, &existing)) return;
  const WNDCLASSEXW windowClass{.cbSize = sizeof(WNDCLASSEXW),
                                .lpfnWndProc = OverlayProc,
                                .hInstance = instance,
                                .hCursor = ::LoadCursorW(nullptr, IDC_ARROW),
                                .lpszClassName = kOverlayClass};
  ::RegisterClassExW(&windowClass);
}

}  // namespace

void CloseDisplayNumbers(HINSTANCE instance) {
  ::EnumThreadWindows(
      ::GetCurrentThreadId(),
      [](HWND window, LPARAM) -> BOOL {
        std::array<wchar_t, 64> name{};
        if (::GetClassNameW(window, name.data(), static_cast<int>(name.size())) > 0 &&
            std::wstring_view{name.data()} == kOverlayClass)
          ::DestroyWindow(window);
        return TRUE;
      },
      0);
  ::UnregisterClassW(kOverlayClass, instance);
}

void ShowDisplayNumbers(HINSTANCE instance, const MonitorList& monitors) {
  RegisterOverlayClass(instance);
  for (const MonitorInfo& monitor : monitors.All()) {
    const int width = monitor.rect.right - monitor.rect.left;
    const int height = monitor.rect.bottom - monitor.rect.top;
    const int size = std::min(width, height) / 3;
    const int x = monitor.rect.left + (width - size) / 2;
    const int y = monitor.rect.top + (height - size) / 2;

    auto content = std::make_unique<OverlayContent>(
        OverlayContent{.number = std::to_wstring(monitor.index + 1), .name = monitor.friendlyName});
    const HWND window =
        ::CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE, kOverlayClass, L"",
                          WS_POPUP, x, y, size, size, nullptr, nullptr, instance, content.get());
    if (window == nullptr) continue;
    (void)content.release();  // now owned by the window, freed in WM_NCDESTROY
    ::SetLayeredWindowAttributes(window, 0, kOpacity, LWA_ALPHA);
    ::SetTimer(window, kCloseTimer, kVisibleMs, nullptr);
    ::ShowWindow(window, SW_SHOWNOACTIVATE);
  }
}

}  // namespace monitone
