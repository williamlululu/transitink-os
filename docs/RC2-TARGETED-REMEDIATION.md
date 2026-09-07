# 1.3.0 rc.2 targeted remediation

Local candidate only. No release/feed/device operation. rc.1 is preserved at
183e2cffabd72551c3312de56827566cf7956628. Product version remains 1.3.0.

## R1: absolute provisional slots

The caller's Hong Kong civil minute plus the epoch's seconds identifies civil
midnight. Locate the transfer-ready instant's own service day (before02:00 is
the preceding service-day tail), round readiness upward to an eligible slot,
then convert that slot from the same absolute service-day anchor. Do not add
rounded minute differences back to a seconds-bearing transfer-ready instant.

Synthetic fixed snapshot at06:20:00,10606:30:20,ready06:44:20:

| Local clock | rc.1 estimated8P / arrival | rc.2 estimated8P / arrival |
|---|---|---|
| 06:20:00 | 06:55:20 /07:12:20 | 06:56:00 /07:13:00 |
| 06:20:30 | 06:44:20 /07:01:20 | 06:56:00 /07:13:00 |
| 06:20:59 | clock-sensitive | 06:56:00 /07:13:00 |

The assumed origin slots are06:05,06:17,06:29,06:41 with15 minutes to the
transfer stop.06:44:00 is before06:44:20 readiness; the next model slot is06:56.
These remain frequency-model estimates, not observed/scheduled bus departures.
New ETA evidence, a changed model/service day, expiration of evidence or a
missed boarding deadline can legitimately change the output; clock-only
recalculation while the same first leg remains usable cannot shift its slot.
No boarding grace or frozen expired first leg was added.

## R2: separate scenario and explicit choice policy

Nominal assessment still uses nominal arrival. A separate riskAssessment uses
the already-computed slower arrival, with no additional buffer. The renderer
labels a slow-scenario deadline miss and shows its arrival when selected.
Leave-now keeps both target assessments unavailable; no target is invented.

The bounded12-by12 live-pair loop now evaluates every eligible pair but retains
only the best candidate per first leg plus the global nominal-fastest candidate.
It does not allocate a144-trip workspace. Later-first-leg fallback is retained.

Within-route choice: nominal-on-time before nominal-late; then, for an on-time
target, prefer a candidate without the known transfer/slow-arrival miss.
Within the remaining group choose nominal earliest arrival, then first bus.
Existing row/route freshness and scheduled labels are retained; this fix does
not redesign the deferred stale/partial-source quality policy. Across routes
the existing route-level freshness/provisional ordering remains,
with the same known-risk comparison before arrival time. If all options are
late, or no target exists, nominal-fastest ordering remains. The model does not
assign statistical reliability probabilities, nor always force118.

Synthetic10606:30 with8P06:46/06:52 and11806:39 now selects8P06:52, arriving07:09,
instead of the risky06:46 connection arriving07:03. The nominal-fastest pair and
its risk remain observable;118's07:14 arrival does not override a suitable A.
When the only nominal-on-time choice is risky, retain it and its warning rather
than switching blindly to a late alternative.

Synthetic8P06:59 retains nominal07:16. Setting its stress-test risk ride to35
changes only the slow arrival to07:34 and its risk assessment/presentation.35 is
not a proposed real ride-time setting; approved12/17 and default risk15/20 remain.

## Evidence and unchanged scope

test_host/test_commute_rc2.cpp checks seconds00/30/59, exact/off-grid ETA,
headway and profile boundaries, service midnight/tail, boarding cutoff, twelve
candidate horizon, target-risk choice and target-free behavior. Four added
renderer fixtures13–16 run production planner outputs through production
EInkDisplay.cpp, real glyphs and Adafruit GFX; the risk/recommendation fields
are not manually populated. Full/partial framebuffer parity remains required.

Existing provider identity/freshness/capacity, independent per-leg profiles,
mode/date policy, optional private calendar, forecast parsing, persisted settings,
OTA mechanism and power schedule are unchanged. Hardware/actual ETA accuracy
remains outside this local targeted re-review candidate's validation scope.
