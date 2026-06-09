/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QList>
#include <QString>

///
/// One enumerated audio device. `id` is the value to put on a GStreamer sink's
/// "device" property (empty for the system default); `name` is for display.
///
struct AudioDeviceInfo
{
  QString id;
  QString name;
};

///
/// Enumerates audio devices via GStreamer's GstDeviceMonitor so the GUI can offer
/// an output-device dropdown. Safe to call from the GUI process; gst_init() is
/// invoked defensively (it is idempotent).
///
class AudioDevices
{
public:
  /// Initialise GStreamer once for this process. On Windows/macOS it first points
  /// GStreamer at the plugins bundled next to the executable (set via GST_PLUGIN_PATH
  /// before gst_init); on Linux the system plugins are found by default. Idempotent.
  /// A Q(Core)Application must already exist (needed to locate the executable dir).
  static void initGStreamer();

  /// Output (playback) devices — used for the server-side "play audio on" picker.
  static QList<AudioDeviceInfo> outputDevices();
};
