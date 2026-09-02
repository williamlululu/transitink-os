#include "core/HkoForecastParser.h"

#include <unity.h>

#include <fstream>
#include <sstream>
#include <string>

namespace {

constexpr int64_t kNoonOnSeptemberSecondHkt = 1788321600;

std::string loadFixture() {
    std::ifstream input("test_host/fixtures/hko_fnd_tc.json",
                        std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

void test_extracts_today_through_the_following_five_days() {
    const std::string json = loadFixture();
    transitink::ForecastSnapshot snapshot;
    std::string error = "sentinel";

    TEST_ASSERT_TRUE(transitink::parseHkoForecastJson(
        json.c_str(), kNoonOnSeptemberSecondHkt, snapshot, error));
    TEST_ASSERT_EQUAL_STRING("", error.c_str());
    TEST_ASSERT_TRUE(snapshot.valid);
    TEST_ASSERT_FALSE(snapshot.stale);
    TEST_ASSERT_EQUAL_UINT32(6, snapshot.dayCount);
    TEST_ASSERT_EQUAL_STRING("香港", snapshot.locationTc.c_str());
    TEST_ASSERT_EQUAL_STRING(
        "受偏東氣流影響，本週後期廣東沿岸有幾陣驟雨。",
        snapshot.summaryTc.c_str());
    TEST_ASSERT_EQUAL_INT64(1788278400, snapshot.updatedAtEpoch);

    TEST_ASSERT_EQUAL_UINT8(9, snapshot.days[0].month);
    TEST_ASSERT_EQUAL_UINT8(2, snapshot.days[0].day);
    TEST_ASSERT_EQUAL_UINT8(3, snapshot.days[0].weekday);
    TEST_ASSERT_EQUAL_INT8(26, snapshot.days[0].minimumC);
    TEST_ASSERT_EQUAL_INT8(30, snapshot.days[0].maximumC);
    TEST_ASSERT_EQUAL_UINT8(75, snapshot.days[0].minimumHumidity);
    TEST_ASSERT_EQUAL_UINT8(95, snapshot.days[0].maximumHumidity);
    TEST_ASSERT_EQUAL_STRING("雨", snapshot.days[0].conditionTc.c_str());
    TEST_ASSERT_EQUAL_STRING("中高", snapshot.days[0].rainChanceTc.c_str());

    TEST_ASSERT_EQUAL_UINT8(7, snapshot.days[5].day);
    TEST_ASSERT_EQUAL_UINT8(1, snapshot.days[5].weekday);
    TEST_ASSERT_EQUAL_STRING("間有陽光",
                              snapshot.days[5].conditionTc.c_str());
    TEST_ASSERT_EQUAL_STRING("中", snapshot.days[5].rainChanceTc.c_str());
}

void test_condition_and_psr_labels_use_traditional_chinese() {
    TEST_ASSERT_EQUAL_STRING(
        "間有陽光幾陣驟雨",
        transitink::hkoForecastConditionTextTc(53).c_str());
    TEST_ASSERT_EQUAL_STRING(
        "未知狀況",
        transitink::hkoForecastConditionTextTc(999, "未知狀況").c_str());
    TEST_ASSERT_EQUAL_STRING(
        "高", transitink::hkoRainChanceTextTc(" High ").c_str());
    TEST_ASSERT_EQUAL_STRING(
        "中高", transitink::hkoRainChanceTextTc("Medium High").c_str());
    TEST_ASSERT_EQUAL_STRING(
        "未提供", transitink::hkoRainChanceTextTc("").c_str());
}

void test_unusable_clock_falls_back_to_official_update_date() {
    const std::string json = loadFixture();
    transitink::ForecastSnapshot snapshot;
    std::string error;
    TEST_ASSERT_TRUE(transitink::parseHkoForecastJson(
        json.c_str(), 0, snapshot, error));
    TEST_ASSERT_EQUAL_UINT8(2, snapshot.days[0].day);
    TEST_ASSERT_EQUAL_UINT8(7, snapshot.days[5].day);
}

void test_malformed_days_after_required_six_are_ignored() {
    std::string json = loadFixture();
    const std::size_t extraDay =
        json.find("\"forecastDate\":\"20260909\"");
    TEST_ASSERT_NOT_EQUAL(std::string::npos, extraDay);
    const std::size_t extraValue = json.find("\"value\":31", extraDay);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, extraValue);
    json.replace(extraValue, std::string("\"value\":31").size(),
                 "\"value\":131");

    transitink::ForecastSnapshot snapshot;
    std::string error;
    TEST_ASSERT_TRUE(transitink::parseHkoForecastJson(
        json.c_str(), kNoonOnSeptemberSecondHkt, snapshot, error));
    TEST_ASSERT_EQUAL_UINT32(6, snapshot.dayCount);
    TEST_ASSERT_EQUAL_UINT8(7, snapshot.days[5].day);
}

void test_missing_today_is_rejected_without_mutating_destination() {
    std::string json = loadFixture();
    const std::string rowStart =
        "{\"forecastDate\":\"20260902\"";
    const std::size_t begin = json.find(rowStart);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, begin);
    const std::size_t end = json.find("},\n    {", begin);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, end);
    json.erase(begin, end + 2 - begin);

    transitink::ForecastSnapshot snapshot;
    snapshot.valid = true;
    snapshot.locationTc = "保留";
    snapshot.dayCount = 1;
    std::string error;
    TEST_ASSERT_FALSE(transitink::parseHkoForecastJson(
        json.c_str(), kNoonOnSeptemberSecondHkt, snapshot, error));
    TEST_ASSERT_EQUAL_STRING("天文台預報未包含由今日起完整六日",
                              error.c_str());
    TEST_ASSERT_TRUE(snapshot.valid);
    TEST_ASSERT_EQUAL_STRING("保留", snapshot.locationTc.c_str());
    TEST_ASSERT_EQUAL_UINT32(1, snapshot.dayCount);
}

void test_malformed_values_and_update_time_are_rejected() {
    std::string json = loadFixture();
    const std::string value = "\"value\":25";
    const std::size_t valueOffset = json.find(value);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, valueOffset);
    json.replace(valueOffset, value.size(), "\"value\":125");

    transitink::ForecastSnapshot snapshot;
    std::string error;
    TEST_ASSERT_FALSE(transitink::parseHkoForecastJson(
        json.c_str(), kNoonOnSeptemberSecondHkt, snapshot, error));
    TEST_ASSERT_EQUAL_STRING("天文台預報數值格式錯誤", error.c_str());

    json = loadFixture();
    const std::string timestamp = "2026-09-02T00:00:00+08:00";
    const std::size_t timeOffset = json.find(timestamp);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, timeOffset);
    json.replace(timeOffset, timestamp.size(), "2026-09-99T00:00:00+08:00");
    TEST_ASSERT_FALSE(transitink::parseHkoForecastJson(
        json.c_str(), kNoonOnSeptemberSecondHkt, snapshot, error));
    TEST_ASSERT_EQUAL_STRING("天文台預報更新時間格式錯誤",
                             error.c_str());
}

void test_refresh_failure_preserves_only_a_complete_valid_cache() {
    transitink::ForecastSnapshot cached;
    cached.valid = true;
    cached.dayCount = transitink::kForecastDayCount;
    cached.updatedAtEpoch = 1234;
    cached.days[0].day = 3;
    transitink::markHkoForecastFailure(cached, "連線失敗");
    TEST_ASSERT_TRUE(cached.valid);
    TEST_ASSERT_TRUE(cached.stale);
    TEST_ASSERT_EQUAL_INT64(1234, cached.updatedAtEpoch);
    TEST_ASSERT_EQUAL_UINT8(3, cached.days[0].day);
    TEST_ASSERT_EQUAL_STRING("連線失敗", cached.error.c_str());

    transitink::ForecastSnapshot incomplete;
    incomplete.valid = true;
    incomplete.dayCount = 2;
    incomplete.locationTc = "屯門";
    transitink::markHkoForecastFailure(incomplete, "格式錯誤");
    TEST_ASSERT_FALSE(incomplete.valid);
    TEST_ASSERT_FALSE(incomplete.stale);
    TEST_ASSERT_EQUAL_UINT32(0, incomplete.dayCount);
    TEST_ASSERT_EQUAL_STRING("屯門", incomplete.locationTc.c_str());
    TEST_ASSERT_EQUAL_STRING("格式錯誤", incomplete.error.c_str());
}

}  // namespace

void setUp() {}
void tearDown() {}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_extracts_today_through_the_following_five_days);
    RUN_TEST(test_condition_and_psr_labels_use_traditional_chinese);
    RUN_TEST(test_unusable_clock_falls_back_to_official_update_date);
    RUN_TEST(test_malformed_days_after_required_six_are_ignored);
    RUN_TEST(test_missing_today_is_rejected_without_mutating_destination);
    RUN_TEST(test_malformed_values_and_update_time_are_rejected);
    RUN_TEST(test_refresh_failure_preserves_only_a_complete_valid_cache);
    return UNITY_END();
}
