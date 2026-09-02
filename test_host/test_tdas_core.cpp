#include "core/TdasCore.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>

namespace {

using transitink::JourneyEstimateSource;
using transitink::TdasDestination;
using transitink::TdasRoute3Evidence;

transitink::TdasRouteResult parse(const std::string& json) {
    transitink::TdasRouteResult result;
    std::string error = "sentinel";
    assert(transitink::parseTdasRouteResponse(json, result, error));
    assert(error.empty());
    assert(result.valid);
    assert(result.usedWesternHarbourCrossing);
    return result;
}

void testRequestBodies() {
    const std::string shunTak =
        transitink::tdasRequestBody(TdasDestination::ShunTak);
    assert(shunTak.find(
               R"("start":{"lat":22.362928,"long":114.015522,"buffer":100})") !=
           std::string::npos);
    assert(shunTak.find(
               R"("end":{"lat":22.288080073,"long":114.152236390,"buffer":150})") !=
           std::string::npos);
    assert(shunTak.find(R"("departIn":0)") != std::string::npos);
    assert(shunTak.find(R"("lang":"tc")") != std::string::npos);
    assert(shunTak.find(R"("type":"ST")") != std::string::npos);
    assert(shunTak.find(R"("tunnel":"wht")") != std::string::npos);

    const std::string gordon =
        transitink::tdasRequestBody(TdasDestination::GordonRoad);
    assert(gordon.find(
               R"("end":{"lat":22.286212030,"long":114.190372163,"buffer":150})") !=
           std::string::npos);
}

void testOfficialShapeAndDurationParsing() {
    const auto official = parse(R"({
        "jSpeed":"50 km/h","eta":"00:20","wht":true,
        "route":[{"segment":[
            {"dir":3,"rid":[3086]},
            {"dir":1,"rid":[5461,2222]}
        ]}]
    })");
    assert(official.rawMinutes == 20);
    assert(official.segmentCount == 2);
    assert(official.route3Evidence == TdasRoute3Evidence::NotExposed);

    assert(parse(R"({"eta":"01:05","wht":true})").rawMinutes == 65);
    assert(parse(R"({"eta":"00:20:01","wht":true})").rawMinutes == 21);
    assert(parse(R"({"eta":" 75 ","wht":true})").rawMinutes == 75);
    assert(parse(R"({"eta":42,"wht":true})").rawMinutes == 42);
}

void testRoute3EvidenceWhenNamesAreExposed() {
    const auto present = parse(R"({
        "eta":"00:42","wht":true,
        "route":[{"segment":[
            {"dir":3,"road_name_tc":"屯門公路"},
            {"dir":3,"routeNumber":3},
            {"dir":3,"name":"Western Harbour Crossing"}
        ]}]
    })");
    assert(present.segmentCount == 3);
    assert(present.route3Evidence == TdasRoute3Evidence::Present);

    const auto presentByText = parse(R"json({
        "eta":"00:42","wht":true,
        "route":[{"route_name":"Route 3 (Southbound)","segment":[]}]
    })json");
    assert(presentByText.route3Evidence == TdasRoute3Evidence::Present);

    const auto absent = parse(R"({
        "eta":"00:42","wht":true,
        "route":[{"segment":[{"dir":3,"route_no":"8"}]}]
    })");
    assert(absent.route3Evidence == TdasRoute3Evidence::Absent);
}

void testUnsafeResponsesFailClosed() {
    const char* invalid[] = {
        "",
        "not json",
        R"({"eta":"00:20","wht":false})",
        R"({"eta":"00:20"})",
        R"({"eta":"00:00","wht":true})",
        R"({"eta":"00:60","wht":true})",
        R"({"eta":"17:00","wht":true})",
        R"({"eta":"00:10:60","wht":true})",
        R"({"eta":"00:10:","wht":true})",
        R"({"eta":"71582789:00","wht":true})",
        R"({"eta":0,"wht":true})",
        R"({"eta":1000,"wht":true})",
    };
    for (const char* json : invalid) {
        transitink::TdasRouteResult result;
        std::string error;
        assert(!transitink::parseTdasRouteResponse(json, result, error));
        assert(!result.valid);
        assert(!error.empty());
    }
}

void testLiveEstimateCalibration() {
    transitink::TdasRouteResult result;
    result.valid = true;
    result.usedWesternHarbourCrossing = true;
    result.rawMinutes = 40;
    result.route3Evidence = TdasRoute3Evidence::NotExposed;
    const transitink::TdasCalibration calibration{110, 3};
    const auto estimate = transitink::makeTdasJourneyEstimate(
        TdasDestination::ShunTak, result, calibration, 2000000000);
    assert(estimate.valid);
    assert(!estimate.stale);
    assert(estimate.source == JourneyEstimateSource::Tdas);
    assert(estimate.destinationTc == "信德中心");
    assert(estimate.routeLabel == "屯門公路・三號幹綫・西隧");
    assert(estimate.rawMinutes == 40);
    assert(estimate.adjustedMinutes == 47);
    assert(estimate.scalePercent == 110);
    assert(estimate.offsetMinutes == 3);
    assert(estimate.updatedAtEpoch == 2000000000);
    assert(!estimate.messageTc.empty());
}

void testConservativeFallbacks() {
    const transitink::TdasCalibration neutral;
    const auto shunTak = transitink::makeTdasFallbackEstimate(
        TdasDestination::ShunTak, neutral, 2000000001);
    assert(shunTak.valid);
    assert(shunTak.stale);
    assert(shunTak.source == JourneyEstimateSource::CalibratedFallback);
    assert(shunTak.rawMinutes == 31);
    assert(shunTak.adjustedMinutes == 31);
    assert(shunTak.updatedAtEpoch == 2000000001);
    assert(!shunTak.messageTc.empty());

    const transitink::TdasCalibration tuned{90, -2};
    const auto gordon = transitink::makeTdasFallbackEstimate(
        TdasDestination::GordonRoad, tuned, 2000000002);
    assert(gordon.valid);
    assert(gordon.stale);
    assert(gordon.source == JourneyEstimateSource::CalibratedFallback);
    assert(gordon.rawMinutes == 45);
    assert(gordon.adjustedMinutes == 39);
    assert(gordon.destinationTc == "歌頓道／萬國寶通中心");
}

void testSharedCorridorCoupling() {
    const transitink::TdasCalibration neutral;

    transitink::TdasRouteResult gordonResult;
    gordonResult.valid = true;
    gordonResult.usedWesternHarbourCrossing = true;
    gordonResult.rawMinutes = 27;
    auto gordon = transitink::makeTdasJourneyEstimate(
        TdasDestination::GordonRoad, gordonResult, neutral, 2000000010);
    auto shunTak = transitink::makeTdasFallbackEstimate(
        TdasDestination::ShunTak, neutral, 2000000010);
    transitink::reconcileCommuteJourneyEstimates(
        shunTak, false, gordon, true, 5, 14, 31, 22, 100, 2000000010);
    assert(shunTak.adjustedMinutes == 31);
    assert(gordon.adjustedMinutes == 45);
    assert(shunTak.source == JourneyEstimateSource::TrafficModel);
    assert(!shunTak.stale);
    assert(gordon.source == JourneyEstimateSource::TrafficModel);

    transitink::TdasRouteResult shunTakResult;
    shunTakResult.valid = true;
    shunTakResult.usedWesternHarbourCrossing = true;
    shunTakResult.rawMinutes = 31;
    shunTak = transitink::makeTdasJourneyEstimate(
        TdasDestination::ShunTak, shunTakResult, neutral, 2000000020);
    gordon = transitink::makeTdasFallbackEstimate(
        TdasDestination::GordonRoad, neutral, 2000000020);
    transitink::reconcileCommuteJourneyEstimates(
        shunTak, true, gordon, false, 5, 14, 31, 22, 100, 2000000020);
    assert(shunTak.adjustedMinutes == 40);
    assert(gordon.adjustedMinutes == 54);
    assert(gordon.source == JourneyEstimateSource::TrafficModel);

    shunTak = transitink::makeTdasFallbackEstimate(
        TdasDestination::ShunTak, neutral, 2000000030);
    gordon = transitink::makeTdasFallbackEstimate(
        TdasDestination::GordonRoad, neutral, 2000000030);
    transitink::reconcileCommuteJourneyEstimates(
        shunTak, false, gordon, false, 5, 14, 31, 22, 100, 2000000030);
    assert(shunTak.adjustedMinutes == 31);
    assert(gordon.adjustedMinutes == 45);
    assert(gordon.source == JourneyEstimateSource::CalibratedFallback);

    shunTakResult.rawMinutes = 35;
    gordonResult.rawMinutes = 27;
    shunTak = transitink::makeTdasJourneyEstimate(
        TdasDestination::ShunTak, shunTakResult, neutral, 2000000040);
    gordon = transitink::makeTdasJourneyEstimate(
        TdasDestination::GordonRoad, gordonResult, neutral, 2000000040);
    transitink::reconcileCommuteJourneyEstimates(
        shunTak, true, gordon, true, 5, 14, 31, 22, 100, 2000000040);
    assert(shunTak.adjustedMinutes == 44);
    assert(gordon.adjustedMinutes == 58);
    assert(gordon.source == JourneyEstimateSource::TrafficModel);
}

}  // namespace

int main() {
    testRequestBodies();
    testOfficialShapeAndDurationParsing();
    testRoute3EvidenceWhenNamesAreExposed();
    testUnsafeResponsesFailClosed();
    testLiveEstimateCalibration();
    testConservativeFallbacks();
    testSharedCorridorCoupling();
    std::cout << "TDAS core tests passed\n";
    return 0;
}
