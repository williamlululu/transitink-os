#include "core/CommuteBusCore.h"
#include "core/CommuteDashboardCore.h"

#include <unity.h>

#include <cstdint>
#include <vector>

namespace {

constexpr int64_t kNow = 2'000'000'000;

void setEtas(transitink::CommuteDashboardSnapshot& dashboard,
             transitink::CommuteEtaKind kind,
             std::initializer_list<int> minutes) {
    auto& eta = transitink::commuteEta(dashboard, kind);
    eta = {};
    eta.fetchedAtEpoch = kNow;
    for (int minute : minutes) {
        eta.epochs[eta.count++] = kNow + minute * 60;
    }
}

void test_official_commute_feed_configuration() {
    using namespace transitink;
    const auto& configs = commuteBusFeedConfigs();
    TEST_ASSERT_EQUAL_UINT32(3, configs.size());
    TEST_ASSERT_EQUAL_STRING("106", configs[0].citybus.routeId.c_str());
    TEST_ASSERT_EQUAL_STRING("001533", configs[0].citybus.stopId.c_str());
    TEST_ASSERT_EQUAL_STRING("997CCAB996935BD7", configs[0].kmbStopId);
    TEST_ASSERT_EQUAL_STRING("8P", configs[1].citybus.routeId.c_str());
    TEST_ASSERT_EQUAL_STRING("001213", configs[1].citybus.stopId.c_str());
    TEST_ASSERT_NULL(configs[1].kmbStopId);
    TEST_ASSERT_EQUAL_STRING("118", configs[2].citybus.routeId.c_str());
    TEST_ASSERT_EQUAL_STRING("001476", configs[2].citybus.stopId.c_str());
    TEST_ASSERT_EQUAL_STRING("C564EDC91AFD7D04", configs[2].kmbStopId);
}

void test_joint_operator_etas_are_merged_filtered_and_deduplicated() {
    using namespace transitink;
    CommuteDashboardSnapshot dashboard;
    std::vector<BusEtaRecord> records = {
        {BusOperator::Kmb, "106", "O", "3", kNow + 20 * 60,
         "小西灣", "", false},
        {BusOperator::Citybus, "106", "I", "", kNow + 10 * 60,
         "小西灣", "", false},
        {BusOperator::Citybus, "106", "I", "", kNow + 10 * 60,
         "小西灣", "", false},
        {BusOperator::Kmb, "106", "I", "1", kNow + 5 * 60,
         "黃大仙", "", false},
        {BusOperator::Kmb, "118", "O", "1", kNow + 7 * 60,
         "小西灣", "", false},
    };
    applyCommuteEtaRecords(dashboard, CommuteEtaKind::Bus106, records, kNow,
                           true, true);
    const auto& eta = commuteEta(dashboard, CommuteEtaKind::Bus106);
    TEST_ASSERT_EQUAL_UINT32(2, eta.count);
    TEST_ASSERT_EQUAL_INT64(kNow + 10 * 60, eta.epochs[0]);
    TEST_ASSERT_EQUAL_INT64(kNow + 20 * 60, eta.epochs[1]);
    TEST_ASSERT_FALSE(eta.stale);
    TEST_ASSERT_FALSE(eta.partial);
}

void test_partial_and_failed_refreshes_are_explicit() {
    using namespace transitink;
    CommuteDashboardSnapshot dashboard;
    std::vector<BusEtaRecord> records = {
        {BusOperator::Citybus, "118", "I", "", kNow + 20 * 60,
         "小西灣", "", false},
    };
    applyCommuteEtaRecords(dashboard, CommuteEtaKind::Bus118, records, kNow,
                           true, false, "KMB unavailable");
    auto& eta = commuteEta(dashboard, CommuteEtaKind::Bus118);
    TEST_ASSERT_EQUAL_UINT32(1, eta.count);
    TEST_ASSERT_TRUE(eta.stale);
    TEST_ASSERT_TRUE(eta.partial);

    applyCommuteEtaRecords(dashboard, CommuteEtaKind::Bus118, {},
                           kNow + 60, false, false, "offline");
    TEST_ASSERT_EQUAL_UINT32(1, eta.count);
    TEST_ASSERT_TRUE(eta.stale);
    TEST_ASSERT_FALSE(eta.partial);
    TEST_ASSERT_EQUAL_STRING("offline", eta.error.c_str());
}

void test_planner_builds_primary_fallback_and_transfer_margin() {
    using namespace transitink;
    CommuteDashboardSnapshot dashboard;
    setEtas(dashboard, CommuteEtaKind::Bus106, {8, 18, 28});
    setEtas(dashboard, CommuteEtaKind::Bus8P, {27, 37, 47});
    setEtas(dashboard, CommuteEtaKind::Bus118, {14, 20, 30});

    const int64_t target = kNow + 65 * 60;
    planCommuteDashboard(dashboard, kNow, target, true);

    TEST_ASSERT_TRUE(dashboard.routeA.primary.valid);
    TEST_ASSERT_EQUAL_INT64(kNow + 8 * 60,
                            dashboard.routeA.primary.firstBusEpoch);
    TEST_ASSERT_EQUAL_INT64(kNow + 27 * 60,
                            dashboard.routeA.primary.connectionEpoch);
    TEST_ASSERT_EQUAL_INT16(2,
                            dashboard.routeA.primary.transferMarginMinutes);
    TEST_ASSERT_EQUAL_INT64(kNow + 54 * 60,
                            dashboard.routeA.primary.arrivalEpoch);
    TEST_ASSERT_TRUE(dashboard.routeA.fallback.valid);
    TEST_ASSERT_EQUAL_INT64(kNow + 18 * 60,
                            dashboard.routeA.fallback.firstBusEpoch);
    TEST_ASSERT_EQUAL_INT64(kNow + 64 * 60,
                            dashboard.routeA.fallback.arrivalEpoch);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommuteAssessment::Safe),
                          static_cast<int>(dashboard.routeA.assessment));

    // The first 118 is not catchable after the 13-minute walk plus buffer.
    TEST_ASSERT_EQUAL_INT64(kNow + 20 * 60,
                            dashboard.routeB.primary.firstBusEpoch);
    TEST_ASSERT_EQUAL_INT64(kNow + 55 * 60,
                            dashboard.routeB.primary.arrivalEpoch);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommuteChoice::RouteA),
                          static_cast<int>(dashboard.recommendation));
}

void test_fresh_route_is_preferred_and_old_eta_is_not_recommended() {
    using namespace transitink;
    CommuteDashboardSnapshot dashboard;
    setEtas(dashboard, CommuteEtaKind::Bus106, {8, 18});
    setEtas(dashboard, CommuteEtaKind::Bus8P, {27, 37});
    setEtas(dashboard, CommuteEtaKind::Bus118, {20, 30});
    commuteEta(dashboard, CommuteEtaKind::Bus106).stale = true;

    planCommuteDashboard(dashboard, kNow, kNow + 65 * 60, true);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommuteChoice::RouteB),
                          static_cast<int>(dashboard.recommendation));

    for (auto& eta : dashboard.etas) {
        eta.fetchedAtEpoch = kNow - 4 * 60;
    }
    planCommuteDashboard(dashboard, kNow, kNow + 65 * 60, true);
    TEST_ASSERT_FALSE(dashboard.routeA.primary.valid);
    TEST_ASSERT_FALSE(dashboard.routeB.primary.valid);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommuteChoice::None),
                          static_cast<int>(dashboard.recommendation));
}

void test_weekend_does_not_issue_a_commute_recommendation() {
    using namespace transitink;
    CommuteDashboardSnapshot dashboard;
    setEtas(dashboard, CommuteEtaKind::Bus118, {20, 30});
    planCommuteDashboard(dashboard, kNow, kNow + 65 * 60, false);
    TEST_ASSERT_TRUE(dashboard.routeB.primary.valid);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommuteChoice::None),
                          static_cast<int>(dashboard.recommendation));
}

void test_weekday_wake_skips_saturday_and_sunday() {
    bus_eta::SleepSettings settings;
    settings.scheduledWakeEnabled = true;
    settings.scheduledWakeStartMinutes = 6 * 60;
    settings.scheduledWakeEndMinutes = 7 * 60 + 30;

    TEST_ASSERT_EQUAL_UINT32(
        60 * 60,
        bus_eta::secondsUntilWeekdayScheduledWakeStart(
            settings, 5 * 60 * 60, 1));
    TEST_ASSERT_EQUAL_UINT32(
        71 * 60 * 60,
        bus_eta::secondsUntilWeekdayScheduledWakeStart(
            settings, 7 * 60 * 60, 5));
    TEST_ASSERT_EQUAL_UINT32(
        42 * 60 * 60,
        bus_eta::secondsUntilWeekdayScheduledWakeStart(
            settings, 12 * 60 * 60, 6));
}

}  // namespace

void setUp() {}
void tearDown() {}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_official_commute_feed_configuration);
    RUN_TEST(test_joint_operator_etas_are_merged_filtered_and_deduplicated);
    RUN_TEST(test_partial_and_failed_refreshes_are_explicit);
    RUN_TEST(test_planner_builds_primary_fallback_and_transfer_margin);
    RUN_TEST(test_fresh_route_is_preferred_and_old_eta_is_not_recommended);
    RUN_TEST(test_weekend_does_not_issue_a_commute_recommendation);
    RUN_TEST(test_weekday_wake_skips_saturday_and_sunday);
    return UNITY_END();
}
