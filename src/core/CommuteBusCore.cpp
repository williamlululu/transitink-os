#include "core/CommuteBusCore.h"

#include <algorithm>
#include <cstdint>
#include <utility>

#include "ProductConfig.h"

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
    {citybusConfig("106", "001533", "紅磡街市"), 14, 44, "001224",
     "997CCAB996935BD7", "O", "1", 14,
     CommutePhysicalDirection::TowardYueWan},
    {citybusConfig("8P", "001213", "維多利亞公園"), 5, 9, "001224",
     nullptr, nullptr, nullptr, 0,
     CommutePhysicalDirection::TowardYueWan},
    {citybusConfig("118", "001476", "紅磡海底隧道收費廣場"), 16, 27,
     "001224", "C564EDC91AFD7D04", "O", "1", 16,
     CommutePhysicalDirection::TowardYueWan},
    {citybusConfig("8P", "001251", "興發街"), 6, 9, "001224",
     nullptr, nullptr, nullptr, 0, CommutePhysicalDirection::TowardYueWan},
}};

bool recordMatches(const BusEtaRecord& record,
                   const CommuteBusFeedConfig& config) {
    if (record.cancelled || record.routeId != config.citybus.routeId) {
        return false;
    }
    if (record.operatorId == BusOperator::Citybus) {
        return record.directionId == config.citybus.directionId &&
               // The Citybus ETA schema does not expose service_type. The
               // route-stop sequence below binds the row to service variant 1.
               (record.serviceType.empty() ||
                record.serviceType == config.citybus.serviceType) &&
               record.stopId == config.citybus.stopId &&
               record.stopSequence == config.citybusBoardingSequence;
    }
    if (record.operatorId == BusOperator::Kmb && config.kmbStopId != nullptr) {
        return record.directionId == config.kmbDirectionId &&
               record.serviceType == config.kmbServiceType &&
               record.stopId == config.kmbStopId &&
               record.stopSequence == config.kmbBoardingSequence;
    }
    return false;
}

struct MatchedEta {
    int64_t epoch = 0;
    int64_t evidenceEpoch = 0;
    int64_t fetchedAtEpoch = 0;
    int64_t generatedAtEpoch = 0;
    int64_t dataAtEpoch = 0;
    uint8_t operatorId = 0;
    uint8_t etaSequence = 0;
    bool scheduled = false;
    bool stale = false;
};

uint8_t sourceMask(uint8_t operatorId) {
    return operatorId == static_cast<uint8_t>(BusOperator::Citybus) ? 1 : 2;
}

MatchedEta readRow(const CommuteEtaSnapshot& snapshot, std::size_t index) {
    return {snapshot.epochs[index], snapshot.evidenceEpochs[index],
            snapshot.rowFetchedAtEpochs[index],
            snapshot.rowGeneratedAtEpochs[index], snapshot.rowDataAtEpochs[index],
            snapshot.operators[index], snapshot.etaSequences[index],
            snapshot.scheduled[index], snapshot.rowStale[index]};
}

void writeRow(CommuteEtaSnapshot& snapshot, std::size_t index,
               const MatchedEta& row) {
    snapshot.epochs[index] = row.epoch;
    snapshot.evidenceEpochs[index] = row.evidenceEpoch;
    snapshot.rowFetchedAtEpochs[index] = row.fetchedAtEpoch;
    snapshot.rowGeneratedAtEpochs[index] = row.generatedAtEpoch;
    snapshot.rowDataAtEpochs[index] = row.dataAtEpoch;
    snapshot.operators[index] = row.operatorId;
    snapshot.etaSequences[index] = row.etaSequence;
    snapshot.scheduled[index] = row.scheduled;
    snapshot.rowStale[index] = row.stale;
}

void removeExpired(CommuteEtaSnapshot& snapshot, int64_t nowEpoch) {
    std::size_t writeIndex = 0;
    for (std::size_t readIndex = 0; readIndex < snapshot.count; ++readIndex) {
        if (snapshot.epochs[readIndex] <= nowEpoch) continue;
        writeRow(snapshot, writeIndex++, readRow(snapshot, readIndex));
    }
    for (std::size_t index = writeIndex; index < snapshot.epochs.size();
         ++index) {
        writeRow(snapshot, index, {});
    }
    snapshot.count = writeIndex;
}

}  // namespace

bool commuteProviderPollDue(const CommuteProviderPollState& state,
                            uint32_t nowMs) {
    return !state.attempted ||
           static_cast<int32_t>(nowMs - state.nextRequestMs) >= 0;
}

void recordCommuteProviderPoll(CommuteProviderPollState& state,
                               uint32_t nowMs, bool succeeded) {
    state.attempted = true;
    state.succeeded = succeeded;
    state.consecutiveFailures = succeeded ? 0 :
        static_cast<uint8_t>(std::min<int>(state.consecutiveFailures + 1, 5));
    const uint32_t baseMs = COMMUTE_PROVIDER_FETCH_SECONDS * 1000UL;
    const uint32_t multiplier = succeeded ? 1 :
        (1UL << (state.consecutiveFailures - 1));
    state.nextRequestMs = nowMs + std::min<uint32_t>(baseMs * multiplier, 600000);
}

const std::array<CommuteBusFeedConfig, kCommuteEtaStreamCount>&
commuteBusFeedConfigs() {
    return kConfigs;
}

void initializeCommuteBusRows(CommuteDashboardSnapshot& dashboard) {
    for (auto& eta : dashboard.etas) {
        if (eta.count > eta.epochs.size()) eta.count = eta.epochs.size();
    }
}

bool commuteEtaRecordMatches(CommuteEtaKind kind,
                             const BusEtaRecord& record) {
    const std::size_t index = commuteEtaIndex(kind);
    return index < kConfigs.size() && recordMatches(record, kConfigs[index]);
}

const char* commutePhysicalDirectionId(CommutePhysicalDirection direction) {
    switch (direction) {
        case CommutePhysicalDirection::TowardYueWan:
            return "toward_yue_wan";
    }
    return "unknown";
}

void applyCommuteEtaRecords(CommuteDashboardSnapshot& dashboard,
                            CommuteEtaKind kind,
                            const std::vector<BusEtaRecord>& records,
                            int64_t nowEpoch,
                            bool anySourceSucceeded,
                            bool allSourcesSucceeded,
                            const std::string& error,
                            const CommuteEtaEvidence& evidence) {
    CommuteEtaSnapshot& previous = commuteEta(dashboard, kind);
    initializeCommuteBusRows(dashboard);
    if (evidence.sourceStatusKnown) {
        for (std::size_t source = 0; source < previous.sources.size(); ++source) {
            const uint8_t mask = static_cast<uint8_t>(1U << source);
            auto& status = previous.sources[source];
            if ((evidence.sourcesUpdatedMask & mask) != 0) {
                status.fetchedAtEpoch = evidence.requestedAtEpochs[source] > 0 ?
                    evidence.requestedAtEpochs[source] : nowEpoch;
                status.generatedAtEpoch = evidence.generatedAtEpochs[source];
                status.dataAtEpoch = evidence.dataAtEpochs[source];
                status.succeeded = true;
            } else if ((evidence.sourcesFailedMask & mask) != 0) {
                status.succeeded = false;
            }
        }
    }
    if (!anySourceSucceeded) {
        removeExpired(previous, nowEpoch);
        previous.rawRowCount = evidence.rawRowCount;
        previous.parsedRowCount = evidence.parsedRowCount;
        previous.sourcesExpected = evidence.sourcesExpected;
        previous.sourcesSucceeded = evidence.sourcesSucceeded;
        previous.sourceChanged = false;
        previous.stale = true;
        previous.partial = false;
        previous.error = error;
        for (std::size_t index = 0; index < previous.count; ++index) {
            previous.rowStale[index] = true;
        }
        for (auto& source : previous.sources) source.succeeded = false;
        return;
    }

    const auto& config = kConfigs[commuteEtaIndex(kind)];
    std::vector<MatchedEta> matched;
    matched.reserve(records.size() + previous.count);
    uint8_t updatedMask = evidence.sourcesUpdatedMask;
    if (!evidence.sourceStatusKnown) {
        if (allSourcesSucceeded) updatedMask = 3;
        else for (const auto& record : records) {
            updatedMask |= sourceMask(static_cast<uint8_t>(record.operatorId));
        }
    }
    for (std::size_t index = 0; index < previous.count; ++index) {
        MatchedEta old = readRow(previous, index);
        const uint8_t mask = sourceMask(old.operatorId);
        if (old.epoch <= nowEpoch || (updatedMask & mask) != 0) continue;
        if (!evidence.sourceStatusKnown ||
            (evidence.sourcesFailedMask & mask) != 0) old.stale = true;
        matched.push_back(old);
    }
    for (const auto& record : records) {
        if (!recordMatches(record, config) || record.eventEpoch <= nowEpoch) {
            continue;
        }
        // An ETA more than six hours away is not useful for this morning
        // decision and is more likely malformed or for a later service day.
        if (record.eventEpoch > nowEpoch + 6 * 60 * 60) continue;
        // eta_seq is only an ordinal within this source/stop response. It is
        // never a cross-operator (or cross-stop) vehicle identifier.
        const auto duplicate = std::find_if(
            matched.begin(), matched.end(), [&](const MatchedEta& existing) {
                return existing.operatorId == static_cast<uint8_t>(record.operatorId) &&
                       existing.etaSequence == record.etaSequence &&
                       existing.epoch == record.eventEpoch;
            });
        if (duplicate != matched.end()) continue;
        MatchedEta row;
        row.epoch = record.eventEpoch;
        row.operatorId = static_cast<uint8_t>(record.operatorId);
        row.etaSequence = record.etaSequence;
        row.scheduled = record.scheduled;
        row.fetchedAtEpoch = record.requestedAtEpoch > 0 ?
            record.requestedAtEpoch : nowEpoch;
        row.generatedAtEpoch = record.generatedAtEpoch;
        row.dataAtEpoch = record.dataAtEpoch;
        // Legacy synthetic callers supply a response-level marker. Production
        // always supplies each raw row's own timestamps and explicit status.
        if (!evidence.sourceStatusKnown) {
            if (row.generatedAtEpoch == 0) row.generatedAtEpoch = evidence.generatedAtEpoch;
            if (row.dataAtEpoch == 0) row.dataAtEpoch = evidence.dataAtEpoch;
        }
        row.evidenceEpoch = row.dataAtEpoch > 0 ? row.dataAtEpoch :
            (row.generatedAtEpoch > 0 ? row.generatedAtEpoch : row.fetchedAtEpoch);
        if (row.dataAtEpoch == 0 && row.generatedAtEpoch == 0) {
            for (std::size_t index = 0; index < previous.count; ++index) {
                if (previous.operators[index] == row.operatorId &&
                    previous.etaSequences[index] == row.etaSequence &&
                    previous.epochs[index] == row.epoch) {
                    row.evidenceEpoch = previous.evidenceEpochs[index] > 0 ?
                        previous.evidenceEpochs[index] : previous.fetchedAtEpoch;
                    break;
                }
            }
        }
        matched.push_back(row);
    }
    std::stable_sort(matched.begin(), matched.end(),
                     [](const MatchedEta& left, const MatchedEta& right) {
                         return left.epoch < right.epoch;
                     });

    CommuteEtaSnapshot next;
    next.sources = previous.sources;
    next.count = std::min(matched.size(), next.epochs.size());
    for (std::size_t index = 0; index < next.count; ++index) {
        writeRow(next, index, matched[index]);
    }
    next.fetchedAtEpoch = updatedMask != 0 ? nowEpoch : previous.fetchedAtEpoch;
    for (const auto& row : matched) {
        next.sourceGeneratedAtEpoch = std::max(next.sourceGeneratedAtEpoch, row.generatedAtEpoch);
        next.sourceDataAtEpoch = std::max(next.sourceDataAtEpoch, row.dataAtEpoch);
    }
    // Empty responses still carry upstream evidence. Keep their clock markers
    // so an old empty source cannot masquerade as a newly confirmed no-service.
    if (matched.empty()) {
        for (const auto& source : next.sources) {
            next.sourceGeneratedAtEpoch = std::max(next.sourceGeneratedAtEpoch, source.generatedAtEpoch);
            next.sourceDataAtEpoch = std::max(next.sourceDataAtEpoch, source.dataAtEpoch);
        }
        if (!evidence.sourceStatusKnown) {
            next.sourceGeneratedAtEpoch = evidence.generatedAtEpoch;
            next.sourceDataAtEpoch = evidence.dataAtEpoch;
        }
    }
    next.rawRowCount = evidence.rawRowCount;
    next.parsedRowCount = evidence.parsedRowCount;
    next.acceptedRowCount = matched.size();
    next.sourcesExpected = evidence.sourcesExpected == 0 ? 1
                                                         : evidence.sourcesExpected;
    next.sourcesSucceeded = evidence.sourcesSucceeded == 0 ? 1
                                                           : evidence.sourcesSucceeded;
    next.sourceChanged = previous.count != next.count ||
                         previous.sourceGeneratedAtEpoch !=
                             next.sourceGeneratedAtEpoch ||
                         previous.sourceDataAtEpoch != next.sourceDataAtEpoch;
    for (std::size_t index = 0; index < next.count && !next.sourceChanged; ++index) {
        next.sourceChanged = previous.epochs[index] != next.epochs[index] ||
            previous.operators[index] != next.operators[index] ||
            previous.evidenceEpochs[index] != next.evidenceEpochs[index] ||
            previous.scheduled[index] != next.scheduled[index] ||
            previous.rowStale[index] != next.rowStale[index];
    }
    next.stale = !allSourcesSucceeded;
    next.partial = !allSourcesSucceeded;
    next.error = error;
    previous = std::move(next);
    dashboard.updatedAtEpoch = previous.sourceDataAtEpoch > 0
                                   ? previous.sourceDataAtEpoch
                                   : previous.fetchedAtEpoch;
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
