# 3.21+ required by CMakePresets.json schema v3.
cmake_minimum_required(VERSION 3.21)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
    "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project("dictate_cpp")

find_package(CURL CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_path(MP3LAME_INCLUDE_DIR NAMES lame/lame.h lame.h)
find_library(MP3LAME_LIBRARY NAMES mp3lame lame libmp3lame)

if (NOT MP3LAME_INCLUDE_DIR OR NOT MP3LAME_LIBRARY)
  message(FATAL_ERROR "mp3lame not found. Make sure vcpkg installed it for the active triplet.")
endif()

add_executable(dictate_cpp WIN32
    src/main.cpp
    src/app.cpp
    src/hotkey_manager.cpp
    src/overlay_window.cpp
    src/audio_recorder.cpp
    src/mp3_encoder.cpp
    src/tray_icon.cpp
    src/user_config.cpp
    src/transcription_client.cpp
    src/clipboard_service.cpp
    src/paste_service.cpp
    src/history_store.cpp
    resources/app.rc
)

target_compile_features(dictate_cpp PRIVATE cxx_std_20)

# Source files are UTF-8; without this MSVC decodes them as the system
# codepage and mangles non-ASCII string literals.
target_compile_options(dictate_cpp PRIVATE /utf-8)

target_compile_definitions(dictate_cpp PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

target_include_directories(dictate_cpp PRIVATE
    include
    resources
    external
    ${MP3LAME_INCLUDE_DIR})

target_link_libraries(dictate_cpp PRIVATE
    CURL::libcurl
    nlohmann_json::nlohmann_json
    ${MP3LAME_LIBRARY}
    user32 gdi32 shell32 ole32 advapi32)
