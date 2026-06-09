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
# Make GStreamer's vcpkg-provided .pc files discoverable by pkg-config. vcpkg
# installs them under <installed>/<triplet>/lib/pkgconfig but does not add that
# dir to PKG_CONFIG_PATH, so pkg_check_modules cannot find GStreamer without
# this. set(ENV{}) is process-global, so it also covers the pkg_check_modules
# call in src/lib/audio/CMakeLists.txt. No-op when not building with vcpkg.
#
function(add_vcpkg_pkgconfig_path)
  if(NOT DEFINED VCPKG_INSTALLED_DIR OR NOT DEFINED VCPKG_TARGET_TRIPLET)
    return()
  endif()

  # CMake's find_program cannot locate the host pkg-config.bat, and the vcpkg
  # toolchain does not always wire one up. Point PKG_CONFIG_EXECUTABLE at the
  # pkgconf the 'pkgconf' vcpkg dependency installs. Must be set before
  # find_package(PkgConfig). Cached + FORCE so it sticks across the configure.
  set(_pkgconf "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/pkgconf/pkgconf${CMAKE_EXECUTABLE_SUFFIX}")
  if(EXISTS "${_pkgconf}")
    set(PKG_CONFIG_EXECUTABLE "${_pkgconf}" CACHE FILEPATH "pkg-config from vcpkg pkgconf" FORCE)
  endif()

  set(_pcdir "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/pkgconfig")
  if(NOT EXISTS "${_pcdir}")
    return()
  endif()
  file(TO_NATIVE_PATH "${_pcdir}" _pcdir_native)
  if(WIN32)
    set(_sep ";")
  else()
    set(_sep ":")
  endif()
  set(_existing "$ENV{PKG_CONFIG_PATH}")
  if("${_existing}" STREQUAL "")
    set(ENV{PKG_CONFIG_PATH} "${_pcdir_native}")
  elseif(NOT "${_existing}" MATCHES "vcpkg_installed")
    set(ENV{PKG_CONFIG_PATH} "${_pcdir_native}${_sep}${_existing}")
  endif()
  message(STATUS "Audio: added vcpkg pkgconfig path ${_pcdir_native}")
endfunction()

function(configure_audio_libs)

  if(NOT BUILD_AUDIO_SUPPORT)
    return()
  endif()

  # On Windows/macOS GStreamer is installed by vcpkg, which ships .pc files but
  # does NOT add its pkgconfig dir to PKG_CONFIG_PATH for us. Without it,
  # pkg_check_modules cannot find GStreamer. Prepend the vcpkg pkgconfig dir so
  # detection (here and in src/lib/audio/CMakeLists.txt) works. set(ENV{}) is
  # process-global, so it persists to the audio subdirectory's pkg_check_modules.
  add_vcpkg_pkgconfig_path()

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
