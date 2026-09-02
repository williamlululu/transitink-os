#include "CommuteBusClient.h"

#include <vector>

#include "core/CommuteBusCore.h"

namespace {

void appendError(String& combined, const String& source, const String& detail) {
    if (!combined.isEmpty()) combined += "; ";
    combined += source;
    combined += ": ";
    combined += detail.isEmpty() ? String("request failed") : detail;
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
        String sourceError;
        String routeError;

        const bool citybusOk = citybus_.fetchEtaRecords(
            config.citybus, sourceRecords, sourceError);
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
            sourceError = "";
            kmbOk = kmb_.fetchStopEtaRecords(config.kmbStopId, sourceRecords,
                                             sourceError);
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
            allSourcesSucceeded ? std::string() : routeError.c_str());
    }

    error = combinedError;
    return allSucceeded;
}
