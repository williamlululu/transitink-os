#include "core/HkoForecastParser.h"

#include <ArduinoJson.h>

#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <utility>

namespace transitink {
namespace {

constexpr int64_t kSecondsPerDay = 86400;
constexpr int64_t kHongKongUtcOffsetSeconds = 8 * 60 * 60;
constexpr int64_t kMinimumUsableClockEpoch = 1577836800;  // 2020-01-01 UTC
constexpr std::size_t kMaximumForecastRows = 16;
constexpr std::size_t kForecastJsonCapacity = 16 * 1024;
constexpr std::size_t kMaximumSummaryBytes = 2048;
constexpr std::size_t kMaximumWeatherTextBytes = 768;

struct ParsedDate {
    int year = 0;
    unsigned month = 0;
    unsigned day = 0;
    int64_t serialDay = 0;
};

bool isLeapYear(int year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

unsigned daysInMonth(int year, unsigned month) {
    constexpr std::array<unsigned, 12> kDays = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
    };
    if (month < 1 || month > kDays.size()) {
        return 0;
    }
    return month == 2 && isLeapYear(year) ? 29 : kDays[month - 1];
}

int64_t daysFromCivil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
    const unsigned shiftedMonth = month > 2 ? month - 3 : month + 9;
    const unsigned dayOfYear =
        (153U * shiftedMonth + 2U) / 5U + day - 1U;
    const unsigned dayOfEra =
        yearOfEra * 365U + yearOfEra / 4U - yearOfEra / 100U + dayOfYear;
    return static_cast<int64_t>(era) * 146097 +
           static_cast<int64_t>(dayOfEra) - 719468;
}

bool parseDigits(const char* text, std::size_t offset, std::size_t count,
                 int& value) {
    value = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const unsigned char character =
            static_cast<unsigned char>(text[offset + index]);
        if (!std::isdigit(character)) {
            return false;
        }
        value = value * 10 + (character - '0');
    }
    return true;
}

bool parseForecastDate(const char* text, ParsedDate& parsed) {
    if (text == nullptr || std::char_traits<char>::length(text) != 8) {
        return false;
    }
    int year = 0;
    int month = 0;
    int day = 0;
    if (!parseDigits(text, 0, 4, year) ||
        !parseDigits(text, 4, 2, month) ||
        !parseDigits(text, 6, 2, day) || year < 2000 || year > 2200 ||
        month < 1 || month > 12 || day < 1 ||
        static_cast<unsigned>(day) >
            daysInMonth(year, static_cast<unsigned>(month))) {
        return false;
    }
    parsed.year = year;
    parsed.month = static_cast<unsigned>(month);
    parsed.day = static_cast<unsigned>(day);
    parsed.serialDay = daysFromCivil(parsed.year, parsed.month, parsed.day);
    return true;
}

bool parseIso8601Epoch(const char* text, int64_t& epoch) {
    if (text == nullptr) {
        return false;
    }
    const std::size_t length = std::char_traits<char>::length(text);
    if (length < 20 || text[4] != '-' || text[7] != '-' ||
        (text[10] != 'T' && text[10] != ' ') || text[13] != ':' ||
        text[16] != ':') {
        return false;
    }

    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (!parseDigits(text, 0, 4, year) ||
        !parseDigits(text, 5, 2, month) ||
        !parseDigits(text, 8, 2, day) ||
        !parseDigits(text, 11, 2, hour) ||
        !parseDigits(text, 14, 2, minute) ||
        !parseDigits(text, 17, 2, second) || year < 2000 || year > 2200 ||
        month < 1 || month > 12 || day < 1 ||
        static_cast<unsigned>(day) >
            daysInMonth(year, static_cast<unsigned>(month)) ||
        hour > 23 || minute > 59 || second > 59) {
        return false;
    }

    std::size_t timezoneOffset = 19;
    if (timezoneOffset < length && text[timezoneOffset] == '.') {
        ++timezoneOffset;
        const std::size_t fractionStart = timezoneOffset;
        while (timezoneOffset < length &&
               std::isdigit(static_cast<unsigned char>(text[timezoneOffset]))) {
            ++timezoneOffset;
        }
        if (timezoneOffset == fractionStart) {
            return false;
        }
    }

    int offsetSeconds = 0;
    if (timezoneOffset < length && text[timezoneOffset] == 'Z') {
        if (timezoneOffset + 1 != length) {
            return false;
        }
    } else {
        if (timezoneOffset + 6 != length ||
            (text[timezoneOffset] != '+' && text[timezoneOffset] != '-') ||
            text[timezoneOffset + 3] != ':') {
            return false;
        }
        int offsetHours = 0;
        int offsetMinutes = 0;
        if (!parseDigits(text, timezoneOffset + 1, 2, offsetHours) ||
            !parseDigits(text, timezoneOffset + 4, 2, offsetMinutes) ||
            offsetHours > 14 || offsetMinutes > 59 ||
            (offsetHours == 14 && offsetMinutes != 0)) {
            return false;
        }
        offsetSeconds = (offsetHours * 60 + offsetMinutes) * 60;
        if (text[timezoneOffset] == '-') {
            offsetSeconds = -offsetSeconds;
        }
    }

    epoch = daysFromCivil(year, static_cast<unsigned>(month),
                          static_cast<unsigned>(day)) *
                kSecondsPerDay +
            static_cast<int64_t>(hour) * 3600 +
            static_cast<int64_t>(minute) * 60 + second -
            offsetSeconds;
    return true;
}

int weekdayFromSerialDay(int64_t serialDay) {
    int weekday = static_cast<int>((serialDay + 4) % 7);
    return weekday < 0 ? weekday + 7 : weekday;
}

bool readBoundedText(JsonVariantConst value,
                     std::size_t maximumBytes,
                     std::string& output) {
    if (!value.is<const char*>()) {
        return false;
    }
    const char* text = value.as<const char*>();
    if (text == nullptr) {
        return false;
    }
    output = text;
    return output.size() <= maximumBytes;
}

bool readMeasurement(JsonObjectConst object,
                     const char* expectedUnit,
                     int minimum,
                     int maximum,
                     int& value) {
    if (object.isNull() || !object["value"].is<int>() ||
        !object["unit"].is<const char*>()) {
        return false;
    }
    const char* unit = object["unit"].as<const char*>();
    value = object["value"].as<int>();
    return unit != nullptr && std::string(unit) == expectedUnit &&
           value >= minimum && value <= maximum;
}

std::string trimmedAscii(const std::string& value) {
    std::size_t begin = 0;
    std::size_t end = value.size();
    while (begin < end &&
           std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(begin, end - begin);
}

bool parseCandidate(JsonObjectConst item,
                    const ParsedDate& date,
                    ForecastDay& day,
                    std::string& error) {
    int minimumC = 0;
    int maximumC = 0;
    int minimumHumidity = 0;
    int maximumHumidity = 0;
    if (!readMeasurement(item["forecastMintemp"].as<JsonObjectConst>(), "C",
                         -50, 60, minimumC) ||
        !readMeasurement(item["forecastMaxtemp"].as<JsonObjectConst>(), "C",
                         -50, 60, maximumC) ||
        !readMeasurement(item["forecastMinrh"].as<JsonObjectConst>(),
                         "percent", 0, 100, minimumHumidity) ||
        !readMeasurement(item["forecastMaxrh"].as<JsonObjectConst>(),
                         "percent", 0, 100, maximumHumidity) ||
        minimumC > maximumC || minimumHumidity > maximumHumidity ||
        !item["ForecastIcon"].is<int>()) {
        error = "天文台預報數值格式錯誤";
        return false;
    }

    std::string forecastWeather;
    std::string psr;
    if (!readBoundedText(item["forecastWeather"], kMaximumWeatherTextBytes,
                         forecastWeather) ||
        !readBoundedText(item["PSR"], 32, psr)) {
        error = "天文台預報文字格式錯誤";
        return false;
    }

    const int icon = item["ForecastIcon"].as<int>();
    day.month = static_cast<uint8_t>(date.month);
    day.day = static_cast<uint8_t>(date.day);
    day.weekday =
        static_cast<uint8_t>(weekdayFromSerialDay(date.serialDay));
    day.minimumC = static_cast<int8_t>(minimumC);
    day.maximumC = static_cast<int8_t>(maximumC);
    day.minimumHumidity = static_cast<uint8_t>(minimumHumidity);
    day.maximumHumidity = static_cast<uint8_t>(maximumHumidity);
    day.conditionTc = hkoForecastConditionTextTc(icon, forecastWeather);
    day.rainChanceTc = hkoRainChanceTextTc(psr);
    error.clear();
    return true;
}

}  // namespace

std::string hkoForecastConditionTextTc(int icon,
                                       const std::string& fallbackTc) {
    switch (icon) {
        case 50:
            return "陽光充沛";
        case 51:
            return "間有陽光";
        case 52:
            return "短暫陽光";
        case 53:
            return "間有陽光幾陣驟雨";
        case 54:
            return "短暫陽光有驟雨";
        case 60:
            return "多雲";
        case 61:
            return "密雲";
        case 62:
            return "微雨";
        case 63:
            return "雨";
        case 64:
            return "大雨";
        case 65:
            return "雷暴";
        case 70:
        case 71:
        case 72:
        case 73:
        case 74:
        case 75:
            return "天色良好";
        case 76:
            return "大致多雲";
        case 77:
            return "天色大致良好";
        case 80:
            return "大風";
        case 81:
            return "乾燥";
        case 82:
            return "潮濕";
        case 83:
            return "有霧";
        case 84:
            return "薄霧";
        case 85:
            return "煙霞";
        case 90:
            return "酷熱";
        case 91:
            return "溫暖";
        case 92:
            return "清涼";
        case 93:
            return "寒冷";
        default:
            return fallbackTc.empty() ? "天氣" : fallbackTc;
    }
}

std::string hkoRainChanceTextTc(const std::string& psr) {
    const std::string value = trimmedAscii(psr);
    if (value == "高" || value == "High") {
        return "高";
    }
    if (value == "中高" || value == "Medium High") {
        return "中高";
    }
    if (value == "中" || value == "Medium") {
        return "中";
    }
    if (value == "中低" || value == "Medium Low") {
        return "中低";
    }
    if (value == "低" || value == "Low") {
        return "低";
    }
    return "未提供";
}

void markHkoForecastFailure(ForecastSnapshot& snapshot,
                            const std::string& failure) {
    const std::string message =
        failure.empty() ? "暫未能取得天氣預報" : failure;
    if (snapshot.valid && snapshot.dayCount == kForecastDayCount) {
        snapshot.stale = true;
        snapshot.error = message;
        return;
    }

    const std::string previousLocation = snapshot.locationTc;
    snapshot = ForecastSnapshot{};
    snapshot.locationTc =
        previousLocation.empty() ? std::string("香港") : previousLocation;
    snapshot.error = message;
}

bool parseHkoForecastJson(const char* json,
                          int64_t nowEpoch,
                          ForecastSnapshot& snapshot,
                          std::string& error) {
    if (json == nullptr || json[0] == '\0') {
        error = "天文台預報 JSON 無法解析";
        return false;
    }

    StaticJsonDocument<1024> filter;
    filter["generalSituation"] = true;
    filter["updateTime"] = true;
    filter["weatherForecast"][0]["forecastDate"] = true;
    filter["weatherForecast"][0]["forecastWeather"] = true;
    filter["weatherForecast"][0]["ForecastIcon"] = true;
    filter["weatherForecast"][0]["PSR"] = true;
    filter["weatherForecast"][0]["forecastMaxtemp"]["value"] = true;
    filter["weatherForecast"][0]["forecastMaxtemp"]["unit"] = true;
    filter["weatherForecast"][0]["forecastMintemp"]["value"] = true;
    filter["weatherForecast"][0]["forecastMintemp"]["unit"] = true;
    filter["weatherForecast"][0]["forecastMaxrh"]["value"] = true;
    filter["weatherForecast"][0]["forecastMaxrh"]["unit"] = true;
    filter["weatherForecast"][0]["forecastMinrh"]["value"] = true;
    filter["weatherForecast"][0]["forecastMinrh"]["unit"] = true;

    DynamicJsonDocument document(kForecastJsonCapacity);
    const DeserializationError jsonError = deserializeJson(
        document, json, DeserializationOption::Filter(filter));
    if (jsonError) {
        error = jsonError == DeserializationError::NoMemory
                    ? "天文台預報 JSON 超出記憶體上限"
                    : "天文台預報 JSON 無法解析";
        return false;
    }
    if (!document.is<JsonObject>()) {
        error = "天文台預報格式錯誤";
        return false;
    }

    std::string summary;
    if (!readBoundedText(document["generalSituation"], kMaximumSummaryBytes,
                         summary)) {
        error = "天文台預報摘要格式錯誤";
        return false;
    }

    const char* updateTime = document["updateTime"].as<const char*>();
    int64_t updateEpoch = 0;
    if (!parseIso8601Epoch(updateTime, updateEpoch)) {
        error = "天文台預報更新時間格式錯誤";
        return false;
    }
    const int64_t referenceEpoch =
        nowEpoch >= kMinimumUsableClockEpoch ? nowEpoch : updateEpoch;
    const int64_t referenceDay =
        (referenceEpoch + kHongKongUtcOffsetSeconds) / kSecondsPerDay;

    const JsonArrayConst rows =
        document["weatherForecast"].as<JsonArrayConst>();
    if (rows.isNull() || rows.size() == 0 ||
        rows.size() > kMaximumForecastRows) {
        error = "天文台預報日數格式錯誤";
        return false;
    }

    std::array<ForecastDay, kForecastDayCount> forecastDays{};
    std::array<bool, kForecastDayCount> forecastDaySeen{};
    for (JsonObjectConst item : rows) {
        const char* dateText = item["forecastDate"].as<const char*>();
        ParsedDate date;
        if (!parseForecastDate(dateText, date)) {
            error = "天文台預報日期格式錯誤";
            return false;
        }
        if (date.serialDay < referenceDay ||
            date.serialDay >=
                referenceDay + static_cast<int64_t>(kForecastDayCount)) {
            continue;
        }
        const std::size_t forecastIndex = static_cast<std::size_t>(
            date.serialDay - referenceDay);
        if (forecastDaySeen[forecastIndex]) {
            error = "天文台預報日期重複";
            return false;
        }
        if (!parseCandidate(item, date, forecastDays[forecastIndex], error)) {
            return false;
        }
        forecastDaySeen[forecastIndex] = true;
    }

    ForecastSnapshot parsed;
    parsed.locationTc = "香港";
    parsed.summaryTc = std::move(summary);
    parsed.updatedAtEpoch = updateEpoch;
    for (std::size_t index = 0; index < kForecastDayCount; ++index) {
        if (!forecastDaySeen[index]) {
            error = "天文台預報未包含由今日起完整六日";
            return false;
        }
        parsed.days[index] = std::move(forecastDays[index]);
    }
    parsed.dayCount = kForecastDayCount;
    parsed.valid = true;
    parsed.stale = false;
    parsed.error.clear();
    snapshot = std::move(parsed);
    error.clear();
    return true;
}

}  // namespace transitink
