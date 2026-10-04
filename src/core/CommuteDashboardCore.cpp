#include "core/CommuteDashboardCore.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace transitink {
namespace {

constexpr int64_t kSecondsPerMinute = 60;

int64_t etaEvidenceEpoch(const CommuteEtaSnapshot& snapshot) {
    if (snapshot.sourceDataAtEpoch > 0) return snapshot.sourceDataAtEpoch;
    if (snapshot.sourceGeneratedAtEpoch > 0) {
        return snapshot.sourceGeneratedAtEpoch;
    }
    return snapshot.fetchedAtEpoch;
}

bool etaResponseCurrent(const CommuteEtaSnapshot& snapshot,
                        int64_t nowEpoch,
                        const CommutePlannerSettings& settings) {
    const bool sourceStatusKnown = std::any_of(
        snapshot.sources.begin(), snapshot.sources.end(),
        [](const CommuteEtaSourceSnapshot& source) {
            return source.fetchedAtEpoch > 0;
        });
    if (sourceStatusKnown) {
        // A fresh reply from one joint operator cannot certify a failed or old
        // reply from the other as a current empty response / service horizon.
        const std::size_t count = std::min<std::size_t>(
            std::max<uint8_t>(1, snapshot.sourcesExpected), snapshot.sources.size());
        for (std::size_t index = 0; index < count; ++index) {
            const auto& source = snapshot.sources[index];
            const int64_t evidence = source.dataAtEpoch > 0 ? source.dataAtEpoch
                : (source.generatedAtEpoch > 0 ? source.generatedAtEpoch
                                               : source.fetchedAtEpoch);
            if (!source.succeeded || evidence <= 0 || nowEpoch <= 0 ||
                std::max<int64_t>(0, nowEpoch - evidence) >
                    static_cast<int64_t>(settings.maximumEtaAgeMinutes) * 60) {
                return false;
            }
        }
        return true;
    }
    const int64_t evidenceEpoch = etaEvidenceEpoch(snapshot);
    if (evidenceEpoch <= 0 || nowEpoch <= 0) return false;
    const int64_t ageSeconds =
        std::max<int64_t>(0, nowEpoch - evidenceEpoch);
    return ageSeconds <=
           static_cast<int64_t>(settings.maximumEtaAgeMinutes) *
               kSecondsPerMinute;
}

bool etaLatestRequestSucceeded(const CommuteEtaSnapshot& snapshot) {
    return snapshot.sourcesExpected == 0 || snapshot.sourcesSucceeded > 0;
}

bool etaRowCurrent(const CommuteEtaSnapshot& snapshot,
                    std::size_t index,
                    int64_t nowEpoch,
                    const CommutePlannerSettings& settings) {
    const int64_t evidence = snapshot.evidenceEpochs[index] > 0
                                 ? snapshot.evidenceEpochs[index]
                                 : etaEvidenceEpoch(snapshot);
    return evidence > 0 && nowEpoch > 0 && snapshot.epochs[index] > nowEpoch &&
           std::max<int64_t>(0, nowEpoch - evidence) <=
               static_cast<int64_t>(settings.maximumEtaAgeMinutes) * 60;
}

bool etaRowPlanUsable(const CommuteEtaSnapshot& snapshot, std::size_t index,
                      int64_t now, const CommutePlannerSettings& settings) {
    return snapshot.contextImpacts[index].severity != ContextSeverity::HardBlock &&
           etaRowCurrent(snapshot, index, now, settings);
}

bool etaStreamUsable(const CommuteEtaSnapshot& snapshot,
                     int64_t nowEpoch,
                     const CommutePlannerSettings& settings) {
    for (std::size_t index = 0;
         index < std::min(snapshot.count, snapshot.epochs.size()); ++index) {
        if (etaRowCurrent(snapshot, index, nowEpoch, settings)) return true;
    }
    return false;
}

bool earlyProfileAt(int64_t boardingEpoch,
                     int64_t nowEpoch,
                     bool weekdayService,
                     uint16_t localMinuteOfDay,
                     const CommutePlannerSettings& settings) {
    if (!settings.earlyProfileEnabled || !weekdayService || nowEpoch <= 0 ||
        boardingEpoch < nowEpoch ||
        localMinuteOfDay == kUnknownCommuteLocalMinute) return false;
    // The caller supplies the local minute containing nowEpoch. Keep seconds
    // when testing an exclusive band edge (e.g. 06:59:59 versus 07:00:00).
    const int64_t localBoardSeconds =
        static_cast<int64_t>(localMinuteOfDay) * 60 + nowEpoch % 60 +
        boardingEpoch - nowEpoch;
    return localBoardSeconds >= 6 * 3600 && localBoardSeconds < 7 * 3600;
}

CommuteTripEstimate firstLegEstimate(
    int64_t firstBus,
    int64_t nowEpoch,
    bool weekday,
    uint16_t localMinuteOfDay,
    const CommutePlannerSettings& settings) {
    CommuteTripEstimate candidate;
    candidate.firstLegValid = true;
    candidate.firstLegEarlyProfile = earlyProfileAt(
        firstBus, nowEpoch, weekday, localMinuteOfDay, settings);
    candidate.firstLegRideMinutes = candidate.firstLegEarlyProfile
                                        ? settings.route106EarlyRideMinutes
                                        : settings.route106RideMinutes;
    candidate.leaveHomeEpoch =
        firstBus - static_cast<int64_t>(settings.routeAWalkMinutes +
                                        settings.boardingBufferMinutes) *
                       kSecondsPerMinute;
    candidate.firstBusEpoch = firstBus;
    candidate.transferArrivalEpoch =
        firstBus + static_cast<int64_t>(candidate.firstLegRideMinutes) *
                       kSecondsPerMinute;
    candidate.transferReadyEpoch =
        candidate.transferArrivalEpoch +
        static_cast<int64_t>(settings.transferBufferMinutes) *
            kSecondsPerMinute;
    candidate.riskTransferReadyEpoch = firstBus +
        static_cast<int64_t>((candidate.firstLegEarlyProfile
                                  ? settings.route106RiskRideMinutes
                                  : candidate.firstLegRideMinutes) +
                             settings.transferBufferMinutes) * 60;
    return candidate;
}

void completeConnection(CommuteTripEstimate& candidate,
                          int64_t connection,
                          int64_t nowEpoch,
                          bool weekday,
                          uint16_t localMinuteOfDay,
                          const CommutePlannerSettings& settings,
                          bool provisional) {
    candidate.valid = true;
    candidate.provisional = provisional;
    candidate.connectionConfirmed = !provisional && !candidate.staleEvidence;
    candidate.connectionEpoch = connection;
    candidate.connectionEarlyProfile = earlyProfileAt(
        connection, nowEpoch, weekday, localMinuteOfDay, settings);
    candidate.connectionRideMinutes = candidate.connectionEarlyProfile
                                         ? settings.route8pEarlyRideMinutes
                                         : settings.route8pRideMinutes;
    candidate.arrivalEpoch = connection +
        static_cast<int64_t>(candidate.connectionRideMinutes) * 60;
    candidate.riskArrivalEpoch = connection +
        static_cast<int64_t>(candidate.connectionEarlyProfile
                                 ? settings.route8pRiskRideMinutes
                                 : candidate.connectionRideMinutes) * 60;
    candidate.connectionAtRisk =
        connection - static_cast<int64_t>(
                         settings.connectionEarlyAllowanceMinutes) * 60 <
        candidate.riskTransferReadyEpoch;
    candidate.transferMarginMinutes = static_cast<int16_t>(
        (connection - candidate.transferArrivalEpoch) / 60);
}

enum class ServiceExpectation : uint8_t {
    Unknown,
    NotStarted,
    Operating,
    Ended,
};

int64_t transferServiceDayEpoch(int64_t nowEpoch,
                                int64_t transferReadyEpoch,
                                uint16_t localMinuteOfDay) {
    if (localMinuteOfDay >= 24 * 60 || nowEpoch <= 0 ||
        transferReadyEpoch < nowEpoch) return 0;
    // The caller supplies Hong Kong's civil minute. Anchor its midnight once,
    // retaining now's seconds, then locate the transfer's own civil/service day.
    // Advancing now (including across midnight) cannot move this absolute axis.
    const int64_t midnight = nowEpoch - nowEpoch % 60 -
        static_cast<int64_t>(localMinuteOfDay) * 60;
    int64_t serviceDay = midnight +
        ((transferReadyEpoch - midnight) / 86400) * 86400;
    if (transferReadyEpoch - serviceDay < 2 * 3600) serviceDay -= 86400;
    return serviceDay;
}

uint32_t transferServiceMinute(int64_t nowEpoch,
                               int64_t transferReadyEpoch,
                               uint16_t localMinuteOfDay) {
    const int64_t serviceDay = transferServiceDayEpoch(
        nowEpoch, transferReadyEpoch, localMinuteOfDay);
    if (serviceDay <= 0) return UINT32_MAX;
    return static_cast<uint32_t>((transferReadyEpoch - serviceDay + 59) / 60);
}

ServiceExpectation serviceExpectationAt(
    uint32_t serviceMinute,
    const CommutePlannerSettings& settings) {
    if (serviceMinute == UINT32_MAX) return ServiceExpectation::Unknown;
    if (serviceMinute < settings.route8pServiceStartMinutes) {
        return ServiceExpectation::NotStarted;
    }
    if (serviceMinute > settings.route8pLastPossibleTransferMinutes) {
        return ServiceExpectation::Ended;
    }
    return ServiceExpectation::Operating;
}

uint32_t roundUpToHeadway(uint32_t minute,
                          uint32_t bandStart,
                          uint32_t headway) {
    if (headway == 0) return UINT32_MAX;
    if (minute <= bandStart) return bandStart;
    const uint32_t elapsed = minute - bandStart;
    return bandStart + ((elapsed + headway - 1) / headway) * headway;
}

int64_t provisional8pEpoch(
    int64_t nowEpoch,
    int64_t transferReadyEpoch,
    bool weekday,
    uint16_t localMinuteOfDay,
    const CommutePlannerSettings& settings) {
    if (!weekday) return 0;
    const uint32_t transferMinute = transferServiceMinute(
        nowEpoch, transferReadyEpoch, localMinuteOfDay);
    if (transferMinute == UINT32_MAX ||
        transferMinute < settings.route8pOriginToTransferMinutes) {
        return 0;
    }
    const uint32_t originReadyMinute =
        transferMinute - settings.route8pOriginToTransferMinutes;
    uint32_t originDepartureMinute = 0;
    if (originReadyMinute < settings.route8pWeekdayEarlyHeadwayEndMinutes) {
        originDepartureMinute = roundUpToHeadway(
            originReadyMinute, settings.route8pServiceStartMinutes,
            settings.route8pWeekdayEarlyHeadwayMinutes);
    } else if (originReadyMinute <
               settings.route8pWeekdayHeadwayEndMinutes) {
        originDepartureMinute = roundUpToHeadway(
            originReadyMinute, settings.route8pWeekdayEarlyHeadwayEndMinutes,
            settings.route8pWeekdayLaterHeadwayMinutes);
    } else {
        return 0;
    }
    if (originDepartureMinute >= settings.route8pWeekdayHeadwayEndMinutes) {
        return 0;
    }
    const uint32_t connectionMinute =
        originDepartureMinute + settings.route8pOriginToTransferMinutes;
    if (connectionMinute < transferMinute) return 0;
    const int64_t connection = transferServiceDayEpoch(
        nowEpoch, transferReadyEpoch, localMinuteOfDay) +
        static_cast<int64_t>(connectionMinute) * kSecondsPerMinute;
    return connection >= transferReadyEpoch ? connection : 0;
}

void tracePair(CommutePlannerDiagnostics* diagnostics,
               int64_t firstBus,
               int64_t transferReady,
               int64_t connection,
               CommuteTransferPairOutcome outcome) {
    if (diagnostics == nullptr ||
        diagnostics->routeAPairCount >= diagnostics->routeAPairs.size()) {
        return;
    }
    diagnostics->routeAPairs[diagnostics->routeAPairCount++] =
        {firstBus, transferReady, connection, outcome};
}

CommuteAssessment assess(const CommuteTripEstimate& trip,
                         int64_t targetEpoch,
                         const CommutePlannerSettings& settings,
                         bool recoveryMode) {
    if (!trip.valid || targetEpoch <= 0) {
        return CommuteAssessment::Unavailable;
    }
    if (!recoveryMode && trip.arrivalEpoch <=
        targetEpoch - static_cast<int64_t>(settings.safeArrivalMarginMinutes) *
                          kSecondsPerMinute) {
        return CommuteAssessment::Safe;
    }
    return trip.arrivalEpoch <= targetEpoch ? CommuteAssessment::Tight
                                             : CommuteAssessment::Late;
}

bool lowerKnownRiskOnTime(const CommuteTripEstimate& trip, int64_t targetEpoch) {
    return targetEpoch > 0 && trip.valid && trip.arrivalEpoch <= targetEpoch &&
        trip.riskArrivalEpoch > 0 && trip.riskArrivalEpoch <= targetEpoch &&
        !trip.connectionAtRisk && !trip.provisional;
}

bool betterTripForTarget(const CommuteTripEstimate& candidate,
                         const CommuteTripEstimate& current,
                         int64_t targetEpoch) {
    if (!candidate.valid) return false;
    if (!current.valid) return true;
    if (candidate.contextDegraded != current.contextDegraded)
        return !candidate.contextDegraded;
    const bool candidateOnTime = targetEpoch > 0 && candidate.arrivalEpoch <= targetEpoch;
    const bool currentOnTime = targetEpoch > 0 && current.arrivalEpoch <= targetEpoch;
    if (candidateOnTime != currentOnTime) return candidateOnTime;
    if (candidate.provisional != current.provisional) return !candidate.provisional;
    // For a target, prefer an on-time option without the known transfer/slow
    // miss over a faster risky on-time option. If both are late, or no target
    // exists, keep nominal-fastest ordering. This is not a probability model.
    const bool candidateLowerRisk = lowerKnownRiskOnTime(candidate, targetEpoch);
    const bool currentLowerRisk = lowerKnownRiskOnTime(current, targetEpoch);
    if (candidateLowerRisk != currentLowerRisk) return candidateLowerRisk;
    if (candidate.arrivalEpoch != current.arrivalEpoch)
        return candidate.arrivalEpoch < current.arrivalEpoch;
    return candidate.firstBusEpoch < current.firstBusEpoch;
}

CommuteRouteEstimate planRouteA(const CommuteEtaSnapshot& bus106,
                                 const CommuteEtaSnapshot& bus8p,
                                 int64_t nowEpoch,
                                 int64_t targetEpoch,
                                 const CommutePlannerSettings& settings,
                                 bool recoveryMode,
                                 bool weekday,
                                 uint16_t localMinuteOfDay,
                                 CommuteRouteAStatus& status,
                                 CommutePlannerDiagnostics* diagnostics,
                                 const CommuteContextSnapshot& context) {
    CommuteRouteEstimate result;
    result.stale = bus106.stale || bus8p.stale;
    status = {};
    if (diagnostics != nullptr) *diagnostics = {};
    for (std::size_t index = 0;
         index < std::min(bus8p.count, bus8p.epochs.size()); ++index) {
        if (!etaRowPlanUsable(bus8p, index, nowEpoch, settings)) continue;
        if (status.next8pEpoch == 0 ||
            bus8p.epochs[index] < status.next8pEpoch) {
            status.next8pEpoch = bus8p.epochs[index];
        }
        status.live8pHorizonEpoch =
            std::max(status.live8pHorizonEpoch, bus8p.epochs[index]);
    }

    if (!etaStreamUsable(bus106, nowEpoch, settings)) {
        if (bus106.stale ||
            (etaEvidenceEpoch(bus106) > 0 &&
             !etaResponseCurrent(bus106, nowEpoch, settings))) {
            status.state = CommuteRouteAState::Stale;
            status.reason = CommuteRouteAReason::FirstLegStale;
        } else if (etaResponseCurrent(bus106, nowEpoch, settings) &&
                   etaLatestRequestSucceeded(bus106)) {
            status.state = CommuteRouteAState::TransferPending;
            status.reason = CommuteRouteAReason::FirstLegBeyondEtaHorizon;
        } else {
            status.state = CommuteRouteAState::DataUnavailable;
            status.reason = CommuteRouteAReason::FirstLegUnavailable;
        }
        return result;
    }

    const int64_t earliestBoard =
        nowEpoch + static_cast<int64_t>(settings.routeAWalkMinutes +
                                        settings.boardingBufferMinutes) *
                       kSecondsPerMinute;
    std::array<CommuteTripEstimate, kCommuteEtaCount> firstLegs{};
    std::size_t firstLegCount = 0;
    for (std::size_t firstIndex = 0;
         firstIndex < std::min(bus106.count, bus106.epochs.size());
         ++firstIndex) {
        const int64_t firstBus = bus106.epochs[firstIndex];
        if (firstBus < earliestBoard ||
            !etaRowPlanUsable(bus106, firstIndex, nowEpoch, settings)) continue;
        auto candidate = firstLegEstimate(
            firstBus, nowEpoch, weekday, localMinuteOfDay, settings);
        candidate.firstOperator = bus106.operators[firstIndex];
        candidate.contextDegraded = bus106.contextImpacts[firstIndex].severity == ContextSeverity::Degraded;
        candidate.firstLegScheduled = bus106.scheduled[firstIndex];
        candidate.staleEvidence = bus106.rowStale[firstIndex] ||
            (bus106.stale && bus106.evidenceEpochs[firstIndex] == 0);
        firstLegs[firstLegCount++] = candidate;
    }
    if (firstLegCount == 0) {
        status.state = CommuteRouteAState::TransferPending;
        status.reason = CommuteRouteAReason::FirstLegBeyondEtaHorizon;
        return result;
    }
    std::stable_sort(firstLegs.begin(), firstLegs.begin() + firstLegCount,
                     [](const CommuteTripEstimate& left,
                        const CommuteTripEstimate& right) {
                         return left.firstBusEpoch < right.firstBusEpoch;
                     });
    result.primary = firstLegs[0];
    if (firstLegCount > 1) result.fallback = firstLegs[1];

    std::array<CommuteTripEstimate, kCommuteEtaCount> candidates{};
    std::size_t candidateCount = 0;
    if (etaStreamUsable(bus8p, nowEpoch, settings)) {
        for (std::size_t firstIndex = 0; firstIndex < firstLegCount;
             ++firstIndex) {
            const auto& firstLeg = firstLegs[firstIndex];
            CommuteTripEstimate bestForFirstLeg;
            for (std::size_t connectionIndex = 0;
                 connectionIndex < std::min(bus8p.count, bus8p.epochs.size());
                 ++connectionIndex) {
                if (!etaRowPlanUsable(bus8p, connectionIndex, nowEpoch, settings)) {
                    continue;
                }
                const int64_t connection = bus8p.epochs[connectionIndex];
                if (connection < firstLeg.transferReadyEpoch) {
                    tracePair(
                        diagnostics, firstLeg.firstBusEpoch,
                        firstLeg.transferReadyEpoch, connection,
                        CommuteTransferPairOutcome::RejectedBeforeTransferReady);
                    continue;
                }
                CommuteTripEstimate candidate = firstLeg;
                candidate.connectionOperator = bus8p.operators[connectionIndex];
                candidate.contextDegraded = candidate.contextDegraded || bus8p.contextImpacts[connectionIndex].severity == ContextSeverity::Degraded;
                candidate.connectionScheduled = bus8p.scheduled[connectionIndex];
                candidate.staleEvidence = candidate.staleEvidence ||
                    bus8p.rowStale[connectionIndex] ||
                    (bus8p.stale && bus8p.evidenceEpochs[connectionIndex] == 0);
                completeConnection(candidate, connection, nowEpoch, weekday,
                                   localMinuteOfDay, settings, false);
                tracePair(diagnostics, candidate.firstBusEpoch,
                          candidate.transferReadyEpoch, connection,
                          CommuteTransferPairOutcome::AcceptedLive);
                if (!result.nominalFastest.valid ||
                    candidate.arrivalEpoch < result.nominalFastest.arrivalEpoch ||
                    (candidate.arrivalEpoch == result.nominalFastest.arrivalEpoch &&
                     candidate.firstBusEpoch < result.nominalFastest.firstBusEpoch))
                    result.nominalFastest = candidate;
                if (betterTripForTarget(candidate, bestForFirstLeg, targetEpoch))
                    bestForFirstLeg = candidate;
            }
            // Evaluate every bounded pair, retaining only one best option per
            // first leg. Do not allocate a 12x12 trip array on the device stack.
            if (bestForFirstLeg.valid) candidates[candidateCount++] = bestForFirstLeg;
        }
    }

    std::stable_sort(candidates.begin(), candidates.begin() + candidateCount,
                     [targetEpoch](const CommuteTripEstimate& left,
                        const CommuteTripEstimate& right) {
                         return betterTripForTarget(left, right, targetEpoch);
                     });
    if (candidateCount > 0) {
        result.primary = candidates.front();
        result.stale = result.stale || result.primary.staleEvidence;
        result.fallback = {};
        // Keep the next boardable first leg even when its connection lies
        // beyond the visible live horizon. A confirmed primary must not erase
        // the useful information needed after missing that first bus.
        for (std::size_t index = 0; index < firstLegCount; ++index) {
            if (firstLegs[index].firstBusEpoch > result.primary.firstBusEpoch) {
                result.fallback = firstLegs[index];
                break;
            }
        }
        for (std::size_t index = 0; index < candidateCount; ++index) {
            const auto& candidate = candidates[index];
            if (candidate.firstBusEpoch == result.fallback.firstBusEpoch) {
                result.fallback = candidate;
                break;
            }
        }
        if (result.fallback.firstLegValid && !result.fallback.valid &&
            etaResponseCurrent(bus8p, nowEpoch, settings) &&
            etaLatestRequestSucceeded(bus8p)) {
            const int64_t connection = provisional8pEpoch(
                nowEpoch, result.fallback.transferReadyEpoch, weekday,
                localMinuteOfDay, settings);
            const auto impact = commuteContextImpact(context, 1, 1, connection,
                connection + settings.route8pRideMinutes * 60, nowEpoch);
            if (connection > 0 && impact.severity != ContextSeverity::HardBlock) {
                result.fallback.contextDegraded = result.fallback.contextDegraded || impact.severity == ContextSeverity::Degraded;
                completeConnection(result.fallback, connection, nowEpoch,
                                   weekday, localMinuteOfDay, settings, true);
            }
        }
        result.assessment =
            assess(result.primary, targetEpoch, settings, recoveryMode);
        status.state = result.stale ? CommuteRouteAState::Stale
                                    : CommuteRouteAState::ConfirmedPair;
        status.reason = bus106.stale
                            ? CommuteRouteAReason::FirstLegStale
                            : (bus8p.stale || result.primary.staleEvidence
                                   ? CommuteRouteAReason::TransferDataStale
                                   : CommuteRouteAReason::LivePair);
        return result;
    }

    const uint32_t serviceMinute = transferServiceMinute(
        nowEpoch, result.primary.transferReadyEpoch, localMinuteOfDay);
    const ServiceExpectation service =
        serviceExpectationAt(serviceMinute, settings);
    if (service == ServiceExpectation::NotStarted ||
        service == ServiceExpectation::Ended) {
        status.state = CommuteRouteAState::NoService;
        status.reason = service == ServiceExpectation::NotStarted
                            ? CommuteRouteAReason::ServiceNotStarted
                            : CommuteRouteAReason::ServiceEnded;
        return result;
    }

    if (!etaResponseCurrent(bus8p, nowEpoch, settings) ||
        !etaLatestRequestSucceeded(bus8p)) {
        if (etaEvidenceEpoch(bus8p) > 0 || bus8p.stale) {
            status.state = CommuteRouteAState::Stale;
            status.reason = CommuteRouteAReason::TransferDataStale;
        } else {
            status.state = CommuteRouteAState::DataUnavailable;
            status.reason = CommuteRouteAReason::TransferDataUnavailable;
        }
        return result;
    }

    // No live candidates exist on this path. Reuse their bounded workspace
    // rather than putting another twelve trip objects on the device stack.
    auto& provisional = candidates;
    std::size_t provisionalCount = 0;
    for (std::size_t index = 0; index < firstLegCount; ++index) {
        CommuteTripEstimate candidate = firstLegs[index];
        const int64_t connection = provisional8pEpoch(
            nowEpoch, candidate.transferReadyEpoch, weekday,
            localMinuteOfDay, settings);
        const auto impact = commuteContextImpact(context, 1, 1, connection,
            connection + settings.route8pRideMinutes * 60, nowEpoch);
        if (connection <= 0 || impact.severity == ContextSeverity::HardBlock) continue;
        candidate.contextDegraded = candidate.contextDegraded || impact.severity == ContextSeverity::Degraded;
        completeConnection(candidate, connection, nowEpoch, weekday,
                           localMinuteOfDay, settings, true);
        provisional[provisionalCount++] = candidate;
    }
    if (provisionalCount > 0) {
        result.primary = provisional[0];
        result.fallback = {};
        for (std::size_t index = 0; index < firstLegCount; ++index) {
            if (firstLegs[index].firstBusEpoch > result.primary.firstBusEpoch) {
                result.fallback = firstLegs[index];
                break;
            }
        }
        for (std::size_t index = 1; index < provisionalCount; ++index) {
            if (provisional[index].firstBusEpoch ==
                result.fallback.firstBusEpoch) {
                result.fallback = provisional[index];
                break;
            }
        }
        result.assessment =
            assess(result.primary, targetEpoch, settings, recoveryMode);
        status.state = result.stale ? CommuteRouteAState::Stale
                                    : CommuteRouteAState::ProvisionalTransfer;
        status.reason = bus106.stale
                            ? CommuteRouteAReason::FirstLegStale
                            : CommuteRouteAReason::TimetableHeadway;
        status.timetableUsed = true;
        return result;
    }

    status.state = CommuteRouteAState::TransferPending;
    status.reason = CommuteRouteAReason::TransferBeyondEtaHorizon;
    return result;
}

// A2 has its own engineering profile. Do not pass these values through normal
// route settings or apply normal Victoria Park timetable extrapolation here.
CommuteRouteEstimate planRouteA2(const CommuteDashboardSnapshot& dashboard,
        int64_t now, int64_t target, const CommutePlannerSettings& settings,
        bool recovery, bool weekday, uint16_t localMinute, std::string& reason) {
    CommuteRouteEstimate result;
    const auto& context=dashboard.context;
    reason="normal_transfer_available";
    if (!contextFresh(context.generatedEpoch,now) || context.lastFetchFailed ||
        context.alternateCheckedMask == 0) {
        reason="alternate_context_unverified";return result;
    }
    const auto& first=dashboard.etas[0];const auto& connections=dashboard.etas[3];
    // No product-wide recalibration: 12/14 first ride, four-minute walk, 12/14
    // second ride; early-arrival allowance remains the existing risk setting.
    for (std::size_t i=0;i<std::min(first.count,first.epochs.size());++i) {
        const auto mask=contextOperatorMask(first.operators[i]);
        const auto& source=context.sources[mask==2?1:0];
        if (!mask || !(context.alternateCheckedMask & mask) ||
            !source.coverage || !contextFresh(source.fetchedEpoch,now) ||
            !context.sources[0].coverage || !contextFresh(context.sources[0].fetchedEpoch,now)) continue;
        const auto bus=first.epochs[i];
        if (bus<now+(settings.routeAWalkMinutes+settings.boardingBufferMinutes)*60 ||
            !etaRowCurrent(first,i,now,settings) || first.rowStale[i] ||
            (first.stale && first.evidenceEpochs[i]==0)) continue;
        bool activated=false;
        const auto normalArrival=firstLegEstimate(bus,now,weekday,localMinute,settings).transferArrivalEpoch;
        for (std::size_t n=0;n<std::min(context.count,context.events.size());++n) {
            const auto& e=context.events[n];
            const bool normalStop=(e.route==0 && e.scope==ContextScope::Transfer && (e.operatorMask&mask)) ||
                (e.route==1 && e.scope==ContextScope::Boarding && (e.operatorMask&1));
            // Must be active now, finite and recently verified, not merely a
            // future notice, stale cached block, crowding, or advisory.
            if (normalStop && e.verified && e.towardYueWan && !e.athleticMeet &&
                e.severity>=ContextSeverity::Degraded && e.startEpoch<=now && now<e.endEpoch &&
                normalArrival>=e.startEpoch && normalArrival<e.endEpoch &&
                contextFresh(e.fetchedEpoch,now)) activated=true;
        }
        if (!activated) continue;
        reason="alternate_live_eta_pending";
        const auto firstImpact=commuteContextImpact(context,0,mask,bus,bus+14*60,now,true);
        if (firstImpact.severity>=ContextSeverity::Degraded || alternateSegmentChanged(context,0,mask,bus,bus+12*60,bus+14*60)) {
            reason="ngan_mok_or_required_segment_changed";continue;
        }
        CommuteTripEstimate leg;
        leg.alternateTransfer=true;leg.firstLegValid=true;leg.firstOperator=first.operators[i];
        leg.firstLegScheduled=first.scheduled[i];leg.firstLegRideMinutes=12;
        leg.firstBusEpoch=bus;leg.leaveHomeEpoch=bus-(settings.routeAWalkMinutes+settings.boardingBufferMinutes)*60;
        leg.transferArrivalEpoch=bus+12*60;leg.transferReadyEpoch=bus+16*60;
        leg.riskTransferReadyEpoch=bus+18*60;
        if (!result.primary.firstLegValid) result.primary=leg;
        for (std::size_t j=0;j<std::min(connections.count,connections.epochs.size());++j) {
            const auto next=connections.epochs[j];
            if (connections.operators[j]!=2 || !etaRowCurrent(connections,j,now,settings) ||
                connections.rowStale[j] || connections.stale) continue;
            const auto impact=commuteContextImpact(context,1,1,next,next+14*60,now,true);
            if (impact.severity>=ContextSeverity::Degraded || alternateSegmentChanged(context,1,1,next,next+12*60,next+14*60)) {
                reason="hing_fat_or_required_segment_changed";continue;
            }
            if (next<leg.transferReadyEpoch) continue;
            auto candidate=leg;
            candidate.valid=candidate.connectionConfirmed=true;
            candidate.connectionOperator=2;candidate.connectionScheduled=connections.scheduled[j];
            candidate.connectionRideMinutes=12;candidate.connectionEpoch=next;
            candidate.arrivalEpoch=next+12*60;candidate.riskArrivalEpoch=next+14*60;
            candidate.connectionAtRisk=next-settings.connectionEarlyAllowanceMinutes*60<leg.riskTransferReadyEpoch;
            candidate.transferMarginMinutes=static_cast<int16_t>((next-leg.transferArrivalEpoch)/60);
            if (!result.nominalFastest.valid || candidate.arrivalEpoch<result.nominalFastest.arrivalEpoch)
                result.nominalFastest=candidate;
            if (betterTripForTarget(candidate,result.primary,target)) result.primary=candidate;
        }
    }
    // Retain a later safer connection for the selected first leg, independently
    // of the nominal-fastest/target-based primary selection.
    if (result.primary.valid) {
        reason="active_official_normal_stop_disruption";
        const auto& trip=result.primary;
        for (std::size_t j=0;j<std::min(connections.count,connections.epochs.size());++j) {
            const auto next=connections.epochs[j];
            if (next<=trip.connectionEpoch || next-settings.connectionEarlyAllowanceMinutes*60<trip.riskTransferReadyEpoch ||
                connections.operators[j]!=2 || connections.rowStale[j] || connections.stale ||
                !etaRowCurrent(connections,j,now,settings) ||
                commuteContextImpact(context,1,1,next,next+14*60,now,true).severity>=ContextSeverity::Degraded ||
                alternateSegmentChanged(context,1,1,next,next+12*60,next+14*60)) continue;
            if (!result.fallback.valid || next<result.fallback.connectionEpoch) {
                result.fallback=trip;result.fallback.connectionEpoch=next;
                result.fallback.connectionScheduled=connections.scheduled[j];
                result.fallback.arrivalEpoch=next+12*60;result.fallback.riskArrivalEpoch=next+14*60;
                result.fallback.connectionAtRisk=false;
                result.fallback.transferMarginMinutes=static_cast<int16_t>((next-trip.transferArrivalEpoch)/60);
            }
        }
    }
    result.assessment=assess(result.primary,target,settings,recovery);
    return result;
}

CommuteRouteEstimate planRouteB(const CommuteEtaSnapshot& bus118,
                                int64_t nowEpoch,
                                int64_t targetEpoch,
                                const CommutePlannerSettings& settings,
                                bool recoveryMode) {
    CommuteRouteEstimate result;
    result.stale = bus118.stale;
    if (!etaStreamUsable(bus118, nowEpoch, settings)) return result;

    const int64_t earliestBoard =
        nowEpoch + static_cast<int64_t>(settings.routeBWalkMinutes +
                                        settings.boardingBufferMinutes) *
                       kSecondsPerMinute;
    const int64_t leaveOffset =
        static_cast<int64_t>(settings.routeBWalkMinutes +
                             settings.boardingBufferMinutes) *
        kSecondsPerMinute;
    const int64_t rideSeconds =
        static_cast<int64_t>(settings.route118RideMinutes) * kSecondsPerMinute;

    for (std::size_t index = 0;
         index < std::min(bus118.count, bus118.epochs.size()); ++index) {
        const int64_t bus = bus118.epochs[index];
        if (bus < earliestBoard ||
            !etaRowPlanUsable(bus118, index, nowEpoch, settings)) continue;
        CommuteTripEstimate candidate;
        candidate.valid = true;
        candidate.firstLegValid = true;
        candidate.firstLegRideMinutes = settings.route118RideMinutes;
        candidate.firstOperator = bus118.operators[index];
        candidate.contextDegraded = bus118.contextImpacts[index].severity == ContextSeverity::Degraded;
        candidate.firstLegScheduled = bus118.scheduled[index];
        candidate.staleEvidence = bus118.rowStale[index] ||
            (bus118.stale && bus118.evidenceEpochs[index] == 0);
        candidate.leaveHomeEpoch = bus - leaveOffset;
        candidate.firstBusEpoch = bus;
        candidate.arrivalEpoch = bus + rideSeconds;
        candidate.riskArrivalEpoch = candidate.arrivalEpoch;
        if (!result.primary.valid) {
            result.primary = candidate;
            result.stale = result.stale || candidate.staleEvidence;
        } else if (!result.fallback.valid) {
            result.fallback = candidate;
            break;
        }
    }
    result.assessment =
        assess(result.primary, targetEpoch, settings, recoveryMode);
    return result;
}

bool betterRecommendation(const CommuteRouteEstimate& candidate,
                          const CommuteRouteEstimate& current) {
    if (!candidate.primary.valid) return false;
    if (!current.primary.valid) return true;
    if (candidate.primary.contextDegraded != current.primary.contextDegraded)
        return !candidate.primary.contextDegraded;
    const bool candidateOnTime =
        candidate.assessment == CommuteAssessment::Safe ||
        candidate.assessment == CommuteAssessment::Tight;
    const bool currentOnTime =
        current.assessment == CommuteAssessment::Safe ||
        current.assessment == CommuteAssessment::Tight;
    if (candidateOnTime != currentOnTime) return candidateOnTime;
    if (candidate.primary.provisional != current.primary.provisional) {
        return !candidate.primary.provisional;
    }
    if (candidate.stale != current.stale) return !candidate.stale;
    const auto lowerRisk = [](const CommuteRouteEstimate& route) {
        return (route.riskAssessment == CommuteAssessment::Safe ||
                route.riskAssessment == CommuteAssessment::Tight) &&
            !route.primary.connectionAtRisk;
    };
    if (candidateOnTime && lowerRisk(candidate) != lowerRisk(current))
        return lowerRisk(candidate);
    return candidate.primary.arrivalEpoch < current.primary.arrivalEpoch;
}

CommuteDataQuality dataQuality(const CommuteDashboardSnapshot& dashboard,
                               int64_t nowEpoch,
                               const CommutePlannerSettings& settings) {
    bool anyUsable = false;
    bool allUsable = true;
    bool anyPartial = false;
    bool anyStale = false;
    for (std::size_t stream=0;stream<3;++stream) {
        const auto& eta=dashboard.etas[stream];
        const bool usable = etaStreamUsable(eta, nowEpoch, settings);
        anyUsable = anyUsable || usable;
        allUsable = allUsable && usable;
        anyPartial = anyPartial || eta.partial;
        anyStale = anyStale || eta.stale || (eta.count > 0 && !usable);
    }
    if (!anyUsable) return CommuteDataQuality::Unavailable;
    if (anyPartial) return CommuteDataQuality::Partial;
    if (anyStale) return CommuteDataQuality::Stale;
    if (!allUsable) return CommuteDataQuality::Partial;
    return CommuteDataQuality::Fresh;
}

}  // namespace

CommuteEtaSnapshot& commuteEta(CommuteDashboardSnapshot& dashboard,
                               CommuteEtaKind kind) {
    return dashboard.etas[commuteEtaIndex(kind)];
}

const CommuteEtaSnapshot& commuteEta(const CommuteDashboardSnapshot& dashboard,
                                     CommuteEtaKind kind) {
    return dashboard.etas[commuteEtaIndex(kind)];
}

const char* commuteRouteAStateId(CommuteRouteAState state) {
    switch (state) {
        case CommuteRouteAState::DataUnavailable:
            return "data_unavailable";
        case CommuteRouteAState::Stale:
            return "stale";
        case CommuteRouteAState::ConfirmedPair:
            return "confirmed_pair";
        case CommuteRouteAState::TransferPending:
            return "transfer_pending";
        case CommuteRouteAState::ProvisionalTransfer:
            return "provisional_transfer";
        case CommuteRouteAState::NoService:
            return "no_service";
    }
    return "unknown";
}

const char* commuteRouteAReasonId(CommuteRouteAReason reason) {
    switch (reason) {
        case CommuteRouteAReason::None:
            return "none";
        case CommuteRouteAReason::LivePair:
            return "live_pair";
        case CommuteRouteAReason::FirstLegUnavailable:
            return "first_leg_unavailable";
        case CommuteRouteAReason::FirstLegStale:
            return "first_leg_stale";
        case CommuteRouteAReason::FirstLegBeyondEtaHorizon:
            return "first_leg_beyond_eta_horizon";
        case CommuteRouteAReason::TransferDataUnavailable:
            return "transfer_data_unavailable";
        case CommuteRouteAReason::TransferDataStale:
            return "transfer_data_stale";
        case CommuteRouteAReason::TransferBeyondEtaHorizon:
            return "transfer_beyond_eta_horizon";
        case CommuteRouteAReason::TimetableHeadway:
            return "timetable_headway";
        case CommuteRouteAReason::ServiceNotStarted:
            return "service_not_started";
        case CommuteRouteAReason::ServiceEnded:
            return "service_ended";
    }
    return "unknown";
}

void planCommuteDashboard(CommuteDashboardSnapshot& dashboard,
                          int64_t nowEpoch,
                          int64_t targetEpoch,
                          bool weekday,
                          const CommutePlannerSettings& requestedSettings,
                          CommuteSessionMode sessionMode,
                          uint16_t localMinuteOfDay,
                          CommutePlannerDiagnostics* diagnostics) {
    const auto settings = sanitizeCommutePlannerSettings(requestedSettings);
    dashboard.connectionEarlyAllowanceMinutes = settings.connectionEarlyAllowanceMinutes;
    dashboard.targetEpoch = targetEpoch;
    dashboard.weekday = weekday;
    dashboard.sessionMode = sessionMode;
    const bool recoveryMode =
        sessionMode == CommuteSessionMode::AutomaticRecovery;
    dashboard.routeAContext = {};
    dashboard.routeBContext = {};
    dashboard.crowding8p = athleticMeetToday(dashboard.context, nowEpoch);
    dashboard.contextStale = !contextFresh(dashboard.context.generatedEpoch, nowEpoch) ||
                            dashboard.context.lastFetchFailed;
    for (const auto& source : dashboard.context.sources)
        dashboard.contextStale = dashboard.contextStale || source.coverage == 0 ||
                                !contextFresh(source.fetchedEpoch, nowEpoch);
    for (std::size_t stream = 0; stream < dashboard.etas.size(); ++stream) {
        auto& eta = dashboard.etas[stream];
        eta.contextImpacts = {};
        if (stream == 3) {
            for (std::size_t row=0;row<std::min(eta.count,eta.epochs.size());++row)
                eta.contextImpacts[row]=commuteContextImpact(dashboard.context,1,1,eta.epochs[row],eta.epochs[row]+14*60,nowEpoch,true);
            continue;
        }
        auto& routeImpact = stream == 2 ? dashboard.routeBContext : dashboard.routeAContext;
        for (std::size_t row = 0; row < std::min(eta.count, eta.epochs.size()); ++row) {
            const int64_t bus = eta.epochs[row];
            if (bus <= nowEpoch) continue;
            const bool early = earlyProfileAt(bus, nowEpoch, weekday, localMinuteOfDay, settings);
            const int64_t ride = stream == 0 ? (early ? settings.route106EarlyRideMinutes : settings.route106RideMinutes) :
                                 stream == 1 ? (early ? settings.route8pEarlyRideMinutes : settings.route8pRideMinutes) : settings.route118RideMinutes;
            eta.contextImpacts[row] = commuteContextImpact(dashboard.context,
                static_cast<uint8_t>(stream), contextOperatorMask(eta.operators[row]),
                bus, bus + ride * 60, nowEpoch);
            if (eta.contextImpacts[row].severity > routeImpact.severity) routeImpact = eta.contextImpacts[row];
        }
        // Also report a scoped active notice when upstream has no usable rows.
        const auto active = commuteContextImpact(dashboard.context, static_cast<uint8_t>(stream),
            stream == 1 ? 1 : 3, nowEpoch, nowEpoch, nowEpoch);
        if (active.severity > routeImpact.severity) routeImpact = active;
    }
    dashboard.routeA = planRouteA(
        commuteEta(dashboard, CommuteEtaKind::Bus106),
        commuteEta(dashboard, CommuteEtaKind::Bus8P), nowEpoch, targetEpoch,
        settings, recoveryMode, weekday, localMinuteOfDay,
        dashboard.routeAStatus, diagnostics, dashboard.context);
    dashboard.routeB = planRouteB(
        commuteEta(dashboard, CommuteEtaKind::Bus118), nowEpoch, targetEpoch,
        settings, recoveryMode);
    dashboard.routeA2=planRouteA2(dashboard,nowEpoch,targetEpoch,settings,recoveryMode,weekday,localMinuteOfDay,dashboard.alternateReason);
    for (auto* route : {&dashboard.routeA, &dashboard.routeB, &dashboard.routeA2}) {
        auto slow = route->primary;
        slow.valid = slow.valid && slow.riskArrivalEpoch > 0;
        slow.arrivalEpoch = slow.riskArrivalEpoch;
        route->riskAssessment = assess(slow, targetEpoch, settings, recoveryMode);
        if (!route->nominalFastest.valid && route->primary.valid)
            route->nominalFastest = route->primary;
    }
    dashboard.dataQuality = dataQuality(dashboard, nowEpoch, settings);
    dashboard.updatedAtEpoch = 0;
    for (const auto& eta : dashboard.etas) {
        dashboard.updatedAtEpoch =
            std::max(dashboard.updatedAtEpoch, etaEvidenceEpoch(eta));
    }
    dashboard.recommendation = CommuteChoice::None;
    // Session/date policy has already authorized an automatic or manual view.
    // A Special Saturday session is allowed while still using non-weekday bus
    // models; service-day selection must not veto the whole recommendation.
    const bool recommendationEnabled = isActiveCommuteSession(sessionMode);
    if (!recommendationEnabled || nowEpoch <= 0) return;

    CommuteRouteEstimate best;
    if (betterRecommendation(dashboard.routeA, best)) {
        best = dashboard.routeA;
        dashboard.recommendation = CommuteChoice::RouteA;
    }
    if (betterRecommendation(dashboard.routeA2, best)) {
        best=dashboard.routeA2;
        dashboard.recommendation=CommuteChoice::RouteA2;
    }
    if (betterRecommendation(dashboard.routeB, best)) {
        dashboard.recommendation = CommuteChoice::RouteB;
    }
}

CommutePlannerSettings sanitizeCommutePlannerSettings(
    const CommutePlannerSettings& input) {
    auto settings = input;
    const auto boundedRide = [](uint16_t minutes) {
        return std::clamp<uint16_t>(minutes, 1, 120);
    };
    settings.routeAWalkMinutes = std::min<uint16_t>(settings.routeAWalkMinutes, 60);
    settings.routeBWalkMinutes = std::min<uint16_t>(settings.routeBWalkMinutes, 60);
    settings.boardingBufferMinutes =
        std::min<uint16_t>(settings.boardingBufferMinutes, 30);
    settings.transferBufferMinutes =
        std::min<uint16_t>(settings.transferBufferMinutes, 30);
    settings.route106RideMinutes = boundedRide(settings.route106RideMinutes);
    settings.route8pRideMinutes = boundedRide(settings.route8pRideMinutes);
    settings.route118RideMinutes = boundedRide(settings.route118RideMinutes);
    settings.route106EarlyRideMinutes =
        boundedRide(settings.route106EarlyRideMinutes);
    settings.route8pEarlyRideMinutes = boundedRide(settings.route8pEarlyRideMinutes);
    settings.route106RiskRideMinutes = std::max(
        settings.route106EarlyRideMinutes,
        boundedRide(settings.route106RiskRideMinutes));
    settings.route8pRiskRideMinutes = std::max(
        settings.route8pEarlyRideMinutes,
        boundedRide(settings.route8pRiskRideMinutes));
    settings.connectionEarlyAllowanceMinutes =
        std::min<uint16_t>(settings.connectionEarlyAllowanceMinutes, 15);
    settings.safeArrivalMarginMinutes =
        std::min<uint16_t>(settings.safeArrivalMarginMinutes, 60);
    settings.maximumEtaAgeMinutes =
        std::clamp<uint16_t>(settings.maximumEtaAgeMinutes, 1, 15);
    return settings;
}

uint16_t calibratedJourneyMinutes(uint16_t rawMinutes,
                                  uint8_t scalePercent,
                                  int16_t offsetMinutes) {
    const uint32_t scaled =
        (static_cast<uint32_t>(rawMinutes) * scalePercent + 50U) / 100U;
    const int32_t adjusted =
        static_cast<int32_t>(scaled) + static_cast<int32_t>(offsetMinutes);
    return static_cast<uint16_t>(std::min<int32_t>(999,
                                                   std::max<int32_t>(1, adjusted)));
}

}  // namespace transitink
