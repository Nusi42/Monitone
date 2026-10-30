#include "WebResources.h"

#include <array>

#include "settings/resource.h"

namespace monitone::settings {
namespace {

struct Entry {
  std::wstring_view path;
  int id = 0;
  std::wstring_view contentType;
};

constexpr std::array kEntries{
    Entry{L"index.html", IDR_UI_INDEX, L"text/html; charset=utf-8"},
    Entry{L"app.css", IDR_UI_CSS, L"text/css; charset=utf-8"},
    Entry{L"app.js", IDR_UI_JS, L"text/javascript; charset=utf-8"},
    Entry{L"icon.png", IDR_UI_ICON, L"image/png"},
};

}  // namespace

std::optional<WebResource> FindWebResource(HMODULE module, std::wstring_view url) {
  if (!url.starts_with(kUiOrigin)) return std::nullopt;
  std::wstring_view path = url.substr(kUiOrigin.size());
  path = path.substr(0, path.find_first_of(L"?#"));
  if (path.empty()) path = L"index.html";

  for (const Entry& entry : kEntries) {
    if (entry.path != path) continue;
    const HRSRC info = ::FindResourceW(module, MAKEINTRESOURCEW(entry.id), MAKEINTRESOURCEW(10));  // RT_RCDATA
    const HGLOBAL handle = info != nullptr ? ::LoadResource(module, info) : nullptr;
    const void* data = handle != nullptr ? ::LockResource(handle) : nullptr;
    if (data == nullptr) return std::nullopt;
    return WebResource{.data = {static_cast<const std::byte*>(data), ::SizeofResource(module, info)},
                       .contentType = entry.contentType};
  }
  return std::nullopt;
}

}  // namespace monitone::settings
