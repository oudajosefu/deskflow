/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "audio/GstAudioPipeline.h"

#include "audio/AudioDeviceId.h"
#include "base/Log.h"

#include <gst/gst.h>

namespace {

///
/// Bus sync handler: runs synchronously on the GStreamer thread that posted the
/// message. We only inspect it (log + forward fatal status) and always PASS the
/// message on, so we never need a GLib/Qt main loop to be running.
///
GstBusSyncReply busSyncHandler(GstBus * /*bus*/, GstMessage *msg, gpointer user)
{
  auto *self = static_cast<GstAudioPipeline *>(user);

  switch (GST_MESSAGE_TYPE(msg)) {
  case GST_MESSAGE_ERROR: {
    GError *err = nullptr;
    gchar *dbg = nullptr;
    gst_message_parse_error(msg, &err, &dbg);
    const std::string src = GST_OBJECT_NAME(msg->src) ? GST_OBJECT_NAME(msg->src) : "?";
    const std::string what = err && err->message ? err->message : "unknown error";
    LOG_ERR("audio pipeline error [%s]: %s%s%s", src.c_str(), what.c_str(), dbg ? " | " : "", dbg ? dbg : "");
    if (self) {
      self->dispatchStatus(false, src + ": " + what);
    }
    g_clear_error(&err);
    g_free(dbg);
    break;
  }
  case GST_MESSAGE_WARNING: {
    GError *err = nullptr;
    gchar *dbg = nullptr;
    gst_message_parse_warning(msg, &err, &dbg);
    const char *src = GST_OBJECT_NAME(msg->src) ? GST_OBJECT_NAME(msg->src) : "?";
    LOG_WARN("audio pipeline warning [%s]: %s", src, err && err->message ? err->message : "?");
    g_clear_error(&err);
    g_free(dbg);
    break;
  }
  case GST_MESSAGE_EOS:
    LOG_INFO("audio pipeline reached end-of-stream");
    if (self) {
      self->dispatchStatus(false, "end-of-stream");
    }
    break;
  default:
    break;
  }

  // Drop after handling: nothing drains the async bus queue (we run no GLib/Qt
  // main loop on this object's thread), so passing messages on would let them
  // accumulate unbounded (e.g. the level element posts ~10/sec).
  return GST_BUS_DROP;
}

} // namespace

GstAudioPipeline::GstAudioPipeline(std::string name) : m_name(std::move(name))
{
}

GstAudioPipeline::~GstAudioPipeline()
{
  stop();
}

bool GstAudioPipeline::build(const std::string &launch)
{
  if (m_pipeline != nullptr) {
    stop();
  }

  LOG_DEBUG("building audio pipeline '%s': %s", m_name.c_str(), launch.c_str());

  GError *err = nullptr;
  m_pipeline = gst_parse_launch(launch.c_str(), &err);
  if (m_pipeline == nullptr || err != nullptr) {
    LOG_ERR(
        "failed to build audio pipeline '%s': %s", m_name.c_str(), err && err->message ? err->message : "parse error"
    );
    g_clear_error(&err);
    if (m_pipeline != nullptr) {
      gst_object_unref(m_pipeline);
      m_pipeline = nullptr;
    }
    return false;
  }

  GstBus *bus = gst_element_get_bus(m_pipeline);
  gst_bus_set_sync_handler(bus, busSyncHandler, this, nullptr);
  gst_object_unref(bus);
  return true;
}

bool GstAudioPipeline::start()
{
  if (m_pipeline == nullptr) {
    return false;
  }

  const GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    LOG_ERR("audio pipeline '%s' failed to start", m_name.c_str());
    return false;
  }
  LOG_DEBUG("audio pipeline '%s' set to PLAYING", m_name.c_str());
  return true;
}

void GstAudioPipeline::stop()
{
  if (m_pipeline == nullptr) {
    return;
  }

  // Detach the sync handler first so no callback fires during teardown.
  GstBus *bus = gst_element_get_bus(m_pipeline);
  if (bus != nullptr) {
    gst_bus_set_sync_handler(bus, nullptr, nullptr, nullptr);
    gst_object_unref(bus);
  }

  gst_element_set_state(m_pipeline, GST_STATE_NULL);
  gst_object_unref(m_pipeline);
  m_pipeline = nullptr;
  LOG_DEBUG("audio pipeline '%s' stopped", m_name.c_str());
}

void GstAudioPipeline::setElementDouble(const char *elementName, const char *property, double value)
{
  if (m_pipeline == nullptr) {
    return;
  }
  GstElement *el = gst_bin_get_by_name(GST_BIN(m_pipeline), elementName);
  if (el == nullptr) {
    return;
  }
  g_object_set(G_OBJECT(el), property, value, nullptr);
  gst_object_unref(el);
}

void GstAudioPipeline::setElementBool(const char *elementName, const char *property, bool value)
{
  if (m_pipeline == nullptr) {
    return;
  }
  GstElement *el = gst_bin_get_by_name(GST_BIN(m_pipeline), elementName);
  if (el == nullptr) {
    return;
  }
  g_object_set(G_OBJECT(el), property, static_cast<gboolean>(value), nullptr);
  gst_object_unref(el);
}

void GstAudioPipeline::setElementDevice(const char *elementName, const std::string &deviceId)
{
  if (m_pipeline == nullptr) {
    return;
  }
  GstElement *el = gst_bin_get_by_name(GST_BIN(m_pipeline), elementName);
  if (el == nullptr) {
    return;
  }
  // "device" is a string on pulsesink/wasapi2sink but an int (AudioDeviceID) on
  // osxaudiosink -- set it by its declared type, never through parse-launch text.
  if (GParamSpec *pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(el), "device")) {
    if (pspec->value_type == G_TYPE_INT) {
      g_object_set(G_OBJECT(el), "device", static_cast<gint>(audioDeviceIdToInt(deviceId)), nullptr);
    } else if (pspec->value_type == G_TYPE_STRING) {
      g_object_set(G_OBJECT(el), "device", deviceId.c_str(), nullptr);
    }
  }
  gst_object_unref(el);
}

GstElement *GstAudioPipeline::elementByName(const char *name) const
{
  if (m_pipeline == nullptr) {
    return nullptr;
  }
  return gst_bin_get_by_name(GST_BIN(m_pipeline), name);
}

void GstAudioPipeline::dispatchStatus(bool ok, const std::string &detail) const
{
  if (m_statusCb) {
    m_statusCb(ok, detail);
  }
}
