/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <cstdlib>
#include <string>

/// Parse a stored audio "device" id to the integer AudioDeviceID that
/// osxaudiosink's "device" property uses (pulsesink/wasapi2sink use the string
/// directly). Returns 0 (osxaudiosink's "system default") for empty or
/// non-numeric input, so a stale id never selects a bogus device. Kept free of
/// Qt and GStreamer so it is unit-testable on its own.
inline int audioDeviceIdToInt(const std::string &id)
{
  if (id.empty()) {
    return 0;
  }
  char *end = nullptr;
  const long value = std::strtol(id.c_str(), &end, 10);
  if (end == id.c_str() || *end != '\0') {
    return 0;
  }
  return static_cast<int>(value);
}
