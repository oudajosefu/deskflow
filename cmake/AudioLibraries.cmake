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
# GStreamer comes from its official distribution on every platform, discovered via
# pkg-config:
#   * Windows (x64 + arm64): the official prebuilt GStreamer MSVC SDK, installed in CI
#     (see .github/actions/install-dependencies). The install sets the
#     GSTREAMER_1_0_ROOT_MSVC_{X86_64,ARM64} env var we key off below.
#   * macOS (x64 + arm64): the official Universal GStreamer.framework .pkg, under
#     /Library/Frameworks/GStreamer.framework.
#   * Linux/BSD: the system packages (gstreamer1.0 + plugins-{base,good}).
#
# If GStreamer cannot be found the feature is normally disabled (BUILD_AUDIO_SUPPORT
# forced OFF) rather than failing the whole build. Set REQUIRE_AUDIO_SUPPORT=ON (CI does
# this on every target that must ship audio) to turn "not found" into a hard error, so a
# provisioning regression can never again pass as a green build with audio silently off.
#
#
# Make GStreamer's .pc files discoverable by pkg-config, wherever it came from:
#   * the official prebuilt GStreamer MSVC SDK (Windows x64 + arm64), located via the
#     GSTREAMER_1_0_ROOT_MSVC_{X86_64,ARM64} env var the SDK install sets. CMake's
#     find_program cannot locate a host pkg-config.bat, so we also point
#     PKG_CONFIG_EXECUTABLE at the pkgconf the vcpkg 'pkgconf' dependency provides
#     (falling back to the SDK's own bundled pkg-config);
#   * the official GStreamer.framework (macOS), whose lib/pkgconfig is not on
#     pkg-config's default search path, so we add it explicitly;
#   * the system (Linux/BSD) — pkg-config already works; nothing to add.
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

  # macOS: the official Universal GStreamer.framework is not on pkg-config's default
  # search path (unlike Homebrew), so add its pkgconfig dir explicitly.
  if(APPLE)
    set(_gst_fw_pc "/Library/Frameworks/GStreamer.framework/Versions/1.0/lib/pkgconfig")
    if(EXISTS "${_gst_fw_pc}")
      list(APPEND _candidates "${_gst_fw_pc}")
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

# When ON, a missing GStreamer is a hard error instead of silently disabling audio.
# CI sets this on every target that must ship audio; local dev keeps it OFF so a
# checkout without GStreamer still configures.
option(REQUIRE_AUDIO_SUPPORT "Fail the build if audio support was requested but GStreamer is missing" OFF)

# Disable audio — or, under REQUIRE_AUDIO_SUPPORT, fail hard — with a consistent message.
macro(_audio_unavailable _reason)
  if(REQUIRE_AUDIO_SUPPORT)
    message(FATAL_ERROR
      "Audio routing required (REQUIRE_AUDIO_SUPPORT=ON) but ${_reason}.\n"
      "  Windows: install/extract the official GStreamer MSVC SDK (sets GSTREAMER_1_0_ROOT_MSVC_*).\n"
      "  macOS: install the official Universal GStreamer.framework .pkg.\n"
      "  Linux/BSD: install gstreamer1.0 + plugins-{base,good} development packages.")
  else()
    message(WARNING
      "${_reason} — audio routing disabled. Set REQUIRE_AUDIO_SUPPORT=ON to make this fatal.")
    set(BUILD_AUDIO_SUPPORT OFF CACHE BOOL "" FORCE)
  endif()
endmacro()

function(configure_audio_libs)

  if(NOT BUILD_AUDIO_SUPPORT)
    return()
  endif()

  # Make GStreamer discoverable by pkg-config (official SDK / vcpkg / system).
  setup_gstreamer_pkgconfig()

  find_package(PkgConfig QUIET)
  if(NOT PKG_CONFIG_FOUND)
    _audio_unavailable("pkg-config not found (needed to locate GStreamer)")
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
    _audio_unavailable("GStreamer (>= 1.20, with -base/-app/-audio dev files) not found")
    return()
  endif()

  message(STATUS "GStreamer found: ${GSTREAMER_gstreamer-1.0_VERSION}")
  message(STATUS "Audio routing transport: RTP/UDP via GStreamer (opus)")

endfunction()
