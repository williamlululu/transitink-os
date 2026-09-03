# Recovery plan — Zectrix Note 4 factory-layout device

## Recovery asset and its limitation

The same-device factory snapshot is:

`zectrix_factory_3.6.1_full_16MB.bin`

- Expected size: 16,777,216 bytes
- SHA-256:
  `F49C71E843F0DE8FB70E0EE5E8E972B733BB0992ECF29B2E03015F92D1B486EA`
- Status: **COMPLETE 16 MiB SNAPSHOT — NOT INDEPENDENTLY VERIFIED**

The read reached the full physical flash size once and the file is preserved
unchanged. Repeated independent reads could not be completed because the USB
serial transport dropped out reproducibly. Consequently, the hash proves the
identity of the preserved file, not that every byte was independently read a
second time. A restore may recover the factory device, but recovery certainty
must not be overstated.

## Before any recovery write

1. Preserve the failed-install browser and serial logs and record the screen.
2. Rule out cable, power, wrong-port and serial-monitor conflicts without
   changing drivers.
3. Confirm the target is the same ESP32-S3 revision 0.2 device with 16 MiB flash
   and MAC `28:84:85:32:07:AC`.
4. Recalculate the snapshot size and SHA-256 and require the exact values above.
5. Do not use the snapshot for any other device: it contains device-specific
   factory state and may contain credentials.
6. Obtain explicit approval for an erase/write recovery operation. Nothing in
   this document is advance authorisation to write the device.

## Recovery paths, in preferred order

1. If TransitInk boots and its partition table is healthy, diagnose first; do
   not overwrite flash merely because Wi-Fi or display configuration is wrong.
2. If TransitInk boots but a release update is needed, use only a matching,
   signed-off TransitInk OTA image through the on-device updater. This cannot
   restore the Zectrix factory layout.
3. If factory restoration is required, enter ESP32-S3 ROM download mode and use
   repository-pinned esptool 5.3.1 to write the verified-size, same-device full
   snapshot from offset `0x00000000`. The repository restore helper performs
   the size check before its write. On Windows, use an equivalent explicit
   esptool command only after a human reviews the exact path and port.

Because the USB link was intermittent during reads, use a known-good data cable,
stable power and a conservative baud rate for a recovery write. Do not loop
automated retries, change drivers, modify eFuses or substitute only selected
factory partitions. Stop on the first unexplained disconnect or verification
failure and reassess from the saved logs.

## Recovery acceptance checks

A factory recovery is not complete until all of these are observed:

- the write tool verifies all 16 MiB and exits successfully;
- USB `303A:1001` and `COM3` return after a normal hard reset;
- passive 115200-baud serial output identifies the Zectrix runtime;
- the display and controls behave normally;
- factory firmware 3.6.1 is reported;
- no unexpected eFuse, secure-boot or flash-encryption state change is present.

If the snapshot itself is questioned, stop. Do not repair, patch, repack or
combine it with another image.
