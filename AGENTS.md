# M5StickS3 - AI Agent Context

## Project Overview

Firmware for the M5StickS3 ESP32-S3 mini IoT dev kit. Built with PlatformIO + Arduino + M5Unified.

## Hardware

- **SoC**: ESP32-S3-PICO-1-N8R8 (8MB Flash, 8MB PSRAM)
- **Display**: 1.14" LCD 135x240 (ST7789P3), managed by M5GFX via M5Unified
- **IMU**: 6-axis accelerometer + gyroscope
- **Audio**: ES8311 codec, MEMS mic, AW8737 power amp + speaker
- **IR**: Transmitter + receiver (disable speaker amp when using IR receiver)
- **Battery**: 250mAh (keep speaker <75% volume on battery power)
- **Expansion**: Hat2 bus (2.54-16P), HY2.0-4P Grove interface

## Tech Stack

- **Framework**: PlatformIO + Arduino
- **Library**: M5Unified (auto-detects StickS3, includes M5GFX for display)
- **Board**: `esp32-s3-devkitc-1` (no `m5sticks3` board ID in PlatformIO; M5Unified auto-detects the hardware)
- **Partitions**: Custom `partitions.csv` with factory + ota_0 + ota_1 slots (8MB flash)
- **OTA**: Dual-slot with bootloader rollback safety (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=1`)

## Project Structure

```
m5stick/
├── platformio.ini          # PlatformIO config
├── partitions.csv          # 8MB flash: factory + ota_0 + ota_1 + otadata + spiffs
├── src/
│   ├── main.cpp            # Main firmware (WiFiManager, apps, button handling)
│   ├── config_manager.h    # NVS-backed config (toaster IP, etc.)
│   ├── ota_update.h        # GitHub releases OTA check + apply
│   └── web_dashboard.h     # Async web server (config, status, OTA endpoints)
├── .github/workflows/
│   └── build.yml           # Tag-triggered build & release
├── backup/                 # Factory firmware dump
├── .venv/                  # Python venv (PlatformIO + esptool)
├── AGENTS.md               # This file
├── README.md
└── .gitignore
```

## Development Commands

```bash
source .venv/bin/activate          # activate venv first
pio run                              # build
pio run -t upload                    # flash via USB
pio run -t upload --upload-port /dev/cu.usbmodem*  # explicit port
pio device monitor                   # serial monitor
```

## Release Process

1. Bump `-DFIRMWARE_VERSION` in `platformio.ini`
2. Commit and push to main
3. Tag and push: `git tag vX.Y.Z && git push origin vX.Y.Z`
4. GitHub Actions builds and creates a release with the firmware `.bin` attached as `firmware-m5sticks3.bin`
5. Verify: `gh run list --repo githendrik/m5stick --limit 1`

```bash
# After committing:
git push origin main && git tag vX.Y.Z && git push origin vX.Y.Z
# Watch the build:
sleep 15 && gh run list --repo githendrik/m5stick --limit 1
gh run watch <RUN_ID> --repo githendrik/m5stick
# Verify release:
gh release view vX.Y.Z --repo githendrik/m5stick
```

### OTA Update (from the device)

The device checks `githendrik/m5stick` GitHub releases for `firmware-m5sticks3.bin`.

**Via web dashboard** (`http://m5stick.local`):
- Click "Check for Updates" → "Update Now"

**Via device** (App 3 - Status):
- Side button to cycle to Status app
- Main button (BtnA) to check + apply update
- Device downloads firmware to inactive OTA slot, reboots
- If new firmware fails to boot, bootloader auto-rolls back to previous slot

**Via curl**:
```bash
curl -s http://m5stick.local/check-update
# Wait 5s, then:
curl -s http://m5stick.local/check-update
# If available:
curl -s -X POST http://m5stick.local/apply-update
```

## Critical Notes

- M5Unified handles board detection automatically — no manual pin definitions needed
- The `m5sticks3` PlatformIO board definition handles Flash/PSRAM config — do NOT add custom PSRAM board_build flags (causes boot loops, see spotipanel lessons)
- Display rotation 0 = portrait (native StickS3 orientation, USB at bottom)
- Main blue button (BtnA) = interact with current app; side button (BtnB) = cycle apps
- PWR button on StickS3 only does power on/off, not usable in firmware
- IR receiver requires the speaker power amplifier to be turned off
- On battery power, keep speaker volume below 75% to avoid reboots
- Always use `.venv/` for pip installs and PlatformIO commands within this repo

## References

- [M5Unified Library](https://github.com/m5stack/M5Unified)
- [M5GFX Library](https://github.com/m5stack/M5GFX)
- [StickS3 Arduino Docs](https://docs.m5stack.com/en/arduino/m5sticks3/program)
- [StickS3 Product Page](https://shop.m5stack.com/products/m5sticks3-esp32s3-mini-iot-dev-kit)
- Related projects: eink-desk-panel, spotipanel
