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
constexpr uint16_t kUnknownCommuteLocalMinute = UINT16_MAX;
constexpr std::size_t kCommuteTransferPairTraceCount =
    kCommuteEtaCount * kCommuteEtaCount;

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

enum class CommuteRouteAState : uint8_t {
    DataUnavailable,
    Stale,
    ConfirmedPair,
    TransferPending,
    ProvisionalTransfer,
    NoService,
};

enum class CommuteRouteAReason : uint8_t {
    None,
    LivePair,
    FirstLegUnavailable,
    FirstLegStale,
    FirstLegBeyondEtaHorizon,
    TransferDataUnavailable,
    TransferDataStale,
    TransferBeyondEtaHorizon,
    TimetableHeadway,
    ServiceNotStarted,
    ServiceEnded,
};

enum class CommuteTransferPairOutcome : uint8_t {
    RejectedBeforeTransferReady,
    AcceptedLive,
};

struct CommuteEtaSnapshot {
    std::array<int64_t, kCommuteEtaCount> epochs{};
    std::size_t count = 0;
    int64_t fetchedAtEpoch = 0;
    int64_t sourceGeneratedAtEpoch = 0;
    int64_t sourceDataAtEpoch = 0;
    std::size_t rawRowCount = 0;
    std::size_t parsedRowCount = 0;
    std::size_t acceptedRowCount = 0;
    uint8_t sourcesExpected = 0;
    uint8_t sourcesSucceeded = 0;
    bool sourceChanged = false;
    bool stale = false;
    bool partial = false;
    std::string error;
};

struct CommuteTripEstimate {
    bool valid = false;
    bool firstLegValid = false;
    bool connectionConfirmed = false;
    bool provisional = false;
    int64_t leaveHomeEpoch = 0;
    int64_t firstBusEpoch = 0;
    int64_t transferArrivalEpoch = 0;
    int64_t transferReadyEpoch = 0;
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

struct CommuteRouteAStatus {
    CommuteRouteAState state = CommuteRouteAState::DataUnavailable;
    CommuteRouteAReason reason = CommuteRouteAReason::None;
    int64_t live8pHorizonEpoch = 0;
    bool timetableUsed = false;
};

struct CommuteTransferPairTrace {
    int64_t firstBusEpoch = 0;
    int64_t transferReadyEpoch = 0;
    int64_t connectionEpoch = 0;
    CommuteTransferPairOutcome outcome =
        CommuteTransferPairOutcome::RejectedBeforeTransferReady;
};

struct CommutePlannerDiagnostics {
    std::array<CommuteTransferPairTrace, kCommuteTransferPairTraceCount>
        routeAPairs{};
    std::size_t routeAPairCount = 0;
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
    uint16_t route8pServiceStartMinutes = 6 * 60 + 5;
    uint16_t route8pLastPossibleTransferMinutes = 24 * 60 + 55;
    uint16_t route8pOriginToTransferMinutes = 15;
    uint16_t route8pWeekdayEarlyHeadwayEndMinutes = 7 * 60 + 5;
    uint16_t route8pWeekdayHeadwayEndMinutes = 8 * 60 + 50;
    uint16_t route8pWeekdayEarlyHeadwayMinutes = 12;
    uint16_t route8pWeekdayLaterHeadwayMinutes = 15;
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
    CommuteRouteAStatus routeAStatus;
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
                              CommuteSessionMode::AutomaticNormal,
                          uint16_t localMinuteOfDay =
                              kUnknownCommuteLocalMinute,
                          CommutePlannerDiagnostics* diagnostics = nullptr);

const char* commuteRouteAStateId(CommuteRouteAState state);
const char* commuteRouteAReasonId(CommuteRouteAReason reason);

uint16_t calibratedJourneyMinutes(uint16_t rawMinutes,
                                  uint8_t scalePercent,
                                  int16_t offsetMinutes);

}  // namespace transitink
