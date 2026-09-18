# Official public commute context (1.3.1 RC)

Travel-time calibration is unchanged from v1.3.0: early 106/8P 12/17 minutes,
118 fallback 35 minutes, the same risk values, walks and boarding/transfer buffers.
This release adds information visibility, disruption awareness, crowding advisory
and human-readable Traditional Chinese. It does not establish field ETA accuracy.

## Collection and actual coverage

`scripts/build_commute_context.py` uses Python 3 standard library plus pinned host-only `pypdf==6.10.0` for KMB PDFs. It emits
schema 1 JSON, maximum 24 events / 24 KiB, from official public sources:

- Citybus: https://mobile.citybus.com.hk/NWST/special-arrangement-listing.php
  reads the six-column HTML listing. Only exact fixed-route critical stop aliases,
  physical Siu Sai Wan direction and a single explicit full-date interval produce
  planner effects. Omitted stops without a replacement are hard blocks; replacement
  or relocated stops are degraded. Unresolved date/time/recurrence/direction/body
  formats are counted and not guessed. Separate Citybus PDF notices are not covered.
- KMB: the official route site's `MapFunction.js` exposes
  `Function/FunctionRequest.ashx?action=getroutebound&route=...` and `getAnnounce`.
  Actual discovery was verified for 106 and 118. Available bound IDs are read from
  official route metadata; the normalizer queries all listed bounds and preserves
  provider-local bound identifiers without equating them to physical direction.
  Notice IDs and official `AnnouncementPicture.ashx` PDF/HTML links are recorded.
  **Discovery and one explicit dated stop-suspension PDF table format are supported.**
  The host checks physical direction, exact critical stop, full From/To date and time,
  and replacement wording. Other PDF/HTML formats, image-only notices and timetable
  semantics remain unparsed. Titles alone never block a route. Coverage is partial. No historical
  notice URL or search-engine results are used for discovery.
- LCSD: https://www.lcsd.gov.hk/clpss/en/webApp/Facility/Details.do?ftid=38&did=2
  discovers current facility 488 accessible XLSX links. The dated temporary-closure
  detail worksheet provides explicit Athletic Meet rows. The general B legend also
  covers Athletic Training/other activity, so B alone is never a crowding flag.
  Dated meet entries apply from HKT midnight to next midnight, irrespective of event
  hours. Training, football and maintenance do not trigger this flag. Unsupported
  date formats are counted. LCSD advises that bookings may change.

Every effect carries official source URL, identifier, fetch time, route, physical
direction, operator scope, critical stop scope, effect, validity, severity and
Traditional Chinese message. Only public operational data is allowed. No private
calendar, school identity, user journey, device configuration or credentials are
inputs. Development fixtures use synthetic dates and notices.

## RC execution and later publication boundary

Install the pinned host dependency from `requirements-context.txt` in an isolated environment.
Local command: `python scripts/build_commute_context.py --output /private/output/commute-context.json`.
No recurring/publication workflow is installed by this RC. An automatic approval
review rejected adding a scheduled Pages deployment in the RC-only task. The local
normalizer and consuming firmware can be independently tested without publication.
After independent review and separate release authorization, the owner must arrange
bounded six-hour normalization and copy the reviewed JSON output alongside the
existing approved installer, retaining all exact firmware-byte approval gates.
That activation is a release prerequisite; it has NOT been performed here.

The intended endpoint is the existing owner-controlled static origin plus
`/commute-context.json`. Until it is separately published, firmware discloses missing
context and continues its existing ETA/planner behavior. It never claims no notices
exist merely because the endpoint is unavailable.

Firmware uses verified HTTPS, a bounded 24 KiB body and 32 KiB JSON allocator. It
polls only during an existing connected commute refresh, every six hours after
success, backing off 15/30/60/120/240 minutes on failure, capped at six hours. It
creates no wake timer and no extra Wi-Fi reconnection. RAM cache persists through
light sleep; no new NVS/config field or partition change is introduced.

Context is fresh for 24 hours. Explicit verified finite-validity disruptions may
continue within their interval when a source is unavailable; unbounded or ambiguous
stale arrangements cannot hard-block. Stale LCSD data never creates a meet advisory.
The planner tests candidates at the relevant modeled boarding/transfer/arrival
instant, preserving raw ETA rows. Citybus notices filter only Citybus candidates;
the other joint operator remains independently eligible. The core supports an
explicit physical two-operator effect; the parser requires both an explicit
operator-independent flag and physical-stop-closure effect. The current normalizer
emits only independently supported operator-specific events. Degraded journeys keep
unchanged time estimates and prefer an available unchanged journey. Current ETA
cannot override a verified omitted critical stop.

## Diagnostics and validation

Bounded serial cycle records expose retained raw candidate epochs, operator,
request/generated/data/evidence time, scheduled/live marker, selection or rejection
reason, context event ID/severity, context fetch time and per-source coverage.
No raw JSON, private dates, settings or credentials are dumped. Discovery without
interpretable effects is a limitation, not evidence that there are no disruptions.
Synthetic C++/Python cases cover scoped/operator/date/freshness effects, fallback
protection, unchanged calibration, all-day meets and cache bounds. Actual rendered
previews and visual review are required separately before RC readiness.
