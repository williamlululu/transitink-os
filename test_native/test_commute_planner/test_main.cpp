#include "core/CommuteBusCore.h"
#include "core/CommuteDashboardCore.h"
#include "core/CommuteSessionCore.h"

#include <unity.h>

#include <cstdint>
#include <vector>

namespace {

constexpr int64_t kNow = 2'000'000'000;

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
             std::initializer_list<int> minutes) {
    auto& stream = transitink::commuteEta(dashboard, kind);
    stream = {};
    stream.fetchedAtEpoch = kNow;
    for (const int minute : minutes) {
        stream.epochs[stream.count++] = kNow + minute * 60;
    }
}

void test_official_route_scope_and_sequences() {
    using namespace transitink;
    const auto& configs = commuteBusFeedConfigs();
    TEST_ASSERT_EQUAL_UINT32(3, configs.size());
    TEST_ASSERT_EQUAL_STRING("106", configs[0].citybus.routeId.c_str());
    TEST_ASSERT_EQUAL_STRING("001533", configs[0].citybus.stopId.c_str());
    TEST_ASSERT_EQUAL_UINT16(14, configs[0].citybusBoardingSequence);
    TEST_ASSERT_EQUAL_UINT16(44, configs[0].citybusDestinationSequence);
    TEST_ASSERT_EQUAL_STRING("001224", configs[0].destinationStopId);
    TEST_ASSERT_EQUAL_STRING("997CCAB996935BD7", configs[0].kmbStopId);
    TEST_ASSERT_EQUAL_STRING("O", configs[0].kmbDirectionId);
    TEST_ASSERT_EQUAL_STRING("1", configs[0].kmbServiceType);
    TEST_ASSERT_EQUAL_UINT16(14, configs[0].kmbBoardingSequence);
    TEST_ASSERT_EQUAL_STRING("8P", configs[1].citybus.routeId.c_str());
    TEST_ASSERT_EQUAL_STRING("001213", configs[1].citybus.stopId.c_str());
    TEST_ASSERT_EQUAL_UINT16(5, configs[1].citybusBoardingSequence);
    TEST_ASSERT_EQUAL_UINT16(9, configs[1].citybusDestinationSequence);
    TEST_ASSERT_NULL(configs[1].kmbStopId);
    TEST_ASSERT_EQUAL_STRING("118", configs[2].citybus.routeId.c_str());
    TEST_ASSERT_EQUAL_STRING("001476", configs[2].citybus.stopId.c_str());
    TEST_ASSERT_EQUAL_UINT16(16, configs[2].citybusBoardingSequence);
    TEST_ASSERT_EQUAL_UINT16(27, configs[2].citybusDestinationSequence);
    TEST_ASSERT_EQUAL_STRING("C564EDC91AFD7D04", configs[2].kmbStopId);
    TEST_ASSERT_EQUAL_STRING("1", configs[2].kmbServiceType);
}

void test_joint_operator_matching_and_deduplication() {
    using namespace transitink;
    CommuteDashboardSnapshot dashboard;
    const std::vector<BusEtaRecord> records = {
        eta(BusOperator::Citybus, "106", "I", "", "001533", 14, 1,
            kNow + 10 * 60),
        eta(BusOperator::Kmb, "106", "O", "1", "997CCAB996935BD7", 14,
            1, kNow + 10 * 60 + 45),
        eta(BusOperator::Kmb, "106", "O", "1", "997CCAB996935BD7", 14,
            2, kNow + 20 * 60),
        eta(BusOperator::Kmb, "106", "O", "2", "997CCAB996935BD7", 14,
            3, kNow + 30 * 60),
        eta(BusOperator::Citybus, "106", "I", "", "001533", 15, 4,
            kNow + 40 * 60),
        eta(BusOperator::Kmb, "118", "O", "1", "C564EDC91AFD7D04", 16,
            1, kNow + 7 * 60),
    };
    applyCommuteEtaRecords(dashboard, CommuteEtaKind::Bus106, records, kNow,
                           true, true);
    const auto& stream = commuteEta(dashboard, CommuteEtaKind::Bus106);
    TEST_ASSERT_EQUAL_UINT32(2, stream.count);
    TEST_ASSERT_EQUAL_INT64(kNow + 10 * 60 + 45, stream.epochs[0]);
    TEST_ASSERT_EQUAL_INT64(kNow + 20 * 60, stream.epochs[1]);
    TEST_ASSERT_FALSE(stream.stale);
    TEST_ASSERT_FALSE(stream.partial);
}

void test_partial_stale_and_unavailable_data_states() {
    using namespace transitink;
    CommuteDashboardSnapshot dashboard;
    const std::vector<BusEtaRecord> records = {
        eta(BusOperator::Citybus, "118", "I", "", "001476", 16, 1,
            kNow + 20 * 60),
    };
    applyCommuteEtaRecords(dashboard, CommuteEtaKind::Bus118, records, kNow,
                           true, false, "KMB unavailable");
    auto& stream = commuteEta(dashboard, CommuteEtaKind::Bus118);
    TEST_ASSERT_EQUAL_UINT32(1, stream.count);
    TEST_ASSERT_TRUE(stream.stale);
    TEST_ASSERT_TRUE(stream.partial);

    applyCommuteEtaRecords(dashboard, CommuteEtaKind::Bus118, {}, kNow + 60,
                           false, false, "offline");
    TEST_ASSERT_EQUAL_UINT32(1, stream.count);
    TEST_ASSERT_TRUE(stream.stale);
    TEST_ASSERT_FALSE(stream.partial);
    TEST_ASSERT_EQUAL_STRING("offline", stream.error.c_str());
}

void test_planner_fallback_transfer_and_recovery_mode() {
    using namespace transitink;
    CommuteDashboardSnapshot dashboard;
    setEtas(dashboard, CommuteEtaKind::Bus106, {8, 18, 28});
    setEtas(dashboard, CommuteEtaKind::Bus8P, {27, 37, 47});
    setEtas(dashboard, CommuteEtaKind::Bus118, {14, 20, 30});

    const int64_t target = kNow + 65 * 60;
    planCommuteDashboard(dashboard, kNow, target, true);
    TEST_ASSERT_EQUAL_INT64(kNow + 54 * 60,
                            dashboard.routeA.primary.arrivalEpoch);
    TEST_ASSERT_EQUAL_INT16(2,
                            dashboard.routeA.primary.transferMarginMinutes);
    TEST_ASSERT_EQUAL_INT64(kNow + 64 * 60,
                            dashboard.routeA.fallback.arrivalEpoch);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommuteChoice::RouteA),
                          static_cast<int>(dashboard.recommendation));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommuteAssessment::Safe),
                          static_cast<int>(dashboard.routeA.assessment));

    planCommuteDashboard(dashboard, kNow, target, true,
                         CommutePlannerSettings{},
                         CommuteSessionMode::AutomaticRecovery);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommuteAssessment::Tight),
                          static_cast<int>(dashboard.routeA.assessment));
}

void test_weekend_manual_session_and_stale_cutoff() {
    using namespace transitink;
    CommuteDashboardSnapshot dashboard;
    setEtas(dashboard, CommuteEtaKind::Bus106, {8, 18});
    setEtas(dashboard, CommuteEtaKind::Bus8P, {27, 37});
    setEtas(dashboard, CommuteEtaKind::Bus118, {20, 30});
    planCommuteDashboard(dashboard, kNow, kNow + 65 * 60, false,
                         CommutePlannerSettings{}, CommuteSessionMode::Standby);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommuteChoice::None),
                          static_cast<int>(dashboard.recommendation));
    planCommuteDashboard(dashboard, kNow, kNow + 65 * 60, false,
                         CommutePlannerSettings{}, CommuteSessionMode::Manual);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommuteChoice::RouteA),
                          static_cast<int>(dashboard.recommendation));

    for (auto& stream : dashboard.etas) stream.fetchedAtEpoch = kNow - 4 * 60;
    planCommuteDashboard(dashboard, kNow, kNow + 65 * 60, true);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommuteChoice::None),
                          static_cast<int>(dashboard.recommendation));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommuteDataQuality::Unavailable),
                          static_cast<int>(dashboard.dataQuality));
}

void test_schedule_boundaries_manual_deadline_and_weather_cache() {
    using namespace transitink;
    const auto settings = kDefaultCommuteSessionSettings;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(CommuteSessionMode::Standby),
        static_cast<int>(automaticCommuteSessionMode(settings, 1, 5 * 3600 + 59 * 60)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(CommuteSessionMode::AutomaticNormal),
        static_cast<int>(automaticCommuteSessionMode(settings, 1, 6 * 3600)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(CommuteSessionMode::AutomaticRapid),
        static_cast<int>(automaticCommuteSessionMode(settings, 1, 6 * 3600 + 40 * 60)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(CommuteSessionMode::AutomaticRecovery),
        static_cast<int>(automaticCommuteSessionMode(settings, 1, 7 * 3600 + 10 * 60)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(CommuteSessionMode::Standby),
        static_cast<int>(automaticCommuteSessionMode(settings, 1, 7 * 3600 + 30 * 60)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(CommuteSessionMode::Standby),
        static_cast<int>(automaticCommuteSessionMode(settings, 6, 6 * 3600 + 40 * 60)));
    TEST_ASSERT_EQUAL_UINT32(120, commutePollIntervalSeconds(
                                      CommuteSessionMode::AutomaticNormal, settings));
    TEST_ASSERT_EQUAL_UINT32(30, commutePollIntervalSeconds(
                                     CommuteSessionMode::AutomaticRapid, settings));
    TEST_ASSERT_EQUAL_UINT32(120, commutePollIntervalSeconds(
                                      CommuteSessionMode::AutomaticRecovery, settings));
    TEST_ASSERT_EQUAL_UINT32(30, commutePollIntervalSeconds(
                                     CommuteSessionMode::Manual, settings));

    const uint32_t deadline = manualCommuteSessionDeadline(1000, settings);
    TEST_ASSERT_TRUE(manualCommuteSessionActive(true, 1000, deadline));
    TEST_ASSERT_TRUE(manualCommuteSessionActive(true, deadline - 1, deadline));
    TEST_ASSERT_FALSE(manualCommuteSessionActive(true, deadline, deadline));
    TEST_ASSERT_FALSE(manualCommuteSessionActive(false, 1000, deadline));
    TEST_ASSERT_FALSE(cachedDataRefreshDue(true, 1000, 1899, 900));
    TEST_ASSERT_TRUE(cachedDataRefreshDue(true, 1000, 1900, 900));
    TEST_ASSERT_TRUE(cachedDataRefreshDue(false, 0, 1900, 900));
}

}  // namespace

void setUp() {}
void tearDown() {}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_official_route_scope_and_sequences);
    RUN_TEST(test_joint_operator_matching_and_deduplication);
    RUN_TEST(test_partial_stale_and_unavailable_data_states);
    RUN_TEST(test_planner_fallback_transfer_and_recovery_mode);
    RUN_TEST(test_weekend_manual_session_and_stale_cutoff);
    RUN_TEST(test_schedule_boundaries_manual_deadline_and_weather_cache);
    return UNITY_END();
}
