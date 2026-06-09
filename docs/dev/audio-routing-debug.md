# Audio Routing (GStreamer) — Architecture & Debugging

Audio routing streams a **client** machine's system-output audio to the **server**
machine's speakers (the machine you sit at). It is built on **GStreamer**: GStreamer
provides the cross-platform capture/playback elements, the Opus codec, RTP
packetisation, the jitter buffer and clock handling, so Deskflow itself only has to
wire up the control connection and a couple of short pipeline descriptions.

This replaces the earlier hand-rolled WASAPI / CoreAudio / PulseAudio + raw-TCP
implementation, whose problems (TCP head-of-line blocking, unbounded buffering, no
jitter buffer, platform buffer-overread/use-after-free crashes) motivated the rewrite.

## Two planes

| Plane | Transport | Carries | Owner |
|-------|-----------|---------|-------|
| **Control** | TCP, port `kDefaultAudioPort` (24801) | handshake (client name) + assigned RTP port | `AudioClient` / `AudioServer` |
| **Media** | RTP over UDP, per-client port (≥ `kAudioRtpPortBase`, 24810) | Opus-encoded audio | GStreamer (`udpsink` → `udpsrc`) |

Reliable, low-rate control belongs on TCP; real-time media belongs on UDP (a lost
packet should be concealed/skipped, never block the stream). The server assigns each
client its own UDP port so it can run an independent receive pipeline per client.

## Data flow

```
CLIENT (deskflow-core, client mode)                 SERVER (deskflow-core, server mode)
─────────────────────────────────                   ──────────────────────────────────
ServerProxy: kOptionAudioRouting=true
  -> AudioClient.start()
       TCP connect 24801, send "DSKFAUDIO"+name  ─▶  AudioServer.onNewConnection / processHandshake
                                                        allocate UDP port P
                                                        start GstAudioReceiver(P)
       receive "OK"+P                             ◀─  reply "OK" + uint16 P
  -> GstAudioSender(serverHost, P).start()
       <platform-src> ! audioconvert ! audioresample
         ! opusenc ! rtpopuspay ! udpsink host=server port=P  ═══RTP/UDP═══▶  udpsrc port=P
                                                                                ! rtpjitterbuffer
                                                                                ! rtpopusdepay ! opusdec
                                                                                ! audioconvert ! audioresample
                                                                                ! level ! volume ! <sink>
```

Direction is fixed (client captures, server plays). Because the user sits at the
server, all playback controls (output device, volume, mute) are **server-side**,
applied to the receiver pipeline — they never cross the wire.

## Source files (`src/lib/audio/`)

| File | Role |
|------|------|
| `GstAudioPipeline` | RAII wrapper over `gst_parse_launch`; bus **sync handler** logs errors/EOS (no Qt/GLib main loop needed); live `volume`/`mute` setters |
| `GstAudioSender` | Client capture → opus → RTP → `udpsink` pipeline |
| `GstAudioReceiver` | One server `udpsrc` → `rtpjitterbuffer` → opus → `volume` → sink pipeline per client |
| `AudioClient` | Control-plane client: TCP handshake on a worker thread, learns the RTP port, launches the sender |
| `AudioServer` | Control-plane server: accepts clients, assigns RTP ports, runs a receiver each; per-client `setClientVolume/Mute/OutputDevice` |
| `AudioDevices` | `GstDeviceMonitor` enumeration of output devices for the GUI picker |
| `AudioTypes.h` | Ports, sample rate/channels/bitrate, RTP payload type, handshake bytes, `audioRtpCaps()` |
| `MacAudioCapture` (macOS only) | ScreenCaptureKit → `appsrc` shim (no stock loopback source on macOS) |

## Pipelines

Client (sender), platform source varies:

```
# Windows
wasapi2src loopback=true low-latency=true   (fallback: wasapisrc loopback=true)
# Linux
pulsesrc device=@DEFAULT_MONITOR@           (fallback: pulsesrc)
# macOS
appsrc name=macsrc ...   <- fed by ScreenCaptureKit

... ! queue max-size-time=100000000 leaky=downstream
    ! audioconvert ! audioresample ! audio/x-raw,rate=48000,channels=2
    ! opusenc bitrate=96000 inband-fec=true frame-size=20
    ! rtpopuspay pt=96 ! udpsink host=<server> port=<P> sync=false async=false
```

Server (receiver), one per client:

```
udpsrc port=<P> caps="application/x-rtp,media=audio,clock-rate=48000,encoding-name=OPUS,payload=96,encoding-params=2,sprop-stereo=1"
  ! rtpjitterbuffer latency=50 do-lost=true
  ! rtpopusdepay ! opusdec plc=true use-inband-fec=true
  ! audioconvert ! audioresample
  ! level interval=100000000 post-messages=true
  ! volume name=vol volume=1.0 mute=false
  ! autoaudiosink                       # or <wasapi2sink|osxaudiosink|pulsesink> device="<id>"
```

`rtpjitterbuffer` absorbs network timing jitter; `opusdec` PLC/FEC conceals lost
packets; `audioresample` matches the output device's native rate (no more pitch bugs).

## Build / dependencies

Gated behind `BUILD_AUDIO_SUPPORT` (default ON) → defines `HAVE_AUDIO_SUPPORT`.

* **Windows / macOS**: GStreamer comes from vcpkg. The manifest token is generated in
  `CMakeLists.txt` (`AUDIO_LIBS`) into `vcpkg.json`:
  `gstreamer` with features `plugins-base`, `plugins-good`, `plugins-bad`, `opus-base`.
  Those provide: appsrc/appsink, audioconvert/resample, volume, opusenc/opusdec
  (plugins-base + opus-base); udp, rtp (`rtpopuspay/depay`), rtpmanager
  (`rtpjitterbuffer`), level, autodetect (plugins-good); wasapi/wasapi2 (plugins-bad).
* **Linux**: system packages — `gstreamer1.0` + `gstreamer1.0-plugins-{base,good,bad}`
  development files.
* `cmake/AudioLibraries.cmake` locates GStreamer via pkg-config
  (`gstreamer-1.0 ≥ 1.20`, `-base`, `-app`, `-audio`); if missing it disables the
  feature instead of failing the build.

The packaged installer must bundle the GStreamer **plugin** DLLs/dylibs, not just the
core library, or pipelines fail at runtime with "no element …".

## GUI controls

Per-screen, in the server's **Screen Settings** dialog (`ScreenSettingsDialog`):

* **Route audio to this computer** — the enable toggle (`audioRouting`, sent to the
  client via `kOptionAudioRouting`).
* **Output device / Volume / Mute** — server-side; stored in shared `Settings`
  (`Settings::Audio::outputDeviceKey/volumeKey/muteKey`, keyed by screen name). The
  server core reads and applies them in `ServerApp::applyClientAudioSettings()` when
  the client's stream starts (`AudioServer::clientAudioStarted`).

> Note: device/volume/mute are applied when a client's stream **starts**; changing
> them mid-stream takes effect on the next (re)connect. A live status indicator and
> level meter (the `level` element already posts messages) are the remaining UI
> enhancements.

## Debugging

Enable GStreamer's own logging when launching `deskflow-core`:

```
# bash
GST_DEBUG=3 deskflow-core ...
GST_DEBUG=4,rtpjitterbuffer:5,wasapi2src:5 deskflow-core ...    # more detail on specific elements
```

```powershell
# PowerShell
$env:GST_DEBUG = "3"; deskflow-core ...
```

Useful checks:

1. **Validate the pipelines standalone** (no Deskflow) with `gst-launch-1.0` — confirms
   the codec/RTP/jitter path and that the plugins are installed:
   ```
   # on the client box
   gst-launch-1.0 wasapi2src loopback=true ! audioconvert ! opusenc ! rtpopuspay ! udpsink host=<server> port=24810
   # on the server box
   gst-launch-1.0 udpsrc port=24810 caps="application/x-rtp,media=audio,clock-rate=48000,encoding-name=OPUS,payload=96,encoding-params=2,sprop-stereo=1" ! rtpjitterbuffer ! rtpopusdepay ! opusdec ! audioconvert ! autoaudiosink
   ```
2. **List elements / devices**: `gst-inspect-1.0 wasapi2src`, `gst-inspect-1.0 opusenc`,
   `gst-device-monitor-1.0 Audio/Sink`.
3. **Reachability**: the per-client UDP port (24810+) must be open through the firewall
   from client → server. The control port 24801 must also be reachable.
4. **No audio but pipeline runs**: on Windows, `wasapi2src loopback=true` only produces
   data while the default render device is actually playing something.
5. **Deskflow logs** (DEBUG): look for `AudioServer: client "<name>" -> RTP port P`,
   `audio receiver: playing RTP/Opus on UDP port P`, `AudioClient: server assigned RTP
   port P`, and `audio sender: streaming to <server>:P`. A bus error from a pipeline is
   logged as `audio pipeline error [<element>]: …`.

## Known caveats

* **macOS** has no stock GStreamer system-output loopback source, so capture there uses
  the ScreenCaptureKit `appsrc` shim (`MacAudioCapture`, compiled with `-fobjc-arc`).
  The user must grant "Screen & System Audio Recording" permission.
* GStreamer adds significant installer footprint; it is optional via `BUILD_AUDIO_SUPPORT`.
* RTCP is not used (audio-only); `rtpbin`/RTCP could be added later for sender reports.
