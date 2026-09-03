#include "core/CommuteBusCore.h"
#include "core/CommuteDashboardCore.h"
#include "core/CommuteSessionCore.h"

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace {

transitink::BusEtaRecord eta(transitink::BusOperator operatorId,
                             const char* route,
                             const char* direction,
                             const char* serviceType,
                             const char* stopId,
                             uint16_t stopSequence,
                             uint8_t etaSequence,
                             int64_t epoch) {
    transitink::BusEtaRecord record;
    record.operatorId = operatorId;
    record.routeId = route;
    record.directionId = direction;
    record.serviceType = serviceType;
    record.stopId = stopId;
    record.stopSequence = stopSequence;
    record.etaSequence = etaSequence;
    record.eventEpoch = epoch;
    record.destinationLabelTc = "小西灣（藍灣半島）";
    return record;
}

void setEtas(transitink::CommuteDashboardSnapshot& dashboard,
             transitink::CommuteEtaKind kind,
             int64_t now,
             std::initializer_list<int> minutes) {
    auto& stream = transitink::commuteEta(dashboard, kind);
    stream = {};
    stream.fetchedAtEpoch = now;
    for (const int minute : minutes) {
        stream.epochs[stream.count++] = now + minute * 60;
    }
}

}  // namespace

int main() {
    using namespace transitink;
    const auto& configs = commuteBusFeedConfigs();
    assert(configs.size() == 3);
    assert(configs[0].citybus.routeId == "106");
    assert(configs[0].citybus.stopId == "001533");
    assert(configs[0].citybusBoardingSequence == 14);
    assert(configs[0].citybusDestinationSequence == 44);
    assert(std::string(configs[0].destinationStopId) == "001224");
    assert(std::string(configs[0].kmbStopId) == "997CCAB996935BD7");
    assert(std::string(configs[0].kmbDirectionId) == "O");
    assert(std::string(configs[0].kmbServiceType) == "1");
    assert(configs[0].kmbBoardingSequence == 14);
    assert(configs[1].citybus.routeId == "8P");
    assert(configs[1].citybus.stopId == "001213");
    assert(configs[1].citybusBoardingSequence == 5);
    assert(configs[1].citybusDestinationSequence == 9);
    assert(configs[1].kmbStopId == nullptr);
    assert(configs[2].citybus.routeId == "118");
    assert(configs[2].citybus.stopId == "001476");
    assert(configs[2].citybusBoardingSequence == 16);
    assert(configs[2].citybusDestinationSequence == 27);
    assert(std::string(configs[2].kmbStopId) == "C564EDC91AFD7D04");
    assert(std::string(configs[2].kmbServiceType) == "1");

    constexpr int64_t now = 2'000'000'000;
    CommuteDashboardSnapshot dashboard;
    const std::vector<BusEtaRecord> records = {
        eta(BusOperator::Citybus, "106", "I", "", "001533", 14, 1,
            now + 10 * 60),
        eta(BusOperator::Kmb, "106", "O", "1", "997CCAB996935BD7", 14,
            1, now + 10 * 60 + 45),
        eta(BusOperator::Kmb, "106", "O", "1", "997CCAB996935BD7", 14,
            2, now + 20 * 60),
        eta(BusOperator::Kmb, "106", "O", "2", "997CCAB996935BD7", 14,
            3, now + 30 * 60),
        eta(BusOperator::Citybus, "106", "I", "", "001533", 15, 4,
            now + 40 * 60),
        eta(BusOperator::Kmb, "106", "I", "1", "997CCAB996935BD7", 14,
            5, now + 50 * 60),
    };
    applyCommuteEtaRecords(dashboard, CommuteEtaKind::Bus106, records, now,
                           true, true);
    const auto& bus106 = commuteEta(dashboard, CommuteEtaKind::Bus106);
    assert(bus106.count == 2);
    assert(bus106.epochs[0] == now + 10 * 60 + 45);
    assert(bus106.epochs[1] == now + 20 * 60);

    const std::vector<BusEtaRecord> partial118 = {
        eta(BusOperator::Citybus, "118", "I", "", "001476", 16, 1,
            now + 20 * 60),
    };
    applyCommuteEtaRecords(dashboard, CommuteEtaKind::Bus118, partial118, now,
                           true, false, "KMB unavailable");
    auto& bus118 = commuteEta(dashboard, CommuteEtaKind::Bus118);
    assert(bus118.count == 1);
    assert(bus118.stale);
    assert(bus118.partial);

    applyCommuteEtaRecords(dashboard, CommuteEtaKind::Bus118, {}, now + 60,
                           false, false, "offline");
    assert(bus118.count == 1);
    assert(bus118.stale);
    assert(!bus118.partial);
    assert(bus118.error == "offline");

    dashboard = {};
    setEtas(dashboard, CommuteEtaKind::Bus106, now, {8, 18, 28});
    setEtas(dashboard, CommuteEtaKind::Bus8P, now, {27, 37, 47});
    setEtas(dashboard, CommuteEtaKind::Bus118, now, {14, 20, 30});
    planCommuteDashboard(dashboard, now, now + 65 * 60, true);
    assert(dashboard.routeA.primary.valid);
    assert(dashboard.routeA.primary.transferMarginMinutes == 2);
    assert(dashboard.routeA.primary.arrivalEpoch == now + 54 * 60);
    assert(dashboard.routeA.fallback.arrivalEpoch == now + 64 * 60);
    assert(dashboard.routeB.primary.arrivalEpoch == now + 55 * 60);
    assert(dashboard.recommendation == CommuteChoice::RouteA);
    assert(dashboard.dataQuality == CommuteDataQuality::Fresh);

    planCommuteDashboard(dashboard, now, now + 65 * 60, true,
                         CommutePlannerSettings{},
                         CommuteSessionMode::AutomaticRecovery);
    assert(dashboard.routeA.assessment == CommuteAssessment::Tight);

    planCommuteDashboard(dashboard, now, now + 65 * 60, false,
                         CommutePlannerSettings{}, CommuteSessionMode::Standby);
    assert(dashboard.recommendation == CommuteChoice::None);
    planCommuteDashboard(dashboard, now, now + 65 * 60, false,
                         CommutePlannerSettings{}, CommuteSessionMode::Manual);
    assert(dashboard.recommendation == CommuteChoice::RouteA);

    for (auto& stream : dashboard.etas) stream.fetchedAtEpoch = now - 4 * 60;
    planCommuteDashboard(dashboard, now, now + 65 * 60, true);
    assert(dashboard.recommendation == CommuteChoice::None);
    assert(dashboard.dataQuality == CommuteDataQuality::Unavailable);
    return 0;
}
