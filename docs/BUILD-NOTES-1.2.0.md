# TransitInk OS 1.2.0 build notes

Build date: 2026-09-03 (Asia/Hong_Kong)

## Target and toolchain

- PlatformIO environment: `zectrix_note4`
- PlatformIO Core: 6.1.19
- Platform: pioarduino `55.03.39`
- Framework: Arduino-ESP32 3.3.9 / ESP-IDF 5.5.4
- Target compiler: Xtensa ESP ELF 14.2.0
- Packaging/image validation: esptool 5.3.1
- Configuration: ESP32-S3, 16 MiB flash, QIO flash/OPI PSRAM, 8 MiB PSRAM
- Display profile: SSD1683-compatible, 400×300 monochrome

The release version is `1.2.0`. The repository parser accepts only numeric
`x.y.z`, so `1.2.0-rc1` was deliberately not used.

## Clean build

The final build directory was explicitly cleaned before this build. Command
equivalent:

```powershell
platformio run -e zectrix_note4 -t clean
platformio run -e zectrix_note4
```

Result: **PASS**

- Application flash: 3,797,183 / 6,553,600 bytes (57.9%)
- Static RAM: 106,128 / 327,680 bytes (32.4%)

## Release images

### Merged first-install image

- File: `transitink-zectrix-note4-v1.2.0.bin`
- Size: 3,863,232 bytes
- SHA-256:
  `29775DDB5D00BDE2C37085B02D671B994BF0A7765E496AC7362D99A351D80B85`
- Intended offset: `0x00000000`
- Contents: bootloader, TransitInk partition table, boot application and
  application image
- esptool 5.3.1: ESP32-S3 header, 16 MiB flash declaration, valid checksum and
  valid validation hash
- Reproducibility gate: byte hash matched PlatformIO's canonical
  `firmware.factory.bin`

This image is destructive to the existing factory layout and is the only
candidate for a deliberate first installation.

### OTA/application image

- File: `transitink-zectrix-note4-ota-v1.2.0.bin`
- Size: 3,797,696 bytes
- SHA-256:
  `F313D78D8AA86D1DB828E2F30695581C45510371D0AF641BF39378BABFFCB522`
- esptool 5.3.1: eight valid ESP32-S3 segments, valid checksum and valid
  validation hash

This raw application image is only for the on-device updater after TransitInk's
A/B partition layout is installed. It must not be written directly over the
Zectrix factory partition layout.

## Verification summary

- 132 focused Python/static/portal tests: PASS
- 10 additional transport-catalog semantic tests: PASS
- 6 native commute behavior tests: PASS
- High-severity PlatformIO/cppcheck gate: PASS, zero high findings
- Pinned Noto Sans CJK HK and GNU Unifont subset: PASS, 1,449 glyphs
- Five deterministic 400×300 monochrome previews: generated and visually
  checked for hierarchy, clipping, overlap and missing glyphs
- Merged and OTA image structure/checksum validation with esptool 5.3.1: PASS

One transport-catalog wrapper test remains environment-blocked because it
executes the repository's Unix `.venv/bin/python` path. A direct catalogue
`--check` also reports CRLF-sensitive generated text files as stale in this
Windows checkout; the identical warning is present in the immutable baseline.
The compressed catalogue hashes, schema, revision, size limits, selectable
directions and official labels all passed, and no catalogue was regenerated.

The remaining aggregate host-native suite expects a system C++17 compiler and
Unix virtual-environment paths. No system-wide compiler was installed. The
focused commute executable ran with the isolated optional compiler; the full
ESP32-S3 compiler build provides the production compilation check. The native
JSON harness requires `std::string_view`, which is unavailable in the isolated
GCC 5 host package, while the same parser compiled successfully for ESP32-S3.

## Safety statement

No serial port or physical device was accessed. No reset, erase, flash, OTA,
driver change or eFuse operation was performed while preparing this release.
