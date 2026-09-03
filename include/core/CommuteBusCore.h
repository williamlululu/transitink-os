#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/CommuteDashboardCore.h"
#include "core/WidgetCore.h"

namespace transitink {

struct CommuteBusFeedConfig {
    BusWidgetConfig citybus;
    uint16_t citybusBoardingSequence = 0;
    uint16_t citybusDestinationSequence = 0;
    const char* destinationStopId = nullptr;
    const char* kmbStopId = nullptr;
    const char* kmbDirectionId = nullptr;
    const char* kmbServiceType = nullptr;
    uint16_t kmbBoardingSequence = 0;
};

const std::array<CommuteBusFeedConfig, kCommuteEtaStreamCount>&
commuteBusFeedConfigs();

void initializeCommuteBusRows(CommuteDashboardSnapshot& dashboard);

void applyCommuteEtaRecords(CommuteDashboardSnapshot& dashboard,
                            CommuteEtaKind kind,
                            const std::vector<BusEtaRecord>& records,
                            int64_t nowEpoch,
                            bool anySourceSucceeded,
                            bool allSourcesSucceeded,
                            const std::string& error = {});

void markAllCommuteEtasFailed(CommuteDashboardSnapshot& dashboard,
                              int64_t nowEpoch,
                              const std::string& error);

}  // namespace transitink
