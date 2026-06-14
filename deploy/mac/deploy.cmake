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
  # dylib dependency closure into Contents/Frameworks ourselves.
  #
  # We resolve the closure with file(GET_RUNTIME_DEPENDENCIES), not dylibbundler: dylibbundler 1.0.5
  # cannot resolve @rpath-referenced deps — it prints "can't get path for '@rpath/…' … MAY NOT
  # CORRECTLY HANDLE THIS DEPENDENCY", copies nothing, and still exits 0, so the app shipped with an
  # empty Frameworks dir and crashed at launch (dyld: @rpath/libgstapp-1.0.0.dylib missing).
  if(BUILD_AUDIO_SUPPORT AND GSTREAMER_PLUGIN_DIR AND EXISTS "${GSTREAMER_PLUGIN_DIR}")
    # The GStreamer core dylibs live one level above the plugin dir (…/Versions/1.0/lib); hand that
    # to the resolver so it can follow the framework's mutual @rpath references.
    get_filename_component(_gst_libdir "${GSTREAMER_PLUGIN_DIR}/.." ABSOLUTE)
    install(CODE "
      set(_app \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_PROJECT_PROPER_NAME}.app\")
      set(_dst \"\${_app}/Contents/Resources/gstreamer-1.0\")
      set(_fw \"\${_app}/Contents/Frameworks\")
      file(MAKE_DIRECTORY \"\${_dst}\")
      file(MAKE_DIRECTORY \"\${_fw}\")

      # Copy the runtime-loaded plugins we use.
      foreach(_p coreelements app audioconvert audioresample audiomixer audiorate volume level opus rtp rtpmanager udp autodetect osxaudio typefindfunctions audioparsers)
        file(GLOB _pl \"${GSTREAMER_PLUGIN_DIR}/libgst\${_p}.dylib\")
        if(_pl)
          file(COPY \${_pl} DESTINATION \"\${_dst}\")
        endif()
      endforeach()

      # Resolve the plugins' dylib closure. Scanning the plugins alone is enough: every GStreamer
      # core dylib the executables link (libgstreamer/base/app/audio) is also a plugin dependency,
      # so the closure is a superset of what the exes need — and skipping the exes avoids dragging
      # in the Qt frameworks macdeployqt already bundled.
      file(GLOB _plugins \"\${_dst}/*.dylib\")
      file(GET_RUNTIME_DEPENDENCIES
        MODULES \${_plugins}
        RESOLVED_DEPENDENCIES_VAR _res
        UNRESOLVED_DEPENDENCIES_VAR _unres
        DIRECTORIES \"${_gst_libdir}\"
        POST_EXCLUDE_REGEXES \"^/usr/lib\" \"^/System\"
      )
      # A framework dylib we could not resolve is a real packaging gap; fail loudly. (System libs
      # under /usr/lib and /System are dropped by POST_EXCLUDE and never appear here.)
      set(_missing)
      foreach(_u \${_unres})
        string(TOLOWER \"\${_u}\" _ul)
        if(_ul MATCHES \"(gst|glib|gobject|gio|gmodule|orc|opus|ffi|intl|pcre|graphene|libsoup|nice|json-glib)\")
          list(APPEND _missing \"\${_u}\")
        endif()
      endforeach()
      if(_missing)
        message(FATAL_ERROR \"Audio: unresolved GStreamer dylib dependencies (not found under ${_gst_libdir}): \${_missing}\")
      endif()

      # Copy each resolved dylib into Contents/Frameworks and make its sibling @rpath/* references
      # resolve there (add @loader_path as an rpath). Re-sign ad-hoc afterwards (install_name_tool
      # invalidates the signature). Every consumer already references these by @rpath/<basename>.
      foreach(_d \${_res})
        get_filename_component(_dn \"\${_d}\" NAME)
        file(COPY \"\${_d}\" DESTINATION \"\${_fw}\")
        execute_process(COMMAND chmod u+w \"\${_fw}/\${_dn}\")
        execute_process(COMMAND install_name_tool -add_rpath \"@loader_path\" \"\${_fw}/\${_dn}\" ERROR_QUIET)
        execute_process(COMMAND codesign --force --sign - \"\${_fw}/\${_dn}\")
      endforeach()

      # The plugins reference the core dylibs by @rpath too; point that at Contents/Frameworks
      # (Resources/gstreamer-1.0 -> ../../Frameworks) and re-sign.
      foreach(_pl \${_plugins})
        execute_process(COMMAND chmod u+w \"\${_pl}\")
        execute_process(COMMAND install_name_tool -add_rpath \"@loader_path/../../Frameworks\" \"\${_pl}\" ERROR_QUIET)
        execute_process(COMMAND codesign --force --sign - \"\${_pl}\")
      endforeach()

      # The executables already carry an @loader_path/../Frameworks rpath (set via INSTALL_RPATH),
      # so their @rpath/libgst* references now resolve against the dylibs we just bundled.
      message(STATUS \"Audio: bundled GStreamer dylib closure into \${_fw}\")
    ")
  endif()
endif()
