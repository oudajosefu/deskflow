# SPDX-FileCopyrightText: (C) 2024 Chris Rizzitello <sithlord48@gmail.com>
# SPDX-License-Identifier: MIT

# HACK This is set when the files is included so its the real path
# calling CMAKE_CURRENT_LIST_DIR after include would return the wrong scope var
set(MY_DIR ${CMAKE_CURRENT_LIST_DIR})

set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${CMAKE_INSTALL_LIBDIR})
include(InstallRequiredSystemLibraries)

configure_file(${MY_DIR}/pre-cpack.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/pre-cpack.cmake @ONLY)
set(CPACK_PRE_BUILD_SCRIPTS ${CMAKE_CURRENT_BINARY_DIR}/pre-cpack.cmake)

configure_file(${MY_DIR}/cpack-options.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/cpack-options.cmake @ONLY)
set(CPACK_PROJECT_CONFIG_FILE ${CMAKE_CURRENT_BINARY_DIR}/cpack-options.cmake)

set(OS_STRING "win-${BUILD_ARCHITECTURE}")

list(APPEND CPACK_GENERATOR "7Z")

# If Wix4+ is installed make a package
find_program(WIX_APP wix)
if (NOT "${WIX_APP}" STREQUAL "")
  set(CPACK_WIX_VERSION 4)
  set(CPACK_WIX_ARCHITECTURE ${BUILD_ARCHITECTURE})
  list(APPEND CPACK_GENERATOR "WIX")
endif()

set(CPACK_PACKAGE_NAME "${CMAKE_PROJECT_PROPER_NAME}")

# Menu Entry
set(CPACK_WIX_PROGRAM_MENU_FOLDER "${CMAKE_PROJECT_PROPER_NAME}")
set(CPACK_PACKAGE_EXECUTABLES "deskflow" "${CMAKE_PROJECT_PROPER_NAME}")

# Default Install Path
set(CPACK_PACKAGE_INSTALL_DIRECTORY "${CMAKE_PROJECT_PROPER_NAME}")

# Wix Specific Values
set(CPACK_WIX_UPGRADE_GUID "027D1C8A-E7A5-4754-BB93-B2D45BFDBDC8")
set(CPACK_WIX_UI_BANNER "${MY_DIR}/wix-banner.png")
set(CPACK_WIX_UI_DIALOG "${MY_DIR}/wix-dialog.png")

# Required Extra Extenstions
list(APPEND CPACK_WIX_EXTENSIONS "WixToolset.Util.wixext" "WixToolset.Firewall.wixext")

# Make sure to also put the xmlns for the ext into the wix block on generated files
list(APPEND CPACK_WIX_CUSTOM_XMLNS "util=http://wixtoolset.org/schemas/v4/wxs/util" "firewall=http://wixtoolset.org/schemas/v4/wxs/firewall")

# The patch has to know the full path of our msm file
configure_file(
  ${MY_DIR}/wix-patch.xml.in
  ${CMAKE_CURRENT_BINARY_DIR}/wix-patch.xml @ONLY
)

# This patch set ups filewall rules, the service and msm module
set(CPACK_WIX_PATCH_FILE "${CMAKE_CURRENT_BINARY_DIR}/wix-patch.xml")

# Creates a DLL that can be used by our MSI for custom actions.
configure_file(
  ${MY_DIR}/wix-custom.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/wix-custom.h @ONLY
)
add_library(
  wix-custom SHARED
  ${MY_DIR}/wix-custom.cpp
)
target_include_directories(wix-custom PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
set_target_properties(wix-custom PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
target_link_libraries(wix-custom PRIVATE Msi)

# Bundle the GStreamer runtime for audio routing. The runtime-LOADED plugins (not linked, so
# not discovered by the executables' RUNTIME_DEPENDENCY_SET) go into <install>/gstreamer-1.0,
# where the app points GST_PLUGIN_PATH at startup (AudioDevices::initGStreamer). Their own
# dependency closure (GStreamer core libs, the GLib stack, orc, gio, zlib, opus, ...) is
# resolved from the plugins themselves and copied next to the executables.
if(BUILD_AUDIO_SUPPORT AND GSTREAMER_PLUGIN_DIR AND EXISTS "${GSTREAMER_PLUGIN_DIR}")
  # The audio-routing plugins we ship (the SDK plugin dir holds ~hundreds; we want this set).
  set(_gst_plugin_regex "gst(coreelements|app|audioconvert|audioresample|audiomixer|audiorate|volume|level|opus|rtp|rtpmanager|udp|autodetect|typefindfunctions|audioparsers|wasapi2|wasapi)\\.dll$")
  install(DIRECTORY "${GSTREAMER_PLUGIN_DIR}/"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/gstreamer-1.0
    FILES_MATCHING REGEX "${_gst_plugin_regex}"
  )

  # Bundle the plugins' runtime dependency closure next to the executables. We resolve it from
  # the plugins with file(GET_RUNTIME_DEPENDENCIES) rather than name-globbing the SDK bin: the
  # plugins are not linked by our exe, so several deps (orc-0.4-0.dll, gio-2.0-0.dll, z-1.dll)
  # match neither "gst*.dll" nor the exe's own dependency scan and were dropped — which broke
  # plugin load with "the specified module could not be found". Scanning only our plugins also
  # stops us shipping the dozens of unrelated SDK libs (cuda/d3d/webrtc/...) a glob pulls in.
  if(GSTREAMER_PREFIX AND EXISTS "${GSTREAMER_PREFIX}/bin")
    install(CODE "
      file(GLOB _all_plugins \"${GSTREAMER_PLUGIN_DIR}/*.dll\")
      set(_plugins)
      foreach(_p \${_all_plugins})
        get_filename_component(_pn \"\${_p}\" NAME)
        if(_pn MATCHES \"${_gst_plugin_regex}\")
          list(APPEND _plugins \"\${_p}\")
        endif()
      endforeach()
      message(STATUS \"Audio: resolving GStreamer plugin dependency closure from ${GSTREAMER_PREFIX}/bin\")
      file(GET_RUNTIME_DEPENDENCIES
        MODULES \${_plugins}
        RESOLVED_DEPENDENCIES_VAR _res
        UNRESOLVED_DEPENDENCIES_VAR _unres
        DIRECTORIES \"${GSTREAMER_PREFIX}/bin\"
        PRE_EXCLUDE_REGEXES \"^api-ms-\" \"^ext-ms-\"
        POST_EXCLUDE_REGEXES \"[Ss]ystem32\"
      )
      # Fail loudly if a GStreamer/GLib/codec dependency could not be located in the SDK bin
      # (a real packaging gap). Genuine Windows system DLLs resolve via PATH and are dropped by
      # POST_EXCLUDE, so they never land here; we still guard by family name to be safe.
      set(_missing)
      foreach(_u \${_unres})
        string(TOLOWER \"\${_u}\" _ul)
        if(_ul MATCHES \"(gst|glib|gobject|gio-|gmodule|orc|opus|ffi|intl|pcre|graphene|nice|json-glib|^z-)\")
          list(APPEND _missing \"\${_u}\")
        endif()
      endforeach()
      if(_missing)
        message(FATAL_ERROR \"Audio: unresolved GStreamer plugin dependencies (not found in ${GSTREAMER_PREFIX}/bin): \${_missing}\")
      endif()
      file(INSTALL
        DESTINATION \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}\"
        TYPE SHARED_LIBRARY
        FOLLOW_SYMLINK_CHAIN
        FILES \${_res}
      )
    ")
  endif()
endif()
