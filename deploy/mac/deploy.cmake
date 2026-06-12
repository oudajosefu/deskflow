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

  # Bundle the GStreamer runtime for audio routing. macdeployqt (run above) handles
  # Qt and the linked GStreamer core dylibs, but the runtime-LOADED plugins are not
  # linked, so we copy the needed ones into Contents/Resources/gstreamer-1.0 (where
  # the app points GST_PLUGIN_PATH via AudioDevices::initGStreamer) and use dylibbundler
  # to recursively pull in their dylib deps (the framework's @rpath-referenced core libs,
  # resolved via the -s search path below) and rewrite them to @loader_path under
  # Contents/Frameworks.
  if(BUILD_AUDIO_SUPPORT AND GSTREAMER_PLUGIN_DIR AND EXISTS "${GSTREAMER_PLUGIN_DIR}")
    find_program(DYLIBBUNDLER dylibbundler)
    # The plugins reference the GStreamer core dylibs (libgstapp-1.0.0.dylib, etc.) by
    # @rpath/bare name. dylibbundler can't resolve those on its own — without a search path
    # it drops into an interactive "… does not exist. Try again" prompt that hangs CI forever.
    # The core dylibs live one level above the plugin dir (…/Versions/1.0/lib), so hand that to
    # dylibbundler via -s. As a hang-guard we also feed it 'quit' on stdin: if a dependency is
    # still unresolved it aborts (non-zero) instead of looping, and we fail the build loudly.
    get_filename_component(_gst_libdir "${GSTREAMER_PLUGIN_DIR}/.." ABSOLUTE)
    set(_dylibbundler_stdin "${CMAKE_BINARY_DIR}/dylibbundler-stdin.txt")
    file(WRITE "${_dylibbundler_stdin}" "quit\n")
    install(CODE "
      set(_app \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_PROJECT_PROPER_NAME}.app\")
      set(_dst \"\${_app}/Contents/Resources/gstreamer-1.0\")
      file(MAKE_DIRECTORY \"\${_dst}\")
      foreach(_p coreelements app audioconvert audioresample audiomixer audiorate volume level opus rtp rtpmanager udp autodetect osxaudio typefindfunctions audioparsers)
        file(GLOB _pl \"${GSTREAMER_PLUGIN_DIR}/libgst\${_p}.dylib\")
        if(_pl)
          file(COPY \${_pl} DESTINATION \"\${_dst}\")
        endif()
      endforeach()
      if(NOT \"${DYLIBBUNDLER}\" STREQUAL \"DYLIBBUNDLER-NOTFOUND\")
        file(GLOB _plugins \"\${_dst}/*.dylib\")
        foreach(_pl \${_plugins})
          execute_process(COMMAND \"${DYLIBBUNDLER}\" -of -b
            -x \"\${_pl}\"
            -d \"\${_app}/Contents/Frameworks\"
            -p \"@loader_path/../../Frameworks\"
            -s \"${_gst_libdir}\"
            INPUT_FILE \"${_dylibbundler_stdin}\"
            RESULT_VARIABLE _db_rc)
          if(NOT _db_rc EQUAL 0)
            message(FATAL_ERROR \"dylibbundler failed (\${_db_rc}) on \${_pl}: a GStreamer dependency could not be resolved under ${_gst_libdir}\")
          endif()
        endforeach()
      else()
        message(WARNING \"dylibbundler not found; GStreamer plugin dylib deps not bundled\")
      endif()
    ")
  endif()
endif()
