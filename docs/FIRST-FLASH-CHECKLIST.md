# First-flash checklist — TransitInk OS 1.2.0

This is a supervised procedure for a later session. No device was flashed while
this package was prepared.

## Approved target and image

- Physical product: `zectrix-s3-epaper-4.2` / Zectrix Note 4
- PlatformIO target: `zectrix_note4`
- MCU: ESP32-S3 revision 0.2
- Memory: 16 MiB SPI flash and 8 MiB OPI PSRAM
- Display: SSD1683-compatible 400×300 monochrome e-paper
- Expected USB interface: `303A:1001`; expected Windows port: `COM3`
- First-install image: `transitink-zectrix-note4-v1.2.0.bin`
- Expected first-install size: `3,863,232 bytes`
- Expected first-install SHA-256:
  `29775DDB5D00BDE2C37085B02D671B994BF0A7765E496AC7362D99A351D80B85`
- OTA/application image: `transitink-zectrix-note4-ota-v1.2.0.bin`

The merged first-install image is the only installation candidate for the
factory-layout device. It contains the TransitInk bootloader, partition table,
boot application and application at their required offsets. The OTA image is
only for a device already running TransitInk's A/B partition layout. Never
write the OTA image directly over the Zectrix factory layout.

## Stop/go gate before connecting

1. Confirm the label and known hardware above still match the physical device.
2. Recalculate SHA-256 for the candidate and require an exact match with this
   checklist and `SHA256SUMS.txt`.
3. Confirm the factory snapshot is still 16,777,216 bytes and hashes to
   `F49C71E843F0DE8FB70E0EE5E8E972B733BB0992ECF29B2E03015F92D1B486EA`.
   It is complete but was not independently re-read; read the recovery caveat
   in `RECOVERY-PLAN.md` before accepting the first-install risk.
4. Use a known-good short USB data cable, stable PC power and a well-charged
   device. Do not use an unpowered hub.
5. Close every serial monitor and any other program holding `COM3`. Confirm the
   port and USB `303A:1001` interface are present.
6. Record the factory screen, firmware 3.6.1 screen and Windows USB/COM details
   with photographs or screenshots.

Stop here unless the operator explicitly authorises the destructive first
installation after reviewing the unverified-backup limitation.

## Preferred first-install procedure

Use the packaged local ESP Web Tools installer in Chrome or Edge. This is safer
than selecting individual binaries because the release manifest fixes the
merged image at offset `0x00000000`, and the bundled full-erase policy prevents
the operation from being presented as a settings-preserving update.

1. Serve the release `installer` directory from localhost, for example:

   ```powershell
   python -m http.server 8000 --directory .\installer
   ```

2. Open `http://localhost:8000/` in Chrome or Edge.
3. Select **Zectrix Note 4** and **首次安裝／清除安裝**.
4. Select only the confirmed `COM3` interface and verify the installer reports
   ESP32-S3 before continuing.
5. Keep the device still and powered. A first installation is expected to erase
   the existing SPI-flash contents, then write the merged image and reset the
   MCU. The factory firmware, factory partitions and factory settings will be
   replaced.
6. Do not unplug, close the browser or start a serial monitor until the
   installer reports completion and releases the port.

Do not use PlatformIO `upload` as the first-install procedure and do not use the
OTA/application image. Those paths do not provide the same explicit full-erase
and merged-layout guardrails for a factory-layout device.

## Boot and serial verification

1. Allow `COM3` and USB `303A:1001` to return after reset.
2. Open a passive 115200-baud serial monitor only after installation has ended.
3. Require the banner `TransitInk OS firmware 1.2.0 board zectrix_note4` and no
   repeated boot, brownout, partition-table or e-paper errors.
4. Confirm the 400×300 panel refreshes and shows either first-time setup or the
   Traditional Chinese commute dashboard. Photograph the complete screen.
5. If first-time setup appears, connect to the displayed `TransitInk-xxxxxx`
   access point, open the displayed local address, and configure:
   - home Wi-Fi;
   - Traditional Chinese (`zh-HK`);
   - Hong Kong time zone (`Asia/Hong_Kong`);
   - power-saving sleep enabled;
   - weekday commute wake enabled, start `06:00`, end `07:30`.
6. Save once, let the device restart, and record the version shown in the setup
   page plus the first dashboard and serial log.
7. Outside the weekday window, press Home once and verify an immediate manual
   session, 30-second ETA polling, and return to standby after ten minutes.

## Stop conditions

Stop without repeated erase/write attempts if any of these occurs:

- the browser identifies a chip other than ESP32-S3 or a port other than the
  confirmed device;
- the image size or SHA-256 differs;
- USB disconnects and does not return normally;
- the installer reports verification, erase or write errors;
- the device boot-loops, reports an invalid partition table, repeatedly
  browns out, becomes hot, or draws abnormal power;
- the display stays blank, shows severe corruption, or does not complete a
  normal refresh after the first boot;
- the serial banner does not report version 1.2.0 and `zectrix_note4`.

Preserve the complete browser and serial logs before deciding on recovery.
