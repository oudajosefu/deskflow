# Audio Routing — Debugging Guide

This guide covers two client-side crashes that the audio routing feature
(`audioRouting = true` in the server config) originally introduced, their
confirmed root causes, the fixes applied, and how to diagnose regressions while
a client is actively connected to the server.

> **Status:** All three issues below are fixed on the feature branch.
>
> - **Issue 2 (macOS)** was a use-after-free: `MacAudioCapture.mm` was compiled
>   without ARC, so the async `SCShareableContent` was freed before use. Fixed by
>   compiling the file with ARC.
> - **Issue 1 (Windows)** was a WASAPI buffer overread from assuming float32
>   stereo. Fixed by converting from the actual device mix format.
> - **Issue 3 (Windows)** was wrong pitch + clipping on the server: the 48 kHz
>   pipeline was rendered (and captured) without resampling to the device rate.
>   Fixed by letting WASAPI auto-convert (`AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM`).
>
> Each section below has the root cause, the fix, and how to verify it.

## Architecture summary

Audio travels on a **separate TCP connection to port 24801** (not the main
Deskflow control port). The client (`deskflow-core`) captures system audio and
streams Opus-encoded packets to the server (`deskflow-core`) which decodes and
plays them back.

Key source files:

| File                                                                           | Role                                                                     |
| ------------------------------------------------------------------------------ | ------------------------------------------------------------------------ |
| [src/lib/audio/AudioClient.cpp](../../src/lib/audio/AudioClient.cpp)           | Client-side worker thread: connect → handshake → capture → encode → send |
| [src/lib/audio/AudioServer.cpp](../../src/lib/audio/AudioServer.cpp)           | Server-side TCP listener: receive → decode → playback                    |
| [src/lib/audio/WinAudioCapture.cpp](../../src/lib/audio/WinAudioCapture.cpp)   | Windows WASAPI loopback capture                                          |
| [src/lib/audio/WinAudioPlayback.cpp](../../src/lib/audio/WinAudioPlayback.cpp) | Windows WASAPI render                                                    |
| [src/lib/audio/MacAudioCapture.mm](../../src/lib/audio/MacAudioCapture.mm)     | macOS ScreenCaptureKit system-audio capture                              |
| [src/lib/audio/MacAudioPlayback.cpp](../../src/lib/audio/MacAudioPlayback.cpp) | macOS CoreAudio render                                                   |
| [src/lib/audio/AudioTypes.h](../../src/lib/audio/AudioTypes.h)                 | Protocol constants (port, sample rate, handshake magic)                  |

## Step 0 — Enable verbose logging on both machines

`deskflow-core` has no `--log-level` CLI flag. Log level is read from the INI
settings file under the key `log/level`. The accepted values (case-insensitive)
are `FATAL`, `ERROR`, `WARNING`, `INFO`, `DEBUG`, and `VERBOSE`.

The default settings file written by the GUI lives at:

| Platform | Path                               |
| -------- | ---------------------------------- |
| macOS    | `~/Library/Deskflow/Deskflow.conf` |
| Windows  | `%APPDATA%\Deskflow\Deskflow.conf` |
| Linux    | `~/.config/Deskflow/Deskflow.conf` |

The easiest way to enable debug logging without losing any existing configuration
(server layout, client remote host, TLS settings, etc.) is to edit that file
directly. Find or add the `[log]` section and set the level:

```ini
[log]
level=DEBUG
```

Then start `deskflow-core` manually, capturing output to a file. The mode
(`server` or `client`) is a required positional argument; the only other
relevant CLI flag is `-s` / `--settings` to override the settings file path.

**Server (macOS, using the GUI-written settings file):**

```bash
/Applications/Deskflow.app/Contents/MacOS/deskflow-core server \
  2>&1 | tee /tmp/deskflow-server.log
```

**Windows client (PowerShell, using the GUI-written settings file):**

```powershell
& "C:\Program Files\Deskflow\deskflow-core.exe" client 2>&1 |
  Tee-Object $env:TEMP\deskflow-client.log
```

**macOS client (using the GUI-written settings file):**

```bash
/Applications/Deskflow.app/Contents/MacOS/deskflow-core client \
  2>&1 | tee /tmp/deskflow-client.log
```

If you need a self-contained debug run without touching the main settings file,
create a minimal override — but copy the full existing file first so the server
layout and remote host are preserved, then change only `log/level`:

```bash
# macOS example
cp ~/Library/Deskflow/Deskflow.conf /tmp/deskflow-debug.conf
# Edit /tmp/deskflow-debug.conf: set level=DEBUG under [log]
/Applications/Deskflow.app/Contents/MacOS/deskflow-core server \
  -s /tmp/deskflow-debug.conf 2>&1 | tee /tmp/deskflow-server.log
```

Key log lines that confirm each stage of the audio subsystem started:

| Log line                                                       | Where          | Meaning                                        |
| -------------------------------------------------------------- | -------------- | ---------------------------------------------- |
| `AudioServer: listening on port 24801`                         | server         | AudioServer bound the TCP port                 |
| `audio routing enabled — streaming to <host>:24801`            | client         | Client parsed the `kOptionAudioRouting` option |
| `AudioClient: connected to audio server, starting capture`     | client         | TCP + handshake succeeded                      |
| `WASAPI loopback capture started`                              | Windows client | WASAPI capture running                         |
| `macOS ScreenCaptureKit audio capture started`                 | macOS client   | SCStream running                               |
| `AudioServer: audio stream from client "..."`                  | server         | Handshake done, playback starting              |
| `WASAPI audio playback started` / `CoreAudio playback started` | server         | Platform playback device open                  |

If any line is absent, the failure occurred before that stage.

## Step 1 — Verify the audio TCP channel while connected

Run from the **client machine** while it is already connected to the server.

**macOS / Linux:**

```bash
# Simple reachability check
nc -zv <server-ip> 24801

# Confirm the port is bound on the server side
ssh <server> "lsof -nP -iTCP:24801 -sTCP:LISTEN"
```

**Windows (PowerShell):**

```powershell
Test-NetConnection -ComputerName <server-ip> -Port 24801
```

If the port is not open, `AudioServer::listen()` failed — check the server log
for `AudioServer: cannot bind to port 24801`.

Also confirm the server firewall permits inbound connections on port 24801:
**System Settings → Network → Firewall** must allow Deskflow or have the
firewall off for the relevant interface.

## Step 2 — Simultaneous client + server packet capture

Run on the **server** while the client connects to capture the full audio
handshake and data flow:

```bash
sudo tcpdump -i any -w /tmp/audio-session.pcap 'tcp port 24801'
```

Open the resulting `.pcap` in Wireshark:

- Filter `tcp contains "DSKFAUDIO"` — confirms the 9-byte handshake magic
  arrived at the server
- Look for **TCP RST** — abrupt termination; note which side sent it
- Look for **TCP FIN** sequence — clean close; note which side initiated
- After the handshake, you should see continuous small packets (~100–200 bytes
  each) arriving every ~20 ms (one Opus frame per packet)

---

## Issue 1 — Windows client crash

### Root cause: WASAPI format mismatch caused a buffer overread (fixed)

`deskflow-core` crashed inside `WinAudioCapture::readFrames`
([WinAudioCapture.cpp:191](../../src/lib/audio/WinAudioCapture.cpp#L191)). The
old copy unconditionally assumed float32 stereo:

```cpp
std::memcpy(buf + filled * kAudioChannels, data, toCopy * kAudioChannels * sizeof(float));
```

That always copies `toCopy × 2 × 4 = toCopy × 8` bytes. WASAPI, however,
delivers loopback audio in whatever format the Windows audio engine is
configured for (`m_mixFormat`, which is also what `IAudioClient::Initialize`
was handed). When the default output device is **16-bit** (`nBlockAlign = 4`),
**mono** (`nBlockAlign = 4`), or otherwise smaller than 8 bytes per frame, the
WASAPI buffer for `numFrames` only holds `numFrames × nBlockAlign` bytes, so the
copy read **beyond the end of the WASAPI buffer → access violation**. Devices
that happen to default to float32 stereo (`nBlockAlign = 8`) never overran,
which is why only some Windows clients crashed.

### Fix applied

`readFrames` now converts each frame from the device mix format to interleaved
float32 stereo, reading **exactly `nBlockAlign` bytes per frame** so it can
never overread
([WinAudioCapture.cpp:212-231](../../src/lib/audio/WinAudioCapture.cpp#L212-L231)).
Helpers `formatIsFloat()` and `sampleToFloat()` handle 16/24/32-bit integer PCM
and 32/64-bit float, and mono is up-mixed by duplicating the channel. The format
check now reports what it found instead of claiming the audio will be silent
([WinAudioCapture.cpp:145](../../src/lib/audio/WinAudioCapture.cpp#L145)):

```cpp
if (!formatIsCompatible(m_mixFormat)) {
  LOG_WARN("WASAPI capture: device mix format is %u-bit, %u channel(s), %lu Hz; converting to float32 stereo "
           "(sample-rate differences are not resampled)", ...);
}
```

> **Update — fixed:** the sample-rate limitation this note described is resolved.
> Capture now requests our 48 kHz float32 stereo format directly via
> `AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM`, so the device's bit depth, channel count, **and
> sample rate** are all converted by the WASAPI shared-mode mixer. The manual conversion
> shown above (and this `formatIsCompatible` warning) now runs only on the rare
> `AUTOCONVERTPCM` fallback. See **Issue 3** below for the full root cause and the matching
> server-side playback fix.

### Diagnostic steps

1. **Check the log for the format-conversion warning.** If you see:

   ```
   WASAPI capture: device mix format is 16-bit, 2 channel(s), 48000 Hz; converting to float32 stereo ...
   ```

   the per-frame conversion **fallback** is active — `AUTOCONVERTPCM` was rejected, so the
   manual path (the one that used to overread) is handling the buffer. After the fix this
   is informational; confirm audio still flows rather than crashes. On a normal
   configuration there is no warning — WASAPI delivers our format directly (see Issue 3).

2. **Check the Windows audio device format:**
   Right-click the speaker icon → Sound Settings → your default output device
   → Properties → Advanced tab → Default Format.
   If it is anything other than **"2 channel, 32 bit, 48000 Hz"**, the bug
   will fire.

3. **Workaround to verify:** Temporarily change the device format to
   `2 channel, 32-bit, 48000 Hz`, reconnect, and see if the crash stops.
   If it does, the format mismatch is the confirmed cause.

4. **Capture a Windows crash dump to pin the faulting address:**

   Add the following registry key (run as administrator), then reproduce the
   crash:

   ```reg
   Windows Registry Editor Version 5.00
   [HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps]
   "DumpFolder"=hex(2):25,00,4c,00,4f,00,43,00,41,00,4c,00,41,00,50,00,50,00,44,\
     00,41,00,54,00,41,00,25,00,5c,00,43,00,72,00,61,00,73,00,68,00,44,00,75,00,\
     6d,00,70,00,73,00,00,00
   "DumpType"=dword:00000002
   ```

   Open the `.dmp` file in WinDbg → `!analyze -v`. The faulting frame should
   show `WinAudioCapture::readFrames` near the top of the stack.

### Secondary suspect: COM apartment mismatch

[WinAudioCapture.cpp:107-111](../../src/lib/audio/WinAudioCapture.cpp#L107-L111)
calls `CoInitializeEx(nullptr, COINIT_MULTITHREADED)` on the `AudioCapture`
worker thread. Qt or another component on the same process may have already
called `CoInitialize` with `COINIT_APARTMENTTHREADED`. When this happens the
call returns `RPC_E_CHANGED_MODE (0x80010106)` and the code continues without
properly initialising COM for this thread. Subsequent WASAPI calls on the
wrong apartment can produce undefined behaviour.

Add a temporary log to diagnose:

```cpp
HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
LOG_INFO("WASAPI capture: CoInitializeEx hr=0x%08x (RPC_E_CHANGED_MODE=0x%08x)",
         hr, RPC_E_CHANGED_MODE);
```

If this logs `0x80010106` you have a COM threading issue.

### Tertiary note: WASAPI loopback requires an active render stream

WASAPI loopback returns zero packets when nothing is playing on the default
output device. When this happens `AudioClient::runCapture` spins in a tight
loop calling `readFrames` with `got = 0` and immediately looping back. This
pins a CPU core and floods the audio TCP socket with `socket.flush()` calls.
Play audio on the Windows machine before connecting to rule this out as a
contributing factor.

---

## Issue 2 — macOS client crash after granting Screen Recording permission

### Root cause: use-after-free in `MacAudioCapture::start()` — file compiled without ARC (fixed)

The macOS client did not "drop" the connection — `deskflow-core` **segfaulted**,
which tore the control connection down with it. The crash report
(`~/Library/Logs/DiagnosticReports/deskflow-core-*.ips`) is unambiguous:

```
Exception: EXC_BAD_ACCESS (SIGSEGV), KERN_INVALID_ADDRESS
           "possible pointer authentication failure"
Faulting thread: "AudioCapture"
  #0 libobjc.A.dylib  objc_msgSend
  #1 deskflow-core     MacAudioCapture::start()    -> MacAudioCapture.mm:149
  #2 deskflow-core     AudioClient::runCapture()   -> AudioClient.cpp:107
```

`objc_msgSend` to a garbage pointer is the signature of messaging a freed
Objective-C object. The faulting line was
[MacAudioCapture.mm:149](../../src/lib/audio/MacAudioCapture.mm#L149):

```objc
SCDisplay *display = content.displays.firstObject;   // objc_msgSend(content, @selector(displays))
```

`content` is the `SCShareableContent` captured by the async completion handler
at [MacAudioCapture.mm:129](../../src/lib/audio/MacAudioCapture.mm#L129):

```objc
__block SCShareableContent *content = nil;
[SCShareableContent getShareableContentWithCompletionHandler:^(SCShareableContent *c, NSError *e) {
  content = c;   // under MRC this does NOT retain c
  dispatch_semaphore_signal(sem);
}];
```

The file was being compiled **without ARC** (no `-fobjc-arc` in its compile
command — verify in `build/compile_commands.json`), even though it is written
entirely in ARC idiom. Under manual reference counting the assignment
`content = c` does not retain, so the autoreleased `SCShareableContent` is
deallocated as soon as ScreenCaptureKit's internal queue drains its autorelease
pool after the block returns. The worker thread then dereferences the dangling
`content` at line 149 → `EXC_BAD_ACCESS`.

This is why the crash appears only **after** Screen Recording permission is
granted: without permission `getShareableContent` returns an error and the code
bails cleanly at [MacAudioCapture.mm:137](../../src/lib/audio/MacAudioCapture.mm#L137);
with permission it returns the (soon-to-be-freed) object and execution reaches
line 149. The crash is fully deterministic.

> The earlier "macOS sends SIGTERM to restart the process for new entitlements"
> hypothesis was **wrong** — the crash report shows a `SIGSEGV` from
> `objc_msgSend`, not a clean `SIGTERM`, and `getShareableContent` does not
> re-trigger a TCC restart once permission is already granted.

### Fix applied

Compile this one file with ARC, which is what its code already assumes
([CMakeLists.txt:34](../../src/lib/audio/CMakeLists.txt#L34)):

```cmake
set_source_files_properties(MacAudioCapture.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
```

Under ARC the `__block` strong reference retains `content` past the block, and
`m_stream` / `m_delegate` are released correctly when assigned `nullptr` in
`stop()` (an MRC leak that is also resolved).

A second correctness bug was fixed alongside it: ScreenCaptureKit delivers
**planar (non-interleaved)** float32 audio, but the capture callback read the
sample buffer as interleaved via `CMBlockBufferGetDataPointer`, which would have
produced garbled audio once the crash was out of the way. The delegate now uses
`CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer` and interleaves to
stereo before handing samples to the ring buffer
([MacAudioCapture.mm:41-103](../../src/lib/audio/MacAudioCapture.mm#L41-L103)).

### Verifying the fix / diagnosing a regression

1. **Read the crash report** if `deskflow-core` segfaults again:

   ```bash
   ls -lt ~/Library/Logs/DiagnosticReports/ | grep deskflow-core
   ```

   The `.ips` is JSON (a header line + a body line). The body's `faultingThread`
   plus `threads[].frames[].imageOffset` give the stack; symbolicate a frame
   against the dev binary with:

   ```bash
   atos -o build/bin/Deskflow.app/Contents/MacOS/deskflow-core -arch arm64 \
     -l <imageBase> <imageBase + imageOffset>
   ```

   (`imageBase` and `imageOffset` both come from the report's `usedImages` entry
   for `deskflow-core`).

2. **Confirm ARC is actually on** for the file:
   ```bash
   python3 - <<'PY'
   import json
   for e in json.load(open("build/compile_commands.json")):
       if "MacAudioCapture" in e["file"]:
           cmd = e.get("command") or " ".join(e["arguments"])
           print("ARC:", "-fobjc-arc" in cmd)
   PY
   ```
   It must print `ARC: True`.

### Secondary suspect: `NSScreenCaptureUsageDescription` missing from Info.plist

Even after the user grants permission, `SCStream startCapture` (called at
[MacAudioCapture.mm:170](../../src/lib/audio/MacAudioCapture.mm#L170)) fails
if the app bundle's `Info.plist` is missing the
`NSScreenCaptureUsageDescription` key. The error is logged at
[MacAudioCapture.mm:178](../../src/lib/audio/MacAudioCapture.mm#L178):

```
SCStream startCapture failed: <error description>
```

Check the key:

```bash
/usr/libexec/PlistBuddy -c "Print NSScreenCaptureUsageDescription" \
  /Applications/Deskflow.app/Contents/Info.plist
```

If this prints an error, the key is missing.

Check the codesign entitlements on the binary:

```bash
codesign -d --entitlements - \
  /Applications/Deskflow.app/Contents/MacOS/deskflow-core 2>&1
```

Look for any ScreenCaptureKit-related entitlement. Without it, ScreenCaptureKit
may refuse to capture system audio even when the user has approved it in System
Settings.

### Tertiary note: the 10-second blocking window in `MacAudioCapture::start()`

[MacAudioCapture.mm:134](../../src/lib/audio/MacAudioCapture.mm#L134) and
[line 175](../../src/lib/audio/MacAudioCapture.mm#L175) each block on a
`dispatch_semaphore_wait` with a 5-second timeout — up to **10 seconds total**
waiting for ScreenCaptureKit to enumerate content and start the stream. The
audio worker thread (`AudioCapture`) does all of this, so the main Deskflow
event loop is not blocked and the main control connection stays up. If
`start()` returns `false` (permission denied or timeout), the audio TCP
connection closes cleanly and the main connection is unaffected. With the
use-after-free fixed, a `deskflow-core` exit around this point is no longer
expected from audio capture — capture a fresh crash report and symbolicate it
before assuming audio is the cause.

---

## Issue 3 — Wrong pitch and clipping on the server (fixed)

### Symptom

Routed audio plays on the server's speakers but is **slightly low-pitched with
intermittent clipping/crackle** (most visible with a macOS client → Windows server). It
does not crash.

### Root cause: the Windows playback path never resampled to the device rate (fixed)

The pipeline is fixed at **48 kHz float32 stereo** end to end (Opus needs a constant
rate — [AudioTypes.h:15-21](../../src/lib/audio/AudioTypes.h#L15-L21)). The three playback
backends differ in how they reach the speaker:

| Backend | Device handling | Status |
| --- | --- | --- |
| [MacAudioPlayback.cpp](../../src/lib/audio/MacAudioPlayback.cpp) | declares 48 kHz to the AudioUnit → CoreAudio resamples | correct |
| [LinuxAudioPlayback.cpp](../../src/lib/audio/LinuxAudioPlayback.cpp) | declares 48 kHz to PulseAudio → PulseAudio resamples | correct |
| [WinAudioPlayback.cpp](../../src/lib/audio/WinAudioPlayback.cpp) | initialized WASAPI with the **device mix format**, then copied the 48 kHz stream in unconverted | **broken** |

WASAPI shared mode does **not** resample unless told to. When the default render device
runs at **44.1 kHz** (a common Windows default), the 48 kHz samples were emitted at
44.1 kHz:

- **Wrong pitch** — playback ran ~8.8% slow, about 1.5 semitones flat ("a little low").
- **Clipping/crackle** — 48 k frames/sec into a device draining 44.1 k/sec floods the
  200 ms render buffer; once full, `writeFrames` wrote only `min(available, frames)` and
  dropped the rest of each packet → periodic discontinuities.

The capture side had the mirror gap: `WinAudioCapture` read loopback at the device rate
but labelled it 48 kHz, so a Windows *client* on a 44.1 kHz device streamed wrong-pitch
audio to the server.

### Fix applied

Both Windows backends now initialize WASAPI with our fixed 48 kHz float32 stereo
`WAVEFORMATEXTENSIBLE` plus `AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY`, so the shared-mode mixer resamples/reformats
between our format and the device — the same delegation the macOS/Linux backends rely on:

- **Playback** ([WinAudioPlayback::start](../../src/lib/audio/WinAudioPlayback.cpp#L52)).
  `writeFrames` is unchanged — its float32-stereo copy is now correct because the render
  buffer really is that format, and matching the rate stops the buffer-flood drops.
- **Capture** ([WinAudioCapture::start](../../src/lib/audio/WinAudioCapture.cpp#L126)). The
  per-frame conversion from Issue 1 is kept as a **fallback** (`m_convertFromDeviceFormat`),
  used only if a device rejects `AUTOCONVERTPCM`.

If the auto-convert `Initialize` fails, the `IAudioClient` is re-activated (a failed
`Initialize` leaves it in an undefined state) and retried with the device mix format, so
there is no regression versus the pre-fix behavior.

### Verifying the fix / diagnosing a regression

1. **Confirm the device rate** from the new playback log line:

   ```
   WASAPI playback: device mix is 44100 Hz / 2 ch; rendering 48000 Hz float32 stereo via WASAPI auto-convert
   ```

   Or: Sound Settings → output device → Properties → Advanced → Default Format.

2. Play audio from any client and confirm **correct pitch with no crackle**. A device
   already at 48 kHz should be unchanged by the fix.

3. A log line of `WASAPI ...: auto-convert Initialize failed 0x...; falling back to device
   mix format` means the device rejected `AUTOCONVERTPCM` and the fallback is active (pitch
   may be wrong on a non-48 kHz device, as before the fix). This is not expected on normal
   Windows configurations.

---

## Debugging from a dev build (repo binary vs release peers)

Use this when the target machine you are debugging has been built from the repo
and the other machines it is connected to are running the release version.

### Build a debug binary

**macOS / Linux:**

```bash
cmake -Bbuild -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc 2>/dev/null || sysctl -n hw.logicalcpu)
```

**Windows (PowerShell):**

```powershell
cmake -Bbuild -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j $env:NUMBER_OF_PROCESSORS
```

The binary lands at `build/bin/deskflow-core` (macOS/Linux) or
`build\bin\deskflow-core.exe` (Windows).

### How the dev binary finds its settings

The dev binary uses the **same settings file** as the installed release app.
There is no separate config for dev builds (on macOS and Linux). The resolution
order, from [Settings.cpp](../../src/lib/common/Settings.cpp#L59-L95):

| Platform | Path (in order checked)                                                                           |
| -------- | ------------------------------------------------------------------------------------------------- |
| macOS    | `~/Library/Deskflow/Deskflow.conf`                                                                |
| Linux    | `$XDG_CONFIG_HOME/Deskflow/Deskflow.conf`, then `~/.config/Deskflow/Deskflow.conf`                |
| Windows  | `build\bin\settings\deskflow.conf` (if file exists), otherwise `%APPDATA%\Deskflow\Deskflow.conf` |

On Windows you can force the dev binary to use a portable settings file (isolated
from the installed app) by creating `build\bin\settings\deskflow.conf` before
running the binary.

### One-time setup: write the settings file via the GUI

`deskflow-core` in server mode requires a server layout config file
(`deskflow-server.conf`) that the GUI writes at startup
([CoreProcess.cpp:528–536](../../src/lib/gui/core/CoreProcess.cpp#L528-L536)).
Run the release (or dev) GUI at least once with the layout configured and
the core started — that write happens automatically. After that the config
persists and the manual dev run will find it.

The GUI also logs the exact command it used to launch core, which is a useful
sanity-check reference.

**macOS (release app — output captured by the OS; stream it via `log`):**

```bash
log stream --predicate 'process == "deskflow"' --level info | grep "running command"
```

**macOS / Linux (dev GUI — output goes directly to stdout):**

```bash
./build/bin/deskflow 2>&1 | grep "running command"
```

**Windows (dev GUI — PowerShell):**

```powershell
& ".\build\bin\deskflow.exe" 2>&1 | Select-String "running command"
```

### Stop the release core before running the dev binary

`deskflow-core` enforces a single-instance check via `QSharedMemory`. If the
release app's core is still running, a second launch will log:

```
an instance of deskflow core is already running
```

and exit. Either quit Deskflow from the menu bar, or pass `--new-instance` to
skip the check.

**macOS / Linux:**

```bash
./build/bin/deskflow-core server --new-instance 2>&1 | tee /tmp/dev-server.log
```

**Windows (PowerShell):**

```powershell
& ".\build\bin\deskflow-core.exe" server --new-instance 2>&1 |
  Tee-Object $env:TEMP\dev-server.log
```

### Enable verbose logging for the dev run

Set `log/level=DEBUG` (or `VERBOSE`) in the settings file before running, as
described in Step 0. Because the dev binary shares the settings file with the
installed app, that change takes effect for the installed app too — remember to
revert it after debugging.

If you prefer an isolated settings file, copy it and use `-s`:

**macOS:**

```bash
cp ~/Library/Deskflow/Deskflow.conf /tmp/deskflow-dev.conf
# Edit /tmp/deskflow-dev.conf: set level=DEBUG under [log]
./build/bin/deskflow-core server -s /tmp/deskflow-dev.conf \
  2>&1 | tee /tmp/dev-server.log
# client mode
./build/bin/deskflow-core client -s /tmp/deskflow-dev.conf \
  2>&1 | tee /tmp/dev-client.log
```

**Linux:**

```bash
cp ~/.config/Deskflow/Deskflow.conf /tmp/deskflow-dev.conf
# Edit /tmp/deskflow-dev.conf: set level=DEBUG under [log]
./build/bin/deskflow-core server -s /tmp/deskflow-dev.conf \
  2>&1 | tee /tmp/dev-server.log
# client mode
./build/bin/deskflow-core client -s /tmp/deskflow-dev.conf \
  2>&1 | tee /tmp/dev-client.log
```

**Windows (PowerShell):**

```powershell
Copy-Item "$env:APPDATA\Deskflow\Deskflow.conf" "$env:TEMP\deskflow-dev.conf"
# Edit the copy: set level=DEBUG under [log]
& ".\build\bin\deskflow-core.exe" server -s "$env:TEMP\deskflow-dev.conf" 2>&1 |
  Tee-Object $env:TEMP\dev-server.log
# client mode
& ".\build\bin\deskflow-core.exe" client -s "$env:TEMP\deskflow-dev.conf" 2>&1 |
  Tee-Object $env:TEMP\dev-client.log
```

### Accepted CLI flags (all that exist)

Verified against [CoreArgs.h](../../src/apps/deskflow-core/CoreArgs.h):

```
deskflow-core <server|client> [options]

  -h, --help                  Show help text
  -v, --version               Show version information
  -s, --settings <configFile> Override the settings file
  --new-instance              Skip single-instance check
```

There is no `--log-level`, `--debug`, `--verbose`, or `--server`/`--client`
flag. Mode and log level are both set outside the command line.

---

## Debugging from VSCode with breakpoints

VSCode launch configurations for all three platforms are provided in
[.vscode/launch.json](../../.vscode/launch.json). They require:

- **macOS / Linux**: the [CodeLLDB](https://marketplace.visualstudio.com/items?itemName=vadimcn.vscode-lldb)
  extension (`vadimcn.vscode-lldb`) — recommended and already present in this
  repo's dev environment — or the Microsoft C/C++ extension
  (`ms-vscode.cpptools`)
- **Windows**: the Microsoft C/C++ extension (`ms-vscode.cpptools`), which
  bundles the Visual Studio debugger (`cppvsdbg`)

A `build` task that compiles only the `deskflow-core` target is defined in
[.vscode/tasks.json](../../.vscode/tasks.json) and runs automatically before
each debug session via `preLaunchTask`.

### Prerequisites

1. Build a debug binary at least once (see _Build a debug binary_ above) so
   that the build directory and `CMakeCache.txt` exist.
2. Configure the screen layout (server) or remote host (client) through the
   Deskflow GUI at least once so that `deskflow-server.conf` and
   `Deskflow.conf` exist at the standard settings path.

### Starting a debug session

1. Open the **Run and Debug** sidebar (`⇧⌘D` on macOS, `Ctrl+Shift+D` on
   Windows/Linux).
2. Select a configuration from the dropdown at the top:
   - `deskflow-core: server (macOS)` / `(Linux)` / `(Windows)`
   - `deskflow-core: client (macOS)` / `(Linux)` / `(Windows)`
3. Press **F5** (or the green play button).

VSCode compiles only the `deskflow-core` target, then launches the binary
under the debugger. The terminal output (stdout/stderr from the process)
appears in the **Debug Console** tab.

### Binary paths per platform

| Platform | Path                                                  |
| -------- | ----------------------------------------------------- |
| macOS    | `build/bin/Deskflow.app/Contents/MacOS/deskflow-core` |
| Linux    | `build/bin/deskflow-core`                             |
| Windows  | `build\bin\deskflow-core.exe`                         |

On macOS the binary is inside the app bundle produced by `BUILD_OSX_BUNDLE=ON`.
Launching it directly from there (as the launch config does) preserves the
bundle context, so macOS privacy permission checks for ScreenCaptureKit still
resolve against the bundle's `Info.plist`.

### `--new-instance` is always included

Every launch configuration passes `--new-instance` to bypass the
`QSharedMemory` single-instance guard (see
[deskflow-core.cpp:118](../../src/apps/deskflow-core/deskflow-core.cpp#L118)).
Without it, launching a second `deskflow-core` while the release app is running
exits silently before reaching any code you can break in.

### Setting log level for the debug session

Log level is controlled by `log/level` in the settings file — not by a CLI
flag — so it is independent of the VSCode launch config. Options:

- Edit `log/level=DEBUG` in the shared settings file (affects the release app
  too; remember to revert).
- Use the `(macOS, custom settings)` or `(Linux, custom settings)` launch
  configs in `launch.json`, which pass `-s ${workspaceFolder}/build/deskflow-debug.conf`.
  Create that file first:

  ```bash
  # macOS
  cp ~/Library/Deskflow/Deskflow.conf build/deskflow-debug.conf
  # Edit build/deskflow-debug.conf: set level=DEBUG under [log]
  ```

  ```bash
  # Linux
  cp ~/.config/Deskflow/Deskflow.conf build/deskflow-debug.conf
  # Edit build/deskflow-debug.conf: set level=DEBUG under [log]
  ```

### Useful breakpoint locations for audio routing

| File                                                                    | Function                | What to catch                                       |
| ----------------------------------------------------------------------- | ----------------------- | --------------------------------------------------- |
| [AudioClient.cpp:107](../../src/lib/audio/AudioClient.cpp#L107)         | `runCapture`            | Capture start failure                               |
| [AudioClient.cpp:123](../../src/lib/audio/AudioClient.cpp#L123)         | `runCapture`            | Top of the encode/send loop                         |
| [AudioServer.cpp:141](../../src/lib/audio/AudioServer.cpp#L141)         | `processHandshake`      | Handshake magic verification                        |
| [AudioServer.cpp:172](../../src/lib/audio/AudioServer.cpp#L172)         | `processAudioData`      | Packet decode/playback entry                        |
| [WinAudioCapture.cpp:188](../../src/lib/audio/WinAudioCapture.cpp#L188) | `start`                 | Device mix-format warning                           |
| [WinAudioCapture.cpp:270](../../src/lib/audio/WinAudioCapture.cpp#L270) | `readFrames`            | Per-frame format conversion (was the overread site) |
| [MacAudioCapture.mm:41](../../src/lib/audio/MacAudioCapture.mm#L41)     | `didOutputSampleBuffer` | Audio buffers arriving from SCStream                |
| [MacAudioCapture.mm:137](../../src/lib/audio/MacAudioCapture.mm#L137)   | `start`                 | SCShareableContent failure                          |
| [MacAudioCapture.mm:178](../../src/lib/audio/MacAudioCapture.mm#L178)   | `start`                 | SCStream startCapture failure                       |

---

## Combined debugging session checklist

| Check                                           | Windows                                                | macOS                                                       |
| ----------------------------------------------- | ------------------------------------------------------ | ----------------------------------------------------------- |
| Port 24801 reachable                            | `Test-NetConnection <server> 24801`                    | `nc -zv <server> 24801`                                     |
| Device output format                            | Sound Settings → device Advanced tab                   | N/A (SCK sets format)                                       |
| Mix-format conversion warning                   | `WASAPI capture: device mix format is ...; converting` | N/A                                                         |
| Crash report captured                           | WER registry key (see above)                           | `ls ~/Library/Logs/DiagnosticReports \| grep deskflow-core` |
| Faulting frame symbolicated                     | WinDbg `!analyze -v` → `readFrames`                    | `atos` → `MacAudioCapture::start` / capture callback        |
| ARC enabled on capture file                     | N/A                                                    | `grep fobjc-arc build/compile_commands.json`                |
| `NSScreenCaptureUsageDescription` in Info.plist | N/A                                                    | `PlistBuddy -c Print NSScreenCaptureUsageDescription ...`   |
| Entitlements on binary                          | N/A                                                    | `codesign -d --entitlements - ...`                          |
| SCStream error in log                           | N/A                                                    | `SCStream startCapture failed:`                             |
| Audio data flowing                              | Server log: `AudioServer: audio stream from client`    | Same                                                        |
| COM apartment mode                              | `CoInitializeEx hr=0x80010106` in log                  | N/A                                                         |

After reproducing either issue on both machines simultaneously, merge and sort
the two log files by timestamp to correlate events:

```bash
# macOS / Linux
cat /tmp/deskflow-server.log /tmp/deskflow-client.log | sort | less
```

```powershell
# Windows — merge server log (fetched via scp) with client log
Get-Content $env:TEMP\deskflow-client.log, .\deskflow-server.log |
  Sort-Object | Out-Host -Paging
```

### CLI reference for deskflow-core

Verified against [CoreArgs.h](../../src/apps/deskflow-core/CoreArgs.h) and the
[Command Line wiki page](https://github.com/deskflow/deskflow/wiki/Command-Line):

```
deskflow-core <server|client> [options]

Positional argument:
  server    Start in server mode (reads server layout from settings)
  client    Start in client mode (connects to host from settings: client/remoteHost)

Options:
  -h, --help                  Display help text
  -v, --version               Display version information
  -s, --settings <configFile> Override the settings file to use
  --new-instance              Skip the single-instance check
```

There is no `--log-level` flag and no flag to pass the server IP — both are
read from the settings file (`log/level` and `client/remoteHost` respectively).
