#include "TdasClient.h"

#include "TransitTlsTrust.h"
#include "core/CatalogCore.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

namespace {

class BoundedTdasBodyStream final : public Stream {
public:
    explicit BoundedTdasBodyStream(std::size_t limit) : accumulator_(limit) {}

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

struct HttpCleanup {
    HTTPClient& http;
    WiFiClientSecure& tls;

    ~HttpCleanup() {
        http.end();
        tls.stop();
    }
};

}  // namespace

bool TdasClient::postJson(const std::string& request,
                          std::string& response,
                          String& error,
                          int timeoutMs) {
    response.clear();
    WiFiClientSecure tls;
    transitink::configureVerifiedTls(tls);
    HTTPClient http;
    HttpCleanup cleanup{http, tls};
    http.setTimeout(timeoutMs);
    http.setReuse(false);
    if (!http.begin(tls, String(requestUrl()))) {
        error = "未能連接 TDAS 路況服務";
        return false;
    }
    http.setAcceptEncoding("identity");
    http.setUserAgent("TransitInk-OS/1.1.3");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");
    const String requestString(request.c_str());
    const int statusCode = http.POST(requestString);
    if (statusCode != HTTP_CODE_OK) {
        error = String("TDAS HTTP 錯誤: ") + statusCode;
        return false;
    }

    const int expectedLength = http.getSize();
    if (expectedLength > static_cast<int>(kMaxResponseBytes)) {
        error = "TDAS 回應過大";
        return false;
    }
    BoundedTdasBodyStream sink(kMaxResponseBytes);
    const int written = http.writeToStream(&sink);
    std::string completionError;
    const bool complete =
        sink.accumulator().complete(expectedLength, completionError);
    if (written < 0 || !sink.error().empty() || !complete) {
        const std::string& detail =
            !sink.error().empty() ? sink.error() : completionError;
        error = detail.empty() ? "TDAS 回應不完整" : detail.c_str();
        return false;
    }
    response = sink.accumulator().body();
    error = "";
    return true;
}

bool TdasClient::fetchJourneyEstimate(
    transitink::TdasDestination destination,
    const transitink::TdasCalibration& calibration,
    int64_t nowEpoch,
    transitink::JourneyEstimate& estimate,
    String& error,
    int timeoutMs) {
    estimate = transitink::makeTdasFallbackEstimate(destination,
                                                    calibration,
                                                    nowEpoch);
    std::string response;
    if (!postJson(transitink::tdasRequestBody(destination), response, error,
                  timeoutMs)) {
        return false;
    }

    transitink::TdasRouteResult parsed;
    std::string parseError;
    if (!transitink::parseTdasRouteResponse(response, parsed, parseError)) {
        error = parseError.c_str();
        return false;
    }
    estimate = transitink::makeTdasJourneyEstimate(destination,
                                                   parsed,
                                                   calibration,
                                                   nowEpoch);
    if (!estimate.valid) {
        estimate = transitink::makeTdasFallbackEstimate(destination,
                                                        calibration,
                                                        nowEpoch);
        error = "TDAS 回應沒有可用行車時間";
        return false;
    }
    error = "";
    return true;
}
