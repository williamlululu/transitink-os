#include "EInkDisplay.h"

#include <Adafruit_GFX.h>
#include <WiFi.h>
#include <qrcode.h>

#include <algorithm>
#include <cstring>

#include "BatteryMonitor.h"
#include "HkGlyphFont.h"
#include "ProductConfig.h"
#include "core/DisplayTextCore.h"
#include "core/UiText.h"
#include "hardware/BoardProfile.h"
#include "hardware/SelectedDisplayDriver.h"

namespace {

constexpr uint16_t kColorBlack = 0;
constexpr uint16_t kColorWhite = 1;
constexpr int EINK_WIDTH = transitink::hardware::kBoardProfile.display.width;
constexpr int EINK_HEIGHT = transitink::hardware::kBoardProfile.display.height;
constexpr size_t kBytesPerRow = EINK_WIDTH / 8;
constexpr size_t kFrameBufferSize = kBytesPerRow * EINK_HEIGHT;
static_assert(EINK_WIDTH == 400 && EINK_HEIGHT == 300,
              "the current dashboard renderer requires a 400x300 display");
constexpr int kCustomGlyphWidth = 16;
constexpr int kCustomGlyphHeight = 16;
constexpr int kCustomGlyphBaselineOffset = 14;
constexpr int kMaxPartialRefreshes = 8;
constexpr int kLaneTextX = 12;
constexpr int kLaneBaseTextWidth = 204;
constexpr int kLaneValueAreaX = 224;
constexpr int kLaneValueWidth = 76;
constexpr int kLaneValueRightSpacing = 88;
constexpr int kLaneRightEdge = EINK_WIDTH - 12;
constexpr int kLaneColumnGap = 12;

struct DisplayRegion {
    int x;
    int y;
    int w;
    int h;
};

static_assert(transitink::kWidgetsPerPage == 4,
              "dashboard requires exactly four widgets per page");

constexpr bool regionFitsPanel(const DisplayRegion& region) {
    return region.x >= 0 && region.y >= 0 && region.w > 0 && region.h > 0 &&
           region.x + region.w <= EINK_WIDTH && region.y + region.h <= EINK_HEIGHT;
}

constexpr DisplayRegion kStatusRegion{0, 0, EINK_WIDTH, 42};
constexpr DisplayRegion kLaneRegions[transitink::kWidgetsPerPage] = {
    {0, 42, EINK_WIDTH, 57},
    {0, 99, EINK_WIDTH, 57},
    {0, 156, EINK_WIDTH, 57},
    {0, 213, EINK_WIDTH, 57},
};
constexpr DisplayRegion kFooterRegion{0, 270, EINK_WIDTH, 30};
constexpr int kCompositeHeaderHeight = 36;
constexpr DisplayRegion kCommuteHeaderRegion{0, 0, EINK_WIDTH,
                                             kCompositeHeaderHeight};
constexpr DisplayRegion kCommuteBodyRegion{0, kCompositeHeaderHeight,
                                           EINK_WIDTH,
                                           EINK_HEIGHT - kCompositeHeaderHeight};
constexpr DisplayRegion kCommuteHeroRegion{0, kCompositeHeaderHeight,
                                           EINK_WIDTH, 60};
constexpr DisplayRegion kCommuteRouteARegion{
    0, kCommuteHeroRegion.y + kCommuteHeroRegion.h, EINK_WIDTH, 126};
constexpr DisplayRegion kCommuteRouteBRegion{
    0, kCommuteRouteARegion.y + kCommuteRouteARegion.h, EINK_WIDTH, 42};
constexpr DisplayRegion kCommuteFooterRegion{
    0, kCommuteRouteBRegion.y + kCommuteRouteBRegion.h, EINK_WIDTH,
    EINK_HEIGHT - (kCommuteRouteBRegion.y + kCommuteRouteBRegion.h)};
constexpr DisplayRegion kForecastHeaderRegion{0, 0, EINK_WIDTH,
                                              kCompositeHeaderHeight};
constexpr DisplayRegion kForecastCurrentRegion{0, kCompositeHeaderHeight, 148,
                                               EINK_HEIGHT - kCompositeHeaderHeight};
constexpr DisplayRegion kForecastGridRegion{148, kCompositeHeaderHeight,
                                            EINK_WIDTH - 148,
                                            EINK_HEIGHT - kCompositeHeaderHeight};
static_assert(regionFitsPanel(kStatusRegion), "status region bounds");
static_assert(regionFitsPanel(kLaneRegions[0]), "lane 0 region bounds");
static_assert(regionFitsPanel(kLaneRegions[1]), "lane 1 region bounds");
static_assert(regionFitsPanel(kLaneRegions[2]), "lane 2 region bounds");
static_assert(regionFitsPanel(kLaneRegions[3]), "lane 3 region bounds");
static_assert(regionFitsPanel(kFooterRegion), "footer region bounds");
static_assert(kStatusRegion.y + kStatusRegion.h == kLaneRegions[0].y, "status/lane boundary");
static_assert(kLaneRegions[0].y + kLaneRegions[0].h == kLaneRegions[1].y, "lane 0/1 boundary");
static_assert(kLaneRegions[1].y + kLaneRegions[1].h == kLaneRegions[2].y, "lane 1/2 boundary");
static_assert(kLaneRegions[2].y + kLaneRegions[2].h == kLaneRegions[3].y, "lane 2/3 boundary");
static_assert(kLaneRegions[3].y + kLaneRegions[3].h == kFooterRegion.y, "lane/footer boundary");
static_assert(kFooterRegion.y + kFooterRegion.h == EINK_HEIGHT, "dashboard height");
constexpr bool compositeRegionsFitPanel() {
    return regionFitsPanel(kCommuteHeaderRegion) &&
           regionFitsPanel(kCommuteBodyRegion) &&
           regionFitsPanel(kCommuteHeroRegion) &&
           regionFitsPanel(kCommuteRouteARegion) &&
           regionFitsPanel(kCommuteRouteBRegion) &&
           regionFitsPanel(kCommuteFooterRegion) &&
           regionFitsPanel(kForecastHeaderRegion) &&
           regionFitsPanel(kForecastCurrentRegion) &&
           regionFitsPanel(kForecastGridRegion);
}
static_assert(compositeRegionsFitPanel(), "composite dashboard region bounds");
static_assert(kCommuteHeaderRegion.y + kCommuteHeaderRegion.h ==
                  kCommuteBodyRegion.y,
              "commute header/body boundary");
static_assert(kCommuteBodyRegion.y + kCommuteBodyRegion.h == EINK_HEIGHT,
              "commute dashboard height");
static_assert(kCommuteHeroRegion.y == kCommuteBodyRegion.y &&
                  kCommuteFooterRegion.y + kCommuteFooterRegion.h ==
                      EINK_HEIGHT,
              "commute dashboard sub-region alignment");
static_assert(kForecastCurrentRegion.x + kForecastCurrentRegion.w ==
                  kForecastGridRegion.x,
              "forecast current/grid boundary");
static_assert(kForecastCurrentRegion.y == kForecastGridRegion.y &&
                  kForecastCurrentRegion.h == kForecastGridRegion.h,
              "forecast body alignment");

const DisplayRegion& laneRegion(uint8_t slot) {
    return kLaneRegions[slot];
}

constexpr float kForceFullPartialDiffRatio = 0.30f;

uint8_t frameBuffer[kFrameBufferSize];
uint8_t previousFrameBuffer[kFrameBufferSize];
BatteryMonitor batteryMonitor;
bool previousFrameValid = false;
bool dashboardFrameActive = false;
int partialRefreshCount = 0;

enum class DashboardFrameKind : uint8_t { None, Classic, Commute, Forecast };

DashboardFrameKind dashboardFrameKind = DashboardFrameKind::None;

struct PartialDiffStats {
    uint32_t changedBits = 0;
    uint32_t totalBits = 0;

    float ratio() const {
        return totalBits == 0 ? 0.0f : static_cast<float>(changedBits) / static_cast<float>(totalBits);
    }
};

class MonoCanvas : public Adafruit_GFX {
public:
    MonoCanvas() : Adafruit_GFX(EINK_WIDTH, EINK_HEIGHT) {}

    void clear() {
        std::memset(frameBuffer, 0xFF, sizeof(frameBuffer));
    }

    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (x < 0 || y < 0 || x >= EINK_WIDTH || y >= EINK_HEIGHT) {
            return;
        }
        size_t index = static_cast<size_t>(y) * kBytesPerRow + static_cast<size_t>(x / 8);
        uint8_t mask = static_cast<uint8_t>(0x80 >> (x & 7));
        if (color == kColorBlack) {
            frameBuffer[index] &= static_cast<uint8_t>(~mask);
        } else {
            frameBuffer[index] |= mask;
        }
    }
};

MonoCanvas canvas;
transitink::hardware::SelectedDisplayDriver panel;

bool isUtf8Continuation(uint8_t value) {
    return (value & 0xC0) == 0x80;
}

uint16_t decodeUtf8Codepoint(const char* text, size_t length, size_t& offset) {
    uint8_t first = static_cast<uint8_t>(text[offset]);
    if (first < 0x80) {
        ++offset;
        return first;
    }
    if ((first & 0xE0) == 0xC0 && offset + 1 < length) {
        uint8_t second = static_cast<uint8_t>(text[offset + 1]);
        if (isUtf8Continuation(second)) {
            offset += 2;
            return static_cast<uint16_t>(((first & 0x1F) << 6) | (second & 0x3F));
        }
    }
    if ((first & 0xF0) == 0xE0 && offset + 2 < length) {
        uint8_t second = static_cast<uint8_t>(text[offset + 1]);
        uint8_t third = static_cast<uint8_t>(text[offset + 2]);
        if (isUtf8Continuation(second) && isUtf8Continuation(third)) {
            offset += 3;
            return static_cast<uint16_t>(((first & 0x0F) << 12) | ((second & 0x3F) << 6) |
                                         (third & 0x3F));
        }
    }
    ++offset;
    return '?';
}

uint16_t drawCustomGlyph(int x, int y, uint16_t codepoint,
                         uint16_t color = kColorBlack,
                         uint8_t scale = 1) {
    const HkGlyph* glyph = findHkGlyph(codepoint);
    if (glyph == nullptr) {
        return 0;
    }
    const int top = y - kCustomGlyphBaselineOffset * scale;
    for (int row = 0; row < kCustomGlyphHeight; ++row) {
        uint16_t bits = glyph->rows[row];
        for (int col = 0; col < glyph->width; ++col) {
            if ((bits & (0x8000 >> col)) != 0) {
                if (scale == 1) {
                    canvas.drawPixel(x + col, top + row, color);
                } else {
                    canvas.fillRect(x + col * scale, top + row * scale,
                                    scale, scale, color);
                }
            }
        }
    }
    return static_cast<uint16_t>(glyph->width * scale);
}

int displayCodepointWidth(uint32_t codepoint, void*) {
    if (codepoint > 0xFFFFU || codepoint == '\r' || codepoint == '\n') {
        return 0;
    }
    const uint16_t bmpCodepoint = static_cast<uint16_t>(codepoint);
    const HkGlyph* customGlyph = findHkGlyph(bmpCodepoint);
    if (customGlyph != nullptr) {
        return customGlyph->width;
    }
    return 0;
}

int measureTextWidth(const String& text) {
    int width = 0;
    const char* raw = text.c_str();
    const size_t length = text.length();
    for (size_t offset = 0; offset < length;) {
        const uint16_t codepoint = decodeUtf8Codepoint(raw, length, offset);
        if (codepoint == '\r' || codepoint == '\n') {
            continue;
        }
        const HkGlyph* customGlyph = findHkGlyph(codepoint);
        if (customGlyph != nullptr) {
            width += customGlyph->width;
        }
    }
    return width;
}

void drawTextColor(int x, int y, const String& text, uint16_t color,
                   uint8_t scale = 1) {
    int cursorX = x;
    const char* raw = text.c_str();
    size_t length = text.length();
    for (size_t offset = 0; offset < length;) {
        uint16_t codepoint = decodeUtf8Codepoint(raw, length, offset);
        if (codepoint == '\r' || codepoint == '\n') {
            continue;
        }
        const uint16_t width =
            drawCustomGlyph(cursorX, y, codepoint, color, scale);
        if (width > 0) {
            cursorX += width;
        }
    }
}

void drawText(int x, int y, const String& text) {
    int cursorX = x;
    const char* raw = text.c_str();
    const size_t length = text.length();
    for (size_t offset = 0; offset < length;) {
        const uint16_t codepoint = decodeUtf8Codepoint(raw, length, offset);
        if (codepoint == '\r' || codepoint == '\n') {
            continue;
        }
        const uint16_t width = drawCustomGlyph(cursorX, y, codepoint);
        if (width > 0) {
            cursorX += width;
        }
    }
}

void drawAsciiText(int x, int y, const String& text, uint8_t size = 1,
                   uint16_t color = kColorBlack) {
    canvas.setFont(nullptr);
    canvas.setTextWrap(false);
    canvas.setTextSize(size);
    canvas.setTextColor(color);
    canvas.setCursor(x, y);
    canvas.print(text);
}

transitink::DisplayTextPlan displayTextPlan(const String& text, int maxWidth) {
    transitink::DisplayTextPlan plan;
    if (maxWidth <= 0) {
        return plan;
    }
    std::string source;
    source.reserve(text.length());
    source.assign(text.c_str(), text.length());
    return transitink::planTruncatedUtf8(
        source, maxWidth, displayCodepointWidth, nullptr);
}

void drawTruncatedText(int x, int y, const String& text, int maxWidth) {
    const transitink::DisplayTextPlan plan = displayTextPlan(text, maxWidth);
    if (!plan.text.empty()) {
        drawText(x, y, String(plan.text.c_str()));
    }
}

void drawRightAlignedTruncatedText(int rightX, int y, const String& text,
                                   int maxWidth) {
    const transitink::DisplayTextPlan plan = displayTextPlan(text, maxWidth);
    if (!plan.text.empty()) {
        drawText(rightX - plan.pixelWidth, y, String(plan.text.c_str()));
    }
}

String compactLaneTitle(const String& title, int maxWidth) {
    if (transitink::currentUiLocale() != transitink::UiLocale::EnGb ||
        measureTextWidth(title) <= maxWidth) {
        return title;
    }
    const std::string source(title.c_str(), title.length());
    const std::string compact =
        transitink::withoutTrailingParentheticalQualifier(source);
    return compact == source ? title : String(compact.c_str());
}

int adaptiveLaneTextWidth(const String& firstValue, int firstValueRight) {
    const transitink::DisplayTextPlan plan =
        displayTextPlan(firstValue, kLaneValueWidth);
    if (plan.text.empty()) {
        return kLaneBaseTextWidth;
    }
    const int available =
        firstValueRight - plan.pixelWidth - kLaneColumnGap - kLaneTextX;
    return std::max(kLaneBaseTextWidth, available);
}

void drawWifiIcon(int x, int y, bool connected,
                  uint16_t color = kColorBlack) {
    for (int i = 0; i < 3; ++i) {
        int height = 4 + i * 4;
        int barX = x + i * 6;
        int barY = y + 14 - height;
        canvas.drawRect(barX, barY, 4, height, color);
        if (connected) {
            canvas.fillRect(barX + 1, barY + 1, 2, height - 2, color);
        }
    }
    if (!connected) {
        canvas.drawLine(x, y + 14, x + 18, y, color);
    }
}

void drawChargingBolt(int x, int y, uint16_t color = kColorBlack) {
    canvas.drawLine(x + 5, y, x + 1, y + 7, color);
    canvas.drawLine(x + 1, y + 7, x + 6, y + 7, color);
    canvas.drawLine(x + 6, y + 7, x + 3, y + 14, color);
}

void drawBatteryIcon(int x, int y, const bus_eta::BatterySnapshot& status,
                     uint16_t color = kColorBlack) {
    canvas.drawRect(x, y + 3, 24, 12, color);
    canvas.fillRect(x + 24, y + 7, 3, 4, color);
    if (status.valid) {
        const int fillWidth = (status.percent * 18 + 99) / 100;
        if (fillWidth > 0) {
            canvas.fillRect(x + 3, y + 6, fillWidth, 6, color);
        }
    } else {
        canvas.drawLine(x + 3, y + 14, x + 21, y + 4, color);
    }
    if (status.full) {
        canvas.fillRect(x + 3, y + 6, 18, 6, color);
    }
    if (status.charging) {
        drawChargingBolt(x - 10, y + 3, color);
    }
}

void drawStatusBar() {
    bus_eta::BatterySnapshot status = batteryMonitor.read();
    drawWifiIcon(EINK_WIDTH - 72, 8, WiFi.status() == WL_CONNECTED);
    drawBatteryIcon(EINK_WIDTH - 36, 7, status);
}

String batteryStatusText() {
    bus_eta::BatterySnapshot status = batteryMonitor.read();
    if (!status.valid) {
        return transitink::uiText(transitink::UiTextId::BatteryUnavailable);
    }
    String text =
        String(transitink::uiText(transitink::UiTextId::BatteryLabel)) +
        String(status.percent) + "%";
    if (status.full) {
        return text + transitink::uiText(transitink::UiTextId::BatteryFull);
    }
    if (status.charging) {
        return text +
               transitink::uiText(transitink::UiTextId::BatteryCharging);
    }
    if (status.powerPresent) {
        return text +
               transitink::uiText(transitink::UiTextId::BatteryExternalPower);
    }
    return text;
}

void drawMultilineText(int x, int y, const String& text, int lineHeight = 28) {
    int start = 0;
    int lineY = y;
    while (static_cast<std::size_t>(start) <= text.length()) {
        int newline = text.indexOf('\n', start);
        String line = newline >= 0 ? text.substring(start, newline) : text.substring(start);
        drawText(x, lineY, line);
        if (newline < 0) {
            break;
        }
        start = newline + 1;
        lineY += lineHeight;
    }
}

void drawQrCode(int x, int y, const String& text) {
    if (text.isEmpty()) {
        return;
    }
    constexpr uint8_t kQrVersion = 3;
    constexpr uint8_t kQrScale = 4;
    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(kQrVersion)];
    qrcode_initText(&qrcode, qrcodeData, kQrVersion, ECC_LOW, text.c_str());
    const int quiet = kQrScale * 2;
    const int size = qrcode.size * kQrScale + quiet * 2;
    canvas.fillRect(x, y, size, size, kColorWhite);
    canvas.drawRect(x, y, size, size, kColorBlack);
    for (uint8_t row = 0; row < qrcode.size; ++row) {
        for (uint8_t col = 0; col < qrcode.size; ++col) {
            if (qrcode_getModule(&qrcode, col, row)) {
                canvas.fillRect(x + quiet + col * kQrScale, y + quiet + row * kQrScale, kQrScale, kQrScale, kColorBlack);
            }
        }
    }
}

String currentClockText() {
    struct tm tmInfo;
    const time_t now = time(nullptr);
    localtime_r(&now, &tmInfo);
    char buffer[32];
    if (transitink::currentUiLocale() == transitink::UiLocale::EnGb) {
        snprintf(buffer, sizeof(buffer), "%s %02d/%02d %02d:%02d",
                 transitink::weekdayText(tmInfo.tm_wday), tmInfo.tm_mday,
                 tmInfo.tm_mon + 1, tmInfo.tm_hour, tmInfo.tm_min);
    } else {
        snprintf(buffer, sizeof(buffer), "%02d/%02d %s %02d:%02d",
                 tmInfo.tm_mon + 1, tmInfo.tm_mday,
                 transitink::weekdayText(tmInfo.tm_wday), tmInfo.tm_hour,
                 tmInfo.tm_min);
    }
    return String(buffer);
}

void drawTruncatedTextColor(int x, int y, const String& text, int maxWidth,
                            uint16_t color) {
    const transitink::DisplayTextPlan plan = displayTextPlan(text, maxWidth);
    if (!plan.text.empty()) {
        drawTextColor(x, y, String(plan.text.c_str()), color);
    }
}

const char* shortEnglishWeekday(int weekday) {
    static constexpr const char* kWeekdays[] = {
        "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT",
    };
    return weekday >= 0 && weekday < 7 ? kWeekdays[weekday] : "---";
}

void hongKongTime(time_t epoch, struct tm& result) {
    const time_t localEpoch = epoch + 8 * 60 * 60;
    gmtime_r(&localEpoch, &result);
}

String currentFullDateText() {
    struct tm tmInfo;
    const time_t now = time(nullptr);
    hongKongTime(now, tmInfo);
    char buffer[24];
    snprintf(buffer, sizeof(buffer), "%04d.%02d.%02d %s",
             tmInfo.tm_year + 1900, tmInfo.tm_mon + 1, tmInfo.tm_mday,
             shortEnglishWeekday(tmInfo.tm_wday));
    return String(buffer);
}

String currentChineseDateText() {
    static constexpr const char* kWeekdays[] = {
        "日", "一", "二", "三", "四", "五", "六",
    };
    struct tm tmInfo;
    const time_t now = time(nullptr);
    hongKongTime(now, tmInfo);
    const char* weekday = tmInfo.tm_wday >= 0 && tmInfo.tm_wday < 7
                              ? kWeekdays[tmInfo.tm_wday]
                              : "－";
    return String(tmInfo.tm_mon + 1) + "月" + String(tmInfo.tm_mday) +
           "日 週" + weekday;
}

String currentTimeText() {
    struct tm tmInfo;
    const time_t now = time(nullptr);
    hongKongTime(now, tmInfo);
    char buffer[8];
    snprintf(buffer, sizeof(buffer), "%02d:%02d", tmInfo.tm_hour,
             tmInfo.tm_min);
    return String(buffer);
}

void drawCompositeHeader(const WeatherSnapshot& weather, uint8_t pageIndex) {
    canvas.fillRect(0, 0, EINK_WIDTH, kCompositeHeaderHeight, kColorBlack);
    drawAsciiText(8, 3, "TRANSIT / INK", 1, kColorWhite);
    drawAsciiText(8, 19, currentFullDateText(), 1, kColorWhite);
    drawWifiIcon(260, 1, WiFi.status() == WL_CONNECTED, kColorWhite);
    drawBatteryIcon(288, 0, batteryMonitor.read(), kColorWhite);
    drawTruncatedTextColor(142, 29, weatherDisplayText(weather), 166,
                           kColorWhite);
    drawAsciiText(316, 17, currentTimeText(), 2, kColorWhite);
    drawAsciiText(378, 3,
                  String(static_cast<unsigned int>(pageIndex) + 1) + "/2",
                  1, kColorWhite);
}

void drawClockAndStatusBar(uint8_t pageIndex = 0, uint8_t pageCount = 1) {
    drawStatusBar();
    drawText(12, 24, currentClockText());
    if (pageCount > 1) {
        drawText(286, 24,
                 String(static_cast<unsigned int>(pageIndex) + 1) + "/" +
                     String(pageCount));
    }
}

void markNonDashboardFrame() {
    dashboardFrameActive = false;
    dashboardFrameKind = DashboardFrameKind::None;
}

void clearRegion(const DisplayRegion& region) {
    canvas.fillRect(region.x, region.y, region.w, region.h, kColorWhite);
}

void drawLaneDivider(const DisplayRegion& region, bool visible) {
    if (visible) {
        canvas.drawFastHLine(region.x, region.y + region.h - 1, region.w, kColorBlack);
    }
}

bool shouldDrawLaneDivider(const transitink::WidgetPageSnapshotSet& snapshots, uint8_t slot) {
    if (slot >= transitink::kWidgetsPerPage ||
        snapshots[slot].type == transitink::WidgetType::Disabled) {
        return false;
    }
    for (uint8_t index = slot + 1; index < transitink::kWidgetsPerPage; ++index) {
        if (snapshots[index].type != transitink::WidgetType::Disabled) {
            return true;
        }
    }
    return false;
}

bool hasEnabledWidget(const transitink::WidgetPageSnapshotSet& snapshots) {
    return std::any_of(
        snapshots.begin(), snapshots.end(), [](const transitink::WidgetSnapshot& snapshot) {
            return snapshot.type != transitink::WidgetType::Disabled;
        });
}

void drawNoWidgetsHint() {
    const String title =
        transitink::uiText(transitink::UiTextId::NoWidgets);
    const String action =
        transitink::uiText(transitink::UiTextId::OpenSettings);
    drawText(std::max(12, (EINK_WIDTH - measureTextWidth(title)) / 2), 138, title);
    drawText(std::max(12, (EINK_WIDTH - measureTextWidth(action)) / 2), 174, action);
}

void drawWidgetLane(uint8_t slot, const transitink::WidgetSnapshot& snapshot, bool sleeping, bool drawDivider) {
    const DisplayRegion& region = laneRegion(slot);
    if (snapshot.type == transitink::WidgetType::Disabled) {
        return;
    }

    const std::size_t valueLimit = snapshot.type == transitink::WidgetType::JourneyTime ? 1U : 2U;
    const int firstValueRight =
        valueLimit == 1U ? kLaneRightEdge
                         : kLaneRightEdge - kLaneValueRightSpacing;
    const int valueY = region.y + 20;
    const int contextY = region.y + 42;
    int titleWidth = kLaneBaseTextWidth;
    int subtitleWidth = kLaneBaseTextWidth;

    if (sleeping) {
        titleWidth = adaptiveLaneTextWidth("-", firstValueRight);
        subtitleWidth = titleWidth;
    } else if (snapshot.valueCount > 0) {
        titleWidth = adaptiveLaneTextWidth(
            String(snapshot.values[0].text.c_str()), firstValueRight);
        subtitleWidth = titleWidth;
        if (snapshot.freshness == transitink::Freshness::Stale ||
            snapshot.type == transitink::WidgetType::JourneyTime) {
            subtitleWidth = kLaneBaseTextWidth;
        }
    }

    const String title =
        compactLaneTitle(String(snapshot.title.c_str()), titleWidth);
    drawTruncatedText(kLaneTextX, valueY, title, titleWidth);
    drawTruncatedText(
        kLaneTextX, contextY, String(snapshot.subtitle.c_str()), subtitleWidth);

    if (sleeping) {
        for (std::size_t valueIndex = 0; valueIndex < valueLimit; ++valueIndex) {
            const int valueRight =
                firstValueRight +
                static_cast<int>(valueIndex) * kLaneValueRightSpacing;
            drawRightAlignedTruncatedText(
                valueRight, valueY, "-", kLaneValueWidth);
        }
        drawLaneDivider(region, drawDivider);
        return;
    }

    if (snapshot.freshness == transitink::Freshness::Stale && snapshot.valueCount == 0) {
        drawTruncatedText(
            kLaneValueAreaX, valueY,
            transitink::uiText(transitink::UiTextId::DataUnavailable), 164);
        drawLaneDivider(region, drawDivider);
        return;
    }
    if (snapshot.freshness == transitink::Freshness::Fresh &&
        (snapshot.state == transitink::WidgetState::Empty || snapshot.state == transitink::WidgetState::Error)) {
        const String message = snapshot.providerMessage.empty() && snapshot.fetchedAtEpoch == 0
                                   ? String("...")
                                   : (snapshot.providerMessage.empty()
                                          ? String(transitink::uiText(
                                                transitink::UiTextId::DataUnavailable))
                                          : String(snapshot.providerMessage.c_str()));
        drawTruncatedText(kLaneValueAreaX, valueY, message, 164);
        drawLaneDivider(region, drawDivider);
        return;
    }

    const std::size_t shownValueCount = std::min(snapshot.valueCount, valueLimit);
    for (std::size_t valueIndex = 0; valueIndex < shownValueCount; ++valueIndex) {
        const int valueRight =
            firstValueRight +
            static_cast<int>(valueIndex) * kLaneValueRightSpacing;
        drawRightAlignedTruncatedText(
            valueRight, valueY,
            String(snapshot.values[valueIndex].text.c_str()), kLaneValueWidth);
        if (snapshot.type == transitink::WidgetType::JourneyTime &&
            snapshot.freshness == transitink::Freshness::Fresh) {
            drawTruncatedText(
                kLaneValueAreaX, contextY,
                String(snapshot.values[valueIndex].context.c_str()),
                kLaneRightEdge - kLaneValueAreaX);
        }
    }
    if (snapshot.freshness == transitink::Freshness::Stale) {
        drawTruncatedText(
            kLaneValueAreaX, contextY,
            transitink::uiText(transitink::UiTextId::DataExpired), 164);
    }
    drawLaneDivider(region, drawDivider);
}

void drawWeatherFooter(const WeatherSnapshot& weather) {
    drawTruncatedText(12, 291, weatherDisplayText(weather), 376);
}

String clockForEpoch(int64_t epoch);

String compactForecastCondition(const std::string& conditionTc);

bool currentWeatherStale(const WeatherSnapshot& weather) {
    const time_t now = time(nullptr);
    return weather.updatedAt <= 0 || now <= 0 ||
           now - weather.updatedAt > WEATHER_REFRESH_SECONDS * 2;
}

String commuteRainText(const transitink::ForecastSnapshot& forecast,
                       const WeatherSnapshot& weather) {
    // A condition icon is not a rainfall measurement. In particular, a cloudy
    // icon cannot establish that no rain is falling at the user's location.
    String result = weather.valid
                        ? String("現況") + compactForecastCondition(weather.conditionTc.c_str())
                        : String("現況不明");
    if (weather.valid && currentWeatherStale(weather)) result += "*";
    if (!forecast.valid || forecast.dayCount == 0) return result;
    const time_t now = time(nullptr);
    struct tm today;
    hongKongTime(now, today);
    const transitink::ForecastDay* todayForecast = nullptr;
    for (std::size_t index = 0; index < forecast.dayCount; ++index) {
        if (forecast.days[index].month == today.tm_mon + 1 &&
            forecast.days[index].day == today.tm_mday) {
            todayForecast = &forecast.days[index];
            break;
        }
    }
    // The nine-day product often starts tomorrow; never label it today's PSR.
    if (todayForecast == nullptr) return result;
    String chance(todayForecast->rainChanceTc.c_str());
    if (chance.isEmpty()) chance = "不明";
    result += "／全日顯著雨機會" + chance;
    if (forecast.stale) result += "*";
    return result;
}

String commuteTemperatureText(
    const transitink::ForecastSnapshot& forecast,
    const WeatherSnapshot& weather) {
    if (weather.valid) {
        String result = String(weather.temperatureC) + "°C";
        if (currentWeatherStale(weather)) result += "*";
        return result;
    }
    (void)forecast;
    return String("--°C");
}

void drawCommuteHeader(const transitink::CommuteDashboardSnapshot& snapshot,
                       const transitink::ForecastSnapshot& forecast,
    const WeatherSnapshot& weather) {
    canvas.fillRect(0, 0, EINK_WIDTH, kCompositeHeaderHeight, kColorBlack);
    const String title = snapshot.targetEpoch > 0
                             ? String(snapshot.intent == transitink::CommuteIntent::Special
                                          ? "特別 " : "通勤 ") +
                                   "目標" + clockForEpoch(snapshot.targetEpoch)
                             : String("漁灣邨 現在出門");
    drawTruncatedTextColor(8, 15, title, 178, kColorWhite);
    drawTextColor(8, 32, currentChineseDateText(), kColorWhite);
    drawTruncatedTextColor(188, 15, commuteTemperatureText(forecast, weather),
                           64, kColorWhite);
    drawTruncatedTextColor(126, 32, commuteRainText(forecast, weather), 264,
                           kColorWhite);
    drawWifiIcon(260, 1, WiFi.status() == WL_CONNECTED, kColorWhite);
    drawBatteryIcon(288, 0, batteryMonitor.read(), kColorWhite);
    // Keep the clock on the icon row; the full-width weather text uses row two.
    drawAsciiText(316, 1, currentTimeText(), 2, kColorWhite);
    drawAsciiText(378, 3, "1/2", 1, kColorWhite);
}

String assessmentText(transitink::CommuteAssessment assessment, bool stale) {
    String result;
    switch (assessment) {
        case transitink::CommuteAssessment::Safe:
            result = "預計趕及";
            break;
        case transitink::CommuteAssessment::Tight:
            result = "有風險";
            break;
        case transitink::CommuteAssessment::Late:
            result = "預計遲到";
            break;
        default:
            result = "暫無班次";
            break;
    }
    if (stale) result += "*";
    return result;
}

bool saferConnectionSelected(const transitink::CommuteRouteEstimate& route) {
    return route.primary.valid && route.nominalFastest.valid &&
        route.primary.arrivalEpoch > route.nominalFastest.arrivalEpoch;
}

String arrivalRiskText(const transitink::CommuteDashboardSnapshot& snapshot,
                       const transitink::CommuteRouteEstimate& route) {
    if (snapshot.targetEpoch <= 0) return String();
    if (route.riskAssessment == transitink::CommuteAssessment::Late)
        return "慢車約" + clockForEpoch(route.primary.riskArrivalEpoch) + "到／或未趕及目標";
    if (route.riskAssessment == transitink::CommuteAssessment::Tight &&
        route.assessment == transitink::CommuteAssessment::Safe)
        return "慢車約" + clockForEpoch(route.primary.riskArrivalEpoch) + "到／目標餘裕不足";
    return String();
}

String routeAStateText(
    const transitink::CommuteDashboardSnapshot& snapshot) {
    if (snapshot.routeAContext.severity == transitink::ContextSeverity::HardBlock &&
        !snapshot.routeA.primary.valid) return "正常轉車受阻";
    if (snapshot.routeA.primary.contextDegraded) return "改道／步程待核實";

    String result;
    // Keep the nominal assessment separate; show a known slower-scenario miss
    // even when the chosen recommendation is the other route.
    if (snapshot.targetEpoch > 0 && snapshot.routeA.primary.valid) {
        if (snapshot.routeA.riskAssessment == transitink::CommuteAssessment::Late)
            return snapshot.routeA.primary.connectionAtRisk ? "轉車／慢車或遲到" : "慢車或遲到";
        if (snapshot.routeA.riskAssessment == transitink::CommuteAssessment::Tight &&
            snapshot.routeA.assessment == transitink::CommuteAssessment::Safe)
            return "慢車餘裕不足";
    }
    switch (snapshot.routeAStatus.state) {
        case transitink::CommuteRouteAState::ConfirmedPair:
            if (snapshot.routeA.primary.firstLegScheduled ||
                snapshot.routeA.primary.connectionScheduled) return "含表定班次";
            if (snapshot.routeA.primary.connectionAtRisk) return "轉車有風險";
            if (snapshot.targetEpoch > 0 && saferConnectionSelected(snapshot.routeA))
                return "較穩妥轉車";
            return snapshot.targetEpoch <= 0
                       ? String("可轉車")
                       : assessmentText(snapshot.routeA.assessment,
                                        snapshot.routeA.stale);
        case transitink::CommuteRouteAState::ProvisionalTransfer:
            result = "預計轉車";
            break;
        case transitink::CommuteRouteAState::TransferPending:
            result = "轉車待確認";
            break;
        case transitink::CommuteRouteAState::NoService:
            result = "未有服務";
            break;
        case transitink::CommuteRouteAState::Stale:
            result = "資料過時";
            break;
        default:
            result = "資料暫缺";
            break;
    }
    if (snapshot.routeA.stale) result += "*";
    return result;
}

String targetMarginText(
    const transitink::CommuteDashboardSnapshot& snapshot,
    const transitink::CommuteTripEstimate& trip) {
    if (!trip.valid) return String("未能估算");
    if (snapshot.targetEpoch <= 0) return String("只供估算");
    const int64_t seconds = snapshot.targetEpoch - trip.arrivalEpoch;
    if (seconds >= 0) {
        return String("早") + String(static_cast<long>(seconds / 60)) + "分";
    }
    return String("遲") +
           String(static_cast<long>((-seconds + 59) / 60)) + "分";
}

String leaveHomeText(const transitink::CommuteTripEstimate& trip) {
    if (!trip.valid && !trip.firstLegValid) return String("--:--");
    const int64_t now = static_cast<int64_t>(time(nullptr));
    return trip.leaveHomeEpoch <= now + 60 ? String("立即")
                                           : clockForEpoch(trip.leaveHomeEpoch);
}

void drawCommuteHero(const transitink::CommuteDashboardSnapshot& snapshot) {
    canvas.fillRect(kCommuteHeroRegion.x, kCommuteHeroRegion.y,
                    kCommuteHeroRegion.w, kCommuteHeroRegion.h, kColorBlack);
    if (!transitink::isActiveCommuteSession(snapshot.sessionMode)) {
        drawTextColor(12, kCommuteHeroRegion.y + 31,
                      String("自動通勤暫停"),
                      kColorWhite, 2);
        drawTextColor(12, kCommuteHeroRegion.y + 53,
                      "主頁鍵：隨時查車十分鐘", kColorWhite);
        return;
    }

    const transitink::CommuteRouteEstimate* route = nullptr;
    String buses;
    if (snapshot.recommendation == transitink::CommuteChoice::RouteA) {
        route = &snapshot.routeA;
        buses = "106→8P";
    } else if (snapshot.recommendation ==
               transitink::CommuteChoice::RouteB) {
        route = &snapshot.routeB;
        buses = "118";
    }

    if (route == nullptr || !route->primary.valid) {
        if (snapshot.routeAContext.severity == transitink::ContextSeverity::HardBlock ||
            snapshot.routeBContext.severity == transitink::ContextSeverity::HardBlock) {
            drawTextColor(12,kCommuteHeroRegion.y+31,"正常路線受影響",kColorWhite,2);
            drawTextColor(12,kCommuteHeroRegion.y+53,"停站安排已改，請查閱官方通告",kColorWhite);
            return;
        }
        if (snapshot.routeAStatus.state ==
                transitink::CommuteRouteAState::TransferPending &&
            snapshot.routeA.primary.firstLegValid) {
            drawTextColor(12, kCommuteHeroRegion.y + 31,
                          "轉車班次待確認", kColorWhite, 2);
            drawTruncatedTextColor(
                12, kCommuteHeroRegion.y + 53,
                String("106 ") +
                    clockForEpoch(snapshot.routeA.primary.firstBusEpoch) +
                    "／" +
                    clockForEpoch(
                        snapshot.routeA.primary.transferArrivalEpoch) +
                    "到維園",
                376, kColorWhite);
            return;
        }
        if (snapshot.routeAStatus.state ==
            transitink::CommuteRouteAState::NoService) {
            drawTextColor(12, kCommuteHeroRegion.y + 31,
                          "8P未有服務", kColorWhite, 2);
            drawTextColor(12, kCommuteHeroRegion.y + 53,
                          "請查看其他路線", kColorWhite);
            return;
        }
        if (snapshot.routeAStatus.state ==
                transitink::CommuteRouteAState::Stale ||
            snapshot.routeAStatus.state ==
                transitink::CommuteRouteAState::DataUnavailable) {
            drawTextColor(12, kCommuteHeroRegion.y + 31,
                          "即時資料暫缺", kColorWhite, 2);
            drawTextColor(12, kCommuteHeroRegion.y + 53,
                          "資料不足不等同沒有服務", kColorWhite);
            return;
        }
        drawTextColor(12, kCommuteHeroRegion.y + 31,
                       "暫無可靠即時路線", kColorWhite, 2);
        drawTextColor(12, kCommuteHeroRegion.y + 53,
                      "請提早出門並查看實際班次", kColorWhite);
        return;
    }

    const bool recovery = snapshot.sessionMode ==
                          transitink::CommuteSessionMode::AutomaticRecovery;
    drawTextColor(10, kCommuteHeroRegion.y + 18,
                  route->primary.contextDegraded ? String("步程改動／待核實") : recovery && snapshot.targetEpoch > 0
                      ? String("趕路／預計最快") : String("建議／預計"),
                  kColorWhite);
    drawTextColor(10, kCommuteHeroRegion.y + 52, buses, kColorWhite, 2);
    drawTextColor(200, kCommuteHeroRegion.y + 16, "趕此班出門", kColorWhite);
    drawTextColor(200, kCommuteHeroRegion.y + 34,
                  leaveHomeText(route->primary), kColorWhite);
    drawTextColor(310, kCommuteHeroRegion.y + 16, "預計到達", kColorWhite);
    drawTextColor(310, kCommuteHeroRegion.y + 34,
                  clockForEpoch(route->primary.arrivalEpoch), kColorWhite);
    drawTruncatedTextColor(
        200, kCommuteHeroRegion.y + 53,
        targetMarginText(snapshot, route->primary) + "／非保證",
        190, kColorWhite);
}

String connectionLabel(const transitink::CommuteDashboardSnapshot& snapshot,
                       std::size_t row) {
    const auto& eta = snapshot.etas[1];
    const auto& trip = snapshot.routeA.primary;
    if (eta.contextImpacts[row].severity == transitink::ContextSeverity::HardBlock)
        return "停站受影響";
    if (eta.rowStale[row] || eta.stale) return "舊資料，待更新";
    if (trip.contextDegraded) return "步程已改，接駁待核實";
    if (!trip.firstLegValid) return "可見班次，接駁待確認";
    const auto bus = eta.epochs[row];
    if (bus < trip.transferReadyEpoch) return "較早，未能轉乘";
    if (trip.connectionConfirmed && bus == trip.connectionEpoch)
        return trip.connectionAtRisk ? "選乘這班／較趕" : "選乘這班";
    if (bus < trip.riskTransferReadyEpoch + snapshot.connectionEarlyAllowanceMinutes*60) return "可接但較趕";
    if (trip.valid && bus > trip.connectionEpoch) return "後備接駁";
    return "可考慮接駁";
}

void drawCommuteRouteA(const DisplayRegion& region,
    const transitink::CommuteDashboardSnapshot& snapshot) {
    const auto& trip = snapshot.routeA.primary;
    const int64_t now = static_cast<int64_t>(time(nullptr));
    canvas.drawFastHLine(region.x, region.y + region.h - 1, region.w, kColorBlack);
    drawTruncatedText(10, region.y + 16, "A 106→8P 紅磡→維園", 206);
    drawRightAlignedTruncatedText(390, region.y + 16, routeAStateText(snapshot), 174);
    String first = "106 "; unsigned shown = 0;
    const auto& bus106 = snapshot.etas[0];
    for (std::size_t i=0; i<bus106.count && shown<2; ++i) {
        if (bus106.epochs[i] <= now) continue;
        if (shown++) first += "/";
        first += clockForEpoch(bus106.epochs[i]);
    }
    if (!shown) first += trip.firstLegValid ? clockForEpoch(trip.firstBusEpoch) : String("資料暫缺");
    if (trip.firstLegValid) first += " 選" + clockForEpoch(trip.firstBusEpoch) +
        "→" + clockForEpoch(trip.transferReadyEpoch) + "可轉";
    drawTruncatedText(10, region.y + 34, first, 380);
    const auto& bus8p = snapshot.etas[1]; shown = 0;
    for (std::size_t i=0; i<bus8p.count && shown<3; ++i) {
        if (bus8p.epochs[i] <= now) continue;
        const String line = String(bus8p.scheduled[i] ? "8P表定 " : "8P ") +
            clockForEpoch(bus8p.epochs[i]) + "　" + connectionLabel(snapshot,i);
        drawTruncatedText(10, region.y + 52 + shown*18, line, 380); ++shown;
    }
    if (shown == 0) drawTruncatedText(10,region.y+52,
        snapshot.routeAStatus.reason == transitink::CommuteRouteAReason::TimetableHeadway ||
        snapshot.routeAStatus.reason == transitink::CommuteRouteAReason::TransferBeyondEtaHorizon
            ? "8P未入即時範圍；班距暫估" : "8P即時班次暫缺",380);
    String detail = trip.valid ? "轉乘" + clockForEpoch(trip.connectionEpoch) +
        "／約" + clockForEpoch(trip.arrivalEpoch) + "到達" : String("接駁及到達待確認");
    if (trip.provisional) detail += "／班距暫估";
    else if (trip.valid) detail += "／暫行估算";
    drawTruncatedText(10,region.y+106,detail,380);
    String warning;
    if (snapshot.routeAContext.severity != transitink::ContextSeverity::None) {
        warning = snapshot.crowding8p ? String("停站有改動；8P今日或較擠迫") : String(snapshot.routeAContext.messageTc.c_str());
    } else if (snapshot.crowding8p) warning = "今日有運動會；8P上車或較困難";
    else if (trip.connectionAtRisk) warning = "轉車風險：8P或早到／106或較慢";
    else if (snapshot.contextStale) warning = "交通通告資料未更新";
    else warning = "交通通告覆蓋有限，留意站牌";
    drawTruncatedText(10,region.y+122,warning,380);
}

void drawCommuteRouteB(const DisplayRegion& region,
    const transitink::CommuteDashboardSnapshot& snapshot) {
    const auto& route = snapshot.routeB;
    canvas.drawFastHLine(region.x,region.y+region.h-1,region.w,kColorBlack);
    if (!route.primary.valid) {
        drawText(10,region.y+17,"B 118　暫未有可用班次");
        drawTruncatedText(10,region.y+35,snapshot.routeBContext.severity == transitink::ContextSeverity::HardBlock
            ? String(snapshot.routeBContext.messageTc.c_str()) : String("即時資料待確認；並非確認停駛"),380);
        return;
    }
    drawTruncatedText(10,region.y+17,"B 118 " + clockForEpoch(route.primary.firstBusEpoch) +
        "　出門 " + leaveHomeText(route.primary) + "　約" + clockForEpoch(route.primary.arrivalEpoch) + "到",380);
    drawTruncatedText(10,region.y+35,
        snapshot.routeBContext.severity != transitink::ContextSeverity::None ? String(snapshot.routeBContext.messageTc.c_str()) :
        String("行車估算35分／未校準　") + (snapshot.targetEpoch <= 0 ? String("隨時查車") : assessmentText(route.assessment,route.stale)),380);
}

String sourceAgeText(const transitink::CommuteEtaSnapshot& eta) {
    if (eta.stale) return "資料過時";
    if (eta.partial) return "部分營辦商資料暫缺";
    const int64_t age = eta.fetchedAtEpoch > 0 ? std::max<int64_t>(0,time(nullptr)-eta.fetchedAtEpoch) : -1;
    if (age < 0) return "資料暫缺";
    return age < 60 ? String("資料更新 <1分") : String("資料更新 ") + String(static_cast<long>(age/60)) + "分";
}

void drawCommuteFooter(const transitink::CommuteDashboardSnapshot& snapshot) {
    const auto* selected = snapshot.recommendation == transitink::CommuteChoice::RouteA ? &snapshot.routeA :
        snapshot.recommendation == transitink::CommuteChoice::RouteB ? &snapshot.routeB : nullptr;
    String line = selected ? arrivalRiskText(snapshot,*selected) : String();
    if (selected && snapshot.targetEpoch > 0 && selected->assessment == transitink::CommuteAssessment::Late)
        line = "預計未能趕及" + clockForEpoch(snapshot.targetEpoch) + "目標";
    if (line.isEmpty() && selected && selected->primary.provisional) line="8P班距估算，尚未即時確認";
    if (line.isEmpty() && selected && selected->primary.contextDegraded) line="停站有改動，正常步程待核實";
    if (line.isEmpty()) {
        bool partial=false, stale=false;
        for (const auto& eta:snapshot.etas) {partial=partial||eta.partial;stale=stale||eta.stale;}
        line=stale?String("部分資料過時，留意實際班次"):partial?String("部分營辦商資料暫缺"):
            !snapshot.dateHint.empty()?String(snapshot.dateHint.c_str()):sourceAgeText(snapshot.etas[0]);
        if (!partial && !stale && snapshot.dateHint.empty() && selected && selected->fallback.valid)
            line += "／錯過首班約" + clockForEpoch(selected->fallback.arrivalEpoch) + "到";
    }
    drawTruncatedText(10,kCommuteFooterRegion.y+15,line,380);
    drawTruncatedText(10,kCommuteFooterRegion.y+32,"主頁查車　上鍵設定　下鍵天氣",380);
}

void drawCommuteBody(const transitink::CommuteDashboardSnapshot& snapshot) {
    drawCommuteHero(snapshot);
    drawCommuteRouteA(kCommuteRouteARegion, snapshot);
    drawCommuteRouteB(kCommuteRouteBRegion, snapshot);
    drawCommuteFooter(snapshot);
}

String forecastTemperatureText(const transitink::ForecastDay& day) {
    return String(static_cast<int>(day.minimumC)) + "/" +
           String(static_cast<int>(day.maximumC));
}

String forecastHumidityText(const transitink::ForecastDay& day) {
    if (day.minimumHumidity == 0 && day.maximumHumidity == 0) {
        return String();
    }
    return String(static_cast<unsigned int>(day.minimumHumidity)) + "-" +
           static_cast<unsigned int>(day.maximumHumidity);
}

String compactForecastCondition(const std::string& conditionTc) {
    const String condition(conditionTc.c_str());
    struct ConditionLabel {
        const char* match;
        const char* compact;
    };
    static constexpr ConditionLabel kLabels[] = {
        {"雷暴", "雷暴"}, {"大雨", "大雨"}, {"驟雨", "驟雨"},
        {"微雨", "微雨"}, {"密雲", "密雲"}, {"多雲", "多雲"},
        {"薄霧", "薄霧"}, {"有霧", "有霧"}, {"酷熱", "酷熱"},
        {"溫暖", "溫暖"}, {"清涼", "清涼"}, {"寒冷", "寒冷"},
        {"乾燥", "乾燥"}, {"潮濕", "潮濕"}, {"大風", "大風"},
        {"陽光", "晴"},   {"良好", "晴"},   {"雨", "雨"},
    };
    for (const auto& label : kLabels) {
        if (condition.indexOf(label.match) >= 0) {
            return String(label.compact);
        }
    }
    return condition;
}

String forecastDayHeader(const transitink::ForecastDay& day) {
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%s / %02u",
             shortEnglishWeekday(day.weekday),
             static_cast<unsigned int>(day.day));
    return String(buffer);
}

String clockForEpoch(int64_t epoch) {
    if (epoch <= 0) return String("--:--");
    const time_t value = static_cast<time_t>(epoch);
    struct tm tmInfo;
    hongKongTime(value, tmInfo);
    char buffer[8];
    snprintf(buffer, sizeof(buffer), "%02d:%02d", tmInfo.tm_hour,
             tmInfo.tm_min);
    return String(buffer);
}

void drawWeatherSymbol(int x, int y, const String& condition,
                       uint16_t color) {
    const bool rain = condition.indexOf("雨") >= 0 ||
                      condition.indexOf("雷") >= 0;
    if (rain) {
        canvas.drawCircle(x + 11, y + 11, 8, color);
        canvas.drawCircle(x + 22, y + 8, 10, color);
        canvas.drawCircle(x + 33, y + 12, 8, color);
        canvas.drawFastHLine(x + 5, y + 18, 35, color);
        canvas.drawLine(x + 10, y + 24, x + 7, y + 31, color);
        canvas.drawLine(x + 22, y + 24, x + 19, y + 31, color);
        canvas.drawLine(x + 34, y + 24, x + 31, y + 31, color);
        return;
    }

    canvas.drawCircle(x + 22, y + 15, 9, color);
    canvas.drawFastHLine(x + 3, y + 15, 8, color);
    canvas.drawFastHLine(x + 33, y + 15, 8, color);
    canvas.drawFastVLine(x + 22, y - 4, 8, color);
    canvas.drawFastVLine(x + 22, y + 26, 8, color);
    canvas.drawLine(x + 8, y + 1, x + 13, y + 6, color);
    canvas.drawLine(x + 31, y + 24, x + 36, y + 29, color);
    canvas.drawLine(x + 8, y + 29, x + 13, y + 24, color);
    canvas.drawLine(x + 31, y + 6, x + 36, y + 1, color);
}

void drawForecastCurrentPanel(const transitink::ForecastSnapshot& forecast,
                              const WeatherSnapshot& weather) {
    canvas.fillRect(kForecastCurrentRegion.x, kForecastCurrentRegion.y,
                    kForecastCurrentRegion.w, kForecastCurrentRegion.h,
                    kColorBlack);
    drawAsciiText(8, 44, "CURRENT / KOWLOON", 1, kColorWhite);
    const String location = weather.locationTc.isEmpty()
                                ? String("九龍城")
                                : weather.locationTc;
    drawTruncatedTextColor(8, 78, location, 132, kColorWhite);

    if (weather.valid) {
        drawAsciiText(8, 91, String(weather.temperatureC), 6, kColorWhite);
        drawTextColor(99, 94, "°C", kColorWhite);
        drawWeatherSymbol(96, 103, weather.conditionTc, kColorWhite);
        drawTruncatedTextColor(8, 154, weather.conditionTc +
                                   (currentWeatherStale(weather) ? "*" : ""), 132,
                               kColorWhite);
    } else {
        drawAsciiText(8, 91, "--", 6, kColorWhite);
        drawTruncatedTextColor(
            8, 154, transitink::uiText(transitink::UiTextId::DataUnavailable),
            132, kColorWhite);
    }

    drawTextColor(8, 174, "實測雨量未提供", kColorWhite);
    canvas.drawFastHLine(8, 183, 132, kColorWhite);
    drawAsciiText(8, 194, "HKO / NEXT 6 DAYS", 1, kColorWhite);
    drawTextColor(8, 223, "全日顯著雨機會", kColorWhite);
    const String status = !forecast.valid
                              ? String("FORECAST / OFFLINE")
                              : (forecast.stale ? String("FORECAST / STALE")
                                                : String("FORECAST / LIVE"));
    drawAsciiText(8, 235, status, 1, kColorWhite);
    drawAsciiText(8, 248, "HKO " + clockForEpoch(forecast.updatedAtEpoch), 1, kColorWhite);
    drawTextColor(8, 274, "非通勤時段預報", kColorWhite);
    drawAsciiText(8, 286,
                  String("NOW ") + clockForEpoch(weather.updatedAt) +
                      (currentWeatherStale(weather) ? "*" : ""), 1, kColorWhite);
}

void drawForecastMatrixCell(const DisplayRegion& region,
                            const transitink::ForecastDay& day) {
    drawAsciiText(region.x + 8, region.y + 6, forecastDayHeader(day), 1);
    const String humidity = forecastHumidityText(day);
    if (!humidity.isEmpty()) {
        drawAsciiText(region.x + 64, region.y + 6,
                      humidity + "%", 1);
    }
    drawTruncatedText(region.x + 8, region.y + 42,
                      compactForecastCondition(day.conditionTc), 48);
    drawTruncatedText(region.x + 62, region.y + 26, "顯著雨", 60);
    const String rainChance = day.rainChanceTc.empty()
                                  ? String("--")
                                  : String(day.rainChanceTc.c_str());
    drawTruncatedText(region.x + 86, region.y + 42, rainChance, 32);
    drawAsciiText(region.x + 8, region.y + 53,
                  forecastTemperatureText(day), 2);
    drawAsciiText(region.x + 73, region.y + 60, "C", 1);
}

void drawForecastBody(const transitink::ForecastSnapshot& forecast,
                      const WeatherSnapshot& weather) {
    drawForecastCurrentPanel(forecast, weather);
    canvas.drawFastVLine(kForecastGridRegion.x, kForecastGridRegion.y,
                         kForecastGridRegion.h, kColorBlack);
    canvas.drawFastVLine(kForecastGridRegion.x + 126,
                         kForecastGridRegion.y, kForecastGridRegion.h,
                         kColorBlack);
    canvas.drawFastHLine(kForecastGridRegion.x,
                         kForecastGridRegion.y + 88,
                         kForecastGridRegion.w, kColorBlack);
    canvas.drawFastHLine(kForecastGridRegion.x,
                         kForecastGridRegion.y + 176,
                         kForecastGridRegion.w, kColorBlack);

    const std::size_t shownDays =
        std::min(forecast.dayCount, transitink::kForecastDayCount);
    if (!forecast.valid || shownDays == 0) {
        const String message = forecast.error.empty()
                                   ? String(transitink::uiText(
                                         transitink::UiTextId::DataUnavailable))
                                   : String(forecast.error.c_str());
        drawTruncatedText(kForecastGridRegion.x + 12,
                          kForecastGridRegion.y + 48, message,
                          kForecastGridRegion.w - 24);
        return;
    }

    for (std::size_t index = 0; index < shownDays; ++index) {
        const int column = static_cast<int>(index % 2);
        const int row = static_cast<int>(index / 2);
        const int x = kForecastGridRegion.x + column * 126;
        const int y = kForecastGridRegion.y + row * 88;
        const int width = column == 0 ? 126 : EINK_WIDTH - x;
        drawForecastMatrixCell({x, y, width, 88}, forecast.days[index]);
    }
}

void copyPartialRegionToPrevious(int x, int y, int w, int h) {
    const int xEnd = std::min(EINK_WIDTH, x + w);
    const int yEnd = std::min(EINK_HEIGHT, y + h);
    const int alignedX = (std::max(0, x) / 8) * 8;
    const int alignedEnd = std::min(EINK_WIDTH, ((xEnd + 7) / 8) * 8);
    const int startY = std::max(0, y);
    if (alignedEnd <= alignedX || yEnd <= startY) {
        return;
    }
    const size_t startByte = static_cast<size_t>(alignedX / 8);
    const size_t byteCount = static_cast<size_t>((alignedEnd - alignedX) / 8);
    for (int row = startY; row < yEnd; ++row) {
        const size_t offset = static_cast<size_t>(row) * kBytesPerRow + startByte;
        std::memcpy(previousFrameBuffer + offset, frameBuffer + offset, byteCount);
    }
}

PartialDiffStats partialDiffStats(int x, int y, int w, int h) {
    PartialDiffStats stats;
    const int xEnd = std::min(EINK_WIDTH, x + w);
    const int yEnd = std::min(EINK_HEIGHT, y + h);
    const int alignedX = (std::max(0, x) / 8) * 8;
    const int alignedEnd = std::min(EINK_WIDTH, ((xEnd + 7) / 8) * 8);
    const int startY = std::max(0, y);
    if (alignedEnd <= alignedX || yEnd <= startY) {
        return stats;
    }
    const size_t startByte = static_cast<size_t>(alignedX / 8);
    const size_t byteCount = static_cast<size_t>((alignedEnd - alignedX) / 8);
    stats.totalBits = static_cast<uint32_t>(byteCount * 8 * static_cast<size_t>(yEnd - startY));
    for (int row = startY; row < yEnd; ++row) {
        const size_t offset = static_cast<size_t>(row) * kBytesPerRow + startByte;
        for (size_t xb = 0; xb < byteCount; ++xb) {
            const uint8_t changed = previousFrameBuffer[offset + xb] ^ frameBuffer[offset + xb];
            stats.changedBits += static_cast<uint32_t>(__builtin_popcount(static_cast<unsigned int>(changed)));
        }
    }
    return stats;
}

void flushCanvas() {
    panel.show(frameBuffer);
    std::memcpy(previousFrameBuffer, frameBuffer, sizeof(frameBuffer));
    previousFrameValid = true;
    partialRefreshCount = 0;
}

void refreshCanvasPartially(int x, int y, int w, int h) {
    if (!previousFrameValid || partialRefreshCount >= kMaxPartialRefreshes) {
        flushCanvas();
        return;
    }
    const PartialDiffStats stats = partialDiffStats(x, y, w, h);
    if (stats.changedBits == 0) {
        Serial.println("EPD partial skipped: unchanged");
        return;
    }
    if (stats.ratio() >= kForceFullPartialDiffRatio) {
        Serial.println("EPD partial promoted to full: large diff");
        flushCanvas();
        return;
    }
    panel.showPartialRegion(frameBuffer, previousFrameBuffer, x, y, w, h);
    copyPartialRegionToPrevious(x, y, w, h);
    ++partialRefreshCount;
}

}  // namespace

void EInkDisplay::begin(bool showBootScreen) {
    Serial.println("EInkDisplay begin");
    batteryMonitor.begin();
    canvas.clear();
    panel.begin();
    if (showBootScreen) {
        showBoot(transitink::uiText(transitink::UiTextId::Booting));
    }
}

void EInkDisplay::fullRefresh() {
    flushCanvas();
}

void EInkDisplay::partialRefresh(int x, int y, int w, int h) {
    refreshCanvasPartially(x, y, w, h);
}

void EInkDisplay::showBoot(const String& message) {
    canvas.clear();
    drawStatusBar();
    drawText(18, 42, FIRMWARE_PRODUCT_NAME);
    drawText(18, 78, message);
    markNonDashboardFrame();
    fullRefresh();
}

void EInkDisplay::showConfigMode(const String& networkName, const String& details, const String& qrUrl) {
    canvas.clear();
    drawStatusBar();
    drawText(18, 38,
             String(transitink::uiText(transitink::UiTextId::SettingsPrefix)) +
                 FIRMWARE_PRODUCT_NAME);
    drawText(18, 76,
             String(transitink::uiText(transitink::UiTextId::NetworkLabel)) +
                 networkName);
    drawText(18, 100, batteryStatusText());
    drawText(18, 124,
             String(transitink::uiText(transitink::UiTextId::VersionLabel)) +
                 FIRMWARE_VERSION);
    drawMultilineText(18, 150, details, 22);
    drawQrCode(258, 92, qrUrl);
    drawText(18, 260,
             transitink::uiText(transitink::UiTextId::SaveAndRestart));
    markNonDashboardFrame();
    fullRefresh();
}

void EInkDisplay::showWifiStatus(const String& message) {
    canvas.clear();
    drawStatusBar();
    drawText(18, 42,
             transitink::uiText(transitink::UiTextId::ConnectionStatus));
    drawMultilineText(18, 82, message);
    markNonDashboardFrame();
    fullRefresh();
}

void EInkDisplay::showDashboard(const transitink::WidgetPageSnapshotSet& snapshots,
                                const WeatherSnapshot& weather,
                                uint8_t pageIndex,
                                uint8_t pageCount) {
    widgetPageIndex_ = pageIndex;
    widgetPageCount_ = pageCount;
    canvas.clear();
    drawClockAndStatusBar(widgetPageIndex_, widgetPageCount_);
    if (hasEnabledWidget(snapshots)) {
        for (uint8_t slot = 0; slot < transitink::kWidgetsPerPage; ++slot) {
            const bool drawDivider = shouldDrawLaneDivider(snapshots, slot);
            drawWidgetLane(slot, snapshots[slot], false, drawDivider);
        }
    } else {
        drawNoWidgetsHint();
    }
    drawWeatherFooter(weather);
    fullRefresh();
    dashboardFrameActive = true;
    dashboardFrameKind = DashboardFrameKind::Classic;
}

void EInkDisplay::showCommuteDashboard(
    const transitink::CommuteDashboardSnapshot& snapshot,
    const transitink::ForecastSnapshot& forecast,
    const WeatherSnapshot& weather) {
    canvas.clear();
    drawCommuteHeader(snapshot, forecast, weather);
    drawCommuteBody(snapshot);
    fullRefresh();
    dashboardFrameActive = true;
    dashboardFrameKind = DashboardFrameKind::Commute;
}

void EInkDisplay::refreshCommuteHeader(
    const transitink::CommuteDashboardSnapshot& snapshot,
    const transitink::ForecastSnapshot& forecast,
    const WeatherSnapshot& weather) {
    if (!dashboardFrameActive || !previousFrameValid ||
        dashboardFrameKind != DashboardFrameKind::Commute) {
        showCommuteDashboard(snapshot, forecast, weather);
        return;
    }

    std::memcpy(frameBuffer, previousFrameBuffer, sizeof(frameBuffer));
    clearRegion(kCommuteHeaderRegion);
    drawCommuteHeader(snapshot, forecast, weather);
    partialRefresh(kCommuteHeaderRegion.x, kCommuteHeaderRegion.y,
                   kCommuteHeaderRegion.w, kCommuteHeaderRegion.h);
    dashboardFrameActive = true;
    dashboardFrameKind = DashboardFrameKind::Commute;
}

void EInkDisplay::refreshCommuteBody(
    const transitink::CommuteDashboardSnapshot& snapshot,
    const transitink::ForecastSnapshot& forecast,
    const WeatherSnapshot& weather) {
    if (!dashboardFrameActive || !previousFrameValid ||
        dashboardFrameKind != DashboardFrameKind::Commute) {
        showCommuteDashboard(snapshot, forecast, weather);
        return;
    }

    std::memcpy(frameBuffer, previousFrameBuffer, sizeof(frameBuffer));
    clearRegion(kCommuteBodyRegion);
    drawCommuteBody(snapshot);
    partialRefresh(kCommuteBodyRegion.x, kCommuteBodyRegion.y,
                   kCommuteBodyRegion.w, kCommuteBodyRegion.h);
    dashboardFrameActive = true;
    dashboardFrameKind = DashboardFrameKind::Commute;
}

void EInkDisplay::showForecastDashboard(
    const transitink::ForecastSnapshot& forecast,
    const WeatherSnapshot& weather) {
    canvas.clear();
    drawCompositeHeader(weather, 1);
    drawForecastBody(forecast, weather);
    fullRefresh();
    dashboardFrameActive = true;
    dashboardFrameKind = DashboardFrameKind::Forecast;
}

void EInkDisplay::refreshForecastHeader(
    const transitink::ForecastSnapshot& forecast,
    const WeatherSnapshot& weather) {
    if (!dashboardFrameActive || !previousFrameValid ||
        dashboardFrameKind != DashboardFrameKind::Forecast) {
        showForecastDashboard(forecast, weather);
        return;
    }

    std::memcpy(frameBuffer, previousFrameBuffer, sizeof(frameBuffer));
    clearRegion(kForecastHeaderRegion);
    clearRegion(kForecastCurrentRegion);
    drawCompositeHeader(weather, 1);
    drawForecastCurrentPanel(forecast, weather);
    partialRefresh(kForecastHeaderRegion.x, kForecastHeaderRegion.y,
                   kForecastHeaderRegion.w, kForecastHeaderRegion.h);
    partialRefresh(kForecastCurrentRegion.x, kForecastCurrentRegion.y,
                   kForecastCurrentRegion.w, kForecastCurrentRegion.h);
    dashboardFrameActive = true;
    dashboardFrameKind = DashboardFrameKind::Forecast;
}

void EInkDisplay::refreshWidgetLane(uint8_t slot,
                                    const transitink::WidgetPageSnapshotSet& snapshots,
                                    const WeatherSnapshot& weather) {
    if (slot >= transitink::kWidgetsPerPage) {
        return;
    }
    if (!dashboardFrameActive || !previousFrameValid) {
        showDashboard(snapshots, weather);
        return;
    }
    if (dashboardFrameKind != DashboardFrameKind::Classic) {
        showDashboard(snapshots, weather);
        return;
    }

    const DisplayRegion& region = laneRegion(slot);
    std::memcpy(frameBuffer, previousFrameBuffer, sizeof(frameBuffer));
    clearRegion(region);
    const bool drawDivider = shouldDrawLaneDivider(snapshots, slot);
    drawWidgetLane(slot, snapshots[slot], false, drawDivider);
    partialRefresh(region.x, region.y, region.w, region.h);
    dashboardFrameActive = true;
}

void EInkDisplay::refreshClock(const transitink::WidgetPageSnapshotSet& snapshots,
                              const WeatherSnapshot& weather) {
    if (!dashboardFrameActive || !previousFrameValid) {
        showDashboard(snapshots, weather);
        return;
    }
    if (dashboardFrameKind != DashboardFrameKind::Classic) {
        showDashboard(snapshots, weather);
        return;
    }

    std::memcpy(frameBuffer, previousFrameBuffer, sizeof(frameBuffer));
    clearRegion(kStatusRegion);
    drawClockAndStatusBar(widgetPageIndex_, widgetPageCount_);
    partialRefresh(kStatusRegion.x, kStatusRegion.y, kStatusRegion.w, kStatusRegion.h);
    dashboardFrameActive = true;
}

void EInkDisplay::refreshWeatherFooter(const transitink::WidgetPageSnapshotSet& snapshots,
                                       const WeatherSnapshot& weather) {
    if (!dashboardFrameActive || !previousFrameValid) {
        showDashboard(snapshots, weather);
        return;
    }
    if (dashboardFrameKind != DashboardFrameKind::Classic) {
        showDashboard(snapshots, weather);
        return;
    }

    std::memcpy(frameBuffer, previousFrameBuffer, sizeof(frameBuffer));
    clearRegion(kFooterRegion);
    drawWeatherFooter(weather);
    partialRefresh(kFooterRegion.x, kFooterRegion.y, kFooterRegion.w, kFooterRegion.h);
    dashboardFrameActive = true;
}

void EInkDisplay::showSleep(const transitink::WidgetPageSnapshotSet& snapshots,
                            const WeatherSnapshot& weather,
                            uint8_t pageIndex,
                            uint8_t pageCount) {
    widgetPageIndex_ = pageIndex;
    widgetPageCount_ = pageCount;
    canvas.clear();
    drawClockAndStatusBar(widgetPageIndex_, widgetPageCount_);
    if (hasEnabledWidget(snapshots)) {
        for (uint8_t slot = 0; slot < transitink::kWidgetsPerPage; ++slot) {
            const bool drawDivider = shouldDrawLaneDivider(snapshots, slot);
            drawWidgetLane(slot, snapshots[slot], true, drawDivider);
        }
    } else {
        drawNoWidgetsHint();
    }
    drawWeatherFooter(weather);
    markNonDashboardFrame();
    fullRefresh();
}

void EInkDisplay::refreshSleepStatusAndWeather(
    const transitink::WidgetPageSnapshotSet& snapshots,
    const WeatherSnapshot& weather,
    uint8_t pageIndex,
    uint8_t pageCount) {
    widgetPageIndex_ = pageIndex;
    widgetPageCount_ = pageCount;
    if (!previousFrameValid) {
        showSleep(snapshots, weather, widgetPageIndex_, widgetPageCount_);
        return;
    }

    std::memcpy(frameBuffer, previousFrameBuffer, sizeof(frameBuffer));
    clearRegion(kStatusRegion);
    drawClockAndStatusBar(widgetPageIndex_, widgetPageCount_);
    clearRegion(kFooterRegion);
    drawWeatherFooter(weather);
    partialRefresh(kStatusRegion.x, kStatusRegion.y, kStatusRegion.w, kStatusRegion.h);
    partialRefresh(kFooterRegion.x, kFooterRegion.y, kFooterRegion.w, kFooterRegion.h);
    markNonDashboardFrame();
}

void EInkDisplay::prepareForSleep() {
    // Board-specific panel and battery power policy belongs in hardware adapters.
}
