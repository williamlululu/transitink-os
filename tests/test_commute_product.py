import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def numeric_macros():
    source = read("include/ProductConfig.h")
    return {
        name: int(value)
        for name, value in re.findall(
            r"^#define\s+(COMMUTE_[A-Z0-9_]+)\s+(\d+)\s*$",
            source,
            re.MULTILINE,
        )
    }


class CommuteProductTests(unittest.TestCase):
    def setUp(self):
        self.macros = numeric_macros()

    def mode_at(self, weekday, hour, minute, second=0):
        minute_of_day = hour * 60 + minute
        if weekday not in range(1, 6):
            return "standby"
        if not (
            self.macros["COMMUTE_AUTOMATIC_START_MINUTES"]
            <= minute_of_day
            < self.macros["COMMUTE_AUTOMATIC_END_MINUTES"]
        ):
            return "standby"
        if minute_of_day >= self.macros["COMMUTE_RECOVERY_START_MINUTES"]:
            return "recovery"
        if minute_of_day >= self.macros["COMMUTE_RAPID_POLL_START_MINUTES"]:
            return "rapid"
        return "normal"

    def test_exact_weekday_schedule_boundaries(self):
        self.assertEqual("standby", self.mode_at(1, 5, 59, 0))
        self.assertEqual("normal", self.mode_at(1, 6, 0, 0))
        self.assertEqual("normal", self.mode_at(1, 6, 39, 59))
        self.assertEqual("rapid", self.mode_at(1, 6, 40, 0))
        self.assertEqual("rapid", self.mode_at(1, 7, 9, 59))
        self.assertEqual("recovery", self.mode_at(1, 7, 10, 0))
        self.assertEqual("standby", self.mode_at(1, 7, 30, 0))
        self.assertEqual("standby", self.mode_at(0, 6, 40, 0))
        self.assertEqual("standby", self.mode_at(6, 6, 40, 0))

    def test_poll_manual_weather_and_power_defaults(self):
        expected = {
            "COMMUTE_AUTOMATIC_START_MINUTES": 360,
            "COMMUTE_RAPID_POLL_START_MINUTES": 400,
            "COMMUTE_RECOVERY_START_MINUTES": 430,
            "COMMUTE_AUTOMATIC_END_MINUTES": 450,
            "COMMUTE_NORMAL_POLL_SECONDS": 120,
            "COMMUTE_RAPID_POLL_SECONDS": 30,
            "COMMUTE_RECOVERY_POLL_SECONDS": 120,
            "COMMUTE_MANUAL_POLL_SECONDS": 30,
            "COMMUTE_MANUAL_SESSION_MINUTES": 10,
            "COMMUTE_WEATHER_REFRESH_SECONDS": 900,
            "COMMUTE_POWER_TELEMETRY_SECONDS": 900,
            "COMMUTE_TIME_SYNC_RETRY_SECONDS": 300,
        }
        for name, value in expected.items():
            self.assertEqual(value, self.macros[name])

    def test_schedule_logic_is_compiler_checked(self):
        header = read("include/core/CommuteSessionCore.h")
        for token in (
            "5 * 3600 + 59 * 60",
            "6 * 3600 + 39 * 60 + 59",
            "6 * 3600 + 40 * 60",
            "7 * 3600 + 9 * 60 + 59",
            "7 * 3600 + 10 * 60",
            "7 * 3600 + 30 * 60",
            "manualCommuteSessionDeadline",
            "cachedDataRefreshDue",
        ):
            self.assertIn(token, header)
        self.assertGreaterEqual(header.count("static_assert("), 14)

    def test_fixed_route_scope_and_operator_semantics(self):
        core = read("src/core/CommuteBusCore.cpp")
        for token in (
            'citybusConfig("106", "001533", "紅磡街市"), 14, 44, "001224"',
            'citybusConfig("8P", "001213", "維多利亞公園"), 5, 9, "001224"',
            'citybusConfig("118", "001476", "紅磡海底隧道收費廣場"), 16, 27',
            '"997CCAB996935BD7", "O", "1", 14',
            '"C564EDC91AFD7D04", "O", "1", 16',
            "record.stopSequence == config.citybusBoardingSequence",
            "record.serviceType == config.kmbServiceType",
            "record.stopId == config.kmbStopId",
            "equivalentJointEta",
            "COMMUTE_JOINT_ETA_DEDUP_SECONDS",
            "duplicate->epoch = std::max",
        ):
            self.assertIn(token, core)
        for alternative in ('"102"', '"110"', '"606"'):
            self.assertNotIn(alternative, core)

    def test_parser_preserves_stop_and_eta_sequence(self):
        parser = read("src/TransitJsonParsers.cpp")
        for token in (
            'filter["data"][0]["stop"]',
            'filter["data"][0]["seq"]',
            'filter["data"][0]["eta_seq"]',
            "record.stopSequence",
            "record.etaSequence",
        ):
            self.assertIn(token, parser)

    def test_runtime_uses_light_sleep_and_no_deep_sleep(self):
        main = read("src/main.cpp")
        self.assertIn("esp_light_sleep_start()", main)
        self.assertNotIn("esp_deep_sleep_start()", main)
        self.assertIn("configureHomeWakeup()", main)
        self.assertIn('enterSleepMode("commute session inactive")', main)
        self.assertIn("stopNetworkForSleep()", main)
        self.assertIn("FIRMWARE_VERSION", main)
        self.assertIn("FIRMWARE_BOARD_ID", main)

    def test_manual_session_final_update_and_wifi_recovery(self):
        main = read("src/main.cpp")
        for token in (
            "activateManualCommuteSession",
            "manualCommuteSessionDeadlineMs",
            "manualSessionMinutes",
            "Automatic commute session final 07:30 status update",
            "startActiveWifiReconnect",
            "WiFi.setAutoReconnect(true)",
            "WIFI_PS_MIN_MODEM",
            "Power telemetry reason=",
            "Scheduled clock unavailable: retrying time sync",
            "Scheduled window found after time sync",
            "Commute session inactive at boot: transport fetch skipped",
        ):
            self.assertIn(token, main)

    def test_cold_boot_does_not_fetch_transport_outside_session(self):
        main = read("src/main.cpp")
        setup = main[main.index("void setup() {") : main.index("void loop() {")]
        skipped = setup.index(
            'Serial.println("Commute session inactive at boot: transport fetch skipped")'
        )
        self.assertLess(skipped, setup.index("refreshAllWidgetsNow();"))
        self.assertIn(
            'enterSleepMode("commute session inactive at boot")', setup
        )

    def test_portal_describes_the_actual_weekday_power_schedule(self):
        portal = read("src/TransitInkPortalPage.cpp")
        for text in (
            "06:00–07:30",
            "06:40–07:10",
            "低耗電待機",
            "10 分鐘手動通勤更新",
            "low-power standby",
            "10-minute manual commute session",
        ):
            self.assertIn(text, portal)

    def test_weather_failure_retains_a_valid_cache(self):
        main = read("src/main.cpp")
        self.assertIn("const WeatherSnapshot cachedWeather = weatherSnapshot", main)
        self.assertIn("if (!ok && cachedWeather.valid)", main)
        self.assertIn("weatherSnapshot = cachedWeather", main)

    def test_polling_and_epaper_redraw_are_separate(self):
        main = read("src/main.cpp")
        display = read("src/EInkDisplay.cpp")
        self.assertIn("void refreshCommuteBusesNow(bool render)", main)
        self.assertIn("if (render && dashboardVisible", main)
        self.assertIn("stats.changedBits == 0", display)
        self.assertIn('Serial.println("EPD partial skipped: unchanged")', display)
        self.assertIn("kMaxPartialRefreshes = 8", display)
        self.assertIn("panel.showPartialRegion", display)

    def test_weather_cache_is_at_least_fifteen_minutes(self):
        main = read("src/main.cpp")
        self.assertGreaterEqual(
            self.macros["COMMUTE_WEATHER_REFRESH_SECONDS"], 15 * 60
        )
        self.assertIn("cachedDataRefreshDelaySeconds", main)
        self.assertIn("Weather refresh skipped: cached data still fresh", main)

    def test_chinese_dashboard_labels_and_utf8(self):
        display = read("src/EInkDisplay.cpp")
        labels = (
            "建議",
            "立即",
            "最遲出門",
            "預計到達",
            "下一班",
            "轉車餘裕",
            "準時",
            "有風險",
            "將會遲到",
            "資料過時",
            "暫無班次",
            "網絡中斷",
            "更新於",
            "氣溫",
            "現時有雨",
            "現時無雨",
            "今日有雨機會",
            "紅磡街市",
            "維園轉車",
            "海底隧道",
            "漁灣邨",
            "九龍城",
        )
        for label in labels:
            self.assertIn(label, display + read("src/main.cpp"))
        (display + "".join(labels)).encode("utf-8").decode("utf-8")
        self.assertNotIn("TAKE ROUTE", display)
        self.assertNotIn("RAIN LIKELY", display)

    def test_fixed_dashboard_glyphs_are_present(self):
        sources = read("src/EInkDisplay.cpp") + read("src/core/CommuteBusCore.cpp")
        required = {ord(character) for character in sources if ord(character) > 127}
        generated = read("src/generated/HkGlyphFontData.cpp")
        available = {
            int(value, 16)
            for value in re.findall(r"\{0x([0-9A-F]{4}),", generated)
        }
        self.assertEqual(set(), required - available)

    def test_long_stop_names_use_bounded_rendering(self):
        display = read("src/EInkDisplay.cpp")
        self.assertIn(
            'drawTruncatedText(10, region.y + 19,\n'
            '                      "A  106→8P　紅磡街市→維園轉車→漁灣邨", 310)',
            display,
        )
        self.assertIn(
            'drawTruncatedText(10, region.y + 19,\n'
            '                      "B  118　海底隧道→漁灣邨", 310)',
            display,
        )
        self.assertIn("planTruncatedUtf8", display)

    def test_preview_renderer_covers_required_scenarios(self):
        preview = read("scripts/render_commute_previews.py")
        for filename in (
            "01_route_a_on_time.png",
            "02_route_b_rain.png",
            "03_recovery_0710.png",
            "04_stale_unavailable.png",
            "05_manual_weekend.png",
        ):
            self.assertIn(filename, preview)
        self.assertIn("WIDTH = 400", preview)
        self.assertIn("HEIGHT = 300", preview)
        self.assertIn('image.convert("1", dither=Image.Dither.NONE)', preview)

    def test_release_version_uses_supported_format(self):
        config = read("include/ProductConfig.h")
        match = re.search(r'^#define FIRMWARE_VERSION "([^"]+)"$', config, re.M)
        self.assertIsNotNone(match)
        self.assertEqual("1.2.0", match.group(1))
        self.assertRegex(match.group(1), r"^\d+\.\d+\.\d+$")


if __name__ == "__main__":
    unittest.main()
