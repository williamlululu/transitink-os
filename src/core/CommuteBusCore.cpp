#include "core/CommuteBusCore.h"

#include <algorithm>
#include <utility>

namespace transitink {
namespace {

BusWidgetConfig citybusConfig(const char* route,
                              const char* stop,
                              const char* stopLabel) {
    BusWidgetConfig config;
    config.operatorId = BusOperator::Citybus;
    config.routeId = route;
    config.directionId = "I";
    config.serviceType = "1";
    config.stopId = stop;
    config.routeLabelTc = route;
    config.stopLabelTc = stopLabel;
    config.destinationLabelTc = "小西灣（藍灣半島）";
    return config;
}

// Stop identifiers and directions are from the official Citybus/KMB route-stop
// feeds. 106 and 118 are joint routes, so both operators must be queried.
const std::array<CommuteBusFeedConfig, kCommuteEtaStreamCount> kConfigs = {{
    {citybusConfig("106", "001533", "紅磡街市"),
     "997CCAB996935BD7", "O"},
    {citybusConfig("8P", "001213", "維多利亞公園"), nullptr, nullptr},
    {citybusConfig("118", "001476", "紅磡海底隧道收費廣場"),
     "C564EDC91AFD7D04", "O"},
}};

bool recordMatches(const BusEtaRecord& record,
                   const CommuteBusFeedConfig& config) {
    if (record.cancelled || record.routeId != config.citybus.routeId) {
        return false;
    }
    if (record.operatorId == BusOperator::Citybus) {
        return record.directionId == config.citybus.directionId;
    }
    if (record.operatorId == BusOperator::Kmb && config.kmbStopId != nullptr) {
        return record.directionId == config.kmbDirectionId;
    }
    return false;
}

void removeExpired(CommuteEtaSnapshot& snapshot, int64_t nowEpoch) {
    std::size_t writeIndex = 0;
    for (std::size_t readIndex = 0; readIndex < snapshot.count; ++readIndex) {
        if (snapshot.epochs[readIndex] <= nowEpoch) continue;
        snapshot.epochs[writeIndex++] = snapshot.epochs[readIndex];
    }
    for (std::size_t index = writeIndex; index < snapshot.epochs.size();
         ++index) {
        snapshot.epochs[index] = 0;
    }
    snapshot.count = writeIndex;
}

}  // namespace

const std::array<CommuteBusFeedConfig, kCommuteEtaStreamCount>&
commuteBusFeedConfigs() {
    return kConfigs;
}

void initializeCommuteBusRows(CommuteDashboardSnapshot& dashboard) {
    for (auto& eta : dashboard.etas) {
        if (eta.count > eta.epochs.size()) eta.count = eta.epochs.size();
    }
}

void applyCommuteEtaRecords(CommuteDashboardSnapshot& dashboard,
                            CommuteEtaKind kind,
                            const std::vector<BusEtaRecord>& records,
                            int64_t nowEpoch,
                            bool anySourceSucceeded,
                            bool allSourcesSucceeded,
                            const std::string& error) {
    CommuteEtaSnapshot& previous = commuteEta(dashboard, kind);
    if (!anySourceSucceeded) {
        removeExpired(previous, nowEpoch);
        previous.stale = true;
        previous.partial = false;
        previous.error = error;
        return;
    }

    const auto& config = kConfigs[commuteEtaIndex(kind)];
    std::vector<int64_t> epochs;
    epochs.reserve(records.size());
    for (const auto& record : records) {
        if (!recordMatches(record, config) || record.eventEpoch <= nowEpoch) {
            continue;
        }
        // An ETA more than six hours away is not useful for this morning
        // decision and is more likely malformed or for a later service day.
        if (record.eventEpoch > nowEpoch + 6 * 60 * 60) continue;
        epochs.push_back(record.eventEpoch);
    }
    std::sort(epochs.begin(), epochs.end());
    epochs.erase(std::unique(epochs.begin(), epochs.end()), epochs.end());

    CommuteEtaSnapshot next;
    next.count = std::min(epochs.size(), next.epochs.size());
    for (std::size_t index = 0; index < next.count; ++index) {
        next.epochs[index] = epochs[index];
    }
    next.fetchedAtEpoch = nowEpoch;
    next.stale = !allSourcesSucceeded;
    next.partial = !allSourcesSucceeded;
    next.error = error;
    previous = std::move(next);
    dashboard.updatedAtEpoch = nowEpoch;
}

void markAllCommuteEtasFailed(CommuteDashboardSnapshot& dashboard,
                              int64_t nowEpoch,
                              const std::string& error) {
    for (std::size_t index = 0; index < kCommuteEtaStreamCount; ++index) {
        applyCommuteEtaRecords(
            dashboard, static_cast<CommuteEtaKind>(index), {}, nowEpoch, false,
            false, error);
    }
}

}  // namespace transitink
