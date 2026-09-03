#ifndef BUTTON_MAPPER

#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <ctime>

#include "AppConfig.h"
#include "BatteryMonitor.h"
#include "ConfigPortal.h"
#include "ConfigStore.h"
#include "CitybusClient.h"
#include "CommuteBusClient.h"
#include "EInkDisplay.h"
#include "FirmwareUpdateService.h"
#include "GmbClient.h"
#include "HkoForecastClient.h"
#include "HkGlyphFont.h"
#include "JourneyTimeClient.h"
#include "KmbClient.h"
#include "LightRailClient.h"
#include "MtrClient.h"
#include "ProductConfig.h"
#include "TflClient.h"
#include "WeatherClient.h"
#include "WidgetCatalogService.h"
#include "core/BusEtaCore.h"
#include "core/CommuteBusCore.h"
#include "core/CommuteDashboardCore.h"
#include "core/CommuteSessionCore.h"
#include "core/HkoForecastParser.h"
#include "core/UiText.h"
#include "core/WidgetScheduler.h"
#include "hardware/BoardProfile.h"
#include "hardware/BoardSupport.h"
#include "providers/BusProvider.h"
#include "providers/GmbProvider.h"
#include "providers/JourneyTimeProvider.h"
#include "providers/LightRailProvider.h"
#include "providers/MtrProvider.h"
#include "providers/TflRailProvider.h"
#include "providers/WidgetProviderRouter.h"

SET_LOOP_TASK_STACK_SIZE(16 * 1024);

ConfigStore configStore;
BatteryMonitor chargeMonitor;
DeviceConfig deviceConfig;
KmbClient kmbClient;
CitybusClient citybusClient;
CommuteBusClient commuteBusClient(kmbClient, citybusClient);
TflClient tflClient;
GmbClient gmbClient;
MtrClient mtrClient;
LightRailClient lightRailClient;
JourneyTimeClient journeyTimeClient;
BusProvider busProvider(kmbClient, citybusClient, tflClient);
GmbProvider gmbProvider(gmbClient);
MtrProvider mtrProvider(mtrClient);
LightRailProvider lightRailProvider(lightRailClient);
TflRailProvider tflRailProvider(tflClient);
JourneyTimeProvider journeyTimeProvider(journeyTimeClient);
WidgetProviderRouter widgetProviderRouter(
    busProvider, gmbProvider, mtrProvider, lightRailProvider, tflRailProvider,
    journeyTimeProvider);
transitink::WidgetScheduler widgetScheduler(widgetProviderRouter);
WidgetCatalogService widgetCatalogService(kmbClient, citybusClient, gmbClient,
                                          tflClient);
WeatherClient weatherClient;
WeatherSnapshot weatherSnapshot;
HkoForecastClient hkoForecastClient;
transitink::CommuteDashboardSnapshot commuteDashboardSnapshot;
transitink::ForecastSnapshot forecastSnapshot;
EInkDisplay einkDisplay;
ConfigPortal configPortal(deviceConfig, configStore, widgetCatalogService);

unsigned long nextClockRefreshMs = 0;
unsigned long nextWeatherRefreshMs = 0;
unsigned long nextCommuteBusRefreshMs = 0;
unsigned long nextCommuteForecastRefreshMs = 0;
unsigned long wakeStartedAtMs = 0;
unsigned long lastChargeStatusPollMs = 0;
unsigned long nextPowerTelemetryMs = 0;
unsigned long lastWifiReconnectAttemptMs = 0;
bus_eta::BatterySnapshot chargeSnapshot;
bool factoryResetPendingRestart = false;
bool factoryResetApplied = false;
bool configAccessMode = false;
bool dashboardVisible = false;
bool sleepMaintenanceWake = false;
bool scheduledWakeSession = false;
bool sleepScreenPrepared = false;
bool chargeStatusLogged = false;
bool manualCommuteSessionStarted = false;
uint32_t manualCommuteSessionDeadlineMs = 0;
bool automaticFinalUpdateComplete = false;
bool finalAutomaticUpdateInProgress = false;
bool standbyNetworkStopped = false;
transitink::CommuteSessionMode lastCommuteSessionMode =
    transitink::CommuteSessionMode::Standby;
RTC_DATA_ATTR uint8_t activeWidgetPage = 0;
RTC_DATA_ATTR uint8_t activeCommutePage = 0;
enum class HomeWakeRefreshPhase : uint8_t {
    Idle,
    ConnectingWifi,
    WaitingForTime,
    Widgets,
    Weather,
};
HomeWakeRefreshPhase homeWakeRefreshPhase = HomeWakeRefreshPhase::Idle;
unsigned long homeWakePhaseStartedMs = 0;
uint8_t homeWakeWidgetAttempts = 0;
constexpr uint32_t kHomeWakeWifiTimeoutMs = 15000;
constexpr uint32_t kHomeWakeTimeTimeoutMs = 2000;
constexpr uint32_t kSleepResumeMarker = 0x54524E53U;
constexpr bool kCommuteDashboardEnabled = COMMUTE_DASHBOARD_ENABLED != 0;
const transitink::CommutePlannerSettings kCommutePlannerSettings{
    COMMUTE_ROUTE_A_WALK_MINUTES,
    COMMUTE_ROUTE_B_WALK_MINUTES,
    COMMUTE_BOARDING_BUFFER_MINUTES,
    COMMUTE_TRANSFER_BUFFER_MINUTES,
    COMMUTE_106_RIDE_MINUTES,
    COMMUTE_8P_RIDE_MINUTES,
    COMMUTE_118_RIDE_MINUTES,
    COMMUTE_SAFE_ARRIVAL_MARGIN_MINUTES,
    COMMUTE_MAXIMUM_ETA_AGE_MINUTES,
};
RTC_NOINIT_ATTR uint32_t sleepResumeMarker;
RTC_NOINIT_ATTR uint32_t sleepResumeMarkerInverse;

void serviceFactoryResetButtons();
void serviceConfigButton();
void serviceWidgetPageButton();
void serviceHomeButton();
void setupFactoryResetButtons();
void showConfigAccessScreen();
void returnToDashboard();
void refreshWeatherNow();
void refreshAllWidgetsNow();
void serviceOneWidgetIfDue();
void showActiveCommuteDashboard();
void refreshCommuteBusesNow(bool render = true);
void refreshCommuteForecastNow(bool render = true);
void serviceCommuteDashboardIfDue();
void scheduleNextCommuteBusRefresh();
void recalculateCommutePlan();
void startHomeWakeRefresh();
void serviceHomeWakeRefresh();
void finishHomeWakeRefresh();
bool homeWakeRefreshActive();
transitink::WidgetPageSnapshotSet currentDisplaySnapshots();
transitink::WidgetPageSnapshotSet homeWakeLoadingSnapshots();
transitink::WidgetPageSnapshotSet pageSwitchSnapshots();
uint8_t dashboardPageCount();
bool hasValidTime();
bool localScheduleTime(unsigned int& secondOfDay, unsigned int& weekday);
void applyConfiguredTimeZone();
void configureNetworkTime();
void syncTimeAndWeatherBeforeDashboard(bool homeWake);
bus_eta::SleepSettings sleepSettingsFromConfig();
bool scheduledWakeWindowActiveNow();
transitink::CommuteSessionSettings commuteSessionSettingsFromConfig();
transitink::CommuteSessionMode automaticCommuteSessionModeNow();
transitink::CommuteSessionMode currentCommuteSessionMode();
bool manualCommuteSessionActiveNow();
void activateManualCommuteSession(bool refreshImmediately);
void serviceAutomaticFinalUpdate();
void servicePowerTelemetry(bool force = false, const char* reason = "periodic");
void startActiveWifiReconnect();
unsigned long long scheduledWakeDelayUs();
void stopNetworkForSleep();
void configureLightSleepWakeup();
void armSleepResumeMarker();
void clearSleepResumeMarker();
void clearPersistentSleepResumeMarker();
bool consumeSleepResumeMarker();
void waitForHomeRelease();
void returnFromLightSleep(bool manualWake);
void performLightSleepMaintenance();
void enterSleepMode(const char* reason);
void serviceChargeStatus(bool force = false);

String configApSsid() {
    uint64_t mac = ESP.getEfuseMac();
    char suffix[7];
    snprintf(suffix, sizeof(suffix), "%06X", static_cast<unsigned int>(mac & 0xFFFFFF));
    return String(CONFIG_AP_PREFIX) + "-" + suffix;
}

String configAccessPointMessage(const String& configUrl) {
    return String(transitink::uiText(transitink::UiTextId::PasswordLabel)) +
           configPortal.apPassword() + "\n" +
           transitink::uiText(
               transitink::UiTextId::ConnectPhoneToWifi) +
           "\n" +
           transitink::uiText(transitink::UiTextId::OpenAddress) + configUrl;
}

bool connectWifi(const DeviceConfig& config) {
    if (config.wifiSsid.isEmpty()) {
        return false;
    }
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(config.wifiSsid.c_str(), config.wifiPassword.c_str());
    Serial.println("Connecting to configured Wi-Fi");
    unsigned long started = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - started < 15000) {
        serviceFactoryResetButtons();
        if (factoryResetPendingRestart) {
            return false;
        }
        delay(250);
    }
    Serial.print("Wi-Fi status: ");
    Serial.println(static_cast<int>(WiFi.status()));
    if (WiFi.status() == WL_CONNECTED) {
        const esp_err_t powerSave = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
        Serial.print("Wi-Fi modem power save: ");
        Serial.println(powerSave == ESP_OK ? "enabled" : "unavailable");
        standbyNetworkStopped = false;
    }
    return WiFi.status() == WL_CONNECTED;
}

void startActiveWifiReconnect() {
    if (WiFi.status() == WL_CONNECTED || deviceConfig.wifiSsid.isEmpty()) {
        return;
    }
    const uint32_t nowMs = millis();
    if (lastWifiReconnectAttemptMs != 0 &&
        nowMs - lastWifiReconnectAttemptMs < 15000) {
        return;
    }
    lastWifiReconnectAttemptMs = nowMs;
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(deviceConfig.wifiSsid.c_str(),
               deviceConfig.wifiPassword.c_str());
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    Serial.println("Active commute Wi-Fi reconnect requested");
}

bool waitForTimeSync(uint32_t timeoutMs) {
    unsigned long started = millis();
    while (millis() - started < timeoutMs) {
        serviceFactoryResetButtons();
        if (factoryResetPendingRestart) {
            return false;
        }
        if (hasValidTime()) {
            return true;
        }
        delay(250);
    }
    return false;
}

bool hasValidTime() {
    return time(nullptr) >= 1700000000;
}

void recalculateCommutePlan() {
    const transitink::CommuteSessionMode mode =
        finalAutomaticUpdateInProgress
            ? transitink::CommuteSessionMode::AutomaticRecovery
            : currentCommuteSessionMode();
    if (!hasValidTime()) {
        transitink::planCommuteDashboard(
            commuteDashboardSnapshot, 0, 0, false, kCommutePlannerSettings,
            mode);
        return;
    }
    const time_t now = time(nullptr);
    struct tm localNow;
    localtime_r(&now, &localNow);
    struct tm target = localNow;
    target.tm_hour = COMMUTE_TARGET_HOUR;
    target.tm_min = COMMUTE_TARGET_MINUTE;
    target.tm_sec = 0;
    const int64_t targetEpoch = static_cast<int64_t>(mktime(&target));
    const bool weekday = localNow.tm_wday >= 1 && localNow.tm_wday <= 5;
    transitink::planCommuteDashboard(
        commuteDashboardSnapshot, static_cast<int64_t>(now), targetEpoch,
        weekday, kCommutePlannerSettings, mode);
}

void applyConfiguredTimeZone() {
    setenv("TZ", transitink::devicePosixTimeZone(deviceConfig.timeZone), 1);
    tzset();
}

void configureNetworkTime() {
    configTzTime(transitink::devicePosixTimeZone(deviceConfig.timeZone),
                 "pool.ntp.org", "time.cloudflare.com", "time.nist.gov");
}

void syncTimeAndWeatherBeforeDashboard(bool homeWake) {
    configureNetworkTime();
    if (homeWake) {
        if (!hasValidTime()) {
            waitForTimeSync(2000);
        }
        refreshWeatherNow();
        return;
    }
    waitForTimeSync(hasValidTime() ? 1000 : 15000);
    refreshWeatherNow();
}

uint32_t secondsUntilNextMinute(time_t now) {
    if (now < 1700000000) {
        return 60;
    }
    uint32_t seconds = 60 - (now % 60);
    return seconds == 0 ? 60 : seconds;
}

void scheduleNextClockRefresh() {
    nextClockRefreshMs = millis() + secondsUntilNextMinute(time(nullptr)) * 1000UL;
    Serial.print("Next clock refresh ms: ");
    Serial.println(nextClockRefreshMs);
}

void refreshClockNow() {
    Serial.println("Clock refresh start");
    if (kCommuteDashboardEnabled) {
        recalculateCommutePlan();
        if (activeCommutePage == 0) {
            einkDisplay.refreshCommuteHeader(commuteDashboardSnapshot,
                                             forecastSnapshot,
                                             weatherSnapshot);
        } else {
            einkDisplay.refreshForecastHeader(forecastSnapshot,
                                              weatherSnapshot);
        }
    } else {
        einkDisplay.refreshClock(currentDisplaySnapshots(), weatherSnapshot);
    }
    scheduleNextClockRefresh();
}

void scheduleNextWeatherRefresh(uint32_t seconds = WEATHER_REFRESH_SECONDS) {
    nextWeatherRefreshMs = millis() + seconds * 1000UL;
    Serial.print("Next weather refresh ms: ");
    Serial.println(nextWeatherRefreshMs);
}

void refreshVisibleWeather() {
    if (!dashboardVisible) return;
    if (kCommuteDashboardEnabled) {
        if (activeCommutePage == 0) {
            einkDisplay.refreshCommuteHeader(commuteDashboardSnapshot,
                                             forecastSnapshot,
                                             weatherSnapshot);
        } else {
            einkDisplay.refreshForecastHeader(forecastSnapshot,
                                              weatherSnapshot);
        }
        return;
    }
    einkDisplay.refreshWeatherFooter(currentDisplaySnapshots(), weatherSnapshot);
}

void refreshWeatherNow() {
    Serial.println("Weather refresh start");
    if (kCommuteDashboardEnabled && hasValidTime()) {
        const int64_t nowEpoch = static_cast<int64_t>(time(nullptr));
        const uint32_t delaySeconds =
            transitink::cachedDataRefreshDelaySeconds(
                weatherSnapshot.valid,
                static_cast<int64_t>(weatherSnapshot.updatedAt), nowEpoch,
                commuteSessionSettingsFromConfig().weatherRefreshSeconds);
        if (delaySeconds > 0) {
            Serial.println("Weather refresh skipped: cached data still fresh");
            scheduleNextWeatherRefresh(delaySeconds);
            return;
        }
    }
    if (WiFi.status() != WL_CONNECTED) {
        weatherSnapshot.error =
            transitink::uiText(transitink::UiTextId::WifiDisconnected);
        scheduleNextWeatherRefresh(60);
        if (kCommuteDashboardEnabled) {
            refreshVisibleWeather();
        } else if (dashboardVisible) {
            einkDisplay.refreshWeatherFooter(currentDisplaySnapshots(), weatherSnapshot);
        }
        return;
    }

    String error;
    bool ok = false;
    const WeatherSnapshot cachedWeather = weatherSnapshot;
    if (kCommuteDashboardEnabled) {
        ok = weatherClient.fetchCurrentWeather("九龍城", weatherSnapshot, error);
    } else {
        ok = weatherClient.fetchCurrentWeather(deviceConfig.weatherLocationTc, weatherSnapshot,
                                               error);
    }
    if (!ok && cachedWeather.valid) {
        weatherSnapshot = cachedWeather;
        weatherSnapshot.error = error;
    }
    Serial.print("Weather refresh ok: ");
    Serial.println(ok ? "yes" : "no");
    if (!ok) {
        Serial.print("Weather error: ");
        Serial.println(error);
    }
    scheduleNextWeatherRefresh();
    if (kCommuteDashboardEnabled) {
        refreshVisibleWeather();
    } else if (dashboardVisible) {
        einkDisplay.refreshWeatherFooter(currentDisplaySnapshots(), weatherSnapshot);
    }
}

transitink::WidgetPageSnapshotSet currentDisplaySnapshots() {
    const int64_t nowEpoch = hasValidTime() ? static_cast<int64_t>(time(nullptr)) : 0;
    return transitink::snapshotsForWidgetPage(
        widgetScheduler.displaySnapshots(nowEpoch), activeWidgetPage);
}

transitink::WidgetPageSnapshotSet homeWakeLoadingSnapshots() {
    transitink::WidgetPageSnapshotSet snapshots = currentDisplaySnapshots();
    for (auto& snapshot : snapshots) {
        if (snapshot.type == transitink::WidgetType::Disabled) {
            continue;
        }
        snapshot.values = {};
        snapshot.valueCount = 0;
        snapshot.state = transitink::WidgetState::Empty;
        snapshot.providerMessage =
            transitink::uiText(transitink::UiTextId::Updating);
        snapshot.fetchedAtEpoch = 0;
        snapshot.dataAtEpoch = 0;
        snapshot.freshness = transitink::Freshness::Fresh;
        snapshot.consecutiveFailures = 0;
    }
    return snapshots;
}

transitink::WidgetPageSnapshotSet pageSwitchSnapshots() {
    const int64_t nowEpoch =
        hasValidTime() ? static_cast<int64_t>(time(nullptr)) : 0;
    return widgetScheduler.pageSwitchSnapshots(
        nowEpoch, WIDGET_PAGE_CACHE_TTL_SECONDS);
}

uint8_t dashboardPageCount() {
    return transitink::enabledWidgetPageCount(deviceConfig.widgets) > 1
               ? static_cast<uint8_t>(transitink::kWidgetPageCount)
               : 1;
}

void showActiveCommuteDashboard() {
    activeCommutePage = activeCommutePage % 2;
    recalculateCommutePlan();
    if (activeCommutePage == 0) {
        einkDisplay.showCommuteDashboard(commuteDashboardSnapshot,
                                         forecastSnapshot,
                                         weatherSnapshot);
    } else {
        einkDisplay.showForecastDashboard(forecastSnapshot, weatherSnapshot);
    }
    dashboardVisible = true;
}

void refreshCommuteBusesNow(bool render) {
    const int64_t nowEpoch =
        hasValidTime() ? static_cast<int64_t>(time(nullptr)) : 0;
    String error;
    bool ok = false;
    if (WiFi.status() == WL_CONNECTED) {
        ok = commuteBusClient.refresh(commuteDashboardSnapshot, nowEpoch, error);
    } else {
        transitink::initializeCommuteBusRows(commuteDashboardSnapshot);
        const std::string offline =
            transitink::uiText(transitink::UiTextId::WifiDisconnected);
        transitink::markAllCommuteEtasFailed(
            commuteDashboardSnapshot, nowEpoch, offline);
        error = offline.c_str();
    }
    recalculateCommutePlan();
    Serial.print("Commute bus refresh ok: ");
    Serial.println(ok ? "yes" : "partial/fallback");
    if (!error.isEmpty()) {
        Serial.print("Commute bus refresh detail: ");
        Serial.println(error);
    }
    scheduleNextCommuteBusRefresh();
    if (render && dashboardVisible && activeCommutePage == 0) {
        einkDisplay.refreshCommuteBody(commuteDashboardSnapshot,
                                       forecastSnapshot,
                                       weatherSnapshot);
    }
}

void scheduleNextCommuteBusRefresh() {
    const transitink::CommuteSessionSettings settings =
        commuteSessionSettingsFromConfig();
    const transitink::CommuteSessionMode mode =
        finalAutomaticUpdateInProgress
            ? transitink::CommuteSessionMode::AutomaticRecovery
            : currentCommuteSessionMode();
    uint32_t secondOfDay = 0;
    unsigned int scheduleWeekday = 0;
    unsigned int localSecondOfDay = 0;
    if (localScheduleTime(localSecondOfDay, scheduleWeekday)) {
        secondOfDay = localSecondOfDay;
    }
    const uint32_t delaySeconds = transitink::isAutomaticCommuteSession(mode)
                                      ? transitink::nextCommutePollDelaySeconds(
                                            mode, secondOfDay, settings)
                                      : transitink::commutePollIntervalSeconds(
                                            mode, settings);
    if (delaySeconds == 0) {
        nextCommuteBusRefreshMs = UINT32_MAX;
        Serial.println("Commute transport polling paused");
        return;
    }
    nextCommuteBusRefreshMs = millis() + delaySeconds * 1000UL;
    Serial.print("Next commute transport poll seconds: ");
    Serial.println(delaySeconds);
}

void refreshCommuteForecastNow(bool render) {
    if (hasValidTime()) {
        const int64_t nowEpoch = static_cast<int64_t>(time(nullptr));
        const uint32_t delaySeconds =
            transitink::cachedDataRefreshDelaySeconds(
                forecastSnapshot.valid, forecastSnapshot.updatedAtEpoch,
                nowEpoch,
                commuteSessionSettingsFromConfig().forecastRefreshSeconds);
        if (delaySeconds > 0) {
            Serial.println("HKO forecast refresh skipped: cache still fresh");
            nextCommuteForecastRefreshMs =
                millis() + delaySeconds * 1000UL;
            return;
        }
    }
    String error;
    bool ok = false;
    if (WiFi.status() == WL_CONNECTED) {
        ok = hkoForecastClient.fetchForecast(forecastSnapshot, error);
    } else {
        const std::string offline =
            transitink::uiText(transitink::UiTextId::WifiDisconnected);
        transitink::markHkoForecastFailure(forecastSnapshot, offline);
        error = offline.c_str();
    }
    Serial.print("HKO forecast refresh ok: ");
    Serial.println(ok ? "yes" : "cached/unavailable");
    if (!error.isEmpty()) {
        Serial.print("HKO forecast detail: ");
        Serial.println(error);
    }
    const uint32_t nextSeconds =
        ok ? commuteSessionSettingsFromConfig().forecastRefreshSeconds : 300;
    nextCommuteForecastRefreshMs = millis() + nextSeconds * 1000UL;
    if (render && dashboardVisible) {
        if (activeCommutePage == 0) {
            einkDisplay.refreshCommuteHeader(
                commuteDashboardSnapshot, forecastSnapshot, weatherSnapshot);
        } else {
            einkDisplay.showForecastDashboard(forecastSnapshot,
                                              weatherSnapshot);
        }
    }
}

void serviceCommuteDashboardIfDue() {
    const uint32_t nowMs = millis();
    if (transitink::deadlineReached(nowMs, nextCommuteBusRefreshMs)) {
        startActiveWifiReconnect();
        if (WiFi.status() != WL_CONNECTED) {
            nextCommuteBusRefreshMs = nowMs + 5000UL;
            return;
        }
        refreshCommuteBusesNow();
        return;
    }
    if (transitink::deadlineReached(nowMs, nextCommuteForecastRefreshMs)) {
        refreshCommuteForecastNow();
    }
}

void refreshAllWidgetsNow() {
    if (kCommuteDashboardEnabled) {
        Serial.println("Commute dashboard full data refresh start");
        transitink::initializeCommuteBusRows(commuteDashboardSnapshot);
        refreshCommuteBusesNow(false);
        refreshCommuteForecastNow(false);
        showActiveCommuteDashboard();
        scheduleNextClockRefresh();
        lastCommuteSessionMode = currentCommuteSessionMode();
        return;
    }
    Serial.println("Widget refresh active page start");
    const uint32_t nowMs = millis();
    const int64_t nowEpoch = hasValidTime() ? static_cast<int64_t>(time(nullptr)) : 0;
    widgetScheduler.forceActivePageDue(nowMs);
    for (std::size_t attempts = 0;
         attempts < transitink::kWidgetsPerPage && widgetScheduler.hasPendingDue(nowMs);
         ++attempts) {
        widgetScheduler.serviceNextDue(nowMs, nowEpoch);
    }
    einkDisplay.showDashboard(currentDisplaySnapshots(), weatherSnapshot,
                              activeWidgetPage, dashboardPageCount());
    dashboardVisible = true;
    scheduleNextClockRefresh();
}

void serviceOneWidgetIfDue() {
    if (kCommuteDashboardEnabled) {
        if (dashboardVisible) serviceCommuteDashboardIfDue();
        return;
    }
    if (!dashboardVisible || WiFi.status() != WL_CONNECTED) {
        return;
    }
    const uint32_t nowMs = millis();
    const int64_t nowEpoch = hasValidTime() ? static_cast<int64_t>(time(nullptr)) : 0;
    const transitink::WidgetTickResult tick = widgetScheduler.serviceNextDue(nowMs, nowEpoch);
    if (!tick.ran) {
        return;
    }
    const std::size_t pageStart = transitink::widgetPageStart(activeWidgetPage);
    if (tick.slot < pageStart ||
        tick.slot >= pageStart + transitink::kWidgetsPerPage) {
        return;
    }
    const uint8_t lane = static_cast<uint8_t>(tick.slot - pageStart);
    einkDisplay.refreshWidgetLane(lane, currentDisplaySnapshots(), weatherSnapshot);
}

bool homeWakeRefreshActive() {
    return homeWakeRefreshPhase != HomeWakeRefreshPhase::Idle;
}

void finishHomeWakeRefresh() {
    if (!homeWakeRefreshActive()) {
        return;
    }
    homeWakeRefreshPhase = HomeWakeRefreshPhase::Idle;
    clearPersistentSleepResumeMarker();
    Serial.println("Home wake background refresh complete");
}

void startHomeWakeRefresh() {
    Serial.println("Home wake: restore dashboard before network refresh");
    wakeStartedAtMs = millis();
    homeWakeWidgetAttempts = 0;
    widgetScheduler.forceActivePageDue(wakeStartedAtMs);
    if (kCommuteDashboardEnabled) {
        transitink::initializeCommuteBusRows(commuteDashboardSnapshot);
        showActiveCommuteDashboard();
    } else {
        einkDisplay.showDashboard(homeWakeLoadingSnapshots(), weatherSnapshot,
                                  activeWidgetPage, dashboardPageCount());
        dashboardVisible = true;
    }
    scheduleNextClockRefresh();

    if (deviceConfig.wifiSsid.isEmpty()) {
        weatherSnapshot.valid = false;
        weatherSnapshot.error =
            transitink::uiText(transitink::UiTextId::WifiDisconnected);
        scheduleNextWeatherRefresh(60);
        homeWakeRefreshPhase = HomeWakeRefreshPhase::Weather;
        finishHomeWakeRefresh();
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(deviceConfig.wifiSsid.c_str(), deviceConfig.wifiPassword.c_str());
    homeWakePhaseStartedMs = millis();
    homeWakeRefreshPhase = HomeWakeRefreshPhase::ConnectingWifi;
    Serial.println("Home wake: Wi-Fi connection started in background");
}

void serviceHomeWakeRefresh() {
    const uint32_t nowMs = millis();
    switch (homeWakeRefreshPhase) {
        case HomeWakeRefreshPhase::Idle:
            return;
        case HomeWakeRefreshPhase::ConnectingWifi:
            if (WiFi.status() == WL_CONNECTED) {
                Serial.println("Home wake: Wi-Fi connected");
                configureNetworkTime();
                homeWakePhaseStartedMs = nowMs;
                homeWakeRefreshPhase = HomeWakeRefreshPhase::WaitingForTime;
                return;
            }
            if (nowMs - homeWakePhaseStartedMs >= kHomeWakeWifiTimeoutMs) {
                Serial.println("Home wake: Wi-Fi connection timed out");
                weatherSnapshot.valid = false;
                weatherSnapshot.error =
                    transitink::uiText(
                        transitink::UiTextId::WifiDisconnected);
                scheduleNextWeatherRefresh(60);
                finishHomeWakeRefresh();
            }
            return;
        case HomeWakeRefreshPhase::WaitingForTime:
            if (hasValidTime() || nowMs - homeWakePhaseStartedMs >= kHomeWakeTimeTimeoutMs) {
                refreshClockNow();
                homeWakeRefreshPhase = HomeWakeRefreshPhase::Widgets;
            }
            return;
        case HomeWakeRefreshPhase::Widgets:
            if (WiFi.status() != WL_CONNECTED) {
                WiFi.reconnect();
                homeWakePhaseStartedMs = nowMs;
                homeWakeRefreshPhase = HomeWakeRefreshPhase::ConnectingWifi;
                return;
            }
            if (kCommuteDashboardEnabled) {
                refreshCommuteBusesNow(false);
                refreshCommuteForecastNow(false);
                showActiveCommuteDashboard();
                homeWakeRefreshPhase = HomeWakeRefreshPhase::Weather;
                return;
            }
            if (homeWakeWidgetAttempts < static_cast<uint8_t>(transitink::kWidgetsPerPage) &&
                widgetScheduler.hasPendingDue(nowMs)) {
                ++homeWakeWidgetAttempts;
                serviceOneWidgetIfDue();
                return;
            }
            homeWakeRefreshPhase = HomeWakeRefreshPhase::Weather;
            return;
        case HomeWakeRefreshPhase::Weather:
            refreshWeatherNow();
            finishHomeWakeRefresh();
            return;
    }
}

bus_eta::SleepSettings sleepSettingsFromConfig() {
    bus_eta::SleepSettings settings;
    settings.enabled = deviceConfig.sleepEnabled;
    settings.wakeDurationMinutes = deviceConfig.wakeDurationMinutes;
    settings.maintenanceHours = deviceConfig.sleepMaintenanceHours;
    settings.scheduledWakeEnabled = deviceConfig.scheduledWakeEnabled;
    settings.scheduledWakeStartMinutes = deviceConfig.scheduledWakeStartMinutes;
    settings.scheduledWakeEndMinutes = deviceConfig.scheduledWakeEndMinutes;
    return settings;
}

bool localScheduleTime(unsigned int& secondOfDay, unsigned int& weekday) {
    if (!hasValidTime()) {
        return false;
    }
    const time_t now = time(nullptr);
    struct tm localTime;
    if (localtime_r(&now, &localTime) == nullptr) {
        return false;
    }
    secondOfDay = static_cast<unsigned int>(localTime.tm_hour * 60 * 60 +
                                            localTime.tm_min * 60 +
                                            localTime.tm_sec);
    weekday = static_cast<unsigned int>(localTime.tm_wday);
    return true;
}

transitink::CommuteSessionSettings commuteSessionSettingsFromConfig() {
    transitink::CommuteSessionSettings settings =
        transitink::kDefaultCommuteSessionSettings;
    settings.automaticStartMinutes = deviceConfig.scheduledWakeStartMinutes;
    settings.automaticEndMinutes = deviceConfig.scheduledWakeEndMinutes;
    if (!transitink::commuteSessionSettingsValid(settings)) {
        return transitink::kDefaultCommuteSessionSettings;
    }
    return settings;
}

transitink::CommuteSessionMode automaticCommuteSessionModeNow() {
    if (!kCommuteDashboardEnabled || !deviceConfig.scheduledWakeEnabled) {
        return transitink::CommuteSessionMode::Standby;
    }
    unsigned int secondOfDay = 0;
    unsigned int weekday = 0;
    if (!localScheduleTime(secondOfDay, weekday)) {
        return transitink::CommuteSessionMode::Standby;
    }
    return transitink::automaticCommuteSessionMode(
        commuteSessionSettingsFromConfig(), static_cast<uint8_t>(weekday),
        secondOfDay);
}

bool manualCommuteSessionActiveNow() {
    return transitink::manualCommuteSessionActive(
        manualCommuteSessionStarted, millis(), manualCommuteSessionDeadlineMs);
}

transitink::CommuteSessionMode currentCommuteSessionMode() {
    const transitink::CommuteSessionMode automatic =
        automaticCommuteSessionModeNow();
    if (transitink::isAutomaticCommuteSession(automatic)) return automatic;
    return manualCommuteSessionActiveNow()
               ? transitink::CommuteSessionMode::Manual
               : transitink::CommuteSessionMode::Standby;
}

bool scheduledWakeWindowActiveNow() {
    if (kCommuteDashboardEnabled) {
        return transitink::isAutomaticCommuteSession(
            automaticCommuteSessionModeNow());
    }
    unsigned int secondOfDay = 0;
    unsigned int weekday = 0;
    if (!localScheduleTime(secondOfDay, weekday)) return false;
    return bus_eta::isScheduledWakeWindow(sleepSettingsFromConfig(),
                                          secondOfDay / 60);
}

unsigned long long scheduledWakeDelayUs() {
    unsigned int secondOfDay = 0;
    unsigned int weekday = 0;
    if (!localScheduleTime(secondOfDay, weekday)) {
        return static_cast<unsigned long long>(
                   COMMUTE_TIME_SYNC_RETRY_SECONDS) *
               1000000ULL;
    }
    bus_eta::SleepSettings settings = sleepSettingsFromConfig();
    if (kCommuteDashboardEnabled) {
        const transitink::CommuteSessionSettings commuteSettings =
            commuteSessionSettingsFromConfig();
        settings.scheduledWakeStartMinutes =
            commuteSettings.automaticStartMinutes;
        settings.scheduledWakeEndMinutes = commuteSettings.automaticEndMinutes;
    }
    const unsigned int delaySeconds = kCommuteDashboardEnabled
        ? bus_eta::secondsUntilWeekdayScheduledWakeStart(
              settings, secondOfDay, weekday)
        : bus_eta::secondsUntilScheduledWakeStart(settings, secondOfDay);
    return static_cast<unsigned long long>(delaySeconds) * 1000000ULL;
}

void stopNetworkForSleep() {
    configPortal.stop();
    WiFi.disconnect(true, true);
    esp_wifi_stop();
    WiFi.mode(WIFI_OFF);
    standbyNetworkStopped = true;
}

void configureLightSleepWakeup() {
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    transitink::hardware::configureHomeWakeup();
    // Charge state is polled while awake. It must not be able to impersonate
    // a Home press and expose the dashboard while the device is sleeping.

    const bus_eta::SleepSettings settings = sleepSettingsFromConfig();
    const unsigned long long timerUs = settings.scheduledWakeEnabled
                                           ? scheduledWakeDelayUs()
                                           : bus_eta::sleepMaintenanceIntervalUs(settings);
    if (timerUs > 0) {
        esp_sleep_enable_timer_wakeup(timerUs);
    }
}

void armSleepResumeMarker() {
    sleepResumeMarker = kSleepResumeMarker;
    sleepResumeMarkerInverse = ~kSleepResumeMarker;
    if (!configStore.setSleepResumePending(true)) {
        Serial.println("Unable to persist sleep resume marker");
    }
}

void clearSleepResumeMarker() {
    sleepResumeMarker = 0;
    sleepResumeMarkerInverse = 0;
}

void clearPersistentSleepResumeMarker() {
    if (!configStore.setSleepResumePending(false)) {
        Serial.println("Unable to clear persistent sleep resume marker");
    }
}

bool consumeSleepResumeMarker() {
    const bool pending = sleepResumeMarker == kSleepResumeMarker &&
                         sleepResumeMarkerInverse == ~kSleepResumeMarker;
    clearSleepResumeMarker();
    return pending;
}

void waitForHomeRelease() {
    unsigned long started = millis();
    while (transitink::hardware::homeButtonPressed() &&
           millis() - started < 5000 && !factoryResetPendingRestart) {
        serviceFactoryResetButtons();
        delay(20);
    }
}

void returnFromLightSleep(bool manualWake) {
    Serial.println(manualWake ? "Home wake from light sleep"
                              : "Scheduled wake from light sleep");
    setupFactoryResetButtons();
    serviceChargeStatus(true);
    einkDisplay.begin(false);
    sleepScreenPrepared = false;
    if (manualWake && kCommuteDashboardEnabled) {
        activateManualCommuteSession(false);
    }
    startHomeWakeRefresh();
}

void performLightSleepMaintenance() {
    Serial.println("Light sleep maintenance wake");
    setupFactoryResetButtons();
    dashboardVisible = false;
    configAccessMode = false;
    bool wifiOk = connectWifi(deviceConfig);
    if (wifiOk) {
        syncTimeAndWeatherBeforeDashboard(false);
    } else {
        weatherSnapshot.valid = false;
        weatherSnapshot.error =
            transitink::uiText(transitink::UiTextId::WifiDisconnected);
        scheduleNextWeatherRefresh(60);
    }
    stopNetworkForSleep();
    if (kCommuteDashboardEnabled) {
        showActiveCommuteDashboard();
        dashboardVisible = false;
    } else {
        einkDisplay.refreshSleepStatusAndWeather(
            currentDisplaySnapshots(), weatherSnapshot, activeWidgetPage,
            dashboardPageCount());
    }
    einkDisplay.prepareForSleep();
    sleepScreenPrepared = true;
}

void enterSleepMode(const char* reason) {
    if (!deviceConfig.sleepEnabled || factoryResetPendingRestart) {
        return;
    }
    Serial.print("Entering sleep mode: ");
    Serial.println(reason);
    servicePowerTelemetry(true, "standby-entry");
    scheduledWakeSession = false;
    manualCommuteSessionStarted = false;
    if (!sleepScreenPrepared) {
        transitink::hardware::clearPendingHomePress();
        configAccessMode = false;
        if (!kCommuteDashboardEnabled) {
            dashboardVisible = false;
            einkDisplay.showSleep(currentDisplaySnapshots(), weatherSnapshot,
                                  activeWidgetPage, dashboardPageCount());
        }
        dashboardVisible = false;
        stopNetworkForSleep();
        einkDisplay.prepareForSleep();
    }
    sleepScreenPrepared = false;
    setupFactoryResetButtons();

    while (deviceConfig.sleepEnabled && !factoryResetPendingRestart) {
        if (transitink::hardware::takeHomePress() ||
            transitink::hardware::homeButtonPressed()) {
            Serial.println("Home pressed while preparing sleep");
            waitForHomeRelease();
            if (!factoryResetPendingRestart) {
                returnFromLightSleep(true);
            }
            return;
        }
        configureLightSleepWakeup();
        armSleepResumeMarker();
        esp_err_t sleepResult = esp_light_sleep_start();
        const esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
        clearSleepResumeMarker();
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        transitink::hardware::disableHomeWakeup();
        setupFactoryResetButtons();

        if (sleepResult != ESP_OK) {
            Serial.print("Light sleep failed: ");
            Serial.println(static_cast<int>(sleepResult));
            delay(200);
            continue;
        }

        Serial.print("Light sleep wake cause: ");
        Serial.println(static_cast<int>(wakeCause));
        // configureLightSleepWakeup() clears every source before enabling only
        // the Home GPIO and one optional low-power timer.
        if (wakeCause == ESP_SLEEP_WAKEUP_GPIO) {
            waitForHomeRelease();
            if (!factoryResetPendingRestart) {
                returnFromLightSleep(true);
            }
            return;
        }
        if (wakeCause == ESP_SLEEP_WAKEUP_TIMER) {
            if (deviceConfig.scheduledWakeEnabled) {
                if (!hasValidTime()) {
                    Serial.println("Scheduled clock unavailable: retrying time sync");
                    performLightSleepMaintenance();
                    if (hasValidTime() && scheduledWakeWindowActiveNow()) {
                        Serial.println("Scheduled window found after time sync");
                        scheduledWakeSession = true;
                        automaticFinalUpdateComplete = false;
                        returnFromLightSleep(false);
                        return;
                    }
                    continue;
                }
                if (scheduledWakeWindowActiveNow()) {
                    Serial.println("Scheduled wake window started");
                    scheduledWakeSession = true;
                    automaticFinalUpdateComplete = false;
                    returnFromLightSleep(false);
                    return;
                }
                Serial.println("Scheduled wake occurred outside configured window");
                continue;
            }
            performLightSleepMaintenance();
            continue;
        }
        delay(50);
    }
}

void setupFactoryResetButtons() {
    transitink::hardware::configureButtons();
    if (!transitink::hardware::startButtonMonitoring()) {
        Serial.println("Unable to start button monitor");
    }
}

void applyFactoryReset() {
    if (factoryResetApplied) {
        return;
    }
    factoryResetApplied = true;
    factoryResetPendingRestart = true;
    Serial.println("Factory reset requested by volume buttons");
    configStore.clear();
    WiFi.disconnect(true, true);
    if (LittleFS.begin(true)) {
        LittleFS.format();
    }
    einkDisplay.showWifiStatus(
        String(transitink::uiText(transitink::UiTextId::ResetComplete)) +
        "\n" +
        transitink::uiText(transitink::UiTextId::ReleaseVolumeRestart));
}

void serviceFactoryResetButtons() {
    const bool upPressed = transitink::hardware::factoryResetUpButtonPressed();
    const bool downPressed = transitink::hardware::factoryResetDownButtonPressed();
    if (factoryResetPendingRestart) {
        if (!upPressed && !downPressed) {
            delay(300);
            ESP.restart();
        }
        return;
    }
    if (transitink::hardware::takeFactoryResetHold()) {
        applyFactoryReset();
    }
}

void showConfigAccessScreen() {
    Serial.println("Config button clicked");
    finishHomeWakeRefresh();
    const bool useAccessPoint = WiFi.status() != WL_CONNECTED;
    configPortal.begin(useAccessPoint);
    configAccessMode = true;
    dashboardVisible = false;
    const String configUrl = configPortal.pageUrl();
    if (configPortal.isApMode()) {
        const String message = configAccessPointMessage(configUrl);
        einkDisplay.showConfigMode(configApSsid(), message);
        return;
    }
    String displayedConfigUrl = configUrl;
    const int accessPathStart = displayedConfigUrl.lastIndexOf('/') + 1;
    if (accessPathStart > 0 && accessPathStart < displayedConfigUrl.length()) {
        displayedConfigUrl =
            displayedConfigUrl.substring(0, accessPathStart) + "\n" +
            displayedConfigUrl.substring(accessPathStart);
    }
    const String message =
        String(transitink::uiText(transitink::UiTextId::LocalSettings)) +
        "\n" + displayedConfigUrl;
    einkDisplay.showConfigMode(deviceConfig.wifiSsid, message, configUrl);
}

void returnToDashboard() {
    Serial.println("Config button clicked: returning to dashboard");
    if (!hasUsableConfig(deviceConfig)) {
        showConfigAccessScreen();
        return;
    }
    configPortal.stop();
    configAccessMode = false;
    wakeStartedAtMs = millis();
    if (kCommuteDashboardEnabled &&
        !transitink::isAutomaticCommuteSession(
            automaticCommuteSessionModeNow())) {
        activateManualCommuteSession(false);
    }
    if (WiFi.status() != WL_CONNECTED && connectWifi(deviceConfig)) {
        syncTimeAndWeatherBeforeDashboard(true);
    }
    if (kCommuteDashboardEnabled &&
        !transitink::isActiveCommuteSession(currentCommuteSessionMode())) {
        Serial.println("Commute session inactive at boot: transport fetch skipped");
        transitink::initializeCommuteBusRows(commuteDashboardSnapshot);
        activeCommutePage = 0;
        showActiveCommuteDashboard();
        scheduleNextClockRefresh();
        nextCommuteBusRefreshMs = UINT32_MAX;
        nextCommuteForecastRefreshMs = UINT32_MAX;
    } else {
        refreshAllWidgetsNow();
    }
}

void serviceConfigButton() {
    if (!transitink::hardware::takeConfigClick() || factoryResetPendingRestart) {
        return;
    }
    if (configAccessMode) {
        returnToDashboard();
    } else {
        showConfigAccessScreen();
    }
    transitink::hardware::clearPendingConfigClick();
}

void serviceWidgetPageButton() {
    if (!transitink::hardware::takeWidgetPageClick() ||
        factoryResetPendingRestart) {
        return;
    }
    if (configAccessMode || !dashboardVisible) {
        transitink::hardware::clearPendingWidgetPageClick();
        return;
    }

    if (kCommuteDashboardEnabled) {
        activeCommutePage = static_cast<uint8_t>((activeCommutePage + 1) % 2);
        Serial.print("Commute dashboard page changed to: ");
        Serial.println(static_cast<unsigned int>(activeCommutePage) + 1);
        showActiveCommuteDashboard();
        if (activeCommutePage == 1 && !forecastSnapshot.valid) {
            nextCommuteForecastRefreshMs = millis();
        }
        transitink::hardware::clearPendingWidgetPageClick();
        return;
    }

    const std::size_t nextPage =
        transitink::nextEnabledWidgetPage(deviceConfig.widgets, activeWidgetPage);
    if (nextPage == activeWidgetPage ||
        !widgetScheduler.setActivePage(nextPage, millis())) {
        return;
    }

    activeWidgetPage = static_cast<uint8_t>(nextPage);
    homeWakeWidgetAttempts = 0;
    Serial.print("Widget page changed to: ");
    Serial.println(static_cast<unsigned int>(activeWidgetPage) + 1);
    einkDisplay.showDashboard(pageSwitchSnapshots(), weatherSnapshot,
                              activeWidgetPage, dashboardPageCount());
}

void activateManualCommuteSession(bool refreshImmediately) {
    const uint32_t nowMs = millis();
    manualCommuteSessionStarted = true;
    manualCommuteSessionDeadlineMs = transitink::manualCommuteSessionDeadline(
        nowMs, commuteSessionSettingsFromConfig());
    wakeStartedAtMs = nowMs;
    standbyNetworkStopped = false;
    Serial.print("Manual commute session active for minutes: ");
    Serial.println(commuteSessionSettingsFromConfig().manualSessionMinutes);
    servicePowerTelemetry(true, "manual-session-start");
    if (!refreshImmediately || homeWakeRefreshActive()) return;

    if (WiFi.status() == WL_CONNECTED) {
        nextCommuteBusRefreshMs = nowMs;
        refreshCommuteBusesNow();
        refreshCommuteForecastNow();
        refreshWeatherNow();
        return;
    }
    startHomeWakeRefresh();
}

void serviceHomeButton() {
    if (!transitink::hardware::takeHomePress() ||
        factoryResetPendingRestart || configAccessMode ||
        !kCommuteDashboardEnabled) {
        return;
    }
    if (transitink::isAutomaticCommuteSession(
            automaticCommuteSessionModeNow())) {
        Serial.println("Home pressed during automatic commute session: refresh now");
        nextCommuteBusRefreshMs = millis();
        startActiveWifiReconnect();
        return;
    }
    activateManualCommuteSession(true);
}

void serviceAutomaticFinalUpdate() {
    if (!kCommuteDashboardEnabled || !scheduledWakeSession ||
        automaticFinalUpdateComplete ||
        transitink::isAutomaticCommuteSession(
            automaticCommuteSessionModeNow())) {
        return;
    }
    unsigned int secondOfDay = 0;
    unsigned int weekday = 0;
    const transitink::CommuteSessionSettings settings =
        commuteSessionSettingsFromConfig();
    if (!localScheduleTime(secondOfDay, weekday) ||
        !transitink::isWeekday(static_cast<uint8_t>(weekday)) ||
        secondOfDay < static_cast<unsigned int>(settings.automaticEndMinutes) *
                          60U) {
        return;
    }

    Serial.println("Automatic commute session final 07:30 status update");
    finalAutomaticUpdateInProgress = true;
    startActiveWifiReconnect();
    activeCommutePage = 0;
    refreshCommuteBusesNow(false);
    recalculateCommutePlan();
    showActiveCommuteDashboard();
    servicePowerTelemetry(true, "automatic-session-final");
    automaticFinalUpdateComplete = true;
    finalAutomaticUpdateInProgress = false;
}

void servicePowerTelemetry(bool force, const char* reason) {
    const uint32_t nowMs = millis();
    if (!force && !transitink::deadlineReached(nowMs, nextPowerTelemetryMs)) {
        return;
    }
    chargeSnapshot = chargeMonitor.read();
    nextPowerTelemetryMs =
        nowMs + static_cast<uint32_t>(
                    commuteSessionSettingsFromConfig().powerTelemetrySeconds) *
                    1000UL;
    Serial.print("Power telemetry reason=");
    Serial.print(reason);
    Serial.print(" voltage_mv=");
    Serial.print(chargeSnapshot.voltageMv);
    Serial.print(" percent=");
    Serial.print(chargeSnapshot.percent);
    Serial.print(" external_power=");
    Serial.println(chargeSnapshot.powerPresent ? "yes" : "no");
}

void serviceChargeStatus(bool force) {
    const unsigned long now = millis();
    if (!force && now - lastChargeStatusPollMs < 500) {
        return;
    }
    lastChargeStatusPollMs = now;

    const bus_eta::BatterySnapshot next = chargeMonitor.readChargeState();
    const bool changed = !chargeStatusLogged ||
                         next.powerPresent != chargeSnapshot.powerPresent ||
                         next.charging != chargeSnapshot.charging ||
                         next.full != chargeSnapshot.full;
    if (!changed) {
        chargeSnapshot.powerPresent = next.powerPresent;
        chargeSnapshot.charging = next.charging;
        chargeSnapshot.full = next.full;
        return;
    }

    chargeSnapshot = chargeMonitor.read();
    chargeStatusLogged = true;
    Serial.print("Charge state: ");
    Serial.print(chargeSnapshot.full
                     ? "full"
                     : (chargeSnapshot.charging ? "charging" : "battery"));
    Serial.print(", voltage_mv=");
    Serial.print(chargeSnapshot.voltageMv);
    Serial.print(", percent=");
    Serial.println(chargeSnapshot.percent);
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.print(FIRMWARE_PRODUCT_NAME);
    Serial.print(" firmware ");
    Serial.print(FIRMWARE_VERSION);
    Serial.print(" board ");
    Serial.println(FIRMWARE_BOARD_ID);
    applyConfiguredTimeZone();
    const esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
    const esp_reset_reason_t resetReason = esp_reset_reason();
    setupFactoryResetButtons();
    const bool homePressedAtBoot = transitink::hardware::homeButtonPressed();
    const bool rtcResetWake = consumeSleepResumeMarker();
    const bool configStoreReady = configStore.begin();
    const bool persistentResetWake = configStoreReady && configStore.sleepResumePending();
    const bool resetWake = rtcResetWake || persistentResetWake;
    const bus_eta::SleepResumeAction sleepResumeAction =
        bus_eta::decideSleepResumeAction(
            resetWake,
            wakeCause == ESP_SLEEP_WAKEUP_TIMER,
            wakeCause == ESP_SLEEP_WAKEUP_GPIO,
            homePressedAtBoot,
            resetReason == ESP_RST_POWERON);
    sleepMaintenanceWake =
        sleepResumeAction == bus_eta::SleepResumeAction::RunMaintenance;
    const bool homeWake =
        sleepResumeAction == bus_eta::SleepResumeAction::ShowDashboard;
    const bool resumeSleep =
        sleepResumeAction == bus_eta::SleepResumeAction::ResumeSleep;
    Serial.print("Wake cause: ");
    Serial.println(static_cast<int>(wakeCause));
    Serial.print("Reset reason: ");
    Serial.println(static_cast<int>(resetReason));
    Serial.print("Reset wake marker: ");
    Serial.println(persistentResetWake ? "persistent" : (rtcResetWake ? "rtc" : "no"));
    chargeMonitor.begin();
    serviceChargeStatus(true);
    // Keep the retained e-paper dashboard visible until fresh data is ready.
    // Battery wake can reset the MCU, so a transient boot screen is never safe here.
    einkDisplay.begin(false);

    bool loaded = configStore.load(deviceConfig);
    if (loaded && transitink::isUiLocaleSupported(deviceConfig.uiLocale)) {
        transitink::setUiLocale(deviceConfig.uiLocale);
    }
    if (loaded &&
        transitink::isDisplayFontSupported(deviceConfig.displayFont)) {
        setActiveDisplayFont(deviceConfig.displayFont);
    }
    if (loaded &&
        transitink::isDeviceTimeZoneSupported(deviceConfig.timeZone)) {
        applyConfiguredTimeZone();
    }
    Serial.print("Config loaded: ");
    Serial.println(loaded ? "yes" : "no");
    if (!loaded || !hasUsableConfig(deviceConfig)) {
        Serial.println("Starting config portal: missing or invalid config");
        configPortal.begin(true);
        configAccessMode = true;
        const String configUrl = configPortal.pageUrl();
        einkDisplay.showConfigMode(
            configApSsid(),
            configAccessPointMessage(configUrl));
        return;
    }

    widgetScheduler.configure(deviceConfig.widgets, millis());
    if (activeCommutePage > 1) activeCommutePage = 0;
    if (!transitink::widgetPageHasEnabled(deviceConfig.widgets,
                                          activeWidgetPage)) {
        activeWidgetPage = static_cast<uint8_t>(
            transitink::firstEnabledWidgetPage(deviceConfig.widgets));
    }
    widgetScheduler.setActivePage(activeWidgetPage, millis());
    if (!transitink::FirmwareUpdateService::confirmRunningFirmware()) {
        Serial.println("OTA firmware validation failed");
    }

    if (deviceConfig.sleepEnabled && sleepMaintenanceWake) {
        if (deviceConfig.scheduledWakeEnabled) {
            sleepMaintenanceWake = false;
            if (scheduledWakeWindowActiveNow()) {
                Serial.println("Scheduled reset wake: showing dashboard");
                scheduledWakeSession = true;
                automaticFinalUpdateComplete = false;
                startHomeWakeRefresh();
                return;
            }
            Serial.println("Scheduled reset wake outside window: returning to sleep");
            sleepScreenPrepared = true;
            enterSleepMode("scheduled wake outside window");
            return;
        }
        performLightSleepMaintenance();
        sleepMaintenanceWake = false;
        enterSleepMode("maintenance complete");
        return;
    }

    if (deviceConfig.sleepEnabled && resumeSleep) {
        Serial.println("Unconfirmed sleep reset: returning to sleep");
        clearPersistentSleepResumeMarker();
        dashboardVisible = false;
        configAccessMode = false;
        sleepScreenPrepared = true;
        enterSleepMode("unconfirmed sleep reset");
        return;
    }

    if (homeWake) {
        Serial.println("Config portal deferred until button press");
        if (kCommuteDashboardEnabled) {
            activateManualCommuteSession(false);
        }
        startHomeWakeRefresh();
        return;
    }

    // A cold boot may not have a valid clock. Synchronise only the clock first
    // so the weekday session decision is reliable without fetching transport
    // data outside the configured commute window.
    if (kCommuteDashboardEnabled && !hasValidTime() &&
        connectWifi(deviceConfig)) {
        configureNetworkTime();
        waitForTimeSync(15000);
    }

    if (kCommuteDashboardEnabled &&
        !transitink::isActiveCommuteSession(currentCommuteSessionMode())) {
        Serial.println("Commute session inactive at boot: transport fetch skipped");
        nextCommuteBusRefreshMs = UINT32_MAX;
        nextCommuteForecastRefreshMs = UINT32_MAX;
        enterSleepMode("commute session inactive at boot");
        return;
    }

    bool wifiOk = WiFi.status() == WL_CONNECTED || connectWifi(deviceConfig);
    if (wifiOk) {
        syncTimeAndWeatherBeforeDashboard(false);
    } else {
        weatherSnapshot.valid = false;
        weatherSnapshot.error =
            transitink::uiText(transitink::UiTextId::WifiDisconnected);
        scheduleNextWeatherRefresh(60);
    }

    Serial.println("Config portal deferred until button press");
    wakeStartedAtMs = millis();
    refreshAllWidgetsNow();
}

void loop() {
    serviceFactoryResetButtons();
    serviceChargeStatus();
    if (factoryResetPendingRestart) {
        delay(20);
        return;
    }
    configPortal.loop();
    serviceConfigButton();
    serviceWidgetPageButton();
    serviceHomeButton();
    if (configAccessMode) {
        delay(5);
        return;
    }
    if (hasUsableConfig(deviceConfig)) {
        if (kCommuteDashboardEnabled) {
            const transitink::CommuteSessionMode mode =
                currentCommuteSessionMode();
            if (mode != lastCommuteSessionMode) {
                Serial.print("Commute session mode changed: ");
                Serial.println(static_cast<unsigned int>(mode));
                if (transitink::isActiveCommuteSession(mode) &&
                    lastCommuteSessionMode ==
                        transitink::CommuteSessionMode::Standby) {
                    nextCommuteBusRefreshMs = millis();
                    nextPowerTelemetryMs = millis();
                    standbyNetworkStopped = false;
                }
                lastCommuteSessionMode = mode;
            }
            if (transitink::isAutomaticCommuteSession(mode)) {
                if (!scheduledWakeSession) {
                    automaticFinalUpdateComplete = false;
                }
                scheduledWakeSession = true;
                manualCommuteSessionStarted = false;
            }
            if (scheduledWakeSession &&
                mode == transitink::CommuteSessionMode::Standby &&
                !automaticFinalUpdateComplete) {
                serviceAutomaticFinalUpdate();
            }

            if (!transitink::isActiveCommuteSession(mode)) {
                if (!standbyNetworkStopped) stopNetworkForSleep();
                if (!homeWakeRefreshActive()) {
                    enterSleepMode("commute session inactive");
                }
                delay(5);
                return;
            }

            startActiveWifiReconnect();
            if (homeWakeRefreshActive()) {
                serviceHomeWakeRefresh();
            } else {
                if (millis() >= nextWeatherRefreshMs) {
                    refreshWeatherNow();
                }
                if (millis() >= nextClockRefreshMs) {
                    refreshClockNow();
                }
                serviceCommuteDashboardIfDue();
                servicePowerTelemetry();
            }
            delay(5);
            return;
        }

        const bool scheduledWakeWindowActive = scheduledWakeWindowActiveNow();
        if (scheduledWakeWindowActive) {
            scheduledWakeSession = true;
        }
        const bool sleepBlocked = configAccessMode || chargeSnapshot.powerPresent ||
                                  homeWakeRefreshActive();
        if (bus_eta::shouldAutoSleep(
                sleepSettingsFromConfig(),
                wakeStartedAtMs,
                millis(),
                sleepBlocked,
                scheduledWakeSession,
                scheduledWakeWindowActive)) {
            enterSleepMode(scheduledWakeSession
                               ? "scheduled wake window ended"
                               : "button wake window expired");
        } else if (homeWakeRefreshActive()) {
            serviceHomeWakeRefresh();
        } else {
            if (millis() >= nextWeatherRefreshMs) {
                refreshWeatherNow();
            }
            if (millis() >= nextClockRefreshMs) {
                refreshClockNow();
            }
            serviceOneWidgetIfDue();
        }
    }
    delay(5);
}

#endif  // BUTTON_MAPPER
