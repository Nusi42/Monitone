cmake_minimum_required(VERSION 3.20)

# version.txt is the single source of truth; tools/release.ps1 updates it.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" MONITONE_VERSION LIMIT_COUNT 1)
string(STRIP "${MONITONE_VERSION}" MONITONE_VERSION)
project(Monitone VERSION ${MONITONE_VERSION} LANGUAGES CXX RC)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS version.txt)

option(MONITONE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# Monitone.exe loads MonitoneSettings.dll from its own folder.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# --- Third-party dependencies, pinned by hash --------------------------------

include(FetchContent)
FetchContent_Declare(webview2
  URL https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/1.0.4258.31/microsoft.web.webview2.1.0.4258.31.nupkg
  URL_HASH SHA256=56f7f4b8bf9aee4b8efefbbdd4f67d5f74ebd1b100ed0806da71bf76af481aa9
  DOWNLOAD_NAME webview2.zip)
FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d)
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(webview2 nlohmann_json)
# Third-party headers are not subject to our warning flags.
set_target_properties(nlohmann_json PROPERTIES
  INTERFACE_SYSTEM_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:nlohmann_json,INTERFACE_INCLUDE_DIRECTORIES>)

set(WEBVIEW2_NATIVE "${webview2_SOURCE_DIR}/build/native")
add_library(webview2 INTERFACE)
target_include_directories(webview2 SYSTEM INTERFACE "${WEBVIEW2_NATIVE}/include")
if(MSVC)
  # Static loader: no WebView2Loader.dll next to the executable.
  target_link_libraries(webview2 INTERFACE "${WEBVIEW2_NATIVE}/x64/WebView2LoaderStatic.lib" version)
  target_compile_definitions(webview2 INTERFACE MONITONE_WEBVIEW2_STATIC_LOADER)
else()
  # MinGW cannot link the MSVC static loader; WebView2Loader.dll is loaded at runtime.
  target_include_directories(webview2 SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/mingw-compat")
endif()

# --- Version header -----------------------------------------------------------

# version.h is regenerated on every build so the git revision stays current.
set(MONITONE_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(MONITONE_VERSION_HEADER "${MONITONE_GENERATED_DIR}/version.h")
set(MONITONE_VERSION_COMMAND
  "${CMAKE_COMMAND}" -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR} -DOUTPUT=${MONITONE_VERSION_HEADER}
  -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateVersion.cmake")
execute_process(COMMAND ${MONITONE_VERSION_COMMAND})
add_custom_target(monitone_version
  COMMAND ${MONITONE_VERSION_COMMAND}
  BYPRODUCTS "${MONITONE_VERSION_HEADER}"
  COMMENT "Updating version.h")

# --- Common compile settings ---------------------------------------------------

add_library(monitone_options INTERFACE)
target_include_directories(monitone_options INTERFACE src res "${MONITONE_GENERATED_DIR}")
target_compile_definitions(monitone_options INTERFACE
  UNICODE _UNICODE STRICT WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
if(MSVC)
  target_compile_options(monitone_options INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:/W4 /permissive- /utf-8 /Zc:__cplusplus /EHsc>
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<BOOL:${MONITONE_WARNINGS_AS_ERRORS}>>:/WX>)
else()
  # -Wmissing-field-initializers: members omitted from a designated initializer
  #   are value-initialized by the language, the warning is noise for C++20.
  # -Wnon-virtual-dtor is not enabled: COM interfaces have no virtual
  #   destructors by design, objects are released through IUnknown::Release.
  target_compile_options(monitone_options INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wold-style-cast
                              -Woverloaded-virtual -Wno-missing-field-initializers>
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<BOOL:${MONITONE_WARNINGS_AS_ERRORS}>>:-Werror>)
  target_link_options(monitone_options INTERFACE -static $<$<CONFIG:Release>:-s>)
endif()

# --- Code shared by the executable and the settings DLL -----------------------

add_library(monitone_core STATIC
  src/Autostart.cpp
  src/Balance.cpp
  src/Config.cpp
  src/Monitors.cpp
  src/Shell.cpp
)
target_link_libraries(monitone_core PUBLIC monitone_options advapi32 user32 gdi32 ole32 shell32)
add_dependencies(monitone_core monitone_version)

# --- Monitone.exe: tray application and routing engine -------------------------

add_executable(Monitone WIN32
  src/main.cpp
  src/AudioRouter.cpp
  src/Engine.cpp
  src/PolicyConfig.cpp
  src/SettingsModule.cpp
  src/TrayApp.cpp
  src/WindowMap.cpp
  res/Monitone.rc
)
target_link_libraries(Monitone PRIVATE monitone_core shell32 dwmapi comctl32 runtimeobject)
if(MSVC)
  # The MSVC toolchain merges and embeds .manifest sources itself.
  target_sources(Monitone PRIVATE res/Monitone.manifest)
else()
  set_property(SOURCE res/Monitone.rc APPEND PROPERTY COMPILE_DEFINITIONS MONITONE_EMBED_MANIFEST)
  target_link_options(Monitone PRIVATE -municode)
endif()
add_dependencies(Monitone MonitoneSettings)

# --- MonitoneSettings.dll: WebView2 settings UI, loaded only while open -------

add_library(MonitoneSettings SHARED
  src/settings/Exports.cpp
  src/settings/SettingsWindow.cpp
  src/settings/Bridge.cpp
  src/settings/WebResources.cpp
  src/settings/Loader.cpp
  src/IdentifyOverlay.cpp
  src/TestTone.cpp
  res/MonitoneSettings.rc
  res/MonitoneSettings.def
)
set_target_properties(MonitoneSettings PROPERTIES PREFIX "")
target_link_libraries(MonitoneSettings PRIVATE
  monitone_core webview2 nlohmann_json::nlohmann_json shell32 shlwapi dwmapi comdlg32)
set_property(SOURCE res/MonitoneSettings.rc APPEND PROPERTY OBJECT_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/ui/index.html;${CMAKE_CURRENT_SOURCE_DIR}/ui/app.css;${CMAKE_CURRENT_SOURCE_DIR}/ui/app.js;${CMAKE_CURRENT_SOURCE_DIR}/res/Monitone-256.png")
if(NOT MSVC)
  add_custom_command(TARGET MonitoneSettings POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${WEBVIEW2_NATIVE}/x64/WebView2Loader.dll"
            "$<TARGET_FILE_DIR:MonitoneSettings>")
endif()
