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

# Platform-neutral core: state machine, audio pipeline, transcription, storage.
set(CORE_SOURCES
    src/core/app.cpp
    src/core/audio_recorder.cpp
    src/core/mp3_encoder.cpp
    src/core/transcription_client.cpp
    src/core/user_config.cpp
    src/core/history_store.cpp
)

if (WIN32)
  add_executable(dictate_cpp WIN32
      ${CORE_SOURCES}
      src/platform/win/main_win.cpp
      src/platform/win/hotkey_win.cpp
      src/platform/win/overlay_win.cpp
      src/platform/win/tray_win.cpp
      src/platform/win/clipboard_win.cpp
      src/platform/win/paste_win.cpp
      resources/app.rc
  )
  target_include_directories(dictate_cpp PRIVATE src/platform/win resources)
  target_compile_definitions(dictate_cpp PRIVATE
      UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(dictate_cpp PRIVATE
      user32 gdi32 shell32 ole32 advapi32)
  # Source files are UTF-8; without this MSVC decodes them as the system
  # codepage and mangles non-ASCII string literals.
  target_compile_options(dictate_cpp PRIVATE /utf-8)
elseif (APPLE)
  # The macOS platform layer is not implemented yet — see MACOS.md for the
  # porting guide (interfaces to implement, permissions, packaging).
  # Expected shape once implemented:
  #   enable_language(OBJCXX)
  #   add_executable(dictate_cpp MACOSX_BUNDLE
  #       ${CORE_SOURCES}
  #       src/platform/mac/main_mac.mm        # NSApplication run loop, service wiring
  #       src/platform/mac/hotkey_mac.mm      # CGEventTap chord/latch state machine
  #       src/platform/mac/overlay_mac.mm     # non-activating NSPanel
  #       src/platform/mac/tray_mac.mm        # NSStatusItem
  #       src/platform/mac/clipboard_mac.mm   # NSPasteboard
  #       src/platform/mac/paste_mac.mm       # CGEventPost Cmd+V
  #   )
  #   set_target_properties(dictate_cpp PROPERTIES
  #       MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/resources/mac/Info.plist)
  #   target_link_libraries(dictate_cpp PRIVATE
  #       "-framework Cocoa" "-framework Carbon" "-framework CoreAudio"
  #       "-framework AudioToolbox" "-framework ApplicationServices")
  message(FATAL_ERROR "macOS platform layer not implemented yet — see MACOS.md.")
else()
  message(FATAL_ERROR "Unsupported platform.")
endif()

target_compile_features(dictate_cpp PRIVATE cxx_std_20)

target_include_directories(dictate_cpp PRIVATE
    include
    external
    ${MP3LAME_INCLUDE_DIR})

target_link_libraries(dictate_cpp PRIVATE
    CURL::libcurl
    nlohmann_json::nlohmann_json
    ${MP3LAME_LIBRARY})
