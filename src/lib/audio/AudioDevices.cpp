/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "audio/AudioDevices.h"

#include <gst/gst.h>

QList<AudioDeviceInfo> AudioDevices::outputDevices()
{
  QList<AudioDeviceInfo> result;

  // gst_init is idempotent; calling it here lets the GUI enumerate devices
  // without depending on the core having initialised GStreamer first.
  if (!gst_is_initialized()) {
    gst_init(nullptr, nullptr);
  }

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

    // The exact string the sink element expects on its "device" property.
    if (GstElement *element = gst_device_create_element(device, nullptr)) {
      if (g_object_class_find_property(G_OBJECT_GET_CLASS(element), "device") != nullptr) {
        gchar *deviceId = nullptr;
        g_object_get(element, "device", &deviceId, nullptr);
        if (deviceId != nullptr) {
          info.id = QString::fromUtf8(deviceId);
          g_free(deviceId);
        }
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
