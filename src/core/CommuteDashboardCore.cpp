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

bool etaStreamUsable(const CommuteEtaSnapshot& snapshot,
                     int64_t nowEpoch,
                     const CommutePlannerSettings& settings) {
    return snapshot.count > 0 &&
           etaResponseCurrent(snapshot, nowEpoch, settings);
}

CommuteTripEstimate firstLegEstimate(
    int64_t firstBus,
    const CommutePlannerSettings& settings) {
    CommuteTripEstimate candidate;
    candidate.firstLegValid = true;
    candidate.leaveHomeEpoch =
        firstBus - static_cast<int64_t>(settings.routeAWalkMinutes +
                                        settings.boardingBufferMinutes) *
                       kSecondsPerMinute;
    candidate.firstBusEpoch = firstBus;
    candidate.transferArrivalEpoch =
        firstBus + static_cast<int64_t>(settings.route106RideMinutes) *
                       kSecondsPerMinute;
    candidate.transferReadyEpoch =
        candidate.transferArrivalEpoch +
        static_cast<int64_t>(settings.transferBufferMinutes) *
            kSecondsPerMinute;
    return candidate;
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
    const int64_t ride8pSeconds =
        static_cast<int64_t>(settings.route8pRideMinutes) * kSecondsPerMinute;

    std::array<CommuteTripEstimate, kCommuteEtaCount> firstLegs{};
    std::size_t firstLegCount = 0;
    for (std::size_t firstIndex = 0; firstIndex < bus106.count; ++firstIndex) {
        const int64_t firstBus = bus106.epochs[firstIndex];
        if (firstBus < earliestBoard) continue;
        firstLegs[firstLegCount++] = firstLegEstimate(firstBus, settings);
    }
    if (firstLegCount == 0) {
        status.state = CommuteRouteAState::TransferPending;
        status.reason = CommuteRouteAReason::FirstLegBeyondEtaHorizon;
        return result;
    }
    result.primary = firstLegs[0];
    if (firstLegCount > 1) result.fallback = firstLegs[1];
    status.live8pHorizonEpoch =
        bus8p.count == 0 ? 0 : bus8p.epochs[bus8p.count - 1];

    std::array<CommuteTripEstimate, kCommuteEtaCount> candidates{};
    std::size_t candidateCount = 0;
    if (etaStreamUsable(bus8p, nowEpoch, settings)) {
        for (std::size_t firstIndex = 0; firstIndex < firstLegCount;
             ++firstIndex) {
            const auto& firstLeg = firstLegs[firstIndex];
            for (std::size_t connectionIndex = 0;
                 connectionIndex < bus8p.count; ++connectionIndex) {
                const int64_t connection = bus8p.epochs[connectionIndex];
                if (connection < firstLeg.transferReadyEpoch) {
                    tracePair(
                        diagnostics, firstLeg.firstBusEpoch,
                        firstLeg.transferReadyEpoch, connection,
                        CommuteTransferPairOutcome::RejectedBeforeTransferReady);
                    continue;
                }
                CommuteTripEstimate candidate = firstLeg;
                candidate.valid = true;
                candidate.connectionConfirmed = true;
                candidate.connectionEpoch = connection;
                candidate.arrivalEpoch = connection + ride8pSeconds;
                candidate.transferMarginMinutes = static_cast<int16_t>(
                    (connection - candidate.transferArrivalEpoch) /
                    kSecondsPerMinute);
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
        result.fallback = {};
        for (std::size_t index = 0; index < candidateCount; ++index) {
            const auto& candidate = candidates[index];
            if (candidate.firstBusEpoch > result.primary.firstBusEpoch) {
                result.fallback = candidate;
                break;
            }
        }
        result.assessment =
            assess(result.primary, targetEpoch, settings, recoveryMode);
        status.state = result.stale ? CommuteRouteAState::Stale
                                    : CommuteRouteAState::ConfirmedPair;
        status.reason = bus106.stale
                            ? CommuteRouteAReason::FirstLegStale
                            : (bus8p.stale
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

    std::array<CommuteTripEstimate, kCommuteEtaCount> provisional{};
    std::size_t provisionalCount = 0;
    for (std::size_t index = 0; index < firstLegCount; ++index) {
        CommuteTripEstimate candidate = firstLegs[index];
        const int64_t connection = provisional8pEpoch(
            nowEpoch, candidate.transferReadyEpoch, weekday,
            localMinuteOfDay, settings);
        if (connection <= 0) continue;
        candidate.valid = true;
        candidate.provisional = true;
        candidate.connectionEpoch = connection;
        candidate.arrivalEpoch = connection + ride8pSeconds;
        candidate.transferMarginMinutes = static_cast<int16_t>(
            (connection - candidate.transferArrivalEpoch) / kSecondsPerMinute);
        provisional[provisionalCount++] = candidate;
    }
    if (provisionalCount > 0) {
        result.primary = provisional[0];
        result.fallback = provisionalCount > 1 ? provisional[1]
                                               : CommuteTripEstimate{};
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

    for (std::size_t index = 0; index < bus118.count; ++index) {
        const int64_t bus = bus118.epochs[index];
        if (bus < earliestBoard) continue;
        CommuteTripEstimate candidate;
        candidate.valid = true;
        candidate.leaveHomeEpoch = bus - leaveOffset;
        candidate.firstBusEpoch = bus;
        candidate.arrivalEpoch = bus + rideSeconds;
        if (!result.primary.valid) {
            result.primary = candidate;
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
                          const CommutePlannerSettings& settings,
                          CommuteSessionMode sessionMode,
                          uint16_t localMinuteOfDay,
                          CommutePlannerDiagnostics* diagnostics) {
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
    const bool recommendationEnabled =
        isActiveCommuteSession(sessionMode) &&
        (weekday || sessionMode == CommuteSessionMode::Manual);
    if (!recommendationEnabled || nowEpoch <= 0 || targetEpoch <= 0) return;

    CommuteRouteEstimate best;
    if (betterRecommendation(dashboard.routeA, best)) {
        best = dashboard.routeA;
        dashboard.recommendation = CommuteChoice::RouteA;
    }
    if (betterRecommendation(dashboard.routeB, best)) {
        dashboard.recommendation = CommuteChoice::RouteB;
    }
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
