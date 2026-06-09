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
find_package(PkgConfig REQUIRED)
# Ensure the vcpkg pkgconfig dir is on PKG_CONFIG_PATH (defined in AudioLibraries.cmake).
if(COMMAND add_vcpkg_pkgconfig_path)
  add_vcpkg_pkgconfig_path()
endif()
pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET
  gstreamer-1.0>=1.20
  gstreamer-base-1.0
  gstreamer-app-1.0
  gstreamer-audio-1.0
)
target_link_libraries(audio PUBLIC PkgConfig::GSTREAMER)

# Qt Network (for QTcpServer / QTcpSocket in the AudioServer / AudioClient control plane)
target_link_libraries(audio PUBLIC Qt6::Core Qt6::Network)

# Platform capture shim
if(APPLE)
  find_library(LIB_AUDIO_TOOLBOX AudioToolbox REQUIRED)
  find_library(LIB_CORE_MEDIA CoreMedia REQUIRED)
  find_library(LIB_SCREEN_CAPTURE ScreenCaptureKit)
  find_library(LIB_AV_FOUNDATION AVFoundation)
  target_link_libraries(audio PRIVATE ${LIB_AUDIO_TOOLBOX} ${LIB_CORE_MEDIA})
  if(LIB_SCREEN_CAPTURE)
    target_link_libraries(audio PRIVATE ${LIB_SCREEN_CAPTURE})
  endif()
  if(LIB_AV_FOUNDATION)
    target_link_libraries(audio PRIVATE ${LIB_AV_FOUNDATION})
  endif()
endif()

# Also link base (for Log.h)
target_link_libraries(audio PRIVATE base)
