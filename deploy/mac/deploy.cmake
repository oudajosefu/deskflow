# SPDX-FileCopyrightText: (C) 2024 Chris Rizzitello <sithlord48@gmail.com>
# SPDX-License-Identifier: MIT

# HACK This is set when the files is included so its the real path
# calling CMAKE_CURRENT_LIST_DIR after include would return the wrong scope var
set(MY_DIR ${CMAKE_CURRENT_LIST_DIR})
set(OSX_BUNDLE ${BUILD_OSX_BUNDLE})

set(OS_STRING "macos-${BUILD_ARCHITECTURE}")

if (OSX_BUNDLE)
  install(CODE "execute_process(COMMAND
    ${DEPLOYQT}
    \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_PROJECT_PROPER_NAME}.app\"
    -timestamp -codesign=-
  )")
  set(CPACK_PACKAGE_ICON "${MY_DIR}/dmg-volume.icns")
  set(CPACK_DMG_BACKGROUND_IMAGE "${MY_DIR}/dmg-background.tiff")
  set(CPACK_DMG_DS_STORE_SETUP_SCRIPT "${MY_DIR}/generate_ds_store.applescript")
  set(CPACK_DMG_VOLUME_NAME "${CMAKE_PROJECT_PROPER_NAME}")
  set(CPACK_DMG_SLA_USE_RESOURCE_FILE_LICENSE ON)
  set(CPACK_GENERATOR "DragNDrop")

  # Bundle the GStreamer runtime for audio routing. macdeployqt (run above) handles Qt, but it
  # does not follow the GStreamer.framework dylibs (referenced by @rpath), and the runtime-LOADED
  # plugins are not linked at all. So we copy the plugins we use into Contents/Resources/gstreamer-1.0
  # (where the app points GST_PLUGIN_PATH via AudioDevices::initGStreamer) and bundle their full
  # dylib dependency closure into Contents/Frameworks ourselves — see deploy/mac/bundle-gstreamer.sh.
  #
  # That walk is hand-rolled (otool BFS resolving @rpath by basename in the framework lib dir)
  # rather than file(GET_RUNTIME_DEPENDENCIES) or dylibbundler: on macOS both resolve @rpath only
  # from the scanned binary's own LC_RPATH, and once the plugins are copied out of the framework
  # their rpath no longer points there, so everything came back unresolved (dylibbundler 1.0.5 had
  # the same blind spot and silently shipped an empty Frameworks dir -> crash at launch).
  if(BUILD_AUDIO_SUPPORT AND GSTREAMER_PLUGIN_DIR AND EXISTS "${GSTREAMER_PLUGIN_DIR}")
    # The GStreamer core dylibs live one level above the plugin dir (…/Versions/1.0/lib).
    get_filename_component(_gst_libdir "${GSTREAMER_PLUGIN_DIR}/.." ABSOLUTE)
    install(CODE "
      execute_process(
        COMMAND bash \"${MY_DIR}/bundle-gstreamer.sh\"
          \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_PROJECT_PROPER_NAME}.app\"
          \"${GSTREAMER_PLUGIN_DIR}\"
          \"${_gst_libdir}\"
        RESULT_VARIABLE _rc)
      if(NOT _rc EQUAL 0)
        message(FATAL_ERROR \"GStreamer dylib bundling failed (exit \${_rc})\")
      endif()
    ")
  endif()
endif()
