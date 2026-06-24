# SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
# SPDX-License-Identifier: MIT

create_test(
  NAME AudioDeviceIdTests
  DEPENDS audio
  SOURCE AudioDeviceIdTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/audio"
)
