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

void accumulateEvidence(transitink::CommuteEtaEvidence& evidence,
                        const transitink::BusEtaResponseInfo& info,
                        bool succeeded) {
    ++evidence.sourcesExpected;
    if (!succeeded) return;
    ++evidence.sourcesSucceeded;
    evidence.generatedAtEpoch =
        std::max(evidence.generatedAtEpoch, info.generatedAtEpoch);
    evidence.dataAtEpoch = std::max(evidence.dataAtEpoch, info.dataAtEpoch);
    evidence.rawRowCount += info.rawRowCount;
    evidence.parsedRowCount += info.parsedRowCount;
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
    transitink::initializeCommuteBusRows(dashboard);
    bool allSucceeded = true;
    String combinedError;
    const auto& configs = transitink::commuteBusFeedConfigs();

    for (std::size_t index = 0; index < configs.size(); ++index) {
        const auto kind = static_cast<transitink::CommuteEtaKind>(index);
        const auto& config = configs[index];
        std::vector<transitink::BusEtaRecord> combined;
        std::vector<transitink::BusEtaRecord> sourceRecords;
        transitink::CommuteEtaEvidence evidence;
        transitink::BusEtaResponseInfo sourceInfo;
        String sourceError;
        String routeError;

        const bool citybusOk = citybus_.fetchEtaRecords(
            config.citybus, sourceRecords, sourceError, &sourceInfo);
        const std::size_t citybusAccepted =
            acceptedForStream(kind, sourceRecords, nowEpoch);
        accumulateEvidence(evidence, sourceInfo, citybusOk);
        logProviderResult("CTB", config, config.citybus.stopId.c_str(),
                          config.citybus.directionId.c_str(), sourceInfo,
                          citybusOk, citybusAccepted, sourceError);
        if (citybusOk) {
            combined.insert(combined.end(), sourceRecords.begin(),
                            sourceRecords.end());
        } else {
            allSucceeded = false;
            const String source =
                String("CTB ") + config.citybus.routeId.c_str();
            appendError(combinedError, source, sourceError);
            appendError(routeError, source, sourceError);
        }

        const bool hasKmbSource = config.kmbStopId != nullptr;
        bool kmbOk = true;
        if (hasKmbSource) {
            sourceRecords.clear();
            sourceInfo = {};
            sourceError = "";
            kmbOk = kmb_.fetchStopEtaRecords(config.kmbStopId, sourceRecords,
                                             sourceError, &sourceInfo);
            const std::size_t kmbAccepted =
                acceptedForStream(kind, sourceRecords, nowEpoch);
            accumulateEvidence(evidence, sourceInfo, kmbOk);
            logProviderResult("KMB", config, config.kmbStopId,
                              config.kmbDirectionId, sourceInfo, kmbOk,
                              kmbAccepted, sourceError);
            if (kmbOk) {
                combined.insert(combined.end(), sourceRecords.begin(),
                                sourceRecords.end());
            } else {
                allSucceeded = false;
                const String source =
                    String("KMB ") + config.citybus.routeId.c_str();
                appendError(combinedError, source, sourceError);
                appendError(routeError, source, sourceError);
            }
        }

        const bool anySourceSucceeded =
            citybusOk || (hasKmbSource && kmbOk);
        const bool allSourcesSucceeded =
            citybusOk && (!hasKmbSource || kmbOk);
        transitink::applyCommuteEtaRecords(
            dashboard, kind, combined, nowEpoch, anySourceSucceeded,
            allSourcesSucceeded,
            allSourcesSucceeded ? std::string() : routeError.c_str(),
            evidence);
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
