#pragma once

#include <cstdint>
#include <string>

#include "core/CommuteDashboardCore.h"

namespace transitink {

// Parses the official HKO nine-day forecast response and returns today through
// the following five days. nowEpoch is UTC; Hong Kong civil dates are
// calculated with a fixed UTC+8 offset so the result does not depend on the
// process or device locale.
bool parseHkoForecastJson(const char* json,
                          int64_t nowEpoch,
                          ForecastSnapshot& snapshot,
                          std::string& error);

// Compact Traditional Chinese labels derived from the official HKO weather
// icon and Probability of Significant Rain (PSR) vocabularies.
std::string hkoForecastConditionTextTc(int icon,
                                       const std::string& fallbackTc = {});
std::string hkoRainChanceTextTc(const std::string& psr);

// Keeps a previously valid forecast visible after a failed refresh. Without a
// usable cache, the snapshot remains invalid and contains only the error.
void markHkoForecastFailure(ForecastSnapshot& snapshot,
                            const std::string& error);

}  // namespace transitink
