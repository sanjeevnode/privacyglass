cmake_minimum_required(VERSION 3.24)

# CI passes -DAPP_VERSION=<x.y.z> derived from the run number; local builds get 0.0.0.
if(NOT DEFINED APP_VERSION)
  set(APP_VERSION "0.0.0")
endif()
project(WhatsAppPrivacy VERSION ${APP_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# WebView2 SDK ships as a NuGet package; a .nupkg is just a zip, so FetchContent
# unpacks it directly. Avoids needing the nuget CLI (not installed on this machine).
include(FetchContent)
FetchContent_Declare(webview2
  URL https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.3351.48
  URL_HASH SHA256=ad98e9d951361e62b6edc137273ec1787242c5c28539c1a101f98a1e07d060f9
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(webview2)

include(${CMAKE_SOURCE_DIR}/cmake/EmbedAssets.cmake)
set(WEB_ASSETS_HEADER ${CMAKE_BINARY_DIR}/generated/WebAssets.h)
embed_web_assets(${WEB_ASSETS_HEADER}
  web/selectors.js
  web/privacy.css
  web/privacy.js
  web/test_privacy.js)

# Parse APP_VERSION directly rather than relying on PROJECT_VERSION_*: a missing
# component leaves those empty, which emits "1,,,0" and fails the RC compiler.
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" _v "${APP_VERSION}")
if(_v)
  set(VER_MAJOR ${CMAKE_MATCH_1})
  set(VER_MINOR ${CMAKE_MATCH_2})
  set(VER_PATCH ${CMAKE_MATCH_3})
else()
  set(VER_MAJOR 0)
  set(VER_MINOR 0)
  set(VER_PATCH 0)
endif()
set(APP_VERSION_FULL "${VER_MAJOR}.${VER_MINOR}.${VER_PATCH}")
message(STATUS "App version: ${APP_VERSION_FULL}")

configure_file(src/version.rc.in ${CMAKE_BINARY_DIR}/generated/version.rc @ONLY)

add_executable(WhatsAppPrivacy WIN32
  ${CMAKE_BINARY_DIR}/generated/version.rc
  src/main.cpp
  src/app/Application.cpp
  src/window/MainWindow.cpp
  src/webview/WebViewManager.cpp
  src/privacy/PrivacyManager.cpp
  ${WEB_ASSETS_HEADER}
)

target_include_directories(WhatsAppPrivacy PRIVATE
  src
  ${CMAKE_BINARY_DIR}/generated
  ${webview2_SOURCE_DIR}/build/native/include)

# Static loader => single self-contained exe, no WebView2Loader.dll to ship.
target_link_libraries(WhatsAppPrivacy PRIVATE
  ${webview2_SOURCE_DIR}/build/native/x64/WebView2LoaderStatic.lib
  version shlwapi comctl32)

target_compile_definitions(WhatsAppPrivacy PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

# We supply our own manifest via the .rc; stop the linker adding a second one.
target_link_options(WhatsAppPrivacy PRIVATE /MANIFEST:NO)

# Sources are UTF-8. Without this MSVC reads them as ANSI and any non-ASCII
# literal becomes mojibake ("About..." rendering as "Aboutâ€¦").
target_compile_options(WhatsAppPrivacy PRIVATE /utf-8)
