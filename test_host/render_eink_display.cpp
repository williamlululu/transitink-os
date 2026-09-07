// Host fixture runner: compiles the production renderer, production glyphs,
// and the pinned Adafruit GFX implementation. Only hardware/time are faked.
#include <ctime>
#include <fstream>
#include <filesystem>
#include <cassert>
#include <iostream>
#include <vector>
static time_t fixtureNow;
static time_t transitinkHostTime(time_t* result) {
    if (result) *result = fixtureNow;
    return fixtureNow;
}
#define time transitinkHostTime
#include "../src/EInkDisplay.cpp"
#undef time

void BatteryMonitor::begin() {}
bus_eta::BatterySnapshot BatteryMonitor::read() {
    bus_eta::BatterySnapshot result;
    result.valid = true; result.percent = 72;
    return result;
}
void transitink::hardware::Ssd1683DisplayDriver::begin() {}
void transitink::hardware::Ssd1683DisplayDriver::show(const uint8_t*) {}
void transitink::hardware::Ssd1683DisplayDriver::showPartialRegion(
    const uint8_t*, const uint8_t*, int, int, int, int) {}

int main(int argc, char** argv) {
    assert(argc == 2);
#ifdef _WIN32
    _putenv_s("TZ", "HKT-8");
    _tzset();
#else
    setenv("TZ", "HKT-8", 1); tzset();
#endif
    std::tm civil{}; civil.tm_year = 130; civil.tm_mon = 0; civil.tm_mday = 7;
    civil.tm_hour = 6; civil.tm_min = 10;
    fixtureNow = std::mktime(&civil);
    #ifdef _WIN32
    _putenv_s("TZ", "UTC0"); _tzset();
#else
    setenv("TZ", "UTC0", 1); tzset();
#endif
    assert(currentTimeText() == "06:10");
    const auto epoch = [](int minutes) { return static_cast<int64_t>(fixtureNow) + minutes * 60; };
    std::filesystem::create_directories(argv[1]);
    EInkDisplay display;
    WeatherSnapshot weather; weather.valid = true; weather.temperatureC = 25;
    weather.conditionTc = "多雲"; weather.locationTc = "九龍城"; weather.updatedAt = fixtureNow;
    transitink::ForecastSnapshot forecast; forecast.valid = true; forecast.dayCount = 6;
    forecast.updatedAtEpoch = fixtureNow;
    for (size_t i = 0; i < 6; ++i) {
        auto& day = forecast.days[i]; day.month = 1; day.day = static_cast<uint8_t>(7 + i);
        day.weekday = static_cast<uint8_t>((1 + i) % 7); day.minimumC = 23; day.maximumC = 28;
        day.minimumHumidity = 65; day.maximumHumidity = 95; day.rainChanceTc = "中高";
        day.conditionTc = "間有陽光幾陣驟雨";
    }
    assert(commuteRainText(forecast, weather) == "現況多雲／日PSR中高");
    assert(commuteRainText(forecast, weather).indexOf("無雨") < 0);
    transitink::CommuteDashboardSnapshot snapshot;
    snapshot.targetEpoch = epoch(75); snapshot.updatedAtEpoch = fixtureNow;
    snapshot.weekday = true; snapshot.sessionMode = transitink::CommuteSessionMode::Manual;
    snapshot.intent = transitink::CommuteIntent::Scheduled;
    snapshot.dataQuality = transitink::CommuteDataQuality::Fresh;
    snapshot.recommendation = transitink::CommuteChoice::RouteA;
    snapshot.routeA.assessment = snapshot.routeB.assessment = transitink::CommuteAssessment::Safe;
    auto& a = snapshot.routeA.primary;
    a.valid = a.firstLegValid = a.connectionConfirmed = true;
    a.firstLegEarlyProfile = a.connectionEarlyProfile = true;
    a.firstLegRideMinutes = 12; a.connectionRideMinutes = 17;
    a.leaveHomeEpoch = epoch(0); a.firstBusEpoch = epoch(6);
    a.transferArrivalEpoch = epoch(18); a.transferReadyEpoch = epoch(20);
    a.connectionEpoch = epoch(27); a.arrivalEpoch = epoch(44); a.transferMarginMinutes = 7;
    snapshot.routeAStatus.state = transitink::CommuteRouteAState::ConfirmedPair;
    snapshot.routeAStatus.next8pEpoch = epoch(11);
    snapshot.routeAStatus.live8pHorizonEpoch = epoch(39);
    snapshot.routeA.fallback = a;
    snapshot.routeA.fallback.firstBusEpoch = epoch(18);
    snapshot.routeA.fallback.arrivalEpoch = epoch(56);
    auto& b = snapshot.routeB.primary;
    b.valid = b.firstLegValid = true; b.firstLegRideMinutes = 35; b.leaveHomeEpoch = epoch(3);
    b.firstBusEpoch = epoch(18); b.arrivalEpoch = epoch(53);
    snapshot.routeB.fallback = b; snapshot.routeB.fallback.arrivalEpoch = epoch(68);
    for (size_t i = 0; i < snapshot.etas.size(); ++i) {
        const bool is8p = i == transitink::commuteEtaIndex(transitink::CommuteEtaKind::Bus8P);
        auto& eta = snapshot.etas[i]; eta.count = is8p ? 1 : 2;
        eta.fetchedAtEpoch = fixtureNow;
        for (size_t row = 0; row < eta.count; ++row) {
            eta.operators[row] = static_cast<uint8_t>(is8p || row == 0
                ? transitink::BusOperator::Citybus : transitink::BusOperator::Kmb);
            eta.evidenceEpochs[row] = fixtureNow - 30;
            assert(!is8p || eta.operators[row] == static_cast<uint8_t>(transitink::BusOperator::Citybus));
        }
    }
    const auto save = [&](const char* name, bool forecastPage = false) {
        if (forecastPage) display.showForecastDashboard(forecast, weather);
        else display.showCommuteDashboard(snapshot, forecast, weather);
        assert(sizeof(frameBuffer) == 15000);
        std::vector<uint8_t> full(frameBuffer, frameBuffer + sizeof(frameBuffer));
        if (forecastPage) display.refreshForecastHeader(forecast, weather);
        else { display.refreshCommuteHeader(snapshot, forecast, weather); display.refreshCommuteBody(snapshot, forecast, weather); }
        assert(std::equal(full.begin(), full.end(), frameBuffer));
        std::ofstream out(std::filesystem::path(argv[1]) / name, std::ios::binary);
        out << "P4\n400 300\n";
        for (uint8_t pixel : frameBuffer) out.put(static_cast<char>(~pixel));
        assert(out.good());
    };
    save("01_scheduled_confirmed.pbm");
    const auto scheduledSnapshot = snapshot;
    snapshot.intent = transitink::CommuteIntent::LeaveNow; snapshot.targetEpoch = 0;
    assert(targetMarginText(snapshot, a) == "只供估算");
    assert(routeAStateText(snapshot) == "可轉車");
    a.firstLegEarlyProfile = a.connectionEarlyProfile = false;
    a.firstLegRideMinutes = 17; a.connectionRideMinutes = 27;
    a.transferArrivalEpoch = epoch(23); a.transferReadyEpoch = epoch(25);
    a.connectionEpoch = epoch(32); a.arrivalEpoch = epoch(59);
    snapshot.routeA.fallback = a;
    snapshot.routeA.fallback.firstBusEpoch = epoch(18);
    snapshot.routeA.fallback.connectionEpoch = epoch(44);
    snapshot.routeA.fallback.arrivalEpoch = epoch(71);
    b.arrivalEpoch = epoch(64);
    // An actual 10:10 Leave-now layout, with coherent uncalibrated estimates.
    constexpr int64_t later = 4 * 60 * 60;
    const auto shiftTrip = [](transitink::CommuteTripEstimate& trip) {
        for (int64_t* value : {&trip.leaveHomeEpoch, &trip.firstBusEpoch,
             &trip.transferArrivalEpoch, &trip.transferReadyEpoch, &trip.connectionEpoch,
             &trip.arrivalEpoch, &trip.riskTransferReadyEpoch, &trip.riskArrivalEpoch}) {
            if (*value > 0) *value += later;
        }
    };
    shiftTrip(a); shiftTrip(b); shiftTrip(snapshot.routeA.fallback); shiftTrip(snapshot.routeB.fallback);
    snapshot.routeAStatus.next8pEpoch += later;
    snapshot.routeAStatus.live8pHorizonEpoch += later;
    snapshot.updatedAtEpoch += later;
    for (auto& eta : snapshot.etas) {
        eta.fetchedAtEpoch += later;
        for (auto& value : eta.evidenceEpochs) if (value > 0) value += later;
    }
    fixtureNow += later; weather.updatedAt += later;
    save("02_leave_now.pbm");
    snapshot = scheduledSnapshot;
    fixtureNow -= later; weather.updatedAt -= later;
    snapshot.intent = transitink::CommuteIntent::Special; snapshot.targetEpoch = epoch(90);
    snapshot.dateHint = "特別安排／手動目標";
    save("03_special_target.pbm");
    snapshot.dateHint.clear();
    snapshot.intent = transitink::CommuteIntent::Scheduled; snapshot.targetEpoch = epoch(75);
    a.connectionAtRisk = true; save("04_transfer_risk.pbm"); a.connectionAtRisk = false;
    snapshot.recommendation = transitink::CommuteChoice::None;
    a.valid = a.connectionConfirmed = false; a.connectionEpoch = a.arrivalEpoch = 0;
    a.connectionEarlyProfile = false; a.connectionRideMinutes = 0;
    snapshot.routeAStatus.state = transitink::CommuteRouteAState::TransferPending;
    snapshot.routeA.fallback.valid = false;
    save("05_pending_keep_106.pbm");
    snapshot.recommendation = transitink::CommuteChoice::RouteA;
    a.valid = true; a.provisional = true; a.connectionEpoch = epoch(27); a.arrivalEpoch = epoch(44);
    a.connectionEarlyProfile = true; a.connectionRideMinutes = 17;
    snapshot.routeAStatus.state = transitink::CommuteRouteAState::ProvisionalTransfer;
    snapshot.routeA.fallback.valid = true; snapshot.routeA.fallback.provisional = true;
    save("06_provisional_fallback.pbm");
    a.provisional = false; snapshot.routeA.stale = true;
    snapshot.routeAStatus.state = transitink::CommuteRouteAState::Stale;
    for (auto& eta : snapshot.etas) { eta.partial = true; eta.stale = true; }
    WiFi.connected = false; save("07_stale_partial.pbm"); WiFi.connected = true;
    snapshot.sessionMode = transitink::CommuteSessionMode::Standby;
    snapshot.targetEpoch = 0; snapshot.dateHint = "日期待核實；隨時查車可用";
    save("08_standby_date_hint.pbm");
    save("09_weather_today.pbm", true);
    for (auto& day : forecast.days) { ++day.day; day.weekday = (day.weekday + 1) % 7; }
    assert(commuteRainText(forecast, weather).indexOf("PSR") < 0);
    save("10_weather_tomorrow.pbm", true);
    forecast.stale = true; weather.updatedAt = fixtureNow - 4000;
    save("11_weather_stale.pbm", true);
    weather.valid = false;
    assert(commuteTemperatureText(forecast, weather) == "--°C");
    save("12_weather_current_missing.pbm", true);
    std::cout << "12 production renderer previews; semantic and partial/full parity checks passed\n";
}
