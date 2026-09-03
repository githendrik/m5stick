# M5StickS3 Firmware

Firmware for the M5StickS3 (ESP32-S3-PICO-1-N8R8) mini IoT dev kit.

## Hardware

| Spec | Value |
|------|-------|
| SoC | ESP32-S3-PICO-1-N8R8 (dual-core LX7, 240MHz) |
| Flash | 8MB |
| PSRAM | 8MB |
| Display | 1.14" LCD, 135x240, ST7789P3 |
| IMU | 6-axis (accel + gyro) |
| Audio | ES8311 codec + MEMS mic + AW8737 amp |
| IR | Transmitter + receiver |
| Battery | 250mAh |
| USB | Type-C 5V |

## Build

```bash
pio run                    # build
pio run -t upload          # flash via USB
pio device monitor         # serial monitor (115200)
```

## Release

```bash
# Bump version in platformio.ini, then:
git add -A && git commit -m "bump version"
git tag vX.Y.Z && git push origin main && git push origin vX.Y.Z
```

Tag push triggers GitHub Actions to build and create a release with the `.bin` attached.
