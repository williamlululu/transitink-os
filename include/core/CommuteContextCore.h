#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

namespace transitink {

constexpr std::size_t kContextEventLimit = 24;
constexpr int64_t kContextFreshSeconds = 24 * 3600;
enum class ContextSeverity : uint8_t { None, Advisory, Degraded, HardBlock };
enum class ContextScope : uint8_t { Unknown, Route, Boarding, Transfer, Destination, AlternateAlighting, AlternateBoarding };
struct CommuteContextEvent {
    std::string id;
    std::string sourceUrl;
    std::string messageTc;
    uint8_t route = 0;  // 0=106, 1=8P, 2=118; no provider-local direction letters.
    uint8_t operatorMask = 0;  // 1=Citybus, 2=KMB; 3 only with independent physical evidence.
    ContextScope scope = ContextScope::Unknown;
    ContextSeverity severity = ContextSeverity::Advisory;
    int64_t startEpoch = 0;
    int64_t endEpoch = 0;  // exclusive; zero means no explicit end was established
    int64_t fetchedEpoch = 0;
    bool towardYueWan = false;
    bool verified = false;
    bool athleticMeet = false;
};
struct ContextSourceStatus {
    int64_t fetchedEpoch = 0;
    uint8_t coverage = 0;  // 0 unavailable/unsupported, 1 partial, 2 supported format
};
struct CommuteContextSnapshot {
    std::array<CommuteContextEvent, kContextEventLimit> events{};
    std::size_t count = 0;
    std::array<ContextSourceStatus, 3> sources{}; // Citybus, KMB, LCSD
    int64_t generatedEpoch = 0;
    int64_t receivedEpoch = 0;
    bool lastFetchFailed = false;
    uint8_t alternateCheckedMask = 0; // current exact official route-stop graph; absent => no A2
};
struct ContextImpact {
    ContextSeverity severity = ContextSeverity::None;
    std::string eventId;
    std::string messageTc;
};
inline bool contextFresh(int64_t fetched, int64_t now) {
    return now > 0 && fetched > 0 && fetched <= now + 300 &&
           now - fetched <= kContextFreshSeconds;
}
inline uint8_t contextOperatorMask(uint8_t operatorId) {
    // BusOperator::Kmb=0 and Citybus=2; LongWin/Tfl are not fixed-route candidates.
    return operatorId == 0 ? 2 : (operatorId == 2 ? 1 : 0);
}
inline ContextImpact commuteContextImpact(const CommuteContextSnapshot& context,
        uint8_t route, uint8_t operatorMask, int64_t boardingEpoch,
        int64_t requiredStopEpoch, int64_t now, bool alternate = false) {
    ContextImpact result;
    for (std::size_t i = 0; i < std::min(context.count, context.events.size()); ++i) {
        const auto& event = context.events[i];
        if (event.athleticMeet || !event.verified || !event.towardYueWan ||
            event.route != route || !(event.operatorMask & operatorMask) ||
            event.scope == ContextScope::Unknown || event.startEpoch <= 0) continue;
        if (!alternate && (event.scope == ContextScope::AlternateAlighting ||
                           event.scope == ContextScope::AlternateBoarding)) continue;
        if (alternate && ((route == 0 && event.scope == ContextScope::Transfer) ||
                          (route == 1 && event.scope == ContextScope::Boarding))) continue;
        const int64_t at = event.scope == ContextScope::Transfer ||
                           event.scope == ContextScope::AlternateAlighting ||
                           event.scope == ContextScope::Destination ? requiredStopEpoch : boardingEpoch;
        if (at < event.startEpoch || (event.endEpoch > 0 && at >= event.endEpoch)) continue;
        auto severity = event.severity;
        // A fresh open-ended listing is advisory; only explicit finite validity
        // can establish or retain a hard effect after the feed goes stale.
        if (event.endEpoch <= event.startEpoch) {
            if (!contextFresh(event.fetchedEpoch, now)) continue;
            severity = ContextSeverity::Advisory;
        }
        if (severity > result.severity) result = {severity, event.id, event.messageTc};
    }
    return result;
}
inline bool alternateSegmentChanged(const CommuteContextSnapshot& context,
        uint8_t route,uint8_t mask,int64_t board,int64_t stopEarliest,int64_t stopLatest) {
    for(std::size_t i=0;i<std::min(context.count,context.events.size());++i) {
        const auto& e=context.events[i];
        if (!e.verified || !e.towardYueWan || e.athleticMeet || e.route!=route ||
            !(e.operatorMask&mask) || e.severity<ContextSeverity::Degraded || e.endEpoch<=e.startEpoch) continue;
        int64_t start=board,end=stopLatest;
        if (route==0 && e.scope==ContextScope::AlternateAlighting) start=stopEarliest;
        else if (route==1 && e.scope==ContextScope::AlternateBoarding) end=board;
        else if (e.scope==ContextScope::Destination) start=stopEarliest;
        else if (e.scope==ContextScope::Boarding && route==0) end=board;
        else if (e.scope!=ContextScope::Route) continue;
        if (start<e.endEpoch && end>=e.startEpoch) return true;
    }
    return false;
}
inline bool athleticMeetToday(const CommuteContextSnapshot& context, int64_t now) {
    if (context.sources[2].coverage == 0 || !contextFresh(context.sources[2].fetchedEpoch, now)) return false;
    for (std::size_t i = 0; i < std::min(context.count, context.events.size()); ++i) {
        const auto& e = context.events[i];
        if (e.athleticMeet && e.verified && contextFresh(e.fetchedEpoch, now) &&
            now >= e.startEpoch && now < e.endEpoch &&
            e.endEpoch - e.startEpoch == 86400 && (e.startEpoch + 8*3600) % 86400 == 0)
            return true;
    }
    return false;
}
struct ContextPollState {
    uint32_t nextMs = 0;
    uint8_t failures = 0;
    bool attempted = false;
};
inline bool contextPollDue(const ContextPollState& state, uint32_t nowMs) {
    return !state.attempted || static_cast<int32_t>(nowMs - state.nextMs) >= 0;
}
inline void recordContextPoll(ContextPollState& state, uint32_t nowMs, bool success) {
    state.attempted = true;
    state.failures = success ? 0 : static_cast<uint8_t>(std::min<int>(state.failures + 1, 5));
    const uint32_t seconds = success ? 6*3600 : std::min<uint32_t>(900U << (state.failures-1), 6*3600);
    state.nextMs = nowMs + seconds * 1000U;
}

} // namespace transitink
