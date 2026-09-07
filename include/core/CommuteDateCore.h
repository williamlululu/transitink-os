#pragma once

#include <array>
#include <cstdint>
#include <string>
#include "core/CommuteSessionCore.h"
#include "generated/HkPublicHolidays.h"

namespace transitink {

enum class CommuteIntent : uint8_t { Scheduled, Special, LeaveNow };
constexpr std::size_t kCommuteCalendarCapacity = 16;
struct CommuteCalendarHint {
    uint32_t startDate = 0;
    uint32_t endDate = 0;
    std::string label;
    // Only an explicit private user setting can pause automatic sessions.
    bool pauseAutomatic = false;
};
struct CommuteDateSettings {
    bool publicHolidayRule = true;
    uint32_t specialDate = 0;
    uint16_t specialTargetMinutes = 9 * 60;
    std::array<CommuteCalendarHint, kCommuteCalendarCapacity> calendar{};
    std::size_t calendarCount = 0;
};
struct CommuteDateDecision {
    CommuteIntent intent = CommuteIntent::LeaveNow;
    CommuteSessionMode automaticMode = CommuteSessionMode::Standby;
    CommuteSessionSettings phases{};
    uint16_t targetMinutes = UINT16_MAX;
    bool publicHoliday = false;
    bool holidayCoverageKnown = false;
    bool serviceWeekday = false;
    std::string hint;
};

inline bool commuteDateValid(uint32_t date) {
    const auto y = date / 10000;
    const auto m = date / 100 % 100;
    const auto d = date % 100;
    if (y < 2000 || y > 2099 || m < 1 || m > 12 || d < 1) return false;
    constexpr uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    const bool leap = y % 4 == 0 && (y % 100 != 0 || y % 400 == 0);
    return d <= days[m - 1] + (m == 2 && leap ? 1U : 0U);
}

inline bool commuteDateSettingsValid(const CommuteDateSettings& s) {
    // All four special phases fit on one civil date, without wrapped timers.
    if ((s.specialDate && !commuteDateValid(s.specialDate)) ||
        s.specialTargetMinutes < 85 || s.specialTargetMinutes > 1434 ||
        s.calendarCount > s.calendar.size()) return false;
    for (std::size_t i = 0; i < s.calendarCount; ++i) {
        const auto& h = s.calendar[i];
        if (!commuteDateValid(h.startDate) || !commuteDateValid(h.endDate) ||
            h.endDate < h.startDate || h.label.empty() || h.label.size() > 72)
            return false;
    }
    return true;
}

inline CommuteDateDecision resolveCommuteDate(
    const CommuteDateSettings& settings, uint32_t date, uint8_t weekday,
    uint32_t secondOfDay, bool automaticEnabled,
    const CommuteSessionSettings& scheduled = kDefaultCommuteSessionSettings) {
    CommuteDateDecision d;
    d.phases = scheduled;
    if (!commuteDateValid(date) || secondOfDay >= 86400 ||
        !commuteDateSettingsValid(settings)) return d;
    d.holidayCoverageKnown = date >= kHkHolidayCoverageStart && date <= kHkHolidayCoverageEnd;
    d.publicHoliday = weekday == 0;
    for (const auto holiday : kHkPublicHolidays) {
        if (holiday == date) d.publicHoliday = true;
    }
    // Unknown holiday coverage must not silently claim a weekday headway/profile.
    d.serviceWeekday = isWeekday(weekday) && !d.publicHoliday && d.holidayCoverageKnown;
    bool pause = false;
    for (std::size_t i = 0; i < settings.calendarCount; ++i) {
        const auto& h = settings.calendar[i];
        if (date >= h.startDate && date <= h.endDate) {
            if (d.hint.empty()) d.hint = h.label;
            pause = pause || h.pauseAutomatic;
        }
    }
    const bool special = settings.specialDate == date;
    if (special) {
        const auto target = settings.specialTargetMinutes;
        d.phases.automaticStartMinutes = target - 85;
        d.phases.rapidPollStartMinutes = target - 45;
        d.phases.recoveryStartMinutes = target - 15;
        d.phases.automaticEndMinutes = target + 5;
    }
    const bool allowed = special || (isWeekday(weekday) && !pause &&
        !(settings.publicHolidayRule && d.publicHoliday));
    const auto phase = automaticCommuteSessionMode(d.phases, allowed ? 1 : 0, secondOfDay);
    if (automaticEnabled) d.automaticMode = phase;
    if (special && secondOfDay / 60 < d.phases.automaticEndMinutes) {
        d.intent = CommuteIntent::Special;
        d.targetMinutes = settings.specialTargetMinutes;
    } else if (automaticEnabled && isAutomaticCommuteSession(phase)) {
        d.intent = CommuteIntent::Scheduled;
        d.targetMinutes = COMMUTE_TARGET_HOUR * 60 + COMMUTE_TARGET_MINUTE;
    }
    return d;
}

}  // namespace transitink
