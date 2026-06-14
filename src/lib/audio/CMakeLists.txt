# SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
# SPDX-License-Identifier: MIT

#
# Audio routing library.
#
# The heavy lifting (capture, encode, RTP packetisation, jitter buffering,
# decode and playback) is done by GStreamer pipelines. This library only:
#   * GstAudioPipeline  — thin RAII wrapper over gst_parse_launch + bus watch
#   * GstAudioSender    — client capture -> opus -> RTP -> udpsink pipeline
#   * GstAudioReceiver  — server  udpsrc -> jitterbuffer -> opus -> volume -> sink
#   * AudioDevices      — GstDeviceMonitor enumeration for the GUI
#   * AudioClient/Server — the small TCP control plane (handshake + RTP port
#                          negotiation) that drives the sender/receiver
# On macOS a trimmed ScreenCaptureKit capture (MacAudioCapture) feeds an appsrc,
# because GStreamer has no stock system-output loopback source there.
#

set(AUDIO_SOURCES
  AudioClient.cpp
  AudioClient.h
  AudioServer.cpp
  AudioServer.h
  AudioTypes.h
  AudioDevices.cpp
  AudioDevices.h
  GstAudioPipeline.cpp
  GstAudioPipeline.h
  GstAudioSender.cpp
  GstAudioSender.h
  GstAudioReceiver.cpp
  GstAudioReceiver.h
)

if(APPLE)
  list(APPEND AUDIO_SOURCES
    MacAudioCapture.h
    MacAudioCapture.mm
  )
  # MacAudioCapture.mm is written in ARC idiom (no manual retain/release; Obj-C
  # object members released by assigning nullptr). It must be compiled with ARC,
  # otherwise the SCShareableContent returned by the async completion handler is
  # never retained and is freed before use -> EXC_BAD_ACCESS in objc_msgSend.
  set_source_files_properties(MacAudioCapture.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
endif()

add_library(audio STATIC ${AUDIO_SOURCES})

target_include_directories(audio PUBLIC ${PROJECT_SOURCE_DIR}/src/lib)

# GStreamer (and its glib/gobject companions, pulled transitively via the .pc
# Requires). Re-run pkg_check_modules here so the IMPORTED_TARGET is created in
# this directory's scope regardless of what configure_audio_libs() did.
# Ensure GStreamer's pkgconfig dir is on PKG_CONFIG_PATH (defined in AudioLibraries.cmake).
if(COMMAND setup_gstreamer_pkgconfig)
  setup_gstreamer_pkgconfig()
endif()
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET
  gstreamer-1.0>=1.20
  gstreamer-base-1.0
  gstreamer-app-1.0
  gstreamer-audio-1.0
)
target_link_libraries(audio PUBLIC PkgConfig::GSTREAMER)

# Discover the GStreamer install prefix and plugin dir so the packaging code can
# bundle the runtime (core DLLs + the runtime-loaded plugins) on Windows/macOS.
# GStreamer's .pc exposes both as pkg-config variables.
execute_process(
  COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=pluginsdir gstreamer-1.0
  OUTPUT_VARIABLE _gst_plugindir OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(
  COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=prefix gstreamer-1.0
  OUTPUT_VARIABLE _gst_prefix OUTPUT_STRIP_TRAILING_WHITESPACE)
# pkg-config shell-escapes spaces in --variable output as "\ " (the official Windows SDK
# installs under C:/Program\ Files/...). Left raw, that backslash corrupts the path for every
# consumer — EXISTS, install(DIRECTORY), file(GLOB) — which silently skipped plugin bundling
# and made CPack's runtime-dependency scan fail to resolve the GStreamer core DLLs. pkgconf
# emits forward-slash separators, so the only backslashes are these escapes — strip them.
# (No-op on macOS/Linux, whose paths have no spaces. Do NOT pipe this through
# file(TO_CMAKE_PATH): on captured output the plain/regex space-replaces are no-ops, and
# TO_CMAKE_PATH would then turn the surviving "\ " into "/ " and re-break the path.)
string(REPLACE "\\" "" _gst_plugindir "${_gst_plugindir}")
string(REPLACE "\\" "" _gst_prefix "${_gst_prefix}")
set(GSTREAMER_PLUGIN_DIR "${_gst_plugindir}" CACHE PATH "GStreamer runtime plugin directory" FORCE)
set(GSTREAMER_PREFIX "${_gst_prefix}" CACHE PATH "GStreamer install prefix" FORCE)
message(STATUS "Audio: GStreamer prefix ${GSTREAMER_PREFIX}, plugins ${GSTREAMER_PLUGIN_DIR}")

# Qt Network (for QTcpServer / QTcpSocket in the AudioServer / AudioClient control plane)
target_link_libraries(audio PUBLIC Qt6::Core Qt6::Network)

# Platform capture shim
if(APPLE)
  find_library(LIB_AUDIO_TOOLBOX AudioToolbox REQUIRED)
  find_library(LIB_CORE_MEDIA CoreMedia REQUIRED)
  find_library(LIB_AV_FOUNDATION AVFoundation)
  target_link_libraries(audio PRIVATE ${LIB_AUDIO_TOOLBOX} ${LIB_CORE_MEDIA})
  if(LIB_AV_FOUNDATION)
    target_link_libraries(audio PRIVATE ${LIB_AV_FOUNDATION})
  endif()
  # ScreenCaptureKit only exists on macOS 13+. Weak-link it so a build with an
  # older deployment target (e.g. the macos-x64 CI target = 12) still links and
  # runs on macOS 12 (capture is gated at runtime by `if (@available(macOS 13,*))`
  # in MacAudioCapture.mm) and uses it on 13+.
  # PUBLIC (not PRIVATE): `audio` is a STATIC library, so the option must propagate
  # via INTERFACE_LINK_OPTIONS to the executables that consume it — otherwise the
  # framework is never linked and the SC* symbols are undefined at exe link time.
  target_link_options(audio PUBLIC -Wl,-weak_framework,ScreenCaptureKit)
endif()

# Also link base (for Log.h)
target_link_libraries(audio PRIVATE base)
