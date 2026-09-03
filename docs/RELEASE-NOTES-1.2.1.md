# TransitInk OS 1.2.1 release notes

TransitInk OS 1.2.1 is a focused maintenance release for the
`zectrix_note4` target.

## Functional change

- Fixed KMB 106 and 118 departures being discarded when KMB's stop-specific
  ETA response omits the redundant `stop` field. A row without that field now
  inherits the stop ID from the stop-specific request context before the
  existing exact route, direction, service-type and sequence checks run.

No commute-planning, display, weather, Wi-Fi, scheduling, configuration or
other application behaviour is intentionally changed from TransitInk OS 1.2.0.

## Installation paths

- Existing TransitInk OS installations: use the versioned OTA/application
  image through the on-device **Update and keep settings** workflow. This
  writes only the inactive application slot and preserves NVS and LittleFS.
- Archival/first-install image: the merged image is retained for completeness,
  but must not be used for this maintenance update because it replaces the
  complete TransitInk boot and partition layout.

Artifact sizes and SHA-256 hashes are recorded in the packaged
`SHA256SUMS.txt` and `release-metadata.json` files.
