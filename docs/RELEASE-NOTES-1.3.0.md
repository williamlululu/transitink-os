# TransitInk OS 1.3.0 candidate

Status: candidate source; not published or deployed. Firmware target remains
zectrix_note4 (ESP32-S3, 16 MB flash), and partition/NVS/LittleFS layouts remain
unchanged. Device version uses numeric 1.3.0 for existing OTA compatibility.

## ETA and planner behavior

Official route, direction, stop and provider stop sequence must match. KMB
regular service type remains 1. Citybus does not publish an independent
service-type/vehicle-identity field; its fixed route/direction/stop/sequence is
matched, without pretending provider-local eta_seq identifies a bus.
Distinct operators' close or equal timestamps remain separate and unmodified.
Each row carries source/request timestamps; a fresh other operator or local
recalculation cannot refresh old evidence. Responses with no rows also retain
source-level timestamps. The bounded stream holds twelve rows; no cross-stop
eta_seq subtraction is used as a journey-time measurement.

The expected early profile is 106=12 and 8P=17 minutes, selected separately by
projected boarding time in [06:00,07:00), Hong Kong weekdays excluding public
holidays. It is an engineering trial, not a fitted average or percentile.
Outside that band the uncalibrated fallback stays 17/27; 118 stays35 without a
learned profile. Normal walk4/13, boarding2, physical transfer2 and target
margin5 minutes remain. There is no assumed final walk beyond Yue Wan Estate.

The advisory scenario uses 106=15/8P=20 minutes in the early band and tests the
connecting8P up to3 minutes early. A slow first leg plus physical transfer must
fit before the earlier second-leg scenario to avoid the risk label. These
settings are bounded and separately persisted; official ETA and expected
arrival do not absorb the advisory allowances. A confirmed ETA pair is not a
promise of an actual successful transfer. Cached/scheduled/provisional rows
remain labelled; no exact later service is fabricated outside supported
weekday timetable assumptions. Both the next8P and selected8P are shown.

## Modes and private settings

Scheduled: configured normal weekday morning with07:25 at Yue Wan. Defaults
06:00–07:30, rapid local30-second recalculation06:40–07:10. Provider cadence
and backoff are separate; a local redraw does not refetch route tables.
Special: one civil date with target01:25–23:54, shifting phases by the target
change. The date expires naturally; subsequent dates restore normal defaults.
Leave-now: target-free manual journeys outside applicable periods, including
weekends and holidays. No old07:25 lateness is calculated.

Authenticated settings contain the optional commute extension. Existing schema3
and older supported schemas migrate without altering Wi-Fi, widgets or stored
credentials. Old clients that omit the extension retain current private
settings. Up to16 user-entered date ranges provide information-only hints by
default. Explicit pause is optional; no teacher attendance is inferred.
There is no private calendar embedded in source, fixtures or firmware. The
preparation package is not a native configuration and must not be posted.

The public holiday table is an independently obtained1823 government snapshot
covering2025–2027; outside coverage the early/headway model is not claimed as a
known weekday service. Manual journeys continue. Public holiday service type
is independent of an explicit personal Special target or private pause.

Home selects buses before network work, including light-sleep and reset wakes.
The down key selects weather; the up key opens/closes settings. Protected reset
is unchanged. A Home session near the automatic end keeps its remaining manual
time. Offline local planning still advances walking cutoffs and evidence age.

## Weather and evidence limits

The weather renderer accepts six valid consecutive forecast days starting
today or tomorrow. The icon describes current weather conditions, not a rain
gauge; daily significant-rain probability is not a commute-hour forecast.
No unprovided final walk, measured current rainfall, real-time ETA accuracy,
statistical calibration, power consumption or hardware usability is claimed.

Host previews compile EInkDisplay.cpp with production fonts and Adafruit GFX,
with only hardware/time mocked. Full/partial framebuffer parity is checked.
They are actual firmware layout evidence, not physical e-paper photographs.

## Official sources

- Citybus source cadence and dictionary: https://data.gov.hk/en-data/dataset/ctb-eta-transport-realtime-eta
- Citybus fixed boarding sequences106/8P/118:14/5/16, rechecked via official
  https://rt.data.gov.hk/v2/transport/citybus/route-stop/CTB/106/inbound
  and corresponding8P/118 endpoints.
- KMB106/118 regular outbound boarding sequence14/16, rechecked via
  https://data.etabus.gov.hk/v1/transport/kmb/route-stop/106/outbound/1
  and corresponding118 endpoint.
- Government holidays: https://www.1823.gov.hk/common/ical/tc.json
  fetched2026-09-07; SHA-256
  07a7b97084b1fbd75e8d3ee9d064744f578b8aa3493831897c3677ccdb68630e.

## Deployment remains a separate authorized action

This candidate has no release approval or deployment authorization. Checkpoint,
package hashes, tests, clean build and image validation are recorded in the
private delivery report. Real board reboot/settings retention, button response,
Wi-Fi recovery and workday ETA observations remain to be performed.
For a future authorized update, publish only the reviewed immutable package and
verify downloaded feed bytes against its SHA-256; use the existing authenticated
Update and keep settings A/B OTA path. Never use the merged factory image to
update an installed1.2.3 device. Do not erase NVS/LittleFS or reinstall the old
version. The existing tag workflow rebuilds; its output cannot be substituted
for the reviewed candidate without fresh validation and matching approval.
