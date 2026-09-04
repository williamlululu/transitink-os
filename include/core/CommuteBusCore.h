#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/CommuteDashboardCore.h"
#include "core/WidgetCore.h"

namespace transitink {

enum class CommutePhysicalDirection : uint8_t {
    TowardYueWan,
};

struct CommuteBusFeedConfig {
    BusWidgetConfig citybus;
    uint16_t citybusBoardingSequence = 0;
    uint16_t citybusDestinationSequence = 0;
    const char* destinationStopId = nullptr;
    const char* kmbStopId = nullptr;
    const char* kmbDirectionId = nullptr;
    const char* kmbServiceType = nullptr;
    uint16_t kmbBoardingSequence = 0;
    CommutePhysicalDirection physicalDirection =
        CommutePhysicalDirection::TowardYueWan;
};

struct CommuteEtaEvidence {
    int64_t generatedAtEpoch = 0;
    int64_t dataAtEpoch = 0;
    std::size_t rawRowCount = 0;
    std::size_t parsedRowCount = 0;
    uint8_t sourcesExpected = 0;
    uint8_t sourcesSucceeded = 0;
};

const std::array<CommuteBusFeedConfig, kCommuteEtaStreamCount>&
commuteBusFeedConfigs();

void initializeCommuteBusRows(CommuteDashboardSnapshot& dashboard);

bool commuteEtaRecordMatches(CommuteEtaKind kind,
                             const BusEtaRecord& record);

const char* commutePhysicalDirectionId(CommutePhysicalDirection direction);

void applyCommuteEtaRecords(CommuteDashboardSnapshot& dashboard,
                            CommuteEtaKind kind,
                            const std::vector<BusEtaRecord>& records,
                            int64_t nowEpoch,
                            bool anySourceSucceeded,
                            bool allSourcesSucceeded,
                            const std::string& error = {},
                            const CommuteEtaEvidence& evidence = {});

void markAllCommuteEtasFailed(CommuteDashboardSnapshot& dashboard,
                              int64_t nowEpoch,
                              const std::string& error);

}  // namespace transitink
