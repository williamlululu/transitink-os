# TransitInk OS 1.2.3 release notes

TransitInk OS 1.2.3 is a targeted Route A transfer-planning maintenance release
for the `zectrix_note4` target.

## Route A correction

The v1.2.2 planner required both a boardable 106 and a boardable 8P to be
present in the current live ETA rows. Operator stop-ETA responses normally show
only a short list of upcoming departures. When all visible 8P departures were
before the passenger's calculated transfer-ready time, Route A became an empty
candidate even though later 8P service was operating beyond that live horizon.

Version 1.2.3 replaces that binary outcome with explicit states:

- `ConfirmedPair`: both legs have a usable matching live ETA.
- `TransferPending`: a usable 106 is retained, but the required 8P is beyond
  the visible live horizon and no defensible provisional time is available.
- `ProvisionalTransfer`: the next transfer is conservatively projected from the
  verified weekday 8P headway, clearly marked `預計`, and automatically
  superseded by a matching live ETA.
- `NoService`: used only outside the verified published service envelope.
- `DataUnavailable` and `Stale`: provider, parsing and freshness failures remain
  distinct from no service.

The pending screen retains the next 106, latest leave-home time, estimated
Victoria Park arrival, transfer-ready time and visible 8P ETA horizon. A short
live horizon is no longer labelled `暫無班次`.

## Official data basis

The following official sources were checked on 2026-09-04. The Transport
Department GTFS snapshot was retrieved on 2026-09-02 and its source files were
generated on 2026-08-27.

- [Citybus route, stop and ETA API](https://data.gov.hk/en-data/dataset/ctb-eta-transport-realtime-eta)
- [KMB ETA API](https://data.gov.hk/en-data/dataset/hk-td-tis_21-etakmb)
- [Transport Department public-transport GTFS feed](https://data.gov.hk/en-data/dataset/hk-td-tis_11-pt-headway-en)
- [Transport Department GTFS data specification](https://static.data.gov.hk/td/pt-headway-en/dataspec/ptheadway_dataspec.pdf)

For Citybus 8P toward Siu Sai Wan, the weekday GTFS service uses route/direction
`1634-2`, service ID `287`. The verified morning origin bands are 06:05–07:05
at 12-minute headways and 07:05–08:50 at 15-minute headways. Route A remains
pinned to Citybus stop `001213` (Victoria Park, Causeway Road); the different
same-name stop `002561` is never accepted.

Intermediate GTFS stop times are not published for this route. The provisional
calculation therefore uses a configurable 15-minute Exhibition Centre to
Victoria Park allowance. This is deliberately slower than the 9.1–10.0 minute
estimate obtained by proportioning the official route geometry/whole-trip time,
and is used only for a visibly provisional transfer. The existing conservative
106 and 8P ride-time settings remain unchanged.

Citybus and KMB source-generated/data timestamps now identify whether a
one-minute provider snapshot has actually changed. The screen can still
recalculate feasibility every 30 seconds in the rapid window, while network
fetches occur no more than once per minute.

## Compatibility and installation

There is no partition-table, bootloader, configuration-schema, Wi-Fi, weather,
power-schedule, Route B, button or OTA-architecture change. Devices running
1.2.2 should use the on-device **Update and keep settings** path from the
owner-controlled feed. That supported A/B OTA path writes the inactive
application slot and preserves NVS and LittleFS. The merged image is provided
only for archival/recovery completeness and must not be used for this update.
