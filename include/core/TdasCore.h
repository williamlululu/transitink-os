#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "core/CommuteDashboardCore.h"

namespace transitink {

enum class TdasDestination : uint8_t {
    ShunTak,
    GordonRoad,
};

enum class TdasRoute3Evidence : uint8_t {
    NotExposed,
    Present,
    Absent,
};

struct TdasCalibration {
    uint8_t scalePercent = 100;
    int16_t offsetMinutes = 0;
};

struct TdasRouteResult {
    bool valid = false;
    uint16_t rawMinutes = 0;
    bool usedWesternHarbourCrossing = false;
    TdasRoute3Evidence route3Evidence = TdasRoute3Evidence::NotExposed;
    std::size_t segmentCount = 0;
};

constexpr std::size_t kMaxTdasResponseBytes = 128 * 1024;

const char* tdasDestinationLabelTc(TdasDestination destination);
uint16_t tdasFallbackMinutes(TdasDestination destination);

// Builds the fixed, credential-free Transport Department TDAS request for the
// user's commute. The start is TM750; both destinations are on Hong Kong
// Island and the Western Harbour Crossing is forced.
std::string tdasRequestBody(TdasDestination destination);

bool parseTdasRouteResponse(const std::string& json,
                            TdasRouteResult& result,
                            std::string& error);

JourneyEstimate makeTdasJourneyEstimate(TdasDestination destination,
                                        const TdasRouteResult& result,
                                        const TdasCalibration& calibration,
                                        int64_t updatedAtEpoch);

JourneyEstimate makeTdasFallbackEstimate(TdasDestination destination,
                                         const TdasCalibration& calibration,
                                         int64_t attemptedAtEpoch);

// Makes the two displayed estimates obey the shared-corridor relationship.
// A direct estimate anchors the other destination when one request fails; an
// implausible direct pair is normalized against Shun Tak. If neither request
// succeeds, the caller's Shun Tak fallback anchors a consistent fallback pair.
void reconcileCommuteJourneyEstimates(JourneyEstimate& shunTak,
                                      bool shunTakDirect,
                                      JourneyEstimate& gordonRoad,
                                      bool gordonRoadDirect,
                                      uint16_t gordonRoadExtraMinutes,
                                      uint16_t gordonBusExtraMinutes,
                                      uint16_t shunTakBaseBusMinutes,
                                      uint16_t shunTakFreeFlowTdasMinutes,
                                      uint8_t trafficDelayScalePercent,
                                      int64_t attemptedAtEpoch);

}  // namespace transitink
