#include "core/CommuteDashboardCore.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace transitink {
namespace {

constexpr int64_t kSecondsPerMinute = 60;

bool etaStreamUsable(const CommuteEtaSnapshot& snapshot,
                     int64_t nowEpoch,
                     const CommutePlannerSettings& settings) {
    if (snapshot.count == 0 || snapshot.fetchedAtEpoch <= 0 || nowEpoch <= 0) {
        return false;
    }
    const int64_t ageSeconds =
        std::max<int64_t>(0, nowEpoch - snapshot.fetchedAtEpoch);
    return ageSeconds <=
           static_cast<int64_t>(settings.maximumEtaAgeMinutes) *
               kSecondsPerMinute;
}

CommuteAssessment assess(const CommuteTripEstimate& trip,
                         int64_t targetEpoch,
                         const CommutePlannerSettings& settings) {
    if (!trip.valid || targetEpoch <= 0) {
        return CommuteAssessment::Unavailable;
    }
    if (trip.arrivalEpoch <=
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
                                const CommutePlannerSettings& settings) {
    CommuteRouteEstimate result;
    result.stale = bus106.stale || bus8p.stale;
    if (!etaStreamUsable(bus106, nowEpoch, settings) ||
        !etaStreamUsable(bus8p, nowEpoch, settings)) {
        return result;
    }

    const int64_t earliestBoard =
        nowEpoch + static_cast<int64_t>(settings.routeAWalkMinutes +
                                        settings.boardingBufferMinutes) *
                       kSecondsPerMinute;
    const int64_t ride106Seconds =
        static_cast<int64_t>(settings.route106RideMinutes) * kSecondsPerMinute;
    const int64_t ride8pSeconds =
        static_cast<int64_t>(settings.route8pRideMinutes) * kSecondsPerMinute;
    const int64_t transferSeconds =
        static_cast<int64_t>(settings.transferBufferMinutes) *
        kSecondsPerMinute;

    std::array<CommuteTripEstimate, kCommuteEtaCount> candidates{};
    std::size_t candidateCount = 0;
    for (std::size_t firstIndex = 0; firstIndex < bus106.count; ++firstIndex) {
        const int64_t firstBus = bus106.epochs[firstIndex];
        if (firstBus < earliestBoard) continue;
        const int64_t transferArrival = firstBus + ride106Seconds;
        for (std::size_t connectionIndex = 0;
             connectionIndex < bus8p.count; ++connectionIndex) {
            const int64_t connection = bus8p.epochs[connectionIndex];
            if (connection < transferArrival + transferSeconds) continue;
            CommuteTripEstimate candidate;
            candidate.valid = true;
            candidate.leaveHomeEpoch =
                firstBus -
                static_cast<int64_t>(settings.routeAWalkMinutes +
                                     settings.boardingBufferMinutes) *
                    kSecondsPerMinute;
            candidate.firstBusEpoch = firstBus;
            candidate.connectionEpoch = connection;
            candidate.arrivalEpoch = connection + ride8pSeconds;
            candidate.transferMarginMinutes = static_cast<int16_t>(
                (connection - transferArrival) / kSecondsPerMinute);
            if (candidateCount < candidates.size()) {
                candidates[candidateCount++] = candidate;
            }
            // The first reachable 8P is the useful connection for this 106.
            break;
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
    if (candidateCount == 0) return result;

    result.primary = candidates.front();
    for (std::size_t index = 0; index < candidateCount; ++index) {
        const auto& candidate = candidates[index];
        if (candidate.firstBusEpoch > result.primary.firstBusEpoch) {
            result.fallback = candidate;
            break;
        }
    }
    result.assessment = assess(result.primary, targetEpoch, settings);
    return result;
}

CommuteRouteEstimate planRouteB(const CommuteEtaSnapshot& bus118,
                                int64_t nowEpoch,
                                int64_t targetEpoch,
                                const CommutePlannerSettings& settings) {
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
    result.assessment = assess(result.primary, targetEpoch, settings);
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
    if (candidate.stale != current.stale) return !candidate.stale;
    return candidate.primary.arrivalEpoch < current.primary.arrivalEpoch;
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

void planCommuteDashboard(CommuteDashboardSnapshot& dashboard,
                          int64_t nowEpoch,
                          int64_t targetEpoch,
                          bool weekday,
                          const CommutePlannerSettings& settings) {
    dashboard.targetEpoch = targetEpoch;
    dashboard.weekday = weekday;
    dashboard.routeA = planRouteA(
        commuteEta(dashboard, CommuteEtaKind::Bus106),
        commuteEta(dashboard, CommuteEtaKind::Bus8P), nowEpoch, targetEpoch,
        settings);
    dashboard.routeB = planRouteB(
        commuteEta(dashboard, CommuteEtaKind::Bus118), nowEpoch, targetEpoch,
        settings);
    dashboard.recommendation = CommuteChoice::None;
    if (!weekday || nowEpoch <= 0 || targetEpoch <= 0) return;

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
    return static_cast<uint16_t>(std::clamp<int32_t>(adjusted, 1, 999));
}

}  // namespace transitink
