#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "core/CommuteSessionCore.h"

namespace transitink {

constexpr std::size_t kCommuteEtaStreamCount = 3;
constexpr std::size_t kCommuteEtaCount = 3;
constexpr std::size_t kForecastDayCount = 6;

enum class CommuteEtaKind : uint8_t {
    Bus106,
    Bus8P,
    Bus118,
};

enum class CommuteChoice : uint8_t {
    None,
    RouteA,
    RouteB,
};

enum class CommuteAssessment : uint8_t {
    Unavailable,
    Safe,
    Tight,
    Late,
};

enum class CommuteDataQuality : uint8_t {
    Fresh,
    Partial,
    Stale,
    Unavailable,
};

struct CommuteEtaSnapshot {
    std::array<int64_t, kCommuteEtaCount> epochs{};
    std::size_t count = 0;
    int64_t fetchedAtEpoch = 0;
    bool stale = false;
    bool partial = false;
    std::string error;
};

struct CommuteTripEstimate {
    bool valid = false;
    int64_t leaveHomeEpoch = 0;
    int64_t firstBusEpoch = 0;
    int64_t connectionEpoch = 0;
    int64_t arrivalEpoch = 0;
    int16_t transferMarginMinutes = -1;
};

struct CommuteRouteEstimate {
    CommuteTripEstimate primary;
    CommuteTripEstimate fallback;
    CommuteAssessment assessment = CommuteAssessment::Unavailable;
    bool stale = false;
};

struct CommutePlannerSettings {
    uint16_t routeAWalkMinutes = 4;
    uint16_t routeBWalkMinutes = 13;
    uint16_t boardingBufferMinutes = 2;
    uint16_t transferBufferMinutes = 2;
    uint16_t route106RideMinutes = 17;
    uint16_t route8pRideMinutes = 27;
    uint16_t route118RideMinutes = 35;
    uint16_t safeArrivalMarginMinutes = 5;
    uint16_t maximumEtaAgeMinutes = 3;
};

// Kept for the legacy TDAS module, which remains in the tree but is no longer
// part of the Yue Wan commute dashboard runtime.
enum class JourneyEstimateSource : uint8_t {
    Unavailable,
    Tdas,
    TrafficModel,
    CalibratedFallback,
};

struct JourneyEstimate {
    bool valid = false;
    bool stale = false;
    std::string destinationTc;
    std::string routeLabel;
    uint16_t rawMinutes = 0;
    uint16_t adjustedMinutes = 0;
    uint8_t scalePercent = 100;
    int16_t offsetMinutes = 0;
    int64_t updatedAtEpoch = 0;
    JourneyEstimateSource source = JourneyEstimateSource::Unavailable;
    std::string messageTc;
};

struct CommuteDashboardSnapshot {
    std::array<CommuteEtaSnapshot, kCommuteEtaStreamCount> etas{};
    CommuteRouteEstimate routeA;
    CommuteRouteEstimate routeB;
    CommuteChoice recommendation = CommuteChoice::None;
    int64_t targetEpoch = 0;
    int64_t updatedAtEpoch = 0;
    bool weekday = false;
    CommuteSessionMode sessionMode = CommuteSessionMode::Standby;
    CommuteDataQuality dataQuality = CommuteDataQuality::Unavailable;
};

struct ForecastDay {
    uint8_t month = 0;
    uint8_t day = 0;
    uint8_t weekday = 0;
    int8_t minimumC = 0;
    int8_t maximumC = 0;
    uint8_t minimumHumidity = 0;
    uint8_t maximumHumidity = 0;
    std::string conditionTc;
    std::string rainChanceTc;
};

struct ForecastSnapshot {
    bool valid = false;
    bool stale = false;
    std::string locationTc = "香港";
    std::string summaryTc;
    std::array<ForecastDay, kForecastDayCount> days{};
    std::size_t dayCount = 0;
    int64_t updatedAtEpoch = 0;
    std::string error;
};

constexpr std::size_t commuteEtaIndex(CommuteEtaKind kind) {
    return static_cast<std::size_t>(kind);
}

CommuteEtaSnapshot& commuteEta(CommuteDashboardSnapshot& dashboard,
                               CommuteEtaKind kind);
const CommuteEtaSnapshot& commuteEta(const CommuteDashboardSnapshot& dashboard,
                                     CommuteEtaKind kind);

void planCommuteDashboard(CommuteDashboardSnapshot& dashboard,
                          int64_t nowEpoch,
                          int64_t targetEpoch,
                          bool weekday,
                          const CommutePlannerSettings& settings = {},
                          CommuteSessionMode sessionMode =
                              CommuteSessionMode::AutomaticNormal);

uint16_t calibratedJourneyMinutes(uint16_t rawMinutes,
                                  uint8_t scalePercent,
                                  int16_t offsetMinutes);

}  // namespace transitink
