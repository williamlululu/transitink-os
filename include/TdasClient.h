#pragma once

#include <Arduino.h>

#include <cstddef>
#include <cstdint>

#include "core/TdasCore.h"

class TdasClient {
public:
    static constexpr int kTimeoutMs = 8000;
    static constexpr std::size_t kMaxResponseBytes =
        transitink::kMaxTdasResponseBytes;

    static constexpr const char* requestUrl() {
        return "https://tdas-api.hkemobility.gov.hk/tdas/api/route";
    }

    // Returns true only for a fresh TDAS result. On failure, estimate is still
    // populated with a conservative, calibrated and explicitly stale fallback.
    bool fetchJourneyEstimate(transitink::TdasDestination destination,
                              const transitink::TdasCalibration& calibration,
                              int64_t nowEpoch,
                              transitink::JourneyEstimate& estimate,
                              String& error,
                              int timeoutMs = kTimeoutMs);

private:
    bool postJson(const std::string& request,
                  std::string& response,
                  String& error,
                  int timeoutMs);
};
