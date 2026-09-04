# RFC: Voice Messages via Signal

**Status:** In Progress (firmware done, LXC pending)
**Date:** 2026-09-04
**Author:** Hendrik

## Summary

Add a "Voice Message" app to the M5StickS3 firmware that records audio from the
built-in ES8311 microphone and sends it as a voice note to a Signal contact via
`signal-cli-rest-api` running natively in an LXC container on the homelab.

## Motivation

A spare virtual mobile number is available. The M5StickS3 has a microphone,
speaker, and WiFi — making it a capable push-to-talk device. Signal is the
preferred messaging platform (E2E encrypted, REST API available). The homelab
runs on Proxmox with native LXC containers; Docker should be avoided.

## Architecture

```
┌─────────────────┐      HTTP POST (audio/wav)        ┌──────────────────────────┐
│   M5StickS3     │  ──────────────────────────────►  │  Homelab LXC             │
│                 │                                   │  Thin proxy (Python/sh)  │
│  BtnA: record   │  POST /send?recipient=<number>    │  ┌─ ffmpeg WAV→Opus      │
│  BtnB: app nav  │  Content-Type: audio/wav          │  └─ signal-cli-rest-api  │
│                 │  Auth: Bearer (optional)          │    (native, no Docker)   │
│  I2S → ES8311   │                                   │                          │
│  mic capture    │                                   │  Java JRE + signal-cli   │
│  PSRAM buffer   │                                   │  Go binary (REST server) │
│  WAV encode     │  ◄──────────────────────────────  │                          │
│                 │        200 OK / error              │  Registered with spare   │
└─────────────────┘                                   │  virtual number          │
                                                      └────────────┬─────────────┘
                                                                   │ Signal protocol
                                                                   ▼
                                                       ┌──────────────────────────┐
                                                       │  Signal servers          │
                                                       │  → delivers voice note   │
                                                       │    to recipient          │
                                                       └──────────────────────────┘
```

### Components

#### 1. M5StickS3 Firmware (new app)

- **New app slot:** App index 1 (after Toaster=0), pushing Info→2 and Status→3.
  `NUM_APPS` → 4
- **Recording:** I2S capture from ES8311 MEMS mic via M5Unified's `M5.Mic` API
  - `M5.Mic.setSampleRate()` + `M5.Mic.begin()`, then `M5.Mic.record()` in
    1024-sample chunks into a PSRAM buffer
  - Format: 16kHz, 16-bit mono PCM → WAV container (simplest, no encoder needed)
  - Buffer in PSRAM (8MB available, ~320KB for 10s of audio)
- **Controls:**
  - BtnA (main button): press to start recording, press again to stop + send
  - Display: recording indicator (red dot + timer), "Sending..." status
  - BtnB (side button): cycle to next app (unchanged behavior)
- **Upload:** HTTP POST with raw WAV body (not multipart) to a thin proxy on the LXC
  - URL: `http://<gateway_ip>:<port>/send?recipient=<number>`
  - Body: raw WAV bytes (`Content-Type: audio/wav`)
  - Auth: optional Bearer token header
  - Timeout: 15s for upload
- **Config additions** (in `config_manager.h`):
  - `signalGatewayIp` — LXC IP (e.g., `10.0.0.50`)
  - `signalGatewayPort` — default `8080`
  - `signalRecipient` — phone number (e.g., `+41791234567`) **or** group ID.
    The proxy distinguishes: if the value starts with `group=` prefix, send
    to group; otherwise treat as a phone number.
  - `signalAuthToken` — bearer token for the REST API (optional but recommended)
- **Web dashboard** (`web_dashboard.h`): new config fields for all of the above,
  including recipient number — user can change who to send to without touching
  the LXC

#### 2. Homelab LXC Container (signal-cli-rest-api)

**No Docker.** Install natively in a Debian 13 LXC (template already
downloaded on the homelab):

```
LXC: signal-gateway (10.0.0.x)
├── Java JRE 17+           (apt install default-jre)
├── signal-cli              (GitHub release .tar.gz, native binary)
├── signal-cli-rest-api     (GitHub release .bin, native Go binary)
└── ffmpeg                  (apt install ffmpeg — WAV→Opus transcoding)
```

**Registration (one-time):**
1. Start signal-cli-rest-api in registration mode
2. Receive SMS verification code on the virtual number
3. Submit code to complete Signal registration

**Runtime:**
- A thin proxy script (Python/Flask or shell+nc) listens on the LXC and
  exposes `POST /send?recipient=<number>`:
  1. Receive raw WAV body from M5Stick
  2. Transcode WAV → Opus/OGG via `ffmpeg`
  3. Forward to `signal-cli-rest-api` with the recipient from the query param
- `signal-cli-rest-api` listens on `localhost:8080` internally
- signal-cli maintains the Signal session state in `~/.local/share/signal-cli/`

**LXC resource footprint:**
- ~256MB RAM (Java + signal-cli)
- Minimal CPU (idle most of the time)
- 1-2GB disk (Java + signal-cli data + received attachments)

#### 3. Data Flow

1. User cycles to Voice Message app (BtnB), sees "Press OK to record"
2. User presses BtnA → red dot + timer shown, I2S recording starts
3. User presses BtnA again → recording stops, WAV assembled in PSRAM
4. Firmware POSTs raw WAV body to the LXC proxy (`/send?recipient=...`)
5. Proxy transcodes WAV → Opus, forwards to `signal-cli-rest-api`
6. Recipient receives a playable voice note in their Signal chat
7. Display shows "Sent" or error message

## Technical Considerations

### Audio Format

The M5Stick records raw WAV (16kHz, 16-bit, mono) — no encoder needed on the
ESP32. The LXC transcodes WAV → Opus/OGG via `ffmpeg` before forwarding to
`signal-cli`. This ensures the recipient sees a native Signal voice note
(waveform, auto-play, inline player) rather than a generic file attachment.

**Why not send WAV directly?** Signal renders Opus/OGG attachments as voice
notes with inline playback. WAV files appear as downloadable file attachments
with no inline player — not the UX we want.

**Transcoding pipeline (in LXC):**
```
ffmpeg -i voice.wav -c:a libopus -b:a 32k -ar 16000 -ac 1 voice.opus
```
Low bitrate (32k) is sufficient for voice and keeps the file small.

### Memory

- 10s @ 16kHz/16-bit = ~320KB → fits in PSRAM easily
- Max practical recording: ~60s = ~1.9MB (PSRAM has 8MB)
- HTTP POST needs the full audio in memory (no streaming multipart on ESP32)

### Security

- LXC is not exposed to the internet (same LAN only)
- Optional bearer token auth on `signal-cli-rest-api`
- Config stored in NVS (already the pattern via `config_manager.h`)
- Signal session keys live in the LXC filesystem

### Failure Modes

| Scenario | Behavior |
|---|---|
| WiFi not connected | Show "No WiFi" on screen, don't attempt send |
| Gateway unreachable | Show "Gateway error", keep recording in PSRAM (retry?) |
| Signal send fails | Show "Send failed", log to serial |
| Recording too long | Hard cap at 60s, auto-stop and send |
| PSRAM allocation fails | Show "Memory error", abort |

## Firmware Changes Required

| File | Change | Status |
|---|---|---|
| `src/main.cpp` | Add app 1 (Voice Message), recording logic, upload function | Done |
| `src/voice_message.h` | New file: mic capture, WAV header, HTTP upload | Done |
| `src/config_manager.h` | Add `signalGatewayIp`, `signalGatewayPort`, `signalRecipient`, `signalAuthToken` | Done |
| `src/web_dashboard.h` | Add config fields for Signal settings | Done |
| `platformio.ini` | No changes needed — existing HTTPClient handles raw body POST | — |

## LXC Setup (outline)

```bash
# Create LXC on Proxmox (Debian 13 template already downloaded)
pct create <id> debian-13-standard --hostname signal-gateway --memory 512 --rootfs local-lvm:4

# Inside LXC:
apt update && apt install -y default-jre ffmpeg
# Download signal-cli from GitHub releases
# Download signal-cli-rest-api from GitHub releases
# Register number, create systemd service
```

## LXC Proxy Endpoint

The M5Stick POSTs raw WAV to `POST /send?recipient=<value>`. The proxy
needs to:

1. Read the raw body (WAV audio)
2. Pipe through `ffmpeg` to produce m4a (AAC):
   ```
   ffmpeg -i voice.wav -c:a aac -b:a 32k -ar 16000 -ac 1 voice.m4a
   ```
3. Call `signal-cli` to send `voice.m4a` with `--voice-note`:
   - If recipient starts with `group=`: `signal-cli -u <number> send -g <GROUP_ID> -a voice.opus --voice-note`
   - Otherwise: `signal-cli -u <number> send -a voice.opus --voice-note <RECIPIENT>`
4. Return 200 on success, non-200 on failure

Simple implementation: Python + Flask (or even a shell CGI script). Runs on
the same LXC as signal-cli-rest-api.

**signal-cli `--voice-note` flag:** Confirmed working on v0.14.7. The flag
marks the attachment as a voice note in Signal's protocol, which triggers
inline playback UI (waveform, play button, auto-play) on the recipient's
client. Without it, audio arrives as a generic file attachment.

**Attachment dedup caveat:** signal-cli caches uploaded attachments by
content hash. If the same file is sent first without `--voice-note` and then
with it, the cached pointer is reused and the voice-note flag is lost.
In practice this isn't an issue — each voice recording has unique content.
The proxy should not reuse cached attachments.

**Audio format:** Tested with m4a and ogg/opus — only m4a (AAC) renders as
voice notes when `--voice-note` is set. Opus files appear as file attachments
despite the flag. The proxy transcodes WAV→m4a via ffmpeg.

## Decisions

1. **Opus transcoding:** Server-side via ffmpeg in the LXC. WAV would render
   as a file attachment, not a voice note. Transcoding is in Phase 1.
2. **Recipients:** Single recipient, configured in the M5Stick web dashboard
   (stored in NVS). The device sends the recipient value with each POST.
   Supports both phone numbers (e.g., `+41791234567`) and group IDs
   (prefixed with `group=`). The proxy distinguishes and calls signal-cli
   with `-g` for groups or a positional recipient for numbers.
3. **Playback:** Not in scope. May revisit later.
4. **Recording quality:** 16kHz, 16-bit mono. Sufficient for voice, minimal
   memory footprint.
5. **Systemd service:** Yes — signal-cli-rest-api runs as a systemd unit.

## Implementation Phases

### Phase 1 — Minimum Viable Voice Note
- [ ] LXC: Create Debian 13 container, install JRE + ffmpeg
- [ ] LXC: Install signal-cli + signal-cli-rest-api, register number
- [ ] LXC: WAV→Opus transcoding proxy (`POST /send?recipient=...`)
- [ ] LXC: systemd unit for signal-cli-rest-api
- [x] Firmware: I2S mic capture → WAV in PSRAM (16kHz, 16-bit, mono)
- [x] Firmware: HTTP POST raw WAV body to gateway endpoint (with recipient)
- [x] Firmware: New app with record/send UI
- [x] Config: Gateway IP + port + recipient + auth token in config_manager + web dashboard

### Phase 2 — Polish
- [x] Recording timer + max duration enforcement (60s cap)
- [ ] Error handling + retry
- [ ] Battery-level aware recording (warn if low)

### Phase 3 — Optional Enhancements
- [ ] Multiple recipients (web dashboard list, device cycles through)
- [ ] Receive/read text messages from Signal
- [ ] Playback of received voice notes (speaker)
