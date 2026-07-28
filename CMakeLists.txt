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
  enable_language(OBJCXX)

  # TCC (microphone / input monitoring / accessibility) grants are keyed to the
  # code signature. Keeping the bundle id and the signing identifier stable is
  # what stops every rebuild from losing them — see MACOS.md.
  set(DICTATE_BUNDLE_ID "com.shorstok.dictate" CACHE STRING
      "macOS bundle identifier; TCC permissions are keyed to it")
  set(DICTATE_CODESIGN_IDENTITY "-" CACHE STRING
      "codesign identity for the app bundle ('-' = ad-hoc)")
  set(DICTATE_MIN_MACOS "11.0" CACHE STRING "LSMinimumSystemVersion in Info.plist")
  set(DICTATE_VERSION "0.1.0")

  set(MACOSX_BUNDLE_EXECUTABLE_NAME dictate_cpp)
  configure_file(
      ${CMAKE_SOURCE_DIR}/resources/mac/Info.plist.in
      ${CMAKE_CURRENT_BINARY_DIR}/Info.plist
      @ONLY)

  # Menu-bar images. The 128px originals are scaled down to 18pt at runtime,
  # which stays sharp on Retina without separate @2x files.
  set(MAC_RESOURCES
      ${CMAKE_SOURCE_DIR}/assets/app.png
      ${CMAKE_SOURCE_DIR}/assets/app_listening.png
      ${CMAKE_SOURCE_DIR}/assets/app_transcribing.png
      ${CMAKE_SOURCE_DIR}/assets/app_error.png
  )

  # Finder/Get Info icon. The app is an agent, so this is cosmetic — if the
  # generator fails, the build carries on without it.
  set(APP_ICNS ${CMAKE_CURRENT_BINARY_DIR}/AppIcon.icns)
  add_custom_command(
      OUTPUT ${APP_ICNS}
      COMMAND sh ${CMAKE_SOURCE_DIR}/resources/mac/make_icns.sh
              ${CMAKE_SOURCE_DIR}/assets/app.png ${APP_ICNS}
      DEPENDS ${CMAKE_SOURCE_DIR}/assets/app.png
              ${CMAKE_SOURCE_DIR}/resources/mac/make_icns.sh
      COMMENT "Generating AppIcon.icns"
      VERBATIM)
  list(APPEND MAC_RESOURCES ${APP_ICNS})

  add_executable(dictate_cpp MACOSX_BUNDLE
      ${CORE_SOURCES}
      src/platform/mac/main_mac.mm         # NSApplication run loop, service wiring
      src/platform/mac/hotkey_mac.mm       # CGEventTap chord/latch state machine
      src/platform/mac/overlay_mac.mm      # non-activating NSPanel
      src/platform/mac/tray_mac.mm         # NSStatusItem
      src/platform/mac/clipboard_mac.mm    # NSPasteboard
      src/platform/mac/paste_mac.mm        # CGEventPost Cmd+V
      src/platform/mac/permissions_mac.mm  # TCC probes + settings deep links
      src/platform/mac/mac_common.mm       # synthetic-event tagging
      ${MAC_RESOURCES}
  )
  set_source_files_properties(${MAC_RESOURCES} PROPERTIES
      MACOSX_PACKAGE_LOCATION Resources)

  target_include_directories(dictate_cpp PRIVATE src/platform/mac)
  set_target_properties(dictate_cpp PROPERTIES
      MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_BINARY_DIR}/Info.plist
      # cxx_std_20 below only covers CXX; the .mm files need this too.
      OBJCXX_STANDARD 20
      OBJCXX_STANDARD_REQUIRED ON)

  target_link_libraries(dictate_cpp PRIVATE
      "-framework Cocoa"
      "-framework Carbon"
      "-framework AVFoundation"
      "-framework UserNotifications"
      "-framework ApplicationServices"
      "-framework CoreFoundation"
      "-framework CoreAudio"
      "-framework AudioUnit"
      "-framework AudioToolbox")

  # Ad-hoc signing with an explicit --identifier keeps TCC grants across
  # rebuilds; without it every build looks like a different app.
  add_custom_command(TARGET dictate_cpp POST_BUILD
      COMMAND codesign --force --sign "${DICTATE_CODESIGN_IDENTITY}"
              --identifier "${DICTATE_BUNDLE_ID}"
              "$<TARGET_BUNDLE_DIR:dictate_cpp>"
      COMMENT "Code signing dictate_cpp.app as ${DICTATE_BUNDLE_ID}"
      VERBATIM)
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
