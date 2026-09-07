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

uint32_t transferServiceMinute(int64_t nowEpoch,
                               int64_t transferReadyEpoch,
                               uint16_t localMinuteOfDay) {
    if (localMinuteOfDay == kUnknownCommuteLocalMinute || nowEpoch <= 0 ||
        transferReadyEpoch <= 0) {
        return UINT32_MAX;
    }
    const int64_t secondsAhead =
        std::max<int64_t>(0, transferReadyEpoch - nowEpoch);
    uint32_t minute = static_cast<uint32_t>(localMinuteOfDay) +
                      static_cast<uint32_t>((secondsAhead + 59) / 60);
    // The published 8P service day extends past midnight. Treat the first two
    // clock hours as the tail of the prior service day when appropriate.
    if (localMinuteOfDay < 2 * 60 && minute < 2 * 60) minute += 24 * 60;
    return minute;
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
    return transferReadyEpoch +
           static_cast<int64_t>(connectionMinute - transferMinute) *
               kSecondsPerMinute;
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

CommuteRouteEstimate planRouteA(const CommuteEtaSnapshot& bus106,
                                 const CommuteEtaSnapshot& bus8p,
                                 int64_t nowEpoch,
                                 int64_t targetEpoch,
                                 const CommutePlannerSettings& settings,
                                 bool recoveryMode,
                                 bool weekday,
                                 uint16_t localMinuteOfDay,
                                 CommuteRouteAStatus& status,
                                 CommutePlannerDiagnostics* diagnostics) {
    CommuteRouteEstimate result;
    result.stale = bus106.stale || bus8p.stale;
    status = {};
    if (diagnostics != nullptr) *diagnostics = {};
    for (std::size_t index = 0;
         index < std::min(bus8p.count, bus8p.epochs.size()); ++index) {
        if (!etaRowCurrent(bus8p, index, nowEpoch, settings)) continue;
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
            !etaRowCurrent(bus106, firstIndex, nowEpoch, settings)) continue;
        auto candidate = firstLegEstimate(
            firstBus, nowEpoch, weekday, localMinuteOfDay, settings);
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
            for (std::size_t connectionIndex = 0;
                 connectionIndex < std::min(bus8p.count, bus8p.epochs.size());
                 ++connectionIndex) {
                if (!etaRowCurrent(bus8p, connectionIndex, nowEpoch, settings)) {
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
                candidate.connectionScheduled = bus8p.scheduled[connectionIndex];
                candidate.staleEvidence = candidate.staleEvidence ||
                    bus8p.rowStale[connectionIndex] ||
                    (bus8p.stale && bus8p.evidenceEpochs[connectionIndex] == 0);
                completeConnection(candidate, connection, nowEpoch, weekday,
                                   localMinuteOfDay, settings, false);
                tracePair(diagnostics, candidate.firstBusEpoch,
                          candidate.transferReadyEpoch, connection,
                          CommuteTransferPairOutcome::AcceptedLive);
                if (candidateCount < candidates.size()) {
                    candidates[candidateCount++] = candidate;
                }
                // The first reachable 8P is the useful connection for this 106.
                break;
            }
        }
    }

    std::stable_sort(candidates.begin(), candidates.begin() + candidateCount,
                     [](const CommuteTripEstimate& left,
                        const CommuteTripEstimate& right) {
                         if (left.arrivalEpoch != right.arrivalEpoch) {
                             return left.arrivalEpoch < right.arrivalEpoch;
                         }
                         return left.firstBusEpoch < right.firstBusEpoch;
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
            if (connection > 0) {
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
        if (connection <= 0) continue;
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
            !etaRowCurrent(bus118, index, nowEpoch, settings)) continue;
        CommuteTripEstimate candidate;
        candidate.valid = true;
        candidate.firstLegValid = true;
        candidate.firstLegRideMinutes = settings.route118RideMinutes;
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
    return candidate.primary.arrivalEpoch < current.primary.arrivalEpoch;
}

CommuteDataQuality dataQuality(const CommuteDashboardSnapshot& dashboard,
                               int64_t nowEpoch,
                               const CommutePlannerSettings& settings) {
    bool anyUsable = false;
    bool allUsable = true;
    bool anyPartial = false;
    bool anyStale = false;
    for (const auto& eta : dashboard.etas) {
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
    dashboard.targetEpoch = targetEpoch;
    dashboard.weekday = weekday;
    dashboard.sessionMode = sessionMode;
    const bool recoveryMode =
        sessionMode == CommuteSessionMode::AutomaticRecovery;
    dashboard.routeA = planRouteA(
        commuteEta(dashboard, CommuteEtaKind::Bus106),
        commuteEta(dashboard, CommuteEtaKind::Bus8P), nowEpoch, targetEpoch,
        settings, recoveryMode, weekday, localMinuteOfDay,
        dashboard.routeAStatus, diagnostics);
    dashboard.routeB = planRouteB(
        commuteEta(dashboard, CommuteEtaKind::Bus118), nowEpoch, targetEpoch,
        settings, recoveryMode);
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
