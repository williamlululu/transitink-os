#include "HkoForecastClient.h"

#include "TransitTlsTrust.h"
#include "core/CatalogCore.h"
#include "core/HkoForecastParser.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <ctime>
#include <string>
#include <utility>

namespace {

constexpr const char* kHkoForecastUrl =
    "https://data.weather.gov.hk/weatherAPI/opendata/weather.php"
    "?dataType=fnd&lang=tc";
constexpr uint8_t kMaximumAttempts = 2;
constexpr uint16_t kRetryDelayMs = 300;

class BoundedBodyStream final : public Stream {
public:
    explicit BoundedBodyStream(std::size_t limit) : accumulator_(limit) {}

    std::size_t write(uint8_t byte) override { return write(&byte, 1); }
    std::size_t write(const uint8_t* bytes, std::size_t length) override {
        if (!accumulator_.append(bytes, length, error_)) {
            return 0;
        }
        return length;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}

    const transitink::BoundedBodyAccumulator& accumulator() const {
        return accumulator_;
    }
    const std::string& error() const { return error_; }

private:
    transitink::BoundedBodyAccumulator accumulator_;
    std::string error_;
};

}  // namespace

bool HkoForecastClient::httpGetBounded(const String& url,
                                       String& body,
                                       String& error) {
    body = "";
    WiFiClientSecure tls;
    transitink::configureVerifiedTls(tls);
    HTTPClient http;
    http.setConnectTimeout(kConnectTimeoutMs);
    http.setTimeout(kReadTimeoutMs);
    http.setReuse(false);
    if (!http.begin(tls, url)) {
        error = "無法建立天文台預報 HTTPS 連線";
        return false;
    }

    http.addHeader("Accept", "application/json");
    http.addHeader("Accept-Encoding", "identity");
    const int code = http.GET();
    const int expectedLength = http.getSize();
    if (code != HTTP_CODE_OK ||
        expectedLength > static_cast<int>(kMaximumResponseBytes)) {
        error = code == HTTP_CODE_OK
                    ? "天文台預報回應過大"
                    : String("天文台預報 HTTP 錯誤: ") + code;
        http.end();
        return false;
    }

    BoundedBodyStream sink(kMaximumResponseBytes);
    const int written = http.writeToStream(&sink);
    std::string completionError;
    const bool complete =
        sink.accumulator().complete(expectedLength, completionError);
    http.end();
    if (written < 0 || !sink.error().empty() || !complete ||
        sink.accumulator().body().empty()) {
        const std::string& detail =
            !sink.error().empty() ? sink.error() : completionError;
        error = detail.empty() ? "天文台預報回應不完整" : detail.c_str();
        return false;
    }

    body = sink.accumulator().body().c_str();
    error = "";
    return true;
}

bool HkoForecastClient::fetchForecast(
    transitink::ForecastSnapshot& snapshot,
    String& error) {
    String body;
    String fetchError;
    for (uint8_t attempt = 0; attempt < kMaximumAttempts; ++attempt) {
        if (httpGetBounded(kHkoForecastUrl, body, fetchError)) {
            transitink::ForecastSnapshot parsed;
            std::string parserError;
            if (transitink::parseHkoForecastJson(
                    body.c_str(), static_cast<int64_t>(time(nullptr)), parsed,
                    parserError)) {
                snapshot = std::move(parsed);
                error = "";
                return true;
            }
            fetchError = parserError.c_str();
            break;
        }
        if (attempt + 1 < kMaximumAttempts) {
            delay(kRetryDelayMs);
        }
    }

    const std::string failure = fetchError.length() > 0
                                    ? std::string(fetchError.c_str())
                                    : std::string("暫未能取得天氣預報");
    transitink::markHkoForecastFailure(snapshot, failure);
    error = failure.c_str();
    return false;
}
