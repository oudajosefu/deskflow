/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "audio/AudioDevices.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include <gst/gst.h>

void AudioDevices::initGStreamer()
{
  if (gst_is_initialized()) {
    return;
  }

  // On Windows/macOS the GStreamer plugins are bundled next to the app (in a
  // non-default dir), so point GStreamer at them before gst_init scans the
  // registry. On Linux the system plugins are found by default.
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
  if (QCoreApplication::instance() != nullptr) {
    const QString appDir = QCoreApplication::applicationDirPath();
#if defined(Q_OS_WIN)
    const QString pluginDir = appDir + QStringLiteral("/gstreamer-1.0");
#else
    // macOS bundle: <app>/Contents/MacOS/<exe> -> <app>/Contents/Resources/gstreamer-1.0
    const QString pluginDir = appDir + QStringLiteral("/../Resources/gstreamer-1.0");
#endif
    if (QFileInfo::exists(pluginDir)) {
      qputenv("GST_PLUGIN_PATH", QDir::toNativeSeparators(pluginDir).toUtf8());
    }
  }
#endif

  gst_init(nullptr, nullptr);
}

QList<AudioDeviceInfo> AudioDevices::outputDevices()
{
  QList<AudioDeviceInfo> result;

  // Ensure GStreamer is initialised (with the bundled plugin path) so the GUI can
  // enumerate devices without depending on the core having initialised it first.
  initGStreamer();

  GstDeviceMonitor *monitor = gst_device_monitor_new();
  GstCaps *caps = gst_caps_new_empty_simple("audio/x-raw");
  gst_device_monitor_add_filter(monitor, "Audio/Sink", caps);
  gst_caps_unref(caps);

  if (!gst_device_monitor_start(monitor)) {
    gst_object_unref(monitor);
    return result;
  }

  GList *devices = gst_device_monitor_get_devices(monitor);
  for (GList *it = devices; it != nullptr; it = it->next) {
    auto *device = static_cast<GstDevice *>(it->data);

    AudioDeviceInfo info;
    if (gchar *displayName = gst_device_get_display_name(device)) {
      info.name = QString::fromUtf8(displayName);
      g_free(displayName);
    }

    // The value the sink's "device" property expects. It is a string on
    // pulsesink/wasapi2sink but an int (AudioDeviceID) on osxaudiosink, so read
    // it by its declared type; stringified ints round-trip to the receiver.
    if (GstElement *element = gst_device_create_element(device, nullptr)) {
      GParamSpec *pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(element), "device");
      if (pspec != nullptr && pspec->value_type == G_TYPE_STRING) {
        gchar *deviceId = nullptr;
        g_object_get(element, "device", &deviceId, nullptr);
        if (deviceId != nullptr) {
          info.id = QString::fromUtf8(deviceId);
          g_free(deviceId);
        }
      } else if (pspec != nullptr && pspec->value_type == G_TYPE_INT) {
        gint deviceId = 0;
        g_object_get(element, "device", &deviceId, nullptr);
        info.id = QString::number(deviceId);
      }
      gst_object_unref(element);
    }

    if (!info.name.isEmpty()) {
      result.append(info);
    }
    gst_object_unref(device);
  }
  g_list_free(devices);

  gst_device_monitor_stop(monitor);
  gst_object_unref(monitor);
  return result;
}
