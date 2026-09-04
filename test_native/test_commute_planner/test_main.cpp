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
    stream.sourceGeneratedAtEpoch = kNow;
    stream.sourceDataAtEpoch = kNow;
    stream.sourcesExpected = 1;
    stream.sourcesSucceeded = 1;
    stream.sourceChanged = true;
    for (const int minute : minutes) {
        stream.epochs[stream.count++] = kNow + minute * 60;
    }
    stream.rawRowCount = stream.count;
    stream.parsedRowCount = stream.count;
    stream.acceptedRowCount = stream.count;
}

void setEtasAt(transitink::CommuteDashboardSnapshot& dashboard,
               transitink::CommuteEtaKind kind,
               int64_t sourceEpoch,
               std::initializer_list<int64_t> epochs) {
    auto& stream = transitink::commuteEta(dashboard, kind);
    stream = {};
    stream.fetchedAtEpoch = sourceEpoch;
    stream.sourceGeneratedAtEpoch = sourceEpoch;
    stream.sourceDataAtEpoch = sourceEpoch;
    stream.sourcesExpected = 1;
    stream.sourcesSucceeded = 1;
    stream.sourceChanged = true;
    for (const int64_t epoch : epochs) {
        stream.epochs[stream.count++] = epoch;
    }
    stream.rawRowCount = stream.count;
    stream.parsedRowCount = stream.count;
    stream.acceptedRowCount = stream.count;
}

void setSuccessfulEmpty(transitink::CommuteDashboardSnapshot& dashboard,
                        transitink::CommuteEtaKind kind,
                        int64_t sourceEpoch = kNow) {
    auto& stream = transitink::commuteEta(dashboard, kind);
    stream = {};
    stream.fetchedAtEpoch = sourceEpoch;
    stream.sourceGeneratedAtEpoch = sourceEpoch;
    stream.sourceDataAtEpoch = sourceEpoch;
    stream.sourcesExpected = 1;
    stream.sourcesSucceeded = 1;
    stream.sourceChanged = true;
}

void setFailed(transitink::CommuteDashboardSnapshot& dashboard,
               transitink::CommuteEtaKind kind) {
    auto& stream = transitink::commuteEta(dashboard, kind);
    stream = {};
    stream.sourcesExpected = 1;
    stream.sourcesSucceeded = 0;
    stream.stale = true;
    stream.error = "provider failed";
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

    for (auto& stream : dashboard.etas) {
        stream.fetchedAtEpoch = kNow - 4 * 60;
        stream.sourceGeneratedAtEpoch = kNow - 4 * 60;
        stream.sourceDataAtEpoch = kNow - 4 * 60;
    }
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

void test_0600_boundary_uses_conservative_provisional_transfer() {
    using namespace transitink;
    CommuteDashboardSnapshot dashboard;
    setEtas(dashboard, CommuteEtaKind::Bus106, {10, 20});
    setEtas(dashboard, CommuteEtaKind::Bus8P, {8, 14, 20});

    planCommuteDashboard(dashboard, kNow, kNow + 85 * 60, true,
                         CommutePlannerSettings{},
                         CommuteSessionMode::AutomaticNormal, 6 * 60);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(CommuteRouteAState::ProvisionalTransfer),
        static_cast<int>(dashboard.routeAStatus.state));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(CommuteRouteAReason::TimetableHeadway),
        static_cast<int>(dashboard.routeAStatus.reason));
    TEST_ASSERT_TRUE(dashboard.routeA.primary.firstLegValid);
    TEST_ASSERT_TRUE(dashboard.routeA.primary.valid);
    TEST_ASSERT_TRUE(dashboard.routeA.primary.provisional);
    TEST_ASSERT_FALSE(dashboard.routeA.primary.connectionConfirmed);
    TEST_ASSERT_EQUAL_INT64(kNow + 4 * 60,
                            dashboard.routeA.primary.leaveHomeEpoch);
    TEST_ASSERT_EQUAL_INT64(kNow + 27 * 60,
                            dashboard.routeA.primary.transferArrivalEpoch);
    TEST_ASSERT_EQUAL_INT64(kNow + 29 * 60,
                            dashboard.routeA.primary.transferReadyEpoch);
    TEST_ASSERT_EQUAL_INT64(kNow + 32 * 60,
                            dashboard.routeA.primary.connectionEpoch);
    TEST_ASSERT_TRUE(dashboard.routeAStatus.timetableUsed);
}

void test_visible_8p_horizon_becomes_pending_not_no_service() {
    using namespace transitink;
    CommuteDashboardSnapshot dashboard;
    setEtas(dashboard, CommuteEtaKind::Bus106, {10, 20});
    setEtas(dashboard, CommuteEtaKind::Bus8P, {8, 14, 20});
    CommutePlannerSettings settings;
    settings.route8pWeekdayHeadwayEndMinutes = 0;

    planCommuteDashboard(dashboard, kNow, kNow + 85 * 60, true, settings,
                         CommuteSessionMode::AutomaticNormal, 6 * 60);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(CommuteRouteAState::TransferPending),
        static_cast<int>(dashboard.routeAStatus.state));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(CommuteRouteAReason::TransferBeyondEtaHorizon),
        static_cast<int>(dashboard.routeAStatus.reason));
    TEST_ASSERT_NOT_EQUAL(static_cast<int>(CommuteRouteAState::NoService),
                          static_cast<int>(dashboard.routeAStatus.state));
    TEST_ASSERT_EQUAL_INT64(kNow + 20 * 60,
                            dashboard.routeAStatus.live8pHorizonEpoch);
}

void test_pending_state_retains_next_106_leave_and_fallback() {
    using namespace transitink;
    CommuteDashboardSnapshot dashboard;
    setEtas(dashboard, CommuteEtaKind::Bus106, {10, 20});
    setEtas(dashboard, CommuteEtaKind::Bus8P, {8, 14, 20});

    planCommuteDashboard(dashboard, kNow, kNow + 85 * 60, false,
                         CommutePlannerSettings{}, CommuteSessionMode::Manual,
                         6 * 60);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(CommuteRouteAState::TransferPending),
        static_cast<int>(dashboard.routeAStatus.state));
    TEST_ASSERT_TRUE(dashboard.routeA.primary.firstLegValid);
    TEST_ASSERT_FALSE(dashboard.routeA.primary.valid);
    TEST_ASSERT_EQUAL_INT64(kNow + 10 * 60,
                            dashboard.routeA.primary.firstBusEpoch);
    TEST_ASSERT_EQUAL_INT64(kNow + 4 * 60,
                            dashboard.routeA.primary.leaveHomeEpoch);
    TEST_ASSERT_TRUE(dashboard.routeA.fallback.firstLegValid);
    TEST_ASSERT_EQUAL_INT64(kNow + 20 * 60,
                            dashboard.routeA.fallback.firstBusEpoch);
}

void test_live_8p_replaces_provisional_transfer() {
    using namespace transitink;
    CommuteDashboardSnapshot dashboard;
    setEtas(dashboard, CommuteEtaKind::Bus106, {10, 20});
    setEtas(dashboard, CommuteEtaKind::Bus8P, {8, 14, 20});
    planCommuteDashboard(dashboard, kNow, kNow + 85 * 60, true,
                         CommutePlannerSettings{},
                         CommuteSessionMode::AutomaticNormal, 6 * 60);
    TEST_ASSERT_TRUE(dashboard.routeA.primary.provisional);

    setEtas(dashboard, CommuteEtaKind::Bus8P, {20, 32, 44});
    planCommuteDashboard(dashboard, kNow, kNow + 85 * 60, true,
                         CommutePlannerSettings{},
                         CommuteSessionMode::AutomaticNormal, 6 * 60);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(CommuteRouteAState::ConfirmedPair),
        static_cast<int>(dashboard.routeAStatus.state));
    TEST_ASSERT_FALSE(dashboard.routeA.primary.provisional);
    TEST_ASSERT_TRUE(dashboard.routeA.primary.connectionConfirmed);
    TEST_ASSERT_EQUAL_INT64(kNow + 32 * 60,
                            dashboard.routeA.primary.connectionEpoch);
}

void test_wrong_victoria_park_stop_is_rejected() {
    using namespace transitink;
    CommuteDashboardSnapshot dashboard;
    const std::vector<BusEtaRecord> records = {
        eta(BusOperator::Citybus, "8P", "I", "", "002561", 5, 1,
            kNow + 10 * 60),
        eta(BusOperator::Citybus, "8P", "I", "", "001213", 5, 2,
            kNow + 20 * 60),
    };
    applyCommuteEtaRecords(dashboard, CommuteEtaKind::Bus8P, records, kNow,
                           true, true);
    const auto& stream = commuteEta(dashboard, CommuteEtaKind::Bus8P);
    TEST_ASSERT_EQUAL_UINT32(1, stream.count);
    TEST_ASSERT_EQUAL_INT64(kNow + 20 * 60, stream.epochs[0]);
}

void test_joint_operator_118_is_merged_and_deduplicated() {
    using namespace transitink;
    CommuteDashboardSnapshot dashboard;
    const std::vector<BusEtaRecord> records = {
        eta(BusOperator::Citybus, "118", "I", "", "001476", 16, 1,
            kNow + 10 * 60),
        eta(BusOperator::Kmb, "118", "O", "1", "C564EDC91AFD7D04", 16,
            1, kNow + 10 * 60 + 45),
        eta(BusOperator::Kmb, "118", "O", "1", "C564EDC91AFD7D04", 16,
            2, kNow + 20 * 60),
    };
    applyCommuteEtaRecords(dashboard, CommuteEtaKind::Bus118, records, kNow,
                           true, true);
    const auto& stream = commuteEta(dashboard, CommuteEtaKind::Bus118);
    TEST_ASSERT_EQUAL_UINT32(2, stream.count);
    TEST_ASSERT_EQUAL_INT64(kNow + 10 * 60 + 45, stream.epochs[0]);
    TEST_ASSERT_EQUAL_INT64(kNow + 20 * 60, stream.epochs[1]);
}

void test_provider_direction_codes_are_normalized_by_source_config() {
    using namespace transitink;
    const auto& config = commuteBusFeedConfigs()[0];
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(CommutePhysicalDirection::TowardYueWan),
        static_cast<int>(config.physicalDirection));
    TEST_ASSERT_EQUAL_STRING("I", config.citybus.directionId.c_str());
    TEST_ASSERT_EQUAL_STRING("O", config.kmbDirectionId);

    CommuteDashboardSnapshot dashboard;
    const std::vector<BusEtaRecord> records = {
        eta(BusOperator::Citybus, "106", "O", "", "001533", 14, 1,
            kNow + 8 * 60),
        eta(BusOperator::Kmb, "106", "I", "1", "997CCAB996935BD7", 14,
            1, kNow + 9 * 60),
        eta(BusOperator::Citybus, "106", "I", "", "001533", 14, 2,
            kNow + 10 * 60),
        eta(BusOperator::Kmb, "106", "O", "1", "997CCAB996935BD7", 14,
            2, kNow + 10 * 60 + 30),
    };
    applyCommuteEtaRecords(dashboard, CommuteEtaKind::Bus106, records, kNow,
                           true, true);
    const auto& stream = commuteEta(dashboard, CommuteEtaKind::Bus106);
    TEST_ASSERT_EQUAL_UINT32(1, stream.count);
    TEST_ASSERT_EQUAL_INT64(kNow + 10 * 60 + 30, stream.epochs[0]);
}

void test_empty_citybus_kmb_period_rows_are_not_departures() {
    using namespace transitink;
    CommuteDashboardSnapshot dashboard;
    BusEtaRecord empty = eta(BusOperator::Citybus, "118", "I", "",
                             "001476", 16, 1, 0);
    empty.remarkTc = "九巴時段";
    const std::vector<BusEtaRecord> records = {
        empty,
        eta(BusOperator::Kmb, "118", "O", "1", "C564EDC91AFD7D04", 16,
            1, kNow + 10 * 60),
    };
    applyCommuteEtaRecords(dashboard, CommuteEtaKind::Bus118, records, kNow,
                           true, true);
    const auto& stream = commuteEta(dashboard, CommuteEtaKind::Bus118);
    TEST_ASSERT_EQUAL_UINT32(1, stream.count);
    TEST_ASSERT_EQUAL_INT64(kNow + 10 * 60, stream.epochs[0]);
}

void test_same_provider_marker_is_not_new_transport_evidence() {
    using namespace transitink;
    CommuteDashboardSnapshot dashboard;
    const std::vector<BusEtaRecord> records = {
        eta(BusOperator::Citybus, "8P", "I", "", "001213", 5, 1,
            kNow + 10 * 60),
    };
    CommuteEtaEvidence evidence;
    evidence.generatedAtEpoch = kNow - 10;
    evidence.dataAtEpoch = kNow - 20;
    evidence.rawRowCount = 3;
    evidence.parsedRowCount = 3;
    evidence.sourcesExpected = 1;
    evidence.sourcesSucceeded = 1;
    applyCommuteEtaRecords(dashboard, CommuteEtaKind::Bus8P, records, kNow,
                           true, true, {}, evidence);
    TEST_ASSERT_TRUE(
        commuteEta(dashboard, CommuteEtaKind::Bus8P).sourceChanged);

    applyCommuteEtaRecords(dashboard, CommuteEtaKind::Bus8P, records,
                           kNow + 60, true, true, {}, evidence);
    const auto& stream = commuteEta(dashboard, CommuteEtaKind::Bus8P);
    TEST_ASSERT_FALSE(stream.sourceChanged);
    TEST_ASSERT_EQUAL_INT64(kNow + 60, stream.fetchedAtEpoch);
    TEST_ASSERT_EQUAL_INT64(kNow - 20, stream.sourceDataAtEpoch);
    TEST_ASSERT_EQUAL_UINT32(3, stream.rawRowCount);
    TEST_ASSERT_EQUAL_UINT32(1, stream.acceptedRowCount);
}

void test_same_snapshot_crosses_latest_safe_106_without_false_no_service() {
    using namespace transitink;
    const int64_t firstCalculation = kNow + 4 * 60;
    const int64_t secondCalculation = firstCalculation + 30;
    CommuteDashboardSnapshot dashboard;
    setEtasAt(dashboard, CommuteEtaKind::Bus106, firstCalculation,
              {kNow + 10 * 60, kNow + 20 * 60});
    setEtasAt(dashboard, CommuteEtaKind::Bus8P, firstCalculation,
              {kNow + 8 * 60, kNow + 10 * 60, kNow + 12 * 60});
    CommutePlannerSettings settings;
    settings.route8pWeekdayHeadwayEndMinutes = 0;

    planCommuteDashboard(dashboard, firstCalculation, kNow + 85 * 60, true,
                         settings, CommuteSessionMode::AutomaticNormal,
                         6 * 60 + 4);
    TEST_ASSERT_EQUAL_INT64(kNow + 10 * 60,
                            dashboard.routeA.primary.firstBusEpoch);
    TEST_ASSERT_EQUAL_INT64(firstCalculation,
                            dashboard.routeA.primary.leaveHomeEpoch);

    planCommuteDashboard(dashboard, secondCalculation, kNow + 85 * 60, true,
                         settings, CommuteSessionMode::AutomaticNormal,
                         6 * 60 + 4);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(CommuteRouteAState::TransferPending),
        static_cast<int>(dashboard.routeAStatus.state));
    TEST_ASSERT_EQUAL_INT64(kNow + 20 * 60,
                            dashboard.routeA.primary.firstBusEpoch);
    TEST_ASSERT_EQUAL_INT64(kNow + 14 * 60,
                            dashboard.routeA.primary.leaveHomeEpoch);

    setEtasAt(dashboard, CommuteEtaKind::Bus8P, secondCalculation,
              {kNow + 44 * 60, kNow + 50 * 60});
    planCommuteDashboard(dashboard, secondCalculation, kNow + 85 * 60, true,
                         settings, CommuteSessionMode::AutomaticNormal,
                         6 * 60 + 4);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(CommuteRouteAState::ConfirmedPair),
        static_cast<int>(dashboard.routeAStatus.state));
    TEST_ASSERT_EQUAL_INT64(kNow + 44 * 60,
                            dashboard.routeA.primary.connectionEpoch);
}

void test_genuine_service_not_started_state() {
    using namespace transitink;
    CommuteDashboardSnapshot dashboard;
    setEtas(dashboard, CommuteEtaKind::Bus106, {6});
    setSuccessfulEmpty(dashboard, CommuteEtaKind::Bus8P);
    planCommuteDashboard(dashboard, kNow, kNow + 120 * 60, true,
                         CommutePlannerSettings{},
                         CommuteSessionMode::AutomaticNormal, 5 * 60);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommuteRouteAState::NoService),
                          static_cast<int>(dashboard.routeAStatus.state));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(CommuteRouteAReason::ServiceNotStarted),
        static_cast<int>(dashboard.routeAStatus.reason));
}

void test_genuine_service_ended_state() {
    using namespace transitink;
    CommuteDashboardSnapshot dashboard;
    setEtas(dashboard, CommuteEtaKind::Bus106, {100});
    setSuccessfulEmpty(dashboard, CommuteEtaKind::Bus8P);
    planCommuteDashboard(dashboard, kNow, kNow + 180 * 60, true,
                         CommutePlannerSettings{}, CommuteSessionMode::Manual,
                         23 * 60 + 50);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommuteRouteAState::NoService),
                          static_cast<int>(dashboard.routeAStatus.state));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommuteRouteAReason::ServiceEnded),
                          static_cast<int>(dashboard.routeAStatus.reason));
}

void test_api_failure_and_stale_states_remain_distinct_from_no_service() {
    using namespace transitink;
    CommuteDashboardSnapshot dashboard;
    setEtas(dashboard, CommuteEtaKind::Bus106, {10});
    planCommuteDashboard(dashboard, kNow, kNow + 85 * 60, true,
                         CommutePlannerSettings{},
                         CommuteSessionMode::AutomaticNormal, 6 * 60);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(CommuteRouteAState::DataUnavailable),
        static_cast<int>(dashboard.routeAStatus.state));
    TEST_ASSERT_NOT_EQUAL(static_cast<int>(CommuteRouteAState::NoService),
                          static_cast<int>(dashboard.routeAStatus.state));

    setFailed(dashboard, CommuteEtaKind::Bus8P);
    planCommuteDashboard(dashboard, kNow, kNow + 85 * 60, true,
                         CommutePlannerSettings{},
                         CommuteSessionMode::AutomaticNormal, 6 * 60);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommuteRouteAState::Stale),
                          static_cast<int>(dashboard.routeAStatus.state));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(CommuteRouteAReason::TransferDataStale),
        static_cast<int>(dashboard.routeAStatus.reason));
}

void test_current_cached_pair_after_failure_is_stale_not_confirmed() {
    using namespace transitink;
    CommuteDashboardSnapshot dashboard;
    setEtas(dashboard, CommuteEtaKind::Bus106, {10});
    setEtas(dashboard, CommuteEtaKind::Bus8P, {32});
    auto& firstLeg = commuteEta(dashboard, CommuteEtaKind::Bus106);
    firstLeg.sourcesSucceeded = 0;
    firstLeg.stale = true;

    planCommuteDashboard(dashboard, kNow, kNow + 85 * 60, true,
                         CommutePlannerSettings{},
                         CommuteSessionMode::AutomaticNormal, 6 * 60);
    TEST_ASSERT_TRUE(dashboard.routeA.primary.valid);
    TEST_ASSERT_TRUE(dashboard.routeA.stale);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommuteRouteAState::Stale),
                          static_cast<int>(dashboard.routeAStatus.state));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(CommuteRouteAReason::FirstLegStale),
        static_cast<int>(dashboard.routeAStatus.reason));
}

void test_route_b_remains_functional_when_route_a_is_unavailable() {
    using namespace transitink;
    CommuteDashboardSnapshot dashboard;
    setEtas(dashboard, CommuteEtaKind::Bus118, {20, 30});
    planCommuteDashboard(dashboard, kNow, kNow + 70 * 60, true,
                         CommutePlannerSettings{},
                         CommuteSessionMode::AutomaticNormal, 6 * 60);
    TEST_ASSERT_TRUE(dashboard.routeB.primary.valid);
    TEST_ASSERT_EQUAL_INT64(kNow + 55 * 60,
                            dashboard.routeB.primary.arrivalEpoch);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommuteChoice::RouteB),
                          static_cast<int>(dashboard.recommendation));
}

void test_confirmed_route_b_outranks_provisional_route_a() {
    using namespace transitink;
    CommuteDashboardSnapshot dashboard;
    setEtas(dashboard, CommuteEtaKind::Bus106, {10, 20});
    setEtas(dashboard, CommuteEtaKind::Bus8P, {8, 14, 20});
    setEtas(dashboard, CommuteEtaKind::Bus118, {25});
    planCommuteDashboard(dashboard, kNow, kNow + 85 * 60, true,
                         CommutePlannerSettings{},
                         CommuteSessionMode::AutomaticNormal, 6 * 60);
    TEST_ASSERT_TRUE(dashboard.routeA.primary.provisional);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommuteChoice::RouteB),
                          static_cast<int>(dashboard.recommendation));
}

void test_transfer_pending_outside_verified_headway_band() {
    using namespace transitink;
    CommuteDashboardSnapshot dashboard;
    setEtas(dashboard, CommuteEtaKind::Bus106, {10});
    setEtas(dashboard, CommuteEtaKind::Bus8P, {8, 14, 20});
    planCommuteDashboard(dashboard, kNow, kNow + 120 * 60, true,
                         CommutePlannerSettings{}, CommuteSessionMode::Manual,
                         10 * 60);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(CommuteRouteAState::TransferPending),
        static_cast<int>(dashboard.routeAStatus.state));
    TEST_ASSERT_FALSE(dashboard.routeAStatus.timetableUsed);
}

void test_published_service_envelope_uses_last_transfer_stop_time() {
    using namespace transitink;
    const CommutePlannerSettings settings;
    TEST_ASSERT_EQUAL_UINT16(6 * 60 + 5, settings.route8pServiceStartMinutes);
    TEST_ASSERT_EQUAL_UINT16(24 * 60 + 55,
                             settings.route8pLastPossibleTransferMinutes);
}

void test_pair_trace_records_rejections_and_live_acceptance() {
    using namespace transitink;
    CommuteDashboardSnapshot dashboard;
    setEtas(dashboard, CommuteEtaKind::Bus106, {8});
    setEtas(dashboard, CommuteEtaKind::Bus8P, {20, 26, 27});
    CommutePlannerDiagnostics diagnostics;
    planCommuteDashboard(dashboard, kNow, kNow + 85 * 60, true,
                         CommutePlannerSettings{},
                         CommuteSessionMode::AutomaticNormal, 6 * 60,
                         &diagnostics);
    TEST_ASSERT_EQUAL_UINT32(3, diagnostics.routeAPairCount);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            CommuteTransferPairOutcome::RejectedBeforeTransferReady),
        static_cast<int>(diagnostics.routeAPairs[0].outcome));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(CommuteTransferPairOutcome::AcceptedLive),
        static_cast<int>(diagnostics.routeAPairs[2].outcome));
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
    RUN_TEST(test_0600_boundary_uses_conservative_provisional_transfer);
    RUN_TEST(test_visible_8p_horizon_becomes_pending_not_no_service);
    RUN_TEST(test_pending_state_retains_next_106_leave_and_fallback);
    RUN_TEST(test_live_8p_replaces_provisional_transfer);
    RUN_TEST(test_wrong_victoria_park_stop_is_rejected);
    RUN_TEST(test_joint_operator_118_is_merged_and_deduplicated);
    RUN_TEST(test_provider_direction_codes_are_normalized_by_source_config);
    RUN_TEST(test_empty_citybus_kmb_period_rows_are_not_departures);
    RUN_TEST(test_same_provider_marker_is_not_new_transport_evidence);
    RUN_TEST(
        test_same_snapshot_crosses_latest_safe_106_without_false_no_service);
    RUN_TEST(test_genuine_service_not_started_state);
    RUN_TEST(test_genuine_service_ended_state);
    RUN_TEST(test_api_failure_and_stale_states_remain_distinct_from_no_service);
    RUN_TEST(test_current_cached_pair_after_failure_is_stale_not_confirmed);
    RUN_TEST(test_route_b_remains_functional_when_route_a_is_unavailable);
    RUN_TEST(test_confirmed_route_b_outranks_provisional_route_a);
    RUN_TEST(test_transfer_pending_outside_verified_headway_band);
    RUN_TEST(test_published_service_envelope_uses_last_transfer_stop_time);
    RUN_TEST(test_pair_trace_records_rejections_and_live_acceptance);
    return UNITY_END();
}
