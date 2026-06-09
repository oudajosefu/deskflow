# SPDX-FileCopyrightText: (C) 2024 Deskflow Developers
# SPDX-FileCopyrightText: (C) 2012 - 2024 Symless Ltd
# SPDX-FileCopyrightText: (C) 2009 - 2012 Nick Bolton
# SPDX-License-Identifier: MIT

add_subdirectory(arch)
add_subdirectory(base)
add_subdirectory(client)
add_subdirectory(common)
add_subdirectory(deskflow)
add_subdirectory(io)
add_subdirectory(mt)
add_subdirectory(net)
add_subdirectory(platform)
add_subdirectory(server)

# audio must be configured before gui: the GUI links the audio library for
# output-device enumeration (AudioDevices) when audio support is enabled.
if(BUILD_AUDIO_SUPPORT)
  add_subdirectory(audio)
endif()

add_subdirectory(gui)
