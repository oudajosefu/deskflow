# SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
# SPDX-License-Identifier: MIT

set(AUDIO_SOURCES
  AudioClient.cpp
  AudioClient.h
  AudioServer.cpp
  AudioServer.h
  AudioTypes.h
  IAudioCapture.h
  IAudioPlayback.h
  OpusHelper.cpp
  OpusHelper.h
)

if(WIN32)
  list(APPEND AUDIO_SOURCES
    WinAudioCapture.cpp
    WinAudioCapture.h
    WinAudioPlayback.cpp
    WinAudioPlayback.h
  )
elseif(APPLE)
  list(APPEND AUDIO_SOURCES
    MacAudioCapture.h
    MacAudioCapture.mm
    MacAudioPlayback.cpp
    MacAudioPlayback.h
  )
else()
  list(APPEND AUDIO_SOURCES
    LinuxAudioCapture.cpp
    LinuxAudioCapture.h
    LinuxAudioPlayback.cpp
    LinuxAudioPlayback.h
  )
endif()

add_library(audio STATIC ${AUDIO_SOURCES})

target_include_directories(audio PUBLIC ${PROJECT_SOURCE_DIR}/src/lib)

# Opus — prefer the pkg-config imported target for correct RPATH/search paths
if(TARGET PkgConfig::OPUS)
  target_link_libraries(audio PUBLIC PkgConfig::OPUS)
elseif(TARGET Opus::opus)
  target_link_libraries(audio PUBLIC Opus::opus)
elseif(OPUS_FOUND)
  target_include_directories(audio PRIVATE ${OPUS_INCLUDE_DIRS})
  target_link_libraries(audio PUBLIC ${OPUS_LINK_LIBRARIES})
endif()

# Qt Network (for QTcpServer / QTcpSocket in AudioServer / AudioClient)
target_link_libraries(audio PUBLIC Qt6::Core Qt6::Network)

# Platform audio backends
if(WIN32)
  target_link_libraries(audio PRIVATE Ole32 avrt)
elseif(APPLE)
  find_library(LIB_AUDIO_TOOLBOX AudioToolbox REQUIRED)
  find_library(LIB_SCREEN_CAPTURE ScreenCaptureKit)
  find_library(LIB_AV_FOUNDATION AVFoundation)
  target_link_libraries(audio PRIVATE ${LIB_AUDIO_TOOLBOX})
  if(LIB_SCREEN_CAPTURE)
    target_link_libraries(audio PRIVATE ${LIB_SCREEN_CAPTURE})
  endif()
  if(LIB_AV_FOUNDATION)
    target_link_libraries(audio PRIVATE ${LIB_AV_FOUNDATION})
  endif()
else()
  if(TARGET PkgConfig::PULSEAUDIO)
    target_link_libraries(audio PRIVATE PkgConfig::PULSEAUDIO)
  elseif(PULSEAUDIO_FOUND)
    target_include_directories(audio PRIVATE ${PULSEAUDIO_INCLUDE_DIRS})
    target_link_libraries(audio PRIVATE ${PULSEAUDIO_LINK_LIBRARIES})
  endif()
endif()

# Also link base (for Log.h)
target_link_libraries(audio PRIVATE base)
