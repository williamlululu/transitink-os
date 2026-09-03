# TransitInk OS 1.2.2 release notes

TransitInk OS 1.2.2 is a focused transition and maintenance release for the
`zectrix_note4` target.

## Changes relative to the installed 1.2.0 build

- Includes the KMB stop-specific ETA fix first validated in 1.2.1. When a KMB
  106 or 118 stop-specific ETA row omits the redundant `stop` field, the row
  inherits the stop ID from the request context before the existing route,
  direction, service-type and sequence checks run.
- Changes the default OTA feed from the upstream project to the
  owner-controlled endpoint at
  `https://williamlululu.github.io/transitink-os`.

No commute-planning, display, weather, Wi-Fi, scheduling, configuration or
other application behaviour is intentionally changed from 1.2.1.

## Installation paths

- Devices already running 1.2.2 or a later owner-feed build can use the
  on-device **Update and keep settings** workflow. It writes only the inactive
  application slot and preserves NVS and LittleFS.
- The currently installed 1.2.0 build is compiled to use the upstream OTA feed
  and has no local firmware-upload endpoint. It cannot discover this release
  from the new owner feed. The repository-supported transition is therefore a
  one-time merged-image installation through ESP Web Tools, followed by
  re-entering the saved configuration. This transition erases device settings.
- Do not write the OTA image directly with serial flash tools. The merged image
  is only for the supervised one-time transition or a clean first installation.

Artifact sizes and SHA-256 hashes are recorded in the packaged
`SHA256SUMS.txt` and `release-metadata.json` files.
