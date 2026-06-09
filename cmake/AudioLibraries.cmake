# SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
# SPDX-License-Identifier: MIT

#
# Audio routing support detection.
#
# The audio routing feature streams audio from a client to the server using
# GStreamer (capture -> opus -> RTP/UDP -> jitterbuffer -> playback). GStreamer
# provides the cross-platform capture/playback elements, the Opus codec, RTP
# packetisation and the jitter buffer, so the only build dependency we need to
# locate here is GStreamer itself (plus a few of its companion libraries).
#
# On Windows/macOS GStreamer is provided by vcpkg (see cmake/vcpkg.json.in); on
# Linux it comes from the system packages (gstreamer1.0-plugins-{base,good,bad}).
# In all cases it is discovered through pkg-config.
#
# Sets HAVE_AUDIO_SUPPORT-gating: if GStreamer cannot be found the feature is
# disabled (BUILD_AUDIO_SUPPORT forced OFF) rather than failing the whole build.
#
#
# Make GStreamer's .pc files discoverable by pkg-config, wherever it came from:
#   * the official prebuilt GStreamer MSVC SDK (Windows x64), located via the
#     GSTREAMER_1_0_ROOT_MSVC_* env var the SDK installer sets;
#   * vcpkg (Windows arm64), under <installed>/<triplet>/lib/pkgconfig — vcpkg
#     does not add this to PKG_CONFIG_PATH for us, and CMake's find_program cannot
#     locate a host pkg-config.bat, so we also point PKG_CONFIG_EXECUTABLE at the
#     pkgconf the 'pkgconf' dependency provides;
#   * the system (Linux/macOS/BSD) — pkg-config already works; nothing to add.
# set(ENV{}) is process-global, so this also covers the pkg_check_modules call in
# src/lib/audio/CMakeLists.txt. Must run before find_package(PkgConfig).
#
function(setup_gstreamer_pkgconfig)
  if(WIN32)
    set(_sep ";")
  else()
    set(_sep ":")
  endif()

  # Prefer the vcpkg pkgconf as the pkg-config tool when available (reliable on
  # Windows, where the host pkg-config.bat is not found by find_program).
  if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    set(_pkgconf "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/pkgconf/pkgconf${CMAKE_EXECUTABLE_SUFFIX}")
    if(EXISTS "${_pkgconf}")
      set(PKG_CONFIG_EXECUTABLE "${_pkgconf}" CACHE FILEPATH "pkg-config from vcpkg pkgconf" FORCE)
    endif()
  endif()

  # Candidate pkgconfig dirs, highest priority first.
  set(_candidates)
  foreach(_root "$ENV{GSTREAMER_1_0_ROOT_MSVC_X86_64}" "$ENV{GSTREAMER_1_0_ROOT_MSVC_ARM64}")
    if(NOT _root)
      continue()
    endif()
    # The SDK installer sets these with native backslashes and a trailing slash
    # (e.g. C:\gstreamer\1.0\msvc_x86_64\). Normalise to forward slashes and strip
    # the trailing slash so EXISTS / path concatenation work.
    file(TO_CMAKE_PATH "${_root}" _root)
    string(REGEX REPLACE "/+$" "" _root "${_root}")
    if(EXISTS "${_root}/lib/pkgconfig")
      list(APPEND _candidates "${_root}/lib/pkgconfig")
      # Fall back to the SDK's bundled pkg-config if vcpkg's was not found.
      if(NOT PKG_CONFIG_EXECUTABLE AND EXISTS "${_root}/bin/pkg-config${CMAKE_EXECUTABLE_SUFFIX}")
        set(PKG_CONFIG_EXECUTABLE "${_root}/bin/pkg-config${CMAKE_EXECUTABLE_SUFFIX}"
            CACHE FILEPATH "pkg-config from GStreamer SDK" FORCE)
      endif()
    endif()
  endforeach()
  if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    set(_vcpkg_pc "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/pkgconfig")
    if(EXISTS "${_vcpkg_pc}")
      list(APPEND _candidates "${_vcpkg_pc}")
    endif()
  endif()

  # Prepend candidates so the highest-priority one (the SDK) ends up first in
  # PKG_CONFIG_PATH. Iterate in reverse because each iteration prepends.
  if(_candidates)
    list(REVERSE _candidates)
  endif()
  foreach(_dir ${_candidates})
    file(TO_NATIVE_PATH "${_dir}" _native)
    set(_existing "$ENV{PKG_CONFIG_PATH}")
    string(FIND "${_existing}" "${_native}" _already)
    if("${_existing}" STREQUAL "")
      set(ENV{PKG_CONFIG_PATH} "${_native}")
    elseif(_already EQUAL -1)
      set(ENV{PKG_CONFIG_PATH} "${_native}${_sep}${_existing}")
    endif()
    message(STATUS "Audio: added GStreamer pkgconfig path ${_native}")
  endforeach()
endfunction()

function(configure_audio_libs)

  if(NOT BUILD_AUDIO_SUPPORT)
    return()
  endif()

  # Make GStreamer discoverable by pkg-config (official SDK / vcpkg / system).
  setup_gstreamer_pkgconfig()

  find_package(PkgConfig QUIET)
  if(NOT PKG_CONFIG_FOUND)
    message(WARNING "pkg-config not found — audio routing disabled (needed to locate GStreamer)")
    set(BUILD_AUDIO_SUPPORT OFF CACHE BOOL "" FORCE)
    return()
  endif()

  # gstreamer-1.0       : core (GstElement, GstBus, GstDeviceMonitor, gst_parse_launch)
  # gstreamer-base-1.0  : base classes pulled in transitively by most elements
  # gstreamer-app-1.0   : appsrc (used by the macOS ScreenCaptureKit capture shim)
  # gstreamer-audio-1.0 : GstAudioInfo / audio caps helpers
  pkg_check_modules(GSTREAMER QUIET
    gstreamer-1.0>=1.20
    gstreamer-base-1.0
    gstreamer-app-1.0
    gstreamer-audio-1.0
  )

  if(NOT GSTREAMER_FOUND)
    message(WARNING
      "GStreamer (>= 1.20, with -base/-app/-audio dev files) not found — audio routing disabled.\n"
      "  Windows/macOS: ensure the vcpkg 'gstreamer' dependency built successfully.\n"
      "  Linux: install gstreamer1.0 + plugins-{base,good,bad} development packages.")
    set(BUILD_AUDIO_SUPPORT OFF CACHE BOOL "" FORCE)
    return()
  endif()

  message(STATUS "GStreamer found: ${GSTREAMER_gstreamer-1.0_VERSION}")
  message(STATUS "Audio routing transport: RTP/UDP via GStreamer (opus)")

endfunction()
