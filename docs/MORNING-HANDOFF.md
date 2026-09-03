# Morning handoff — Yue Wan commute dashboard

## Ready state

TransitInk OS 1.2.0 is prepared for the `zectrix_note4` target. Development,
tests, previews and packaging were performed offline; `COM3` and the physical
device were not accessed, reset, erased or flashed.

Release build values:

- Clean build: `PASS`
- Application/RAM use: `3,797,183 / 6,553,600 bytes (57.9%)` flash;
  `106,128 / 327,680 bytes (32.4%)` RAM
- Merged first-install image: `transitink-zectrix-note4-v1.2.0.bin`
- Merged image size: `3,863,232 bytes`
- Merged image SHA-256:
  `29775DDB5D00BDE2C37085B02D671B994BF0A7765E496AC7362D99A351D80B85`
- OTA image: `transitink-zectrix-note4-ota-v1.2.0.bin`
- OTA size: `3,797,696 bytes`
- OTA SHA-256:
  `F313D78D8AA86D1DB828E2F30695581C45510371D0AF641BF39378BABFFCB522`

Read `FIRST-FLASH-CHECKLIST.md` and `RECOVERY-PLAN.md` before making a go/no-go
decision. The full factory snapshot exists and has the recorded hash, but it
was not independently re-read because of intermittent USB transport.

## Product behavior

The screen compares only these two fixed journeys to arrive at 漁灣邨 by 07:25:

- A: four-minute walk to 紅磡街市 (`001533`), 106 to 維園轉車
  (`001213`), then 8P to 漁灣邨 (`001224`).
- B: thirteen-minute walk to 海底隧道 (`001476`), then regular 118 to
  漁灣邨 (`001224`).

Conservative configurable defaults are 17 minutes on 106, 27 minutes on 8P,
35 minutes on 118, two minutes to board, and two minutes to transfer. Live ETA
is used for boarding and missed-bus alternatives; it is not misused as an
end-to-end journey-time observation. Joint Citybus/KMB rows for 106 and 118 are
matched by route, operator, direction, service, stop and sequence, then
equivalent operator rows are de-duplicated conservatively.

The default Hong Kong weekday schedule is:

- 06:00 initial update;
- 06:00–06:40 transport polling every 120 seconds;
- 06:40–07:10 every 30 seconds;
- 07:10–07:30 recovery mode, fastest available route and high-risk/late
  treatment, polling every 120 seconds;
- final 07:30 status update, then low-power standby;
- no automatic weekend session.

Home starts or extends a ten-minute manual session outside the automatic window,
polling every 30 seconds. The device uses modem power saving while active and
light sleep—not deep sleep—in standby. Wi-Fi and periodic APIs stop in standby.
Weather is cached for at least 15 minutes. E-paper data polling is separate from
rendering: unchanged regions are skipped, changes use partial refresh, and a
full refresh is forced conservatively after repeated partial updates.

## First physical checks to record

- boot banner, reset reason, Wi-Fi state and version at 115200 baud;
- full first screen and each visible refresh transition;
- route recommendation, leave/arrival time, fallback and transfer margin;
- current-rain wording versus daily significant-rain probability wording;
- stale/offline state with Wi-Fi unavailable;
- Home manual session and ten-minute return to standby;
- battery voltage/percentage telemetry at installation and after one week.

## Assumptions needing field calibration

- The 17/27/35-minute ride defaults are conservative planning inputs, not live
  measured journey times. Compare displayed arrivals with several real trips
  before adjusting them.
- The two-minute boarding and transfer margins, including the roughly ten-metre
  Victoria Park transfer, need real-world confirmation.
- Citybus/KMB stop sequences are fixed to the researched service variants and
  should be rechecked if operators revise routes.
- HKO's current-rain observation and general daily significant-rain probability
  do not establish rain at a specific minute of the commute.
- A practical one-charge-per-week target is unverified until battery logs are
  collected on the actual device.

## Single next action

With the device still unmodified, review the factory-snapshot caveat and the
exact image hash in `FIRST-FLASH-CHECKLIST.md`; only then make an explicit
supervised go/no-go decision for the packaged merged first-install image.
