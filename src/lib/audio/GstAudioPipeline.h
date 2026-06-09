/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <functional>
#include <string>

typedef struct _GstElement GstElement;

///
/// Thin RAII wrapper around a GStreamer pipeline built from a gst_parse_launch
/// description. This is the single reusable core shared by GstAudioSender
/// (capture) and GstAudioReceiver (playback).
///
/// It owns the GstElement* pipeline, installs a bus sync handler that turns
/// GStreamer errors/warnings/EOS into Deskflow log lines (so failures are no
/// longer silent), and exposes a couple of runtime knobs used by the receiver.
///
/// No Qt and no GLib main loop are required: the sync handler runs synchronously
/// on whichever GStreamer thread posts the message, so the owning object may
/// live on any thread (the control plane does not assume a serviced event loop).
///
class GstAudioPipeline
{
public:
  /// Called from a GStreamer streaming thread when the pipeline errors or ends.
  /// `ok == false` means a fatal error/EOS; `detail` is a human-readable reason.
  /// Implementations must be thread-safe and must NOT touch Qt objects directly.
  using StatusCallback = std::function<void(bool ok, const std::string &detail)>;

  explicit GstAudioPipeline(std::string name);
  ~GstAudioPipeline();

  GstAudioPipeline(const GstAudioPipeline &) = delete;
  GstAudioPipeline &operator=(const GstAudioPipeline &) = delete;

  /// Parse `launch` into a pipeline. Returns false (and logs) on parse failure.
  bool build(const std::string &launch);

  /// Set the pipeline to PLAYING. Returns false on immediate state-change failure.
  bool start();

  /// Set the pipeline to NULL and release the bus watch. Safe to call repeatedly.
  void stop();

  [[nodiscard]] bool isValid() const
  {
    return m_pipeline != nullptr;
  }

  /// Install/replace the fatal-status callback. Set before start().
  void setStatusCallback(StatusCallback cb)
  {
    m_statusCb = std::move(cb);
  }

  /// Set a double property on a named element (e.g. the "volume" element's "volume").
  /// No-op if the element is not present. Safe to call while PLAYING.
  void setElementDouble(const char *elementName, const char *property, double value);

  /// Set a boolean property on a named element (e.g. the "volume" element's "mute").
  void setElementBool(const char *elementName, const char *property, bool value);

  /// Look up a named element in the pipeline. Returns a new reference (transfer
  /// full): the caller must gst_object_unref() it. Returns nullptr if absent.
  GstElement *elementByName(const char *name) const;

  /// Invoked by the internal bus handler; not part of the public API.
  void dispatchStatus(bool ok, const std::string &detail) const;

private:
  std::string m_name;
  GstElement *m_pipeline = nullptr;
  StatusCallback m_statusCb;
};
