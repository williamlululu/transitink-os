#pragma once

#include <algorithm>
#include <cstdint>

#include "ProductConfig.h"

namespace transitink {

enum class CommuteSessionMode : uint8_t {
    Standby,
    AutomaticNormal,
    AutomaticRapid,
    AutomaticRecovery,
    Manual,
};

struct CommuteSessionSettings {
    uint16_t automaticStartMinutes = COMMUTE_AUTOMATIC_START_MINUTES;
    uint16_t rapidPollStartMinutes = COMMUTE_RAPID_POLL_START_MINUTES;
    uint16_t recoveryStartMinutes = COMMUTE_RECOVERY_START_MINUTES;
    uint16_t automaticEndMinutes = COMMUTE_AUTOMATIC_END_MINUTES;
    uint16_t normalPollSeconds = COMMUTE_NORMAL_POLL_SECONDS;
    uint16_t rapidPollSeconds = COMMUTE_RAPID_POLL_SECONDS;
    uint16_t recoveryPollSeconds = COMMUTE_RECOVERY_POLL_SECONDS;
    uint16_t manualPollSeconds = COMMUTE_MANUAL_POLL_SECONDS;
    uint16_t manualSessionMinutes = COMMUTE_MANUAL_SESSION_MINUTES;
    uint16_t weatherRefreshSeconds = COMMUTE_WEATHER_REFRESH_SECONDS;
    uint16_t forecastRefreshSeconds = COMMUTE_FORECAST_REFRESH_SECONDS;
    uint16_t powerTelemetrySeconds = COMMUTE_POWER_TELEMETRY_SECONDS;
};

constexpr bool commuteSessionSettingsValid(
    const CommuteSessionSettings& settings) {
    constexpr uint16_t kMinutesPerDay = 24 * 60;
    return settings.automaticStartMinutes < settings.rapidPollStartMinutes &&
           settings.rapidPollStartMinutes < settings.recoveryStartMinutes &&
           settings.recoveryStartMinutes < settings.automaticEndMinutes &&
           settings.automaticEndMinutes < kMinutesPerDay &&
           settings.normalPollSeconds > 0 && settings.rapidPollSeconds > 0 &&
           settings.recoveryPollSeconds > 0 &&
           settings.manualPollSeconds > 0 &&
           settings.manualSessionMinutes > 0 &&
           settings.weatherRefreshSeconds > 0 &&
           settings.forecastRefreshSeconds > 0 &&
           settings.powerTelemetrySeconds > 0;
}

constexpr bool isWeekday(uint8_t weekday) {
    return weekday >= 1 && weekday <= 5;
}

constexpr CommuteSessionMode automaticCommuteSessionMode(
    const CommuteSessionSettings& settings,
    uint8_t weekday,
    uint32_t secondOfDay) {
    if (!commuteSessionSettingsValid(settings) || !isWeekday(weekday) ||
        secondOfDay >= 24U * 60U * 60U) {
        return CommuteSessionMode::Standby;
    }
    const uint32_t minuteOfDay = secondOfDay / 60U;
    if (minuteOfDay < settings.automaticStartMinutes ||
        minuteOfDay >= settings.automaticEndMinutes) {
        return CommuteSessionMode::Standby;
    }
    if (minuteOfDay >= settings.recoveryStartMinutes) {
        return CommuteSessionMode::AutomaticRecovery;
    }
    if (minuteOfDay >= settings.rapidPollStartMinutes) {
        return CommuteSessionMode::AutomaticRapid;
    }
    return CommuteSessionMode::AutomaticNormal;
}

constexpr bool isAutomaticCommuteSession(CommuteSessionMode mode) {
    return mode == CommuteSessionMode::AutomaticNormal ||
           mode == CommuteSessionMode::AutomaticRapid ||
           mode == CommuteSessionMode::AutomaticRecovery;
}

constexpr bool isActiveCommuteSession(CommuteSessionMode mode) {
    return isAutomaticCommuteSession(mode) ||
           mode == CommuteSessionMode::Manual;
}

constexpr uint16_t commutePollIntervalSeconds(
    CommuteSessionMode mode,
    const CommuteSessionSettings& settings) {
    switch (mode) {
        case CommuteSessionMode::AutomaticNormal:
            return settings.normalPollSeconds;
        case CommuteSessionMode::AutomaticRapid:
            return settings.rapidPollSeconds;
        case CommuteSessionMode::AutomaticRecovery:
            return settings.recoveryPollSeconds;
        case CommuteSessionMode::Manual:
            return settings.manualPollSeconds;
        default:
            return 0;
    }
}

constexpr uint32_t secondsUntilCommuteModeBoundary(
    CommuteSessionMode mode,
    uint32_t secondOfDay,
    const CommuteSessionSettings& settings) {
    uint32_t boundary = 0;
    switch (mode) {
        case CommuteSessionMode::AutomaticNormal:
            boundary = static_cast<uint32_t>(settings.rapidPollStartMinutes) * 60U;
            break;
        case CommuteSessionMode::AutomaticRapid:
            boundary = static_cast<uint32_t>(settings.recoveryStartMinutes) * 60U;
            break;
        case CommuteSessionMode::AutomaticRecovery:
            boundary = static_cast<uint32_t>(settings.automaticEndMinutes) * 60U;
            break;
        default:
            return 0;
    }
    return boundary > secondOfDay ? boundary - secondOfDay : 0;
}

constexpr uint32_t nextCommutePollDelaySeconds(
    CommuteSessionMode mode,
    uint32_t secondOfDay,
    const CommuteSessionSettings& settings) {
    const uint32_t interval = commutePollIntervalSeconds(mode, settings);
    if (interval == 0) return 0;
    const uint32_t boundary =
        secondsUntilCommuteModeBoundary(mode, secondOfDay, settings);
    return boundary == 0 ? interval : std::min(interval, boundary);
}

constexpr uint32_t manualCommuteSessionDeadline(
    uint32_t nowMs,
    const CommuteSessionSettings& settings) {
    return nowMs + static_cast<uint32_t>(settings.manualSessionMinutes) *
                       60U * 1000U;
}

constexpr bool manualCommuteSessionActive(bool started,
                                          uint32_t nowMs,
                                          uint32_t deadlineMs) {
    return started && static_cast<int32_t>(deadlineMs - nowMs) > 0;
}

constexpr bool cachedDataRefreshDue(bool valid,
                                    int64_t updatedAtEpoch,
                                    int64_t nowEpoch,
                                    uint32_t minimumIntervalSeconds) {
    if (!valid || updatedAtEpoch <= 0 || nowEpoch <= 0 ||
        minimumIntervalSeconds == 0) {
        return true;
    }
    return nowEpoch - updatedAtEpoch >=
           static_cast<int64_t>(minimumIntervalSeconds);
}

constexpr uint32_t cachedDataRefreshDelaySeconds(
    bool valid,
    int64_t updatedAtEpoch,
    int64_t nowEpoch,
    uint32_t minimumIntervalSeconds) {
    if (cachedDataRefreshDue(valid, updatedAtEpoch, nowEpoch,
                             minimumIntervalSeconds)) {
        return 0;
    }
    const int64_t age = std::max<int64_t>(0, nowEpoch - updatedAtEpoch);
    return static_cast<uint32_t>(
        std::max<int64_t>(1, static_cast<int64_t>(minimumIntervalSeconds) - age));
}

constexpr CommuteSessionSettings kDefaultCommuteSessionSettings{};
static_assert(commuteSessionSettingsValid(kDefaultCommuteSessionSettings),
              "default commute settings must be ordered and non-zero");
static_assert(automaticCommuteSessionMode(
                  kDefaultCommuteSessionSettings, 1, 5 * 3600 + 59 * 60) ==
                  CommuteSessionMode::Standby,
              "05:59 must remain standby");
static_assert(automaticCommuteSessionMode(
                  kDefaultCommuteSessionSettings, 1, 6 * 3600) ==
                  CommuteSessionMode::AutomaticNormal,
              "06:00 must start normal polling");
static_assert(automaticCommuteSessionMode(
                  kDefaultCommuteSessionSettings, 1, 6 * 3600 + 39 * 60 + 59) ==
                  CommuteSessionMode::AutomaticNormal,
              "06:39:59 must remain normal polling");
static_assert(automaticCommuteSessionMode(
                  kDefaultCommuteSessionSettings, 1, 6 * 3600 + 40 * 60) ==
                  CommuteSessionMode::AutomaticRapid,
              "06:40 must start rapid polling");
static_assert(automaticCommuteSessionMode(
                  kDefaultCommuteSessionSettings, 1, 7 * 3600 + 9 * 60 + 59) ==
                  CommuteSessionMode::AutomaticRapid,
              "07:09:59 must remain rapid polling");
static_assert(automaticCommuteSessionMode(
                  kDefaultCommuteSessionSettings, 1, 7 * 3600 + 10 * 60) ==
                  CommuteSessionMode::AutomaticRecovery,
              "07:10 must start recovery polling");
static_assert(automaticCommuteSessionMode(
                  kDefaultCommuteSessionSettings, 1, 7 * 3600 + 30 * 60) ==
                  CommuteSessionMode::Standby,
              "07:30 must end automatic polling");
static_assert(automaticCommuteSessionMode(
                  kDefaultCommuteSessionSettings, 0, 6 * 3600 + 40 * 60) ==
                  CommuteSessionMode::Standby,
              "Sunday must remain standby");
static_assert(automaticCommuteSessionMode(
                  kDefaultCommuteSessionSettings, 6, 6 * 3600 + 40 * 60) ==
                  CommuteSessionMode::Standby,
              "Saturday must remain standby");
static_assert(commutePollIntervalSeconds(
                  CommuteSessionMode::AutomaticNormal,
                  kDefaultCommuteSessionSettings) == 120,
              "normal polling must be 120 seconds");
static_assert(commutePollIntervalSeconds(
                  CommuteSessionMode::AutomaticRapid,
                  kDefaultCommuteSessionSettings) == 30,
              "rapid polling must be 30 seconds");
static_assert(commutePollIntervalSeconds(
                  CommuteSessionMode::AutomaticRecovery,
                  kDefaultCommuteSessionSettings) == 120,
              "recovery polling must be 120 seconds");
static_assert(commutePollIntervalSeconds(
                  CommuteSessionMode::Manual,
                  kDefaultCommuteSessionSettings) == 30,
              "manual polling must be 30 seconds");

}  // namespace transitink
