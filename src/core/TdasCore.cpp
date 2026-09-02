#include "core/TdasCore.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>

namespace transitink {
namespace {

constexpr const char* kRouteLabelTc = "屯門公路・三號幹綫・西隧";

struct Coordinates {
    const char* latitude;
    const char* longitude;
};

constexpr Coordinates destinationCoordinates(TdasDestination destination) {
    switch (destination) {
        case TdasDestination::ShunTak:
            return {"22.288080073", "114.152236390"};
        case TdasDestination::GordonRoad:
            return {"22.286212030", "114.190372163"};
    }
    return {"22.288080073", "114.152236390"};
}

bool parseUnsigned(const std::string& text, uint32_t& value) {
    if (text.empty()) {
        return false;
    }
    uint32_t parsed = 0;
    for (const unsigned char c : text) {
        if (!std::isdigit(c)) {
            return false;
        }
        if (parsed > (std::numeric_limits<uint32_t>::max() - (c - '0')) / 10U) {
            return false;
        }
        parsed = parsed * 10U + static_cast<uint32_t>(c - '0');
    }
    value = parsed;
    return true;
}

std::string trimAscii(const char* text) {
    if (text == nullptr) {
        return {};
    }
    std::string value(text);
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {
        return std::isspace(c) != 0;
    }).base();
    if (first >= last) {
        return {};
    }
    return std::string(first, last);
}

bool parseDurationText(const std::string& text, uint16_t& minutes) {
    const std::size_t firstColon = text.find(':');
    if (firstColon == std::string::npos) {
        uint32_t value = 0;
        if (!parseUnsigned(text, value) || value == 0 || value > 999) {
            return false;
        }
        minutes = static_cast<uint16_t>(value);
        return true;
    }

    const std::size_t secondColon = text.find(':', firstColon + 1);
    if (secondColon != std::string::npos &&
        text.find(':', secondColon + 1) != std::string::npos) {
        return false;
    }

    const std::string hoursText = text.substr(0, firstColon);
    const std::string minutesText = text.substr(
        firstColon + 1,
        secondColon == std::string::npos ? std::string::npos
                                         : secondColon - firstColon - 1);
    const std::string secondsText =
        secondColon == std::string::npos ? "" : text.substr(secondColon + 1);
    uint32_t hours = 0;
    uint32_t minutePart = 0;
    uint32_t seconds = 0;
    if (!parseUnsigned(hoursText, hours) || hours > 999U / 60U ||
        !parseUnsigned(minutesText, minutePart) || minutePart >= 60U ||
        (secondColon != std::string::npos &&
         (secondsText.empty() || !parseUnsigned(secondsText, seconds) ||
          seconds >= 60U))) {
        return false;
    }
    const uint32_t total = hours * 60U + minutePart + (seconds > 0 ? 1U : 0U);
    if (total == 0 || total > 999) {
        return false;
    }
    minutes = static_cast<uint16_t>(total);
    return true;
}

bool parseDuration(JsonVariantConst value, uint16_t& minutes) {
    if (value.is<const char*>()) {
        return parseDurationText(trimAscii(value.as<const char*>()), minutes);
    }
    if (value.is<int>()) {
        const int raw = value.as<int>();
        if (raw < 1 || raw > 999) {
            return false;
        }
        minutes = static_cast<uint16_t>(raw);
        return true;
    }
    if (value.is<unsigned int>()) {
        const unsigned int raw = value.as<unsigned int>();
        if (raw < 1U || raw > 999U) {
            return false;
        }
        minutes = static_cast<uint16_t>(raw);
        return true;
    }
    return false;
}

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool isAsciiRoute3(const std::string& original) {
    const std::string value = lowerAscii(trimAscii(original.c_str()));
    if (value == "3") {
        return true;
    }
    std::size_t position = 0;
    while ((position = value.find("route", position)) != std::string::npos) {
        std::size_t cursor = position + 5;
        while (cursor < value.size() &&
               (std::isspace(static_cast<unsigned char>(value[cursor])) ||
                value[cursor] == '-' || value[cursor] == '_' || value[cursor] == ':' ||
                value[cursor] == '.')) {
            ++cursor;
        }
        if (cursor < value.size() && value[cursor] == '3' &&
            (cursor + 1 == value.size() ||
             !std::isalnum(static_cast<unsigned char>(value[cursor + 1])))) {
            return true;
        }
        ++position;
    }
    return false;
}

bool textIdentifiesRoute3(const std::string& value) {
    return isAsciiRoute3(value) || value.find("三號幹綫") != std::string::npos ||
           value.find("三号干线") != std::string::npos ||
           value.find("3號幹綫") != std::string::npos ||
           value.find("3号干线") != std::string::npos;
}

constexpr std::array<const char*, 16> kRouteNameKeys = {
    "routeNo",      "route_no",      "routeNumber", "route_number",
    "routeNum",     "route_num",     "routeName",   "route_name",
    "roadName",     "road_name",     "roadNameTc",  "road_name_tc",
    "name",         "name_tc",       "rd_name",     "rd_name_tc",
};

void inspectRoute3Fields(JsonObjectConst object, bool& exposed, bool& found) {
    for (const char* key : kRouteNameKeys) {
        if (!object.containsKey(key) || object[key].isNull()) {
            continue;
        }
        exposed = true;
        const JsonVariantConst field = object[key];
        if (field.is<int>() && field.as<int>() == 3) {
            found = true;
        } else if (field.is<unsigned int>() && field.as<unsigned int>() == 3U) {
            found = true;
        } else if (field.is<const char*>() &&
                   textIdentifiesRoute3(field.as<const char*>())) {
            found = true;
        }
    }
}

DynamicJsonDocument makeTdasFilter() {
    DynamicJsonDocument filter(4096);
    filter["eta"] = true;
    filter["wht"] = true;
    JsonObject route = filter["route"][0].to<JsonObject>();
    JsonObject segment = route["segment"][0].to<JsonObject>();
    // Keeping the tiny direction value preserves every segment in the filtered
    // array while intentionally dropping the potentially large IRN rid lists.
    segment["dir"] = true;
    for (const char* key : kRouteNameKeys) {
        route[key] = true;
        segment[key] = true;
    }
    return filter;
}

}  // namespace

const char* tdasDestinationLabelTc(TdasDestination destination) {
    switch (destination) {
        case TdasDestination::ShunTak:
            return "信德中心";
        case TdasDestination::GordonRoad:
            return "歌頓道／萬國寶通中心";
    }
    return "信德中心";
}

uint16_t tdasFallbackMinutes(TdasDestination destination) {
    switch (destination) {
        case TdasDestination::ShunTak:
            return 31;
        case TdasDestination::GordonRoad:
            return 45;
    }
    return 45;
}

std::string tdasRequestBody(TdasDestination destination) {
    const Coordinates end = destinationCoordinates(destination);
    std::string body =
        R"({"start":{"lat":22.362928,"long":114.015522,"buffer":100},"end":{"lat":)";
    body += end.latitude;
    body += R"(,"long":)";
    body += end.longitude;
    body +=
        R"(,"buffer":150},"departIn":0,"lang":"tc","type":"ST","tunnel":"wht"})";
    return body;
}

bool parseTdasRouteResponse(const std::string& json,
                            TdasRouteResult& result,
                            std::string& error) {
    result = {};
    if (json.empty() || json.size() > kMaxTdasResponseBytes) {
        error = json.empty() ? "TDAS 回應為空" : "TDAS 回應過大";
        return false;
    }

    DynamicJsonDocument document(32768);
    DynamicJsonDocument filter = makeTdasFilter();
    const DeserializationError jsonError = deserializeJson(
        document, json, DeserializationOption::Filter(filter));
    if (jsonError || document.overflowed() || !document.is<JsonObject>()) {
        error = "TDAS JSON 格式不正確";
        return false;
    }

    const JsonObjectConst root = document.as<JsonObjectConst>();
    if (!root["wht"].is<bool>() || !root["wht"].as<bool>()) {
        error = "TDAS 回應未確認使用西隧";
        return false;
    }
    uint16_t rawMinutes = 0;
    if (!parseDuration(root["eta"], rawMinutes)) {
        error = "TDAS 行車時間格式不正確";
        return false;
    }

    bool routeNamesExposed = false;
    bool route3Found = false;
    std::size_t segmentCount = 0;
    const JsonArrayConst routes = root["route"].as<JsonArrayConst>();
    for (const JsonVariantConst routeValue : routes) {
        if (!routeValue.is<JsonObjectConst>()) {
            continue;
        }
        const JsonObjectConst route = routeValue.as<JsonObjectConst>();
        inspectRoute3Fields(route, routeNamesExposed, route3Found);
        const JsonArrayConst segments = route["segment"].as<JsonArrayConst>();
        for (const JsonVariantConst segmentValue : segments) {
            ++segmentCount;
            if (segmentValue.is<JsonObjectConst>()) {
                inspectRoute3Fields(segmentValue.as<JsonObjectConst>(),
                                    routeNamesExposed,
                                    route3Found);
            }
        }
    }

    result.valid = true;
    result.rawMinutes = rawMinutes;
    result.usedWesternHarbourCrossing = true;
    result.segmentCount = segmentCount;
    result.route3Evidence = route3Found
                                ? TdasRoute3Evidence::Present
                                : (routeNamesExposed ? TdasRoute3Evidence::Absent
                                                     : TdasRoute3Evidence::NotExposed);
    error.clear();
    return true;
}

JourneyEstimate makeTdasJourneyEstimate(TdasDestination destination,
                                        const TdasRouteResult& result,
                                        const TdasCalibration& calibration,
                                        int64_t updatedAtEpoch) {
    JourneyEstimate estimate;
    if (!result.valid || !result.usedWesternHarbourCrossing ||
        result.rawMinutes == 0) {
        return estimate;
    }
    estimate.valid = true;
    estimate.stale = false;
    estimate.destinationTc = tdasDestinationLabelTc(destination);
    estimate.routeLabel = kRouteLabelTc;
    estimate.rawMinutes = result.rawMinutes;
    estimate.adjustedMinutes = calibratedJourneyMinutes(
        result.rawMinutes, calibration.scalePercent, calibration.offsetMinutes);
    estimate.scalePercent = calibration.scalePercent;
    estimate.offsetMinutes = calibration.offsetMinutes;
    estimate.updatedAtEpoch = updatedAtEpoch;
    estimate.source = JourneyEstimateSource::Tdas;
    if (result.route3Evidence == TdasRoute3Evidence::NotExposed) {
        estimate.messageTc = "TDAS 未提供道路名稱，未能核實三號幹綫";
    } else if (result.route3Evidence == TdasRoute3Evidence::Absent) {
        estimate.messageTc = "TDAS 回傳路線未見三號幹綫標示";
    }
    return estimate;
}

JourneyEstimate makeTdasFallbackEstimate(TdasDestination destination,
                                         const TdasCalibration& calibration,
                                         int64_t attemptedAtEpoch) {
    JourneyEstimate estimate;
    estimate.valid = true;
    estimate.stale = true;
    estimate.destinationTc = tdasDestinationLabelTc(destination);
    estimate.routeLabel = kRouteLabelTc;
    estimate.rawMinutes = tdasFallbackMinutes(destination);
    estimate.adjustedMinutes = calibratedJourneyMinutes(
        estimate.rawMinutes, calibration.scalePercent, calibration.offsetMinutes);
    estimate.scalePercent = calibration.scalePercent;
    estimate.offsetMinutes = calibration.offsetMinutes;
    estimate.updatedAtEpoch = attemptedAtEpoch;
    estimate.source = JourneyEstimateSource::CalibratedFallback;
    estimate.messageTc = "即時路況暫不可用，顯示保守估算";
    return estimate;
}

namespace {

bool usableDirectEstimate(const JourneyEstimate& estimate, bool fetched) {
    return fetched && estimate.valid && !estimate.stale &&
           estimate.source == JourneyEstimateSource::Tdas &&
           estimate.rawMinutes > 0;
}

JourneyEstimate commuteModelEstimate(TdasDestination destination,
                                     uint16_t roadMinutes,
                                     uint16_t busMinutes,
                                     uint8_t delayScalePercent,
                                     int16_t modelOffsetMinutes,
                                     int64_t updatedAtEpoch,
                                     bool trafficFresh) {
    JourneyEstimate estimate;
    estimate.valid = true;
    estimate.destinationTc = tdasDestinationLabelTc(destination);
    estimate.routeLabel = kRouteLabelTc;
    estimate.rawMinutes = roadMinutes;
    estimate.adjustedMinutes = busMinutes;
    estimate.scalePercent = delayScalePercent;
    estimate.offsetMinutes = modelOffsetMinutes;
    estimate.updatedAtEpoch = updatedAtEpoch;
    estimate.stale = !trafficFresh;
    estimate.source = trafficFresh ? JourneyEstimateSource::TrafficModel
                                   : JourneyEstimateSource::CalibratedFallback;
    estimate.messageTc = trafficFresh
                             ? "TDAS 路況配合巴士基準時間推算"
                             : "即時路況暫不可用，顯示保守估算";
    return estimate;
}

}  // namespace

void reconcileCommuteJourneyEstimates(JourneyEstimate& shunTak,
                                      bool shunTakDirect,
                                      JourneyEstimate& gordonRoad,
                                      bool gordonRoadDirect,
                                      uint16_t gordonRoadExtraMinutes,
                                      uint16_t gordonBusExtraMinutes,
                                      uint16_t shunTakBaseBusMinutes,
                                      uint16_t shunTakFreeFlowTdasMinutes,
                                      uint8_t trafficDelayScalePercent,
                                      int64_t attemptedAtEpoch) {
    const bool shunLive = usableDirectEstimate(shunTak, shunTakDirect);
    const bool gordonLive = usableDirectEstimate(gordonRoad, gordonRoadDirect);
    uint16_t roadToShunTak = 0;
    int64_t trafficUpdatedAt = attemptedAtEpoch;
    if (shunLive) {
        roadToShunTak = shunTak.rawMinutes;
        trafficUpdatedAt = shunTak.updatedAtEpoch;
    } else if (gordonLive &&
               gordonRoad.rawMinutes > gordonRoadExtraMinutes) {
        roadToShunTak = static_cast<uint16_t>(
            gordonRoad.rawMinutes - gordonRoadExtraMinutes);
        trafficUpdatedAt = gordonRoad.updatedAtEpoch;
    }

    const bool trafficFresh = roadToShunTak > 0;
    uint32_t trafficDelay = 0;
    if (trafficFresh && roadToShunTak > shunTakFreeFlowTdasMinutes) {
        const uint32_t rawDelay =
            static_cast<uint32_t>(roadToShunTak -
                                  shunTakFreeFlowTdasMinutes);
        trafficDelay =
            (rawDelay * trafficDelayScalePercent + 50U) / 100U;
    }
    const uint32_t shunBusUnclamped =
        static_cast<uint32_t>(shunTakBaseBusMinutes) + trafficDelay;
    const uint16_t shunBus = static_cast<uint16_t>(std::clamp<uint32_t>(
        shunBusUnclamped, 1U,
        999U - std::min<uint32_t>(gordonBusExtraMinutes, 998U)));
    const uint16_t gordonBus = static_cast<uint16_t>(
        shunBus + std::min<uint16_t>(gordonBusExtraMinutes,
                                    static_cast<uint16_t>(999U - shunBus)));
    const uint16_t gordonRoadMinutes = trafficFresh
                                          ? static_cast<uint16_t>(std::min<uint32_t>(
                                                999U, roadToShunTak +
                                                          gordonRoadExtraMinutes))
                                          : gordonBus;
    const int32_t offset =
        static_cast<int32_t>(shunTakBaseBusMinutes) -
        static_cast<int32_t>(shunTakFreeFlowTdasMinutes);
    const int16_t modelOffset = static_cast<int16_t>(
        std::clamp<int32_t>(offset, -32768, 32767));
    shunTak = commuteModelEstimate(
        TdasDestination::ShunTak,
        trafficFresh ? roadToShunTak : shunBus, shunBus,
        trafficDelayScalePercent, modelOffset, trafficUpdatedAt, trafficFresh);
    gordonRoad = commuteModelEstimate(
        TdasDestination::GordonRoad, gordonRoadMinutes, gordonBus,
        trafficDelayScalePercent, modelOffset, trafficUpdatedAt, trafficFresh);
}

}  // namespace transitink
