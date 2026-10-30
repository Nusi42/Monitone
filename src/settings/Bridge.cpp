#include "settings/Bridge.h"

#include <commdlg.h>
#include <mmdeviceapi.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <wincodec.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <span>

#include "Autostart.h"
#include "Balance.h"
#include "IdentifyOverlay.h"
#include "Shell.h"
#include "Win32.h"
#include "settings/Text.h"

namespace monitone::settings {
namespace {

using nlohmann::json;

constexpr PROPERTYKEY kDeviceFriendlyName{
    {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};
// Defined here so no import library is needed for these WIC GUIDs.
constexpr CLSID kClsidWicImagingFactory{0xcacaf262, 0x9370, 0x4615, {0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a}};
constexpr GUID kContainerFormatPng{0x1b7cfaf4, 0x713f, 0x473c, {0xbb, 0xcd, 0x61, 0x37, 0x42, 0x5f, 0xae, 0xaf}};
constexpr GUID kPixelFormat32bppBgra{0x6fddc324, 0x4e03, 0x4bfe, {0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0f}};

std::string_view BalanceName(BalanceMode mode) {
  switch (mode) {
    case BalanceMode::Monitor:
      return "monitor";
    case BalanceMode::Window:
      return "window";
    case BalanceMode::Off:
      break;
  }
  return "off";
}

BalanceMode ParseBalance(std::string_view name) {
  if (name == "monitor") return BalanceMode::Monitor;
  if (name == "window") return BalanceMode::Window;
  return BalanceMode::Off;
}

std::string_view ActionName(AppAction action) {
  switch (action) {
    case AppAction::Exclude:
      return "exclude";
    case AppAction::Pin:
      return "pin";
    case AppAction::Follow:
      break;
  }
  return "follow";
}

AppAction ParseAction(std::string_view name) {
  if (name == "exclude") return AppAction::Exclude;
  if (name == "pin") return AppAction::Pin;
  return AppAction::Follow;
}

std::wstring StringField(const json& message, const char* key) {
  const auto it = message.find(key);
  return it != message.end() && it->is_string() ? FromUtf8(it->get<std::string>()) : std::wstring{};
}

std::string Base64(std::span<const BYTE> data) {
  constexpr std::string_view kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((data.size() + 2) / 3 * 4);
  for (size_t i = 0; i < data.size(); i += 3) {
    const size_t remaining = data.size() - i;
    const auto chunk = (std::uint32_t{data[i]} << 16) | (remaining > 1 ? std::uint32_t{data[i + 1]} << 8 : 0u) |
                       (remaining > 2 ? std::uint32_t{data[i + 2]} : 0u);
    out += kAlphabet[(chunk >> 18) & 0x3F];
    out += kAlphabet[(chunk >> 12) & 0x3F];
    out += remaining > 1 ? kAlphabet[(chunk >> 6) & 0x3F] : '=';
    out += remaining > 2 ? kAlphabet[chunk & 0x3F] : '=';
  }
  return out;
}

// Encodes the icon as PNG; returns an empty string on any failure.
std::string IconPng(HICON icon) {
  ComPtr<IWICImagingFactory> factory;
  if (FAILED(::CoCreateInstance(kClsidWicImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
    return {};
  ComPtr<IWICBitmap> bitmap;
  UINT width = 0;
  UINT height = 0;
  if (FAILED(factory->CreateBitmapFromHICON(icon, &bitmap)) || FAILED(bitmap->GetSize(&width, &height))) return {};

  ComPtr<IStream> stream;
  stream.Attach(::SHCreateMemStream(nullptr, 0));
  ComPtr<IWICBitmapEncoder> encoder;
  ComPtr<IWICBitmapFrameEncode> frame;
  WICPixelFormatGUID format = kPixelFormat32bppBgra;
  if (!stream || FAILED(factory->CreateEncoder(kContainerFormatPng, nullptr, &encoder)) ||
      FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
      FAILED(encoder->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr)) ||
      FAILED(frame->SetSize(width, height)) || FAILED(frame->SetPixelFormat(&format)) ||
      FAILED(frame->WriteSource(bitmap.Get(), nullptr)) || FAILED(frame->Commit()) || FAILED(encoder->Commit()))
    return {};

  ULARGE_INTEGER size{};
  if (FAILED(::IStream_Size(stream.Get(), &size)) || FAILED(::IStream_Reset(stream.Get()))) return {};
  std::vector<BYTE> png(static_cast<size_t>(size.QuadPart));
  if (FAILED(::IStream_Read(stream.Get(), png.data(), static_cast<ULONG>(png.size())))) return {};
  return "data:image/png;base64," + Base64(png);
}

void ReportSink(void* context, const MonitoneReportEntry* entry) {
  auto& entries = *static_cast<std::vector<json>*>(context);
  const std::wstring_view monitor{entry->monitor};
  json item{{"pid", entry->pid},
            {"exe", ToUtf8(entry->exe)},
            {"path", ToUtf8(entry->exePath)},
            {"title", ToUtf8(entry->windowTitle)},
            {"display", ToUtf8(monitor.substr(0, monitor.find(L':')))},
            {"device", ToUtf8(entry->device)},
            {"note", ToUtf8(entry->note)},
            {"playing", entry->playing != 0}};
  item["balance"] = entry->hasBalance != 0 ? json{{"left", entry->left}, {"right", entry->right}} : json(nullptr);
  entries.push_back(std::move(item));
}

}  // namespace

Bridge::Bridge(HINSTANCE module, const HostInfo& host, bool backdrop)
    : module_{module}, host_{host}, backdrop_{backdrop}, config_{host.configPath} {}

void Bridge::RefreshDevices() {
  devices_.clear();
  defaultDeviceId_.clear();
  ComPtr<IMMDeviceEnumerator> enumerator;
  if (FAILED(::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) return;

  ComPtr<IMMDevice> defaultDevice;
  CoTaskMemString defaultId;
  if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &defaultDevice)) &&
      SUCCEEDED(defaultDevice->GetId(defaultId.Put())))
    defaultDeviceId_ = defaultId.Take();

  ComPtr<IMMDeviceCollection> collection;
  UINT count = 0;
  if (FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection)) ||
      FAILED(collection->GetCount(&count)))
    return;
  for (UINT i = 0; i < count; ++i) {
    ComPtr<IMMDevice> device;
    CoTaskMemString id;
    if (FAILED(collection->Item(i, &device)) || FAILED(device->GetId(id.Put()))) continue;
    Device entry{.id = id.Take()};
    ComPtr<IPropertyStore> properties;
    PropVariant name;
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties)) &&
        SUCCEEDED(properties->GetValue(kDeviceFriendlyName, name.Put())) && name.Get().vt == VT_LPWSTR)
      entry.name = name.Get().pwszVal;
    if (entry.name.empty()) entry.name = entry.id;
    devices_.push_back(std::move(entry));
  }
}

json Bridge::State() {
  config_.Load();
  monitors_.Refresh();
  RefreshDevices();

  json devices = json::array();
  for (const Device& device : devices_)
    devices.push_back({{"id", ToUtf8(device.id)},
                       {"name", ToUtf8(device.name)},
                       {"isDefault", EqualsNoCase(device.id, defaultDeviceId_)}});

  json displays = json::array();
  for (const MonitorInfo& monitor : monitors_.All()) {
    const MonitorRule* rule = config_.Find(monitor);
    displays.push_back({{"index", monitor.index},
                        {"name", ToUtf8(monitor.friendlyName)},
                        {"position", ToUtf8(monitors_.Position(monitor))},
                        {"primary", monitor.primary},
                        {"left", monitor.rect.left},
                        {"top", monitor.rect.top},
                        {"width", monitor.rect.right - monitor.rect.left},
                        {"height", monitor.rect.bottom - monitor.rect.top},
                        {"device", rule != nullptr ? ToUtf8(rule->deviceId) : std::string{}}});
  }

  json apps = json::array();
  for (const AppRule& rule : config_.appRules)
    apps.push_back({{"exe", ToUtf8(rule.exe)}, {"action", ActionName(rule.action)}, {"device", ToUtf8(rule.deviceId)}});

  return {{"type", "state"},
          {"version", ToUtf8(host_.version)},
          {"backdrop", backdrop_},
          {"configPath", ToUtf8(config_.Path())},
          {"autostart", IsAutostartEnabled()},
          {"devices", std::move(devices)},
          {"displays", std::move(displays)},
          {"config",
           {{"balance", BalanceName(config_.balance)},
            {"paused", config_.paused},
            {"restoreOnExit", config_.restoreOnExit},
            {"pollMs", config_.pollMs},
            {"pollMinMs", Config::kMinPollMs},
            {"pollMaxMs", Config::kMaxPollMs},
            {"apps", std::move(apps)}}}};
}

void Bridge::Commit() {
  config_.Save();
  host_.callbacks.configChanged(host_.callbacks.context);
}

const MonitorInfo* Bridge::Display(const json& message) const {
  const auto it = message.find("display");
  if (it == message.end() || !it->is_number_integer()) return nullptr;
  const auto index = it->get<int>();
  const auto& all = monitors_.All();
  return index >= 0 && static_cast<size_t>(index) < all.size() ? &all[static_cast<size_t>(index)] : nullptr;
}

json Bridge::Report() {
  std::vector<json> entries;
  host_.callbacks.queryReport(host_.callbacks.context, ReportSink, &entries);
  for (json& entry : entries) entry["icon"] = IconDataUrl(FromUtf8(entry["path"].get<std::string>()));
  return {{"type", "report"}, {"paused", config_.paused}, {"entries", std::move(entries)}};
}

std::string Bridge::IconDataUrl(const std::wstring& exePath) {
  if (exePath.empty()) return {};
  if (const auto it = icons_.find(exePath); it != icons_.end()) return it->second;
  UniqueIcon icon;
  std::string url;
  if (::ExtractIconExW(exePath.c_str(), 0, icon.Put(), nullptr, 1) > 0 && icon) url = IconPng(icon.Get());
  return icons_.emplace(exePath, std::move(url)).first->second;
}

std::wstring Bridge::BrowseExecutable() const {
  constexpr wchar_t kFilter[] = L"Programs (*.exe)\0*.exe\0All files (*.*)\0*.*\0";
  std::array<wchar_t, MAX_PATH> file{};
  OPENFILENAMEW request{};
  request.lStructSize = sizeof(request);
  request.hwndOwner = window_;
  request.lpstrFilter = kFilter;
  request.lpstrFile = file.data();
  request.nMaxFile = static_cast<DWORD>(file.size());
  request.lpstrTitle = L"Choose an application";
  request.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_DONTADDTORECENT | OFN_NOCHANGEDIR;
  if (!::GetOpenFileNameW(&request)) return {};
  return ToLower(std::filesystem::path{file.data()}.filename().wstring());
}

void Bridge::Handle(const json& message, const Post& post) {
  if (!message.is_object() || !message.contains("type") || !message["type"].is_string()) return;
  const std::string type = message["type"].get<std::string>();

  if (type == "getState") {
    post(State());
  } else if (type == "getReport") {
    post(Report());
  } else if (type == "setDisplayDevice") {
    if (const MonitorInfo* display = Display(message)) {
      config_.SetDevice(*display, StringField(message, "device"));
      Commit();
    }
    post(State());
  } else if (type == "setOptions") {
    if (message.contains("balance") && message["balance"].is_string())
      config_.balance = ParseBalance(message["balance"].get<std::string>());
    if (message.contains("paused") && message["paused"].is_boolean()) config_.paused = message["paused"].get<bool>();
    if (message.contains("restoreOnExit") && message["restoreOnExit"].is_boolean())
      config_.restoreOnExit = message["restoreOnExit"].get<bool>();
    if (message.contains("pollMs") && message["pollMs"].is_number_unsigned())
      config_.pollMs = std::clamp(message["pollMs"].get<UINT>(), Config::kMinPollMs, Config::kMaxPollMs);
    Commit();
    post(State());
  } else if (type == "setAutostart") {
    if (message.contains("enabled") && message["enabled"].is_boolean()) SetAutostart(message["enabled"].get<bool>());
    post(State());
  } else if (type == "setAppRule") {
    const std::string action = message.value("action", std::string{"follow"});
    config_.SetAppRule(AppRule{
        .exe = StringField(message, "exe"), .action = ParseAction(action), .deviceId = StringField(message, "device")});
    Commit();
    post(State());
  } else if (type == "removeAppRule") {
    config_.RemoveAppRule(StringField(message, "exe"));
    Commit();
    post(State());
  } else if (type == "testTone") {
    if (const MonitorInfo* display = Display(message)) {
      const MonitorRule* rule = config_.Find(*display);
      tone_.Play(rule != nullptr ? rule->deviceId : std::wstring{}, DisplayGain(config_.balance, monitors_, *display));
    }
  } else if (type == "identify") {
    ShowDisplayNumbers(module_, monitors_);
  } else if (type == "browseExecutable") {
    post({{"type", "browseResult"}, {"exe", ToUtf8(BrowseExecutable())}});
  } else if (type == "openConfigFolder") {
    OpenInShell(std::filesystem::path{config_.Path()}.parent_path().wstring());
  } else if (type == "openUrl") {
    const std::wstring url = StringField(message, "url");
    if (url.starts_with(L"https://")) OpenInShell(url);
  } else if (type == "resetWindowsDevices") {
    host_.callbacks.resetWindowsAppDevices(host_.callbacks.context);
  }
}

}  // namespace monitone::settings
