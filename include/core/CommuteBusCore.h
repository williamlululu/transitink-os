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
    // Bit 0 = Citybus, bit 1 = KMB. Updated means a successful new request;
    // an unpolled/failed source keeps its own prior rows and timestamps.
    uint8_t sourcesUpdatedMask = 0;
    uint8_t sourcesFailedMask = 0;
    bool sourceStatusKnown = false;
    std::array<int64_t, 2> requestedAtEpochs{};
    std::array<int64_t, 2> generatedAtEpochs{};
    std::array<int64_t, 2> dataAtEpochs{};
};

struct CommuteProviderPollState {
    uint32_t nextRequestMs = 0;
    uint8_t consecutiveFailures = 0;
    bool attempted = false;
    bool succeeded = false;
};

bool commuteProviderPollDue(const CommuteProviderPollState& state,
                            uint32_t nowMs);
void recordCommuteProviderPoll(CommuteProviderPollState& state,
                               uint32_t nowMs, bool succeeded);

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
