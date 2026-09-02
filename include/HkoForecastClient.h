#pragma once

#include <Arduino.h>

#include <cstddef>

#include "core/CommuteDashboardCore.h"

class HkoForecastClient {
public:
    static constexpr std::size_t kMaximumResponseBytes = 32 * 1024;
    static constexpr uint16_t kConnectTimeoutMs = 7000;
    static constexpr uint16_t kReadTimeoutMs = 10000;

    bool fetchForecast(transitink::ForecastSnapshot& snapshot,
                       String& error);

private:
    bool httpGetBounded(const String& url, String& body, String& error);
};
