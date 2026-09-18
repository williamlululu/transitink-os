#include "CommuteContextClient.h"
#include "ProductConfig.h"
#include "TransitTlsTrust.h"
#include "core/CommuteContextJson.h"
#include "core/CatalogCore.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

namespace {
class ContextBody final : public Stream {
public:
    const uint32_t startedMs = millis();
    transitink::BoundedBodyAccumulator accumulator{24576};
    std::string error;
    std::size_t write(uint8_t byte) override { return write(&byte,1); }
    std::size_t write(const uint8_t* data, std::size_t size) override {
        if (static_cast<uint32_t>(millis()-startedMs)>15000) { error="context body timeout"; return 0; }
        return accumulator.append(data,size,error) ? size : 0;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
};
}
void CommuteContextClient::refresh(transitink::CommuteContextSnapshot& snapshot, int64_t now) {
    if (now <= 0 || !transitink::contextPollDue(poll_, millis())) return;
    WiFiClientSecure tls;
    transitink::configureFirmwareUpdateVerifiedTls(tls);
    HTTPClient http;
    http.setConnectTimeout(5000); http.setTimeout(5000); http.setReuse(false);
    const String url = String(TRANSITINK_FIRMWARE_UPDATE_BASE_URL) + "/commute-context.json";
    bool success = false;
    if (http.begin(tls,url)) {
        http.addHeader("Accept", "application/json");
        http.addHeader("Accept-Encoding", "identity");
        const int code = http.GET();
        const int length = http.getSize();
        if (code == HTTP_CODE_OK && length <= 24576) {
            ContextBody sink;
            const int written = http.writeToStream(&sink);
            std::string error;
            if (written >= 0 && sink.error.empty() && sink.accumulator.complete(length,error)) {
                // Parsing uses a heap-backed temporary: do not add a 24-event
                // snapshot to the small Arduino loop task stack.
                auto next = std::make_unique<transitink::CommuteContextSnapshot>();
                success = transitink::parseCommuteContext(sink.accumulator.body(),now,*next);
                if (success) {
                    // Keep previously verified finite notices if that provider
                    // was unavailable on this refresh. Never revive stale meets.
                    for (std::size_t i=0; i<snapshot.count && next->count<next->events.size(); ++i) {
                        const auto& e=snapshot.events[i];
                        const std::size_t source=e.operatorMask==2?1:0;
                        if (!e.athleticMeet && e.verified && e.endEpoch>now && e.endEpoch>e.startEpoch &&
                            next->sources[source].fetchedEpoch==0) next->events[next->count++]=e;
                    }
                    snapshot=std::move(*next);
                }
            }
        }
        http.end();
    }
    snapshot.lastFetchFailed=!success;
    transitink::recordContextPoll(poll_,millis(),success);
    Serial.printf("Commute context result=%s generated=%lld events=%u next_ms=%lu coverage=%u,%u,%u\n",
        success?"ok":"failed",static_cast<long long>(snapshot.generatedEpoch),
        static_cast<unsigned>(snapshot.count),static_cast<unsigned long>(poll_.nextMs),
        snapshot.sources[0].coverage,snapshot.sources[1].coverage,snapshot.sources[2].coverage);
}
