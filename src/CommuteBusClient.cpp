#include "CommuteBusClient.h"

#include <algorithm>
#include <vector>

#include "core/CommuteBusCore.h"

namespace {

void appendError(String& combined, const String& source, const String& detail) {
    if (!combined.isEmpty()) combined += "; ";
    combined += source;
    combined += ": ";
    combined += detail.isEmpty() ? String("request failed") : detail;
}

std::size_t acceptedForStream(
    transitink::CommuteEtaKind kind,
    const std::vector<transitink::BusEtaRecord>& records,
    int64_t nowEpoch) {
    return static_cast<std::size_t>(std::count_if(
        records.begin(), records.end(), [&](const auto& record) {
            return transitink::commuteEtaRecordMatches(kind, record) &&
                   record.eventEpoch > nowEpoch &&
                   record.eventEpoch <= nowEpoch + 6 * 60 * 60;
        }));
}

void logProviderResult(
    const char* provider,
    const transitink::CommuteBusFeedConfig& config,
    const char* stopId,
    const char* providerDirection,
    const transitink::BusEtaResponseInfo& info,
    bool succeeded,
    std::size_t accepted,
    const String& error) {
    Serial.printf(
        "Commute provider=%s route=%s http=%d generated=%lld data=%lld "
        "stop=%s provider_dir=%s canonical=%s raw=%u parsed=%u accepted=%u "
        "result=%s\n",
        provider, config.citybus.routeId.c_str(),
        static_cast<int>(info.httpStatus),
        static_cast<long long>(info.generatedAtEpoch),
        static_cast<long long>(info.dataAtEpoch), stopId, providerDirection,
        transitink::commutePhysicalDirectionId(config.physicalDirection),
        static_cast<unsigned int>(info.rawRowCount),
        static_cast<unsigned int>(info.parsedRowCount),
        static_cast<unsigned int>(accepted), succeeded ? "ok" : "failed");
    if (!succeeded && !error.isEmpty()) {
        Serial.print("Commute provider detail: ");
        Serial.println(error);
    }
}

}  // namespace

bool CommuteBusClient::refresh(
    transitink::CommuteDashboardSnapshot& dashboard,
    int64_t nowEpoch,
    String& error) {
    const uint32_t refreshStartedMs = millis();
    transitink::initializeCommuteBusRows(dashboard);
    bool allSucceeded = true;
    String combinedError;
    const auto& configs = transitink::commuteBusFeedConfigs();

    for (std::size_t index = 0; index < configs.size(); ++index) {
        if (index == transitink::commuteEtaIndex(transitink::CommuteEtaKind::Bus8PHingFat)) {
            bool needed=false;
            const auto& context=dashboard.context;
            if (context.alternateCheckedMask && transitink::contextFresh(context.generatedEpoch,nowEpoch) && !context.lastFetchFailed) {
                for (std::size_t i=0;i<std::min(context.count,context.events.size());++i) {
                    const auto& e=context.events[i];
                    if (e.verified && e.towardYueWan && !e.athleticMeet && e.severity>=transitink::ContextSeverity::Degraded &&
                        e.startEpoch<=nowEpoch && nowEpoch<e.endEpoch &&
                        ((e.route==0 && e.scope==transitink::ContextScope::Transfer) ||
                         (e.route==1 && e.scope==transitink::ContextScope::Boarding))) needed=true;
                }
            }
            if (!needed) continue; // Normal operation retains its original provider polling workload.
        }
        const auto kind = static_cast<transitink::CommuteEtaKind>(index);
        const auto& config = configs[index];
        std::vector<transitink::BusEtaRecord> combined;
        transitink::CommuteEtaEvidence evidence;
        evidence.sourceStatusKnown = true;
        String routeError;
        const std::size_t sourceCount = config.kmbStopId != nullptr ? 2 : 1;
        bool anySourceSucceeded = false;
        bool allSourcesSucceeded = true;
        for (std::size_t source = 0; source < sourceCount; ++source) {
            auto& poll = providerPolls_[index][source];
            ++evidence.sourcesExpected;
            const uint8_t mask = static_cast<uint8_t>(1U << source);
            const char* provider = source == 0 ? "CTB" : "KMB";
            const uint32_t requestedAtMs = millis();
            if (transitink::commuteProviderPollDue(poll, requestedAtMs)) {
                std::vector<transitink::BusEtaRecord> sourceRecords;
                transitink::BusEtaResponseInfo sourceInfo;
                String sourceError;
                const bool ok = source == 0 ?
                    citybus_.fetchEtaRecords(config.citybus, sourceRecords,
                                             sourceError, &sourceInfo) :
                    kmb_.fetchStopEtaRecords(config.kmbStopId, sourceRecords,
                                             sourceError, &sourceInfo);
                transitink::recordCommuteProviderPoll(poll, millis(), ok);
                const std::size_t accepted = acceptedForStream(kind, sourceRecords, nowEpoch);
                logProviderResult(provider, config,
                    source == 0 ? config.citybus.stopId.c_str() : config.kmbStopId,
                    source == 0 ? config.citybus.directionId.c_str() : config.kmbDirectionId,
                    sourceInfo, ok, accepted, sourceError);
                if (ok) {
                    evidence.sourcesUpdatedMask |= mask;
                    evidence.requestedAtEpochs[source] = nowEpoch +
                        static_cast<uint32_t>(requestedAtMs - refreshStartedMs) / 1000;
                    evidence.generatedAtEpochs[source] = sourceInfo.generatedAtEpoch;
                    evidence.rawRowCount += sourceInfo.rawRowCount;
                    evidence.parsedRowCount += sourceInfo.parsedRowCount;
                    for (auto& record : sourceRecords) {
                        // Keep the request instant distinct from the provider's
                        // generation/data timestamps. No raw ETA is shifted.
                        record.requestedAtEpoch = nowEpoch +
                            static_cast<uint32_t>(requestedAtMs - refreshStartedMs) / 1000;
                        if (transitink::commuteEtaRecordMatches(kind, record)) {
                            evidence.dataAtEpochs[source] = std::max(
                                evidence.dataAtEpochs[source], record.dataAtEpoch);
                            Serial.printf("Commute row provider=%s route=%s stop=%s dir=%s "
                                          "service=%s stop_seq=%u eta_seq=%u eta=%lld "
                                          "generated=%lld data=%lld request=%lld scheduled=%s\n",
                                provider, record.routeId.c_str(), record.stopId.c_str(),
                                record.directionId.c_str(), record.serviceType.c_str(),
                                static_cast<unsigned int>(record.stopSequence),
                                static_cast<unsigned int>(record.etaSequence),
                                static_cast<long long>(record.eventEpoch),
                                static_cast<long long>(record.generatedAtEpoch),
                                static_cast<long long>(record.dataAtEpoch),
                                static_cast<long long>(record.requestedAtEpoch),
                                record.scheduled ? "yes" : "no");
                        }
                    }
                    combined.insert(combined.end(), sourceRecords.begin(), sourceRecords.end());
                } else {
                    const String label = String(provider) + " " + config.citybus.routeId.c_str();
                    appendError(combinedError, label, sourceError);
                    appendError(routeError, label, sourceError);
                }
            }
            if (poll.succeeded) {
                anySourceSucceeded = true;
                ++evidence.sourcesSucceeded;
            } else {
                evidence.sourcesFailedMask |= mask;
                allSourcesSucceeded = false;
                allSucceeded = false;
                if (routeError.isEmpty()) {
                    appendError(routeError, provider, "retry backoff");
                }
            }
        }
        transitink::applyCommuteEtaRecords(
            dashboard, kind, combined, nowEpoch, anySourceSucceeded,
            allSourcesSucceeded,
            allSourcesSucceeded ? std::string() : routeError.c_str(), evidence);
        const auto& snapshot = transitink::commuteEta(dashboard, kind);
        Serial.printf("Commute route=%s departures=",
                      config.citybus.routeId.c_str());
        if (snapshot.count == 0) {
            Serial.print("none");
        } else {
            for (std::size_t etaIndex = 0; etaIndex < snapshot.count;
                 ++etaIndex) {
                if (etaIndex > 0) Serial.print(',');
                Serial.print(static_cast<long long>(snapshot.epochs[etaIndex]));
            }
        }
        Serial.printf(
            " horizon=%lld source_changed=%s\n",
            static_cast<long long>(snapshot.count == 0
                                       ? 0
                                       : snapshot.epochs[snapshot.count - 1]),
            snapshot.sourceChanged ? "yes" : "no");
    }

    error = combinedError;
    return allSucceeded;
}
