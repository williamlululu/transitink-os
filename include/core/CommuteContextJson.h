#pragma once
#include <ArduinoJson.h>
#include <memory>
#include "core/CommuteContextCore.h"

namespace transitink {
inline bool parseCommuteContext(const std::string& body, int64_t now,
                               CommuteContextSnapshot& output) {
    if (body.empty() || body.size() > 24576 || now <= 0) return false;
    DynamicJsonDocument doc(32768);
    if (deserializeJson(doc, body) || doc["schema"] != 1 ||
        !doc["generated_at"].is<int64_t>() || !doc["events"].is<JsonArray>() ||
        !doc["sources"].is<JsonArray>()) return false;
    auto storage = std::make_unique<CommuteContextSnapshot>();
    auto& next = *storage;
    next.generatedEpoch = doc["generated_at"].as<int64_t>();
    if (next.generatedEpoch <= 0 || next.generatedEpoch > now + 300 ||
        doc["events"].size() > kContextEventLimit || doc["sources"].size() != 3) return false;
    next.receivedEpoch = now;
    if (doc["alternate_checked_mask"].is<uint8_t>()) {
        next.alternateCheckedMask = doc["alternate_checked_mask"].as<uint8_t>();
        if (next.alternateCheckedMask > 3 || doc["truncated_events"].as<unsigned>() > 0) next.alternateCheckedMask = 0;
    }
    uint8_t sourceSeen = 0;
    for (JsonObjectConst source : doc["sources"].as<JsonArrayConst>()) {
        const std::string name = source["source"] | "";
        const int index = name == "citybus" ? 0 : name == "kmb" ? 1 : name == "lcsd" ? 2 : -1;
        if (index < 0 || (sourceSeen & (1U << index)) || !source["fetched_at"].is<int64_t>()) return false;
        sourceSeen |= 1U << index;
        auto& status = next.sources[index];
        status.fetchedEpoch = source["fetched_at"].as<int64_t>();
        const std::string coverage = source["coverage"] | "";
        if (coverage != "unavailable" && coverage != "partial" && coverage != "supported") return false;
        status.coverage = coverage == "supported" ? 2 : coverage == "partial" ? 1 : 0;
        if (status.fetchedEpoch < 0 || status.fetchedEpoch > next.generatedEpoch + 300) return false;
    }
    for (JsonObjectConst row : doc["events"].as<JsonArrayConst>()) {
        CommuteContextEvent e;
        const std::string route = row["route"] | "";
        if (route != "106" && route != "8P" && route != "118") return false;
        e.route = route == "106" ? 0 : route == "8P" ? 1 : 2;
        e.id = row["id"] | ""; e.sourceUrl = row["source_url"] | "";
        e.messageTc = row["message_tc"] | "";
        if (e.id.empty() || e.id.size() > 64 || e.sourceUrl.size() > 256 ||
            e.messageTc.size() > 150 || e.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.") != std::string::npos) return false;
        if (!row["operator_mask"].is<uint8_t>() || !row["start"].is<int64_t>() ||
            !row["end"].is<int64_t>() || !row["fetched_at"].is<int64_t>() ||
            !row["verified"].is<bool>()) return false;
        e.operatorMask = row["operator_mask"].as<uint8_t>();
        e.startEpoch = row["start"].as<int64_t>(); e.endEpoch = row["end"].as<int64_t>();
        e.fetchedEpoch = row["fetched_at"].as<int64_t>(); e.verified = row["verified"].as<bool>();
        if (e.startEpoch <= 0 || e.endEpoch < 0 || (e.endEpoch &&
            (e.endEpoch <= e.startEpoch || e.endEpoch-e.startEpoch > 366LL*86400)) ||
            e.fetchedEpoch <= 0 || e.fetchedEpoch > next.generatedEpoch + 300) return false;
        const std::string source = row["source"] | "";
        const std::string effect = row["effect"] | "";
        if (effect != "stop_omitted" && effect != "stop_relocated" && effect != "service_suspended" &&
            effect != "physical_stop_closure" && effect != "advisory" && effect != "athletic_meet") return false;
        const bool independent = e.operatorMask == 3 && effect == "physical_stop_closure" &&
            row["operator_independent"].is<bool>() && row["operator_independent"].as<bool>();
        const bool official = (source == "citybus" && e.sourceUrl.rfind("https://mobile.citybus.com.hk/",0)==0 && (e.operatorMask==1 || independent)) ||
            (source == "kmb" && e.sourceUrl.rfind("https://search.kmb.hk/",0)==0 && (e.operatorMask==2 || independent)) ||
            (source == "lcsd" && e.sourceUrl.rfind("https://www.lcsd.gov.hk/",0)==0 && e.operatorMask==1);
        if (!official) return false;
        e.towardYueWan = std::string(row["direction"] | "") == "toward_yue_wan";
        const std::string scope = row["scope"] | "";
        e.scope = scope == "route" ? ContextScope::Route : scope == "boarding" ? ContextScope::Boarding :
                  scope == "transfer" && e.route == 0 ? ContextScope::Transfer :
                  scope == "destination" && e.route != 0 ? ContextScope::Destination :
                  scope == "alternate_alighting" && e.route == 0 ? ContextScope::AlternateAlighting :
                  scope == "alternate_boarding" && e.route == 1 ? ContextScope::AlternateBoarding : ContextScope::Unknown;
        const std::string severity = row["severity"] | "";
        e.severity = severity == "hard_block" ? ContextSeverity::HardBlock :
                     severity == "degraded" ? ContextSeverity::Degraded : ContextSeverity::Advisory;
        if (severity != "hard_block" && severity != "degraded" && severity != "advisory") return false;
        e.athleticMeet = effect == "athletic_meet";
        if ((source == "lcsd") != e.athleticMeet ||
            (e.athleticMeet && (e.severity != ContextSeverity::Advisory || e.route != 1))) return false;
        next.events[next.count++] = std::move(e);
    }
    output = std::move(next);
    return true;
}
} // namespace transitink
