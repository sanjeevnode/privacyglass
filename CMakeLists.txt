cmake_minimum_required(VERSION 3.24)
project(WhatsAppPrivacy CXX)

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

add_executable(WhatsAppPrivacy WIN32
  src/main.cpp
  src/app/Application.cpp
  src/window/MainWindow.cpp
  src/webview/WebViewManager.cpp
)

target_include_directories(WhatsAppPrivacy PRIVATE
  src
  ${webview2_SOURCE_DIR}/build/native/include)

# Static loader => single self-contained exe, no WebView2Loader.dll to ship.
target_link_libraries(WhatsAppPrivacy PRIVATE
  ${webview2_SOURCE_DIR}/build/native/x64/WebView2LoaderStatic.lib
  version shlwapi)

target_compile_definitions(WhatsAppPrivacy PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
