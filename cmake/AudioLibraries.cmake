# SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
# SPDX-License-Identifier: MIT

#
# Audio routing support detection.
# Sets AUDIO_CAPTURE_AVAILABLE and AUDIO_PLAYBACK_AVAILABLE,
# and AUDIO_CAPTURE_BACKEND / AUDIO_PLAYBACK_BACKEND for diagnostics.
#
macro(configure_audio_libs)

  if(NOT BUILD_AUDIO_SUPPORT)
    return()
  endif()

  # Opus codec is required on all platforms
  find_package(PkgConfig QUIET)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(OPUS IMPORTED_TARGET opus)
  endif()

  if(NOT OPUS_FOUND)
    find_package(Opus QUIET)
    if(Opus_FOUND)
      set(OPUS_FOUND TRUE)
    endif()
  endif()

  if(NOT OPUS_FOUND)
    message(WARNING "libopus not found — audio support disabled")
    set(BUILD_AUDIO_SUPPORT OFF CACHE BOOL "" FORCE)
    return()
  endif()

  message(STATUS "Opus found: ${OPUS_VERSION}")

  if(WIN32)
    # WASAPI is part of the Windows SDK — always available on Vista+
    set(AUDIO_CAPTURE_AVAILABLE TRUE)
    set(AUDIO_PLAYBACK_AVAILABLE TRUE)
    set(AUDIO_CAPTURE_BACKEND "WASAPI")
    set(AUDIO_PLAYBACK_BACKEND "WASAPI")
  elseif(APPLE)
    # ScreenCaptureKit requires macOS 13+; CoreAudio is always present
    if(CMAKE_OSX_DEPLOYMENT_TARGET STREQUAL "" OR
       CMAKE_OSX_DEPLOYMENT_TARGET VERSION_GREATER_EQUAL "13.0")
      set(AUDIO_CAPTURE_AVAILABLE TRUE)
      set(AUDIO_CAPTURE_BACKEND "ScreenCaptureKit")
    else()
      message(WARNING "macOS deployment target < 13.0; audio capture disabled")
      set(AUDIO_CAPTURE_AVAILABLE FALSE)
    endif()
    set(AUDIO_PLAYBACK_AVAILABLE TRUE)
    set(AUDIO_PLAYBACK_BACKEND "CoreAudio")
  else()
    # Linux: require both libpulse (async client) and libpulse-simple (pa_simple_* API)
    if(PKG_CONFIG_FOUND)
      pkg_check_modules(PULSEAUDIO IMPORTED_TARGET libpulse)
      pkg_check_modules(PULSEAUDIO_SIMPLE IMPORTED_TARGET libpulse-simple)
    endif()
    if(PULSEAUDIO_FOUND AND PULSEAUDIO_SIMPLE_FOUND)
      set(AUDIO_CAPTURE_AVAILABLE TRUE)
      set(AUDIO_PLAYBACK_AVAILABLE TRUE)
      set(AUDIO_CAPTURE_BACKEND "PulseAudio")
      set(AUDIO_PLAYBACK_BACKEND "PulseAudio")
      message(STATUS "PulseAudio found: ${PULSEAUDIO_VERSION}")
    else()
      message(WARNING "libpulse or libpulse-simple not found — audio support disabled on Linux")
      set(BUILD_AUDIO_SUPPORT OFF CACHE BOOL "" FORCE)
      return()
    endif()
  endif()

  message(STATUS "Audio capture backend : ${AUDIO_CAPTURE_BACKEND}")
  message(STATUS "Audio playback backend: ${AUDIO_PLAYBACK_BACKEND}")

endmacro()
