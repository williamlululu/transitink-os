#pragma once
#include <ArduinoJson.h>
#include "AppConfig.h"

struct CommuteProfileConfigField {
    const char* key;
    uint16_t transitink::CommutePlannerSettings::*member;
    uint16_t low;
    uint16_t high;
};

inline constexpr std::array<CommuteProfileConfigField, 5> kCommuteProfileConfigFields = {{
    {"early_106_minutes", &transitink::CommutePlannerSettings::route106EarlyRideMinutes, 1, 60},
    {"early_8p_minutes", &transitink::CommutePlannerSettings::route8pEarlyRideMinutes, 1, 90},
    {"risk_106_minutes", &transitink::CommutePlannerSettings::route106RiskRideMinutes, 1, 90},
    {"risk_8p_minutes", &transitink::CommutePlannerSettings::route8pRiskRideMinutes, 1, 120},
    {"connection_early_minutes", &transitink::CommutePlannerSettings::connectionEarlyAllowanceMinutes, 0, 15},
}};

inline bool commutePersistedProfileValid(const transitink::CommutePlannerSettings& p) {
    for (const auto& field : kCommuteProfileConfigFields) {
        const auto value = p.*(field.member);
        if (value < field.low || value > field.high) return false;
    }
    return p.route106RiskRideMinutes >= p.route106EarlyRideMinutes &&
           p.route8pRiskRideMinutes >= p.route8pEarlyRideMinutes;
}

inline bool readCommuteConfig(JsonVariantConst value, DeviceConfig& config) {
    // v1.2.3 migration keeps valid defaults for the absent extension.
    if (value.isNull()) return commutePersistedProfileValid(config.commutePlanner);
    if (!value.is<JsonObjectConst>()) return false;
    auto obj = value.as<JsonObjectConst>();
    auto& d = config.commuteDates;
    auto& p = config.commutePlanner;
    if (obj.containsKey("public_holiday_rule") && !obj["public_holiday_rule"].is<bool>()) return false;
    d.publicHolidayRule = obj["public_holiday_rule"] | true;
    if (obj.containsKey("special_date") && !obj["special_date"].is<uint32_t>()) return false;
    d.specialDate = obj["special_date"] | uint32_t(0);
    if (obj.containsKey("special_target_minutes") && !obj["special_target_minutes"].is<uint16_t>()) return false;
    d.specialTargetMinutes = obj["special_target_minutes"] | uint16_t(540);
    if (obj.containsKey("early_profile_enabled") && !obj["early_profile_enabled"].is<bool>()) return false;
    p.earlyProfileEnabled = obj["early_profile_enabled"] | true;
    for (const auto& f : kCommuteProfileConfigFields) {
        if (!obj.containsKey(f.key)) continue;
        if (!obj[f.key].is<uint16_t>()) return false;
        p.*(f.member) = obj[f.key].as<uint16_t>();
    }
    if (!commutePersistedProfileValid(p)) return false;
    if (obj.containsKey("calendar")) {
        if (!obj["calendar"].is<JsonArrayConst>()) return false;
        auto entries = obj["calendar"].as<JsonArrayConst>();
        if (entries.size() > d.calendar.size()) return false;
        for (JsonObjectConst entry : entries) {
            if (entry.isNull() || !entry["start_date"].is<uint32_t>() ||
                !entry["end_date"].is<uint32_t>() || !entry["label"].is<const char*>() ||
                (entry.containsKey("pause_automatic") && !entry["pause_automatic"].is<bool>())) return false;
            auto& h = d.calendar[d.calendarCount++];
            h.startDate = entry["start_date"].as<uint32_t>();
            h.endDate = entry["end_date"].as<uint32_t>();
            h.label = entry["label"].as<const char*>();
            h.pauseAutomatic = entry["pause_automatic"] | false;
        }
    }
    return transitink::commuteDateSettingsValid(d);
}

inline void writeCommuteConfig(JsonObject obj, const DeviceConfig& config) {
    const auto& d = config.commuteDates;
    const auto& p = config.commutePlanner;
    obj["public_holiday_rule"] = d.publicHolidayRule;
    obj["special_date"] = d.specialDate;
    obj["special_target_minutes"] = d.specialTargetMinutes;
    obj["early_profile_enabled"] = p.earlyProfileEnabled;
    obj["early_106_minutes"] = p.route106EarlyRideMinutes;
    obj["early_8p_minutes"] = p.route8pEarlyRideMinutes;
    obj["risk_106_minutes"] = p.route106RiskRideMinutes;
    obj["risk_8p_minutes"] = p.route8pRiskRideMinutes;
    obj["connection_early_minutes"] = p.connectionEarlyAllowanceMinutes;
    auto entries = obj.createNestedArray("calendar");
    for (std::size_t i = 0; i < d.calendarCount && i < d.calendar.size(); ++i) {
        const auto& h = d.calendar[i];
        auto entry = entries.createNestedObject();
        entry["start_date"] = h.startDate;
        entry["end_date"] = h.endDate;
        entry["label"] = h.label.c_str();
        entry["pause_automatic"] = h.pauseAutomatic;
    }
}
