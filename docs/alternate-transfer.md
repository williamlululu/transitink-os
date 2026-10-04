# Notice-aware alternate transfer (1.3.2)

Normal Route A retains its 12/17-minute early profile, original risk/walk values,
and timetable behavior. Route B retains 35 minutes. Raw ETA timestamps are never
shifted. An independent `routeA2` estimate competes using the existing target/risk
comparison. Normal, nominal-fastest, selected and safer estimates remain separate.

## Official identities verified 2026-10-04

All rows below are toward Siu Sai Wan / Island Resort. The physical direction is
established by ordered route membership, downstream Yue Wan Estate and terminal,
not by treating provider-local direction letters as equivalent.

| Use | Citybus ID / direction / sequence | KMB ID / direction / sequence |
|---|---|---|
| 106 Hung Hom Market | 001533 / I / 14 | 997CCAB996935BD7 / O / 14 |
| 106 Victoria Park, Causeway Road | 001213 / I / 18 | EAFC59DD23C39D5C / O / 18 |
| 106 Ngan Mok Street, King's Road | 001259 / I / 19 | 834CC2324AAFB567 / O / 19 |
| 8P Victoria Park, Causeway Road | 001213 / I / 5 | Not a KMB stream |
| 8P Hing Fat Street | 001251 / I / 6 | Not a KMB stream |
| 8P Yue Wan Estate | 001224 / I / 9 | Not a KMB stream |

Sources: [Citybus 106 ordered stops](https://rt.data.gov.hk/v2/transport/citybus/route-stop/CTB/106/inbound),
[Citybus 8P ordered stops](https://rt.data.gov.hk/v2/transport/citybus/route-stop/CTB/8P/inbound),
[KMB 106 ordered stops](https://data.etabus.gov.hk/v1/transport/kmb/route-stop/106/outbound/1),
[Citybus Ngan Mok identity](https://rt.data.gov.hk/v2/transport/citybus/stop/001259),
[Citybus Hing Fat identity](https://rt.data.gov.hk/v2/transport/citybus/stop/001251),
[KMB stop identities](https://data.etabus.gov.hk/v1/transport/kmb/stop).

## Activation and estimates

A2 requires a fresh verified finite official normal-transfer stop omission or
relocation active now and at the modeled normal stop arrival, scoped to that
106 operator (or the Citybus 8P normal boarding stop). Advisory/crowding alone
does not activate it. Current positive official route membership must confirm the
exact alternate stops and sequence. Failed/stale context, absent route verification,
or no fresh live alternate ETA leaves A2 unavailable; no alternate timetable is
invented. Identified alternate-stop relocation/omission or required-segment change
invalidates A2, including changes overlapping the expected-to-slower arrival window.

The separate provisional engineering model is 106 expected 12 / slower 14 minutes,
explicit walk 4 minutes, and 8P expected 12 / slower 14 minutes. The normal two-minute
transfer buffer does not replace or add to that walk. Existing home boarding buffer
and connection early-arrival risk allowance still apply. Values are engineering
estimates, not a completed statistical calibration or guaranteed journeys. The
limited supporting field evidence and raw personal journey dates are kept private;
first-leg-only observations never validate a completed second leg. Outside the
observed early-commute conditions these fixed alternate estimates remain provisional.

The selected first leg keeps earlier/tight, selected, and later safer 8P choices.
The display explicitly names Ngan Mok, the four-minute walk and Hing Fat. Route B
can still win. LCSD Athletic Meet remains an 8P crowding advisory on either transfer.

## Feed compatibility and limits

Schema 1 is retained. New optional `alternate_checked_mask` certifies current exact
official route-stop membership (Citybus bit 1, KMB bit 2), with Citybus 8P verified
for either bit. It does **not** certify universal notice coverage. The host refetches
the official route graph each generation, rejects wrong direction/service/sequence,
and suppresses the mask on stale data, unavailable relevant feeds or event truncation.
New scopes `alternate_alighting` (106) and `alternate_boarding` (8P) isolate notices
from normal Route A. Old firmware ignores these unknown scopes and optional field;
new firmware reading an old feed retains normal behavior and cannot invent A2.

Existing partial notice coverage remains explicit. Unsupported PDFs/recurrence or
ambiguous stop descriptions remain unparsed, not guessed or applied to another
operator. Positive route membership plus absence of a recognized alternate block is
bounded evidence, not a guarantee that every published notice was understood. All
known explicit stop and route blocks remain authoritative. Context-feed deployment
must be separately reviewed before release; this candidate does not update the live
feed. The Citybus Hing Fat ETA is polled at the existing cadence only when a verified
active normal-stop disruption makes alternate planning relevant.

Bounded diagnostics preserve operator, raw ETA/evidence clocks, normal-stop event,
alternate reason, exact IDs, first-leg arrival windows, walk/readiness, selected,
nominal and safer connections, crowding, and context freshness. Credentials, private
settings and full source responses are excluded.
