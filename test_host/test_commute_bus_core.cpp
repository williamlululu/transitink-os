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
    assert(bus106.count == 3);
    assert(bus106.epochs[0] == now + 10 * 60);
    assert(bus106.epochs[1] == now + 10 * 60 + 45);
    assert(bus106.epochs[2] == now + 20 * 60);
    assert(bus106.operators[0] == static_cast<uint8_t>(BusOperator::Citybus));
    assert(bus106.operators[1] == static_cast<uint8_t>(BusOperator::Kmb));

    // Equal ETAs and equal provider-local ordinals are still distinct reports.
    auto equalTimeRecords = records;
    equalTimeRecords[1].eventEpoch = equalTimeRecords[0].eventEpoch;
    applyCommuteEtaRecords(dashboard, CommuteEtaKind::Bus106, equalTimeRecords,
                           now, true, true);
    assert(bus106.count == 3);
    assert(bus106.epochs[0] == bus106.epochs[1]);
    assert(bus106.operators[0] != bus106.operators[1]);

    // Six useful candidates survive a joint-route merge; rows 4-6 are not
    // discarded merely because earlier departures cannot be reached on foot.
    std::vector<BusEtaRecord> six;
    for (uint8_t i = 1; i <= 6; ++i) {
        auto row = records[i % 2];
        row.etaSequence = i;
        row.eventEpoch = now + i * 60;
        six.push_back(row);
    }
    applyCommuteEtaRecords(dashboard, CommuteEtaKind::Bus106, six, now, true, true);
    assert(bus106.count == 6);
    assert(bus106.epochs[5] == now + 6 * 60);
    for (uint8_t i = 7; i <= 16; ++i) {
        auto row = records[i % 2];
        row.etaSequence = i;
        row.eventEpoch = now + i * 60;
        six.push_back(row);
    }
    applyCommuteEtaRecords(dashboard, CommuteEtaKind::Bus106, six, now, true, true);
    assert(bus106.count == 12);
    assert(bus106.acceptedRowCount == 16);
    assert(bus106.epochs[11] == now + 12 * 60);
    auto wrongStop = records[0];
    wrongStop.stopId = "SYNTHETIC_WRONG_STOP";
    assert(!commuteEtaRecordMatches(CommuteEtaKind::Bus106, wrongStop));
    auto cancelled = records[0];
    cancelled.cancelled = true;
    assert(!commuteEtaRecordMatches(CommuteEtaKind::Bus106, cancelled));
    assert(!commuteEtaRecordMatches(CommuteEtaKind::Bus8P, records[0]));
    assert(!commuteEtaRecordMatches(CommuteEtaKind::Bus106, records[3]));
    assert(!commuteEtaRecordMatches(CommuteEtaKind::Bus106, records[4]));
    assert(!commuteEtaRecordMatches(CommuteEtaKind::Bus106, records[5]));

    // T10: fresh KMB response never renews CTB's older row. Per-row evidence
    // survives sorting, a failed source, and expiry of another provider's row.
    auto ctb = records[0];
    auto kmb = records[1];
    ctb.dataAtEpoch = now - 240;
    ctb.generatedAtEpoch = now - 10;
    ctb.requestedAtEpoch = now;
    kmb.dataAtEpoch = now - 15;
    kmb.generatedAtEpoch = now;
    kmb.requestedAtEpoch = now;
    CommuteEtaEvidence fullEvidence;
    fullEvidence.sourceStatusKnown = true;
    fullEvidence.sourcesUpdatedMask = 3;
    fullEvidence.sourcesExpected = 2;
    fullEvidence.sourcesSucceeded = 2;
    fullEvidence.requestedAtEpochs = {now, now};
    fullEvidence.generatedAtEpochs = {now - 10, now};
    fullEvidence.dataAtEpochs = {now - 240, now - 15};
    applyCommuteEtaRecords(dashboard, CommuteEtaKind::Bus106, {ctb, kmb}, now,
                           true, true, {}, fullEvidence);
    assert(bus106.evidenceEpochs[0] == now - 240);
    assert(bus106.evidenceEpochs[1] == now - 15);
    assert(bus106.rowGeneratedAtEpochs[0] == now - 10);
    assert(bus106.rowFetchedAtEpochs[0] == now);
    auto partialEvidence = fullEvidence;
    partialEvidence.sourcesUpdatedMask = 2;
    partialEvidence.sourcesFailedMask = 1;
    partialEvidence.sourcesSucceeded = 1;
    kmb.dataAtEpoch = now + 45;
    kmb.requestedAtEpoch = now + 60;
    partialEvidence.dataAtEpochs[1] = now + 45;
    partialEvidence.requestedAtEpochs[1] = now + 60;
    applyCommuteEtaRecords(dashboard, CommuteEtaKind::Bus106, {kmb}, now + 60,
                           true, false, "CTB unavailable", partialEvidence);
    assert(bus106.count == 2);
    assert(bus106.epochs[0] == ctb.eventEpoch);
    assert(bus106.evidenceEpochs[0] == now - 240);
    assert(bus106.rowFetchedAtEpochs[0] == now);
    assert(bus106.rowStale[0]);
    assert(!bus106.rowStale[1]);
    assert(bus106.evidenceEpochs[1] == now + 45);
    assert(bus106.sources[0].fetchedAtEpoch == now);
    assert(bus106.sources[0].dataAtEpoch == now - 240);
    assert(!bus106.sources[0].succeeded);
    assert(bus106.sources[1].fetchedAtEpoch == now + 60);
    assert(bus106.sources[1].dataAtEpoch == now + 45);
    assert(bus106.sources[1].succeeded);

    // T12: repeated upstream data retains its original evidence time and ETA.
    applyCommuteEtaRecords(dashboard, CommuteEtaKind::Bus106, {kmb}, now + 90,
                           true, false, "CTB unavailable", partialEvidence);
    assert(bus106.evidenceEpochs[1] == now + 45);
    assert(bus106.epochs[1] == kmb.eventEpoch);
    assert(!bus106.sourceChanged);
    applyCommuteEtaRecords(dashboard, CommuteEtaKind::Bus106, {}, ctb.eventEpoch,
                           false, false, "offline");
    assert(bus106.count == 1);
    assert(bus106.epochs[0] == kmb.eventEpoch);
    assert(bus106.operators[0] == static_cast<uint8_t>(BusOperator::Kmb));
    assert(bus106.evidenceEpochs[0] == now + 45);
    assert(bus106.etaSequences[0] == kmb.etaSequence);

    // A response with no source clock cannot manufacture fresh evidence by
    // repeating the same report in a new HTTP response.
    dashboard = {};
    applyCommuteEtaRecords(dashboard, CommuteEtaKind::Bus106, {records[0]}, now,
                           true, true);
    applyCommuteEtaRecords(dashboard, CommuteEtaKind::Bus106, {records[0]}, now + 60,
                           true, true);
    assert(bus106.evidenceEpochs[0] == now);
    assert(bus106.rowFetchedAtEpochs[0] == now + 60);
    assert(!bus106.sourceChanged);

    // Successful empty source data removes old rows rather than keeping ghosts.
    applyCommuteEtaRecords(dashboard, CommuteEtaKind::Bus106, {}, now + 90,
                           true, true, {}, fullEvidence);
    assert(bus106.count == 0);
    assert(bus106.sources[0].dataAtEpoch == now - 240);
    assert(bus106.sourceGeneratedAtEpoch == now);
    assert(bus106.sourceDataAtEpoch == now - 15);

    // Cadence skip keeps each empty response's original upstream markers too.
    auto skippedEvidence = fullEvidence;
    skippedEvidence.sourcesUpdatedMask = 0;
    applyCommuteEtaRecords(dashboard, CommuteEtaKind::Bus106, {}, now + 120,
                           true, true, {}, skippedEvidence);
    assert(bus106.sources[0].fetchedAtEpoch == now);
    assert(bus106.sources[0].dataAtEpoch == now - 240);
    assert(bus106.sourceDataAtEpoch == now - 15);
    assert(!bus106.sourceChanged);

    CommuteProviderPollState poll;
    assert(commuteProviderPollDue(poll, 0));
    recordCommuteProviderPoll(poll, 0, true);
    assert(!commuteProviderPollDue(poll, 30000));
    assert(commuteProviderPollDue(poll, 60000));
    recordCommuteProviderPoll(poll, 60000, false);
    assert(poll.nextRequestMs == 120000);
    recordCommuteProviderPoll(poll, 120000, false);
    assert(poll.nextRequestMs == 240000);
    recordCommuteProviderPoll(poll, 240000, true);
    assert(poll.nextRequestMs == 300000);
    assert(poll.consecutiveFailures == 0);
    recordCommuteProviderPoll(poll, UINT32_MAX - 10000, true);
    assert(!commuteProviderPollDue(poll, 1000));
    assert(commuteProviderPollDue(poll, 50000));

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
    assert(dashboard.routeA.primary.transferMarginMinutes == 12);
    assert(dashboard.routeA.primary.arrivalEpoch == now + 64 * 60);
    assert(!dashboard.routeA.primary.connectionAtRisk);
    assert(dashboard.routeA.nominalFastest.arrivalEpoch == now + 54 * 60);
    assert(dashboard.routeA.nominalFastest.connectionAtRisk);
    assert(dashboard.routeA.fallback.arrivalEpoch == now + 64 * 60);
    assert(dashboard.routeB.primary.arrivalEpoch == now + 55 * 60);
    assert(dashboard.recommendation == CommuteChoice::RouteB);
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
    assert(dashboard.recommendation == CommuteChoice::RouteB);

    for (auto& stream : dashboard.etas) stream.fetchedAtEpoch = now - 4 * 60;
    planCommuteDashboard(dashboard, now, now + 65 * 60, true);
    assert(dashboard.recommendation == CommuteChoice::None);
    assert(dashboard.dataQuality == CommuteDataQuality::Unavailable);
    return 0;
}
