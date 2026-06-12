# Audio Routing

Audio routing lets a **client** machine's system audio play through the **server**
machine's speakers — the machine you're actively sitting at. It's built on
[GStreamer](https://gstreamer.freedesktop.org/): GStreamer provides the cross‑platform
capture and playback, the Opus codec, RTP packetisation, the jitter buffer and clock
handling, so the audio "just works" over the LAN with low latency and graceful handling
of packet loss.

> This is a fork feature. Upstream Deskflow does not ship audio routing.
>
> For low‑level debugging and the exact pipeline strings, see
> [docs/dev/audio-routing-debug.md](dev/audio-routing-debug.md).

---

## What it does

In a Deskflow setup, the **server** is the computer whose keyboard/mouse you share out to
**clients**. With audio routing enabled for a client, that client captures whatever is
playing on its speakers (a video, music, a call) and streams it to the server, where it
comes out of the server's selected output device. You can run several clients at once;
their audio mixes together on the server.

Direction is fixed: **client captures → server plays.** It is designed around the idea
that you sit at the server, so all the playback controls (volume, mute, which output
device) live on the server side.

---

## Supported platforms

| Platform | Capture (client) | Playback (server) | Notes |
|---|---|---|---|
| **Windows x64** | ✅ WASAPI loopback | ✅ | GStreamer ships in the installer (official MSVC SDK) |
| **Windows arm64** | ✅ WASAPI loopback | ✅ | GStreamer ships in the installer (official MSVC SDK, 1.28+) |
| **macOS 13+** (x64 & arm64) | ✅ ScreenCaptureKit | ✅ CoreAudio | Requires the "Screen & System Audio Recording" permission |
| **macOS 12** | ❌ capture | ✅ playback | ScreenCaptureKit needs macOS 13; a 12‑target build can still play |
| **Linux** (Debian/Ubuntu, Fedora, openSUSE, Arch) | ✅ PulseAudio/PipeWire monitor | ✅ | Uses the distro's GStreamer packages |
| **Linux (Flatpak)** | ✅ | ✅ | GStreamer comes from the KDE runtime; needs audio permission |
| **FreeBSD** | ✅ | ✅ | Uses the system GStreamer packages |

Audio routing is compiled in only when the build option `BUILD_AUDIO_SUPPORT` is `ON`
(the default) **and** GStreamer is found at configure time. If GStreamer is missing the
feature is silently disabled and the rest of Deskflow builds normally — unless
`REQUIRE_AUDIO_SUPPORT` is `ON` (set on CI), which makes a missing GStreamer a hard
configure error so release builds can never ship with audio silently turned off.

---

## How it works

### Two channels

The feature uses two separate network channels so that control is reliable and media is
low‑latency:

| Channel | Transport | Port | Carries |
|---|---|---|---|
| **Control** | TCP | `24801` (configurable) | Handshake + the RTP port the server assigns to each client |
| **Media** | RTP over UDP | `24810`+ (one per client) | The Opus‑encoded audio |

Real‑time audio belongs on UDP: a lost packet should be skipped/concealed, never block
the stream (which is what TCP's in‑order delivery would do). The control channel is
low‑rate and must be reliable, so TCP is right there.

### Connection flow

1. In the server GUI you tick **Route audio to this computer** for a client's screen.
   When that client connects, the server tells it (over the normal Deskflow protocol) to
   start audio.
2. The client opens a **TCP control connection** to the server on port `24801` and sends a
   handshake (`DSKFAUDIO` + its screen name).
3. The server allocates a **dedicated UDP port** for that client (starting at `24810`),
   starts a receiver pipeline listening on it, and replies with the port number.
4. The client starts a GStreamer pipeline that captures its system audio, encodes it to
   Opus, packetises it as RTP, and sends it to `server:<that port>`.
5. The server's receiver pipeline depacketises, runs it through a jitter buffer, decodes,
   applies the per‑client volume/mute, and plays it on the chosen output device.

Each client gets its own UDP port and its own server‑side receiver pipeline, so multiple
clients stream independently and the OS mixer combines them at the output device.

### The pipelines

**Client (capture → encode → send)** — the platform source differs, the rest is shared:

```
<platform source>
  ! queue leaky=downstream
  ! audioconvert ! audioresample ! audio/x-raw,rate=48000,channels=2
  ! opusenc bitrate=96000 inband-fec=true frame-size=20
  ! rtpopuspay pt=96
  ! udpsink host=<server> port=<P>
```

| OS | Source element |
|---|---|
| Windows | `wasapi2src loopback=true` (falls back to `wasapisrc`) |
| Linux | `pulsesrc device=@DEFAULT_MONITOR@` (the default sink's monitor) |
| macOS | `appsrc` fed by a ScreenCaptureKit capture shim |

**Server (receive → decode → play)**, one per client:

```
udpsrc port=<P> caps="application/x-rtp,...OPUS..."
  ! rtpjitterbuffer latency=50 do-lost=true
  ! rtpopusdepay ! opusdec plc=true use-inband-fec=true
  ! audioconvert ! audioresample
  ! level ! volume name=vol
  ! <output sink>   (autoaudiosink, or the selected device)
```

Audio is **48 kHz stereo Opus at 96 kbps**, 20 ms frames. The `rtpjitterbuffer` (50 ms
default) smooths network timing, and Opus PLC/FEC conceal lost packets, so brief Wi‑Fi
hiccups degrade gracefully instead of stalling.

### Source files

All under `src/lib/audio/`:

- `GstAudioPipeline` — thin RAII wrapper over a GStreamer pipeline (build / start / stop,
  error logging, live volume/mute).
- `GstAudioSender` — the client capture/encode/send pipeline.
- `GstAudioReceiver` — one server receive/decode/play pipeline per client.
- `AudioClient` / `AudioServer` — the small TCP control plane that negotiates RTP ports.
- `AudioDevices` — enumerates output devices for the GUI and initialises GStreamer
  (including the bundled‑plugin path on Windows/macOS).
- `MacAudioCapture` (macOS only) — the ScreenCaptureKit → `appsrc` shim.

---

## Using it (UI/UX)

Audio routing is configured **per client screen, on the server**, in the server
configuration:

1. Open the Deskflow GUI on the **server** and edit the server configuration.
2. Double‑click a client screen to open **Screen Settings**.
3. In the **Audio** group:
   - **Route audio to this computer** — enable/disable streaming this client's audio to
     the server.
   - **Output device** — which of the server's output devices this client's audio plays
     on (defaults to *System default*).
   - **Volume** — playback level for this client's audio on the server.
   - **Mute** — silence this client's audio without stopping the stream.
4. Save. The settings apply when the client connects and starts streaming.

Notes on behaviour:

- The enable toggle is sent to the client; the device/volume/mute controls are applied on
  the **server** (they never travel over the network).
- The output‑device list is populated from the server's GStreamer device monitor.
- Device/volume/mute are applied when a client's stream **starts**. Changing them while a
  client is already streaming takes effect on the next reconnect. (A live status indicator
  and level meter are possible future enhancements — the `level` element is already in the
  pipeline.)
- If audio fails to start, the reason is written to the Deskflow log rather than failing
  silently.

---

## Using a server config file (instead of the GUI)

Deskflow can drive the server from an external configuration file rather than the GUI's
built‑in configuration (in the GUI: *Settings → Use external configuration file*, or pass
the file to the server core directly). The file uses Deskflow's classic `section:` syntax,
with one block per screen under `section: screens`.

All of the per‑client audio settings the GUI exposes can be set in the config file. Put
them in the **client's** screen block (the machine that captures and sends) — not on the
server, which is always the playback target:

```
section: screens
    # the computer you sit at — plays the routed audio
    server-desktop:

    # a client whose audio should play on the server, with explicit controls
    work-laptop:
        audioRouting = true
        audioOutputDevice = alsa_output.pci-0000_00_1f.3.analog-stereo
        audioVolume = 80
        audioMute = false

    # another client: routed with default device/volume/mute
    media-pc:
        audioRouting = true
end

section: links
    server-desktop:
        right = work-laptop
    work-laptop:
        left = server-desktop
end
```

Per‑screen audio keys (all optional; they sit alongside options like `halfDuplexCapsLock`
and `switchCorners`):

| Key | Type | Meaning |
|---|---|---|
| `audioRouting` | boolean | Enable streaming this client's audio to the server. Omitted = off. |
| `audioOutputDevice` | string | The server output device to play this client on. Omitted/empty = system default. |
| `audioVolume` | integer | Playback volume, `0`–`100` (percent). Omitted = `100`. |
| `audioMute` | boolean | Mute this client's audio without stopping the stream. Omitted = `false`. |

The **output device** value is GStreamer's device id for the desired sink. The easiest way
to find it is the GUI's **Screen Settings → Audio → Output device** dropdown (it stores the
id), or run `gst-device-monitor-1.0 Audio/Sink` on the server and use the device's `device`
property string. On Linux this is typically the PulseAudio/PipeWire sink name (as above);
on Windows it's a WASAPI device id.

**Precedence:** a value given in the config file wins. Anything you leave out falls back to
the GUI's **Screen Settings → Audio** preference (stored in Deskflow's settings, keyed by
screen name), and if that's unset too, to the built‑in default in the table. So a pure
config‑file setup has full control without ever opening the GUI.

---

## Installation / runtime requirements per OS

The release installers bundle or declare everything needed — you normally don't install
anything extra. Details:

### Windows (x64)
The installer **bundles the GStreamer runtime** (core DLLs + the needed plugins). Nothing
to install. The app points GStreamer at its bundled plugins at startup. (Windows **arm64**
builds do not include audio routing.)

### macOS (13+)
The `.app` **bundles the GStreamer dylibs and plugins**. The one requirement is a
**permission**: the first time a client tries to capture, macOS asks for
**Screen & System Audio Recording** access (System Settings → Privacy & Security). Grant
it and restart the client. macOS 12 can play received audio but cannot capture (capture
needs ScreenCaptureKit on macOS 13+).

### Linux (deb / rpm / Arch)
The packages **declare** GStreamer as a dependency, so your package manager installs it:

- Debian/Ubuntu: `gstreamer1.0-plugins-base`, `gstreamer1.0-plugins-good`
- Fedora/RHEL: `gstreamer1-plugins-base`, `gstreamer1-plugins-good`
- openSUSE: `gstreamer-plugins-base`, `gstreamer-plugins-good`
- Arch: `gstreamer`, `gst-plugins-base`, `gst-plugins-good`

These are usually already present on a desktop system. Capture uses the default sink's
**monitor** source via PulseAudio/PipeWire.

### Linux (Flatpak)
GStreamer comes from the `org.kde.Platform` runtime, so it's already there. The Flatpak
requests **audio access** (`--socket=pulseaudio`) so it can read the monitor and play back.

### FreeBSD
Install the system packages `gstreamer1`, `gstreamer1-plugins`, `gstreamer1-plugins-good`.

### Building from source
Audio routing needs GStreamer ≥ 1.20 development files (core, base, app, audio) plus the
runtime plugins listed above. The build auto‑detects GStreamer via `pkg-config`; if it
isn't found, `BUILD_AUDIO_SUPPORT` is turned off and the build continues without audio.
On Windows the build uses the official prebuilt GStreamer MSVC SDK rather than building it
from source.

---

## Configuration & networking

- **Control port** — default `24801`. Configurable via the `audio/port` setting
  (`Settings::Audio::Port`).
- **Media ports** — UDP, assigned per client starting at `24810`. Make sure these are open
  client → server in any firewall between the machines.
- Per‑client **enable / output device / volume / mute** can be set either in the GUI
  (Screen Settings → Audio) or in the server config file (see
  [Using a server config file](#using-a-server-config-file-instead-of-the-gui)); config‑file
  values take precedence. GUI preferences are stored in the app settings, keyed by screen name.

---

## Limitations & known issues

- **macOS 12**: can play but not capture (ScreenCaptureKit requires macOS 13+).
- **Direction is fixed**: client → server only. There is no server → client streaming.
- **Live control changes** (volume/mute/device) apply on the next stream start, not
  instantly mid‑stream.
- **macOS capture permission** must be granted manually the first time.
- Encryption: the media stream is plain RTP/Opus on the LAN (no SRTP). Use it on trusted
  networks.

---

## Troubleshooting (quick)

- **No sound, but everything looks connected** — on Windows, the client only produces audio
  while something is actually playing on its default output device.
- **"no element …" / missing‑plugin errors** — the GStreamer plugins aren't on the plugin
  path; on a from‑source/dev build set `GST_PLUGIN_PATH` to the GStreamer `gstreamer-1.0`
  plugin directory.
- **Firewall** — confirm the control port (24801/TCP) and the per‑client media ports
  (24810+/UDP) are reachable from client to server.
- **macOS** — check the capture permission was granted and the client was restarted.
- For verbose GStreamer logging, run `deskflow-core` with `GST_DEBUG=3` (or higher). See
  [docs/dev/audio-routing-debug.md](dev/audio-routing-debug.md) for standalone
  `gst-launch-1.0` test pipelines and element checks.
