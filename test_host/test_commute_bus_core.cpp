#include "core/CommuteBusCore.h"
#include "core/CommuteDashboardCore.h"

#include <cassert>
#include <cstdint>
#include <vector>

int main() {
    using namespace transitink;
    const auto& configs = commuteBusFeedConfigs();
    assert(configs.size() == 3);
    assert(configs[0].citybus.routeId == "106");
    assert(configs[0].citybus.stopId == "001533");
    assert(configs[1].citybus.routeId == "8P");
    assert(configs[2].citybus.routeId == "118");

    constexpr int64_t now = 2'000'000'000;
    CommuteDashboardSnapshot dashboard;
    std::vector<BusEtaRecord> records = {
        {BusOperator::Citybus, "106", "I", "", now + 8 * 60,
         "小西灣", "", false},
        {BusOperator::Kmb, "106", "O", "3", now + 18 * 60,
         "小西灣", "", false},
    };
    applyCommuteEtaRecords(dashboard, CommuteEtaKind::Bus106, records, now,
                           true, true);
    assert(commuteEta(dashboard, CommuteEtaKind::Bus106).count == 2);

    auto& bus8p = commuteEta(dashboard, CommuteEtaKind::Bus8P);
    bus8p.fetchedAtEpoch = now;
    bus8p.epochs = {now + 27 * 60, now + 37 * 60, 0};
    bus8p.count = 2;
    auto& bus118 = commuteEta(dashboard, CommuteEtaKind::Bus118);
    bus118.fetchedAtEpoch = now;
    bus118.epochs = {now + 20 * 60, now + 30 * 60, 0};
    bus118.count = 2;

    planCommuteDashboard(dashboard, now, now + 65 * 60, true);
    assert(dashboard.routeA.primary.valid);
    assert(dashboard.routeA.primary.transferMarginMinutes == 2);
    assert(dashboard.routeA.primary.arrivalEpoch == now + 54 * 60);
    assert(dashboard.routeA.fallback.arrivalEpoch == now + 64 * 60);
    assert(dashboard.routeB.primary.arrivalEpoch == now + 55 * 60);
    assert(dashboard.recommendation == CommuteChoice::RouteA);
    return 0;
}
