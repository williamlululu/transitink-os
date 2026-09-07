#include "core/CommuteDashboardCore.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace transitink;
namespace {
constexpr int64_t day = 1788710400; // Synthetic Monday, HK midnight.
int64_t at(int h, int m, int s = 0) { return day + h*3600 + m*60 + s; }
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void seed(CommuteDashboardSnapshot& d, CommuteEtaKind kind, int64_t now,
          std::initializer_list<int64_t> buses) {
    auto& e=commuteEta(d,kind); e={};
    e.fetchedAtEpoch=e.sourceGeneratedAtEpoch=e.sourceDataAtEpoch=now;
    e.sourcesExpected=e.sourcesSucceeded=1;
    for(auto t:buses) { e.epochs[e.count]=t; e.evidenceEpochs[e.count++]=now; }
}
uint16_t minute(int64_t now) { return static_cast<uint16_t>(((now-day)%86400)/60); }
void plan(CommuteDashboardSnapshot& d, int64_t now, int64_t target=at(7,25),
          CommutePlannerSettings s={}) {
    planCommuteDashboard(d,now,target,true,s,CommuteSessionMode::Manual,minute(now));
}
CommuteDashboardSnapshot provisional(int64_t now, int64_t first) {
    CommuteDashboardSnapshot d;
    seed(d,CommuteEtaKind::Bus106,now,{first});
    seed(d,CommuteEtaKind::Bus8P,now,{});
    return d;
}
void frozen_seconds() {
    auto input=provisional(at(6,20),at(6,30,20));
    seed(input,CommuteEtaKind::Bus8P,at(6,20),{at(6,28)});
    for(int sec:{0,30,59}) {
        auto d=input; plan(d,at(6,20,sec));
        require(d.routeA.primary.provisional,"must remain provisional");
        require(d.routeA.primary.connectionEpoch==at(6,56),"fixed connection must be06:56:00");
        require(d.routeA.primary.transferReadyEpoch==at(6,44,20),"ready must not move");
        require(d.routeA.primary.arrivalEpoch==at(7,13),"nominal must be07:13:00");
        require(d.etas[0].epochs==input.etas[0].epochs && d.etas[1].epochs==input.etas[1].epochs,"raw ETA mutated");
    }
}
void headway_seconds() {
    for(int delta:{0,1,59}) for(int sec:{0,30,59}) {
        auto d=provisional(at(6,20),at(6,30,delta)); plan(d,at(6,20,sec));
        require(d.routeA.primary.connectionEpoch==(delta==0?at(6,44):at(6,56)),"absolute headway rounding");
        require(d.routeA.primary.connectionEpoch>=d.routeA.primary.transferReadyEpoch,"connection precedes readiness");
    }
}
void early_later_boundary() {
    for(int delta:{-1,0,1}) {
        auto d=provisional(at(6,54),at(7,1)+delta); plan(d,at(6,54),at(9,0));
        require(d.routeA.primary.connectionEpoch==(delta<=0?at(7,20):at(7,35)),"early/later band boundary");
        require(d.routeA.primary.connectionRideMinutes==27,"post07:00 uses fallback ride");
    }
}
void verified_band_end() {
    for(int delta:{0,1}) {
        auto d=provisional(at(8,20),at(8,31)+delta); plan(d,at(8,20),at(10,0));
        if(delta==0) require(d.routeA.primary.connectionEpoch==at(8,50),"last supported slot");
        else require(!d.routeA.primary.valid && d.routeAStatus.state==CommuteRouteAState::TransferPending,"do not extend verified band");
    }
}
void profile_boundary() {
    for(int delta:{-1,0,1}) for(int sec:{0,30,59}) {
        auto d=provisional(at(6,50),at(7,0)+delta); plan(d,at(6,50,sec),at(9,0));
        require(d.routeA.primary.firstLegEarlyProfile==(delta<0),"per-leg07:00 exclusive bound");
        require(!d.routeA.primary.connectionEarlyProfile,"second leg after07:00");
        require(d.routeA.primary.connectionEpoch==at(7,20),"same absolute slot across local seconds");
    }
}
void midnight_service_day() {
    auto input=provisional(at(23,50),at(24,10)); CommutePlannerSettings s; s.maximumEtaAgeMinutes=15;
    for(auto now:{at(23,59),at(24,0),at(24,0,59)}) {
        auto d=input; plan(d,now,0,s);
        require(d.routeA.primary.firstLegValid && !d.routeA.primary.valid,"night first leg retained without invented connection");
        require(d.routeAStatus.state==CommuteRouteAState::TransferPending,"same prior service-day envelope");
    }
    for(int delta:{0,1}) {
        auto d=provisional(at(24,25),at(24,36)+delta); plan(d,at(24,25),0);
        require(d.routeAStatus.state==(delta?CommuteRouteAState::NoService:CommuteRouteAState::TransferPending),"last service instant includes seconds");
    }
}
void no_boarding_grace() {
    auto input=provisional(at(6,24),at(6,30,20));
    seed(input,CommuteEtaKind::Bus106,at(6,24),{at(6,30,20),at(6,35)});
    auto before=input; plan(before,at(6,24,20));
    auto after=input; plan(after,at(6,24,21));
    require(before.routeA.primary.firstBusEpoch==at(6,30,20),"exact boarding cutoff");
    require(after.routeA.primary.firstBusEpoch==at(6,35),"expired bus must not freeze");
}
void risk_arrival_changes_advice() {
    auto input=provisional(at(6,20),at(6,30));
    seed(input,CommuteEtaKind::Bus8P,at(6,20),{at(6,59)});
    seed(input,CommuteEtaKind::Bus118,at(6,20),{at(6,45)});
    auto normal=input; plan(normal,at(6,20));
    CommutePlannerSettings s; s.route8pRiskRideMinutes=35;
    auto slow=input; plan(slow,at(6,20),at(7,25),s);
    require(normal.recommendation==CommuteChoice::RouteA,"default scenario favors A");
    require(slow.recommendation==CommuteChoice::RouteB,"known slow miss must affect target recommendation");
    require(slow.routeA.primary.arrivalEpoch==at(7,16) && normal.routeA.primary.arrivalEpoch==at(7,16),"nominal arrival must stay unchanged");
    require(slow.routeA.primary.riskArrivalEpoch==at(7,34),"separate slow scenario");
    require(slow.routeA.riskAssessment==CommuteAssessment::Late &&
            slow.routeA.assessment==CommuteAssessment::Safe,"separate risk and nominal assessments");
}
void safer_same_first_leg() {
    auto d=provisional(at(6,20),at(6,30));
    seed(d,CommuteEtaKind::Bus106,at(6,20),{at(6,30),at(6,36)});
    seed(d,CommuteEtaKind::Bus8P,at(6,20),{at(6,46),at(6,52),at(6,58)});
    seed(d,CommuteEtaKind::Bus118,at(6,20),{at(6,39)}); plan(d,at(6,20));
    require(d.routeA.primary.connectionEpoch==at(6,52),"consider lower-risk same-first-leg connection");
    require(!d.routeA.primary.connectionAtRisk && d.routeA.primary.arrivalEpoch==at(7,9),"chosen scenario is lower risk and on time");
    require(d.recommendation==CommuteChoice::RouteA,"do not force118");
    require(d.routeA.nominalFastest.connectionEpoch==at(6,46) &&
            d.routeA.nominalFastest.connectionAtRisk,"retain nominal fastest with its risk");
    require(d.routeA.fallback.firstBusEpoch==at(6,36) && d.routeA.fallback.connectionEpoch==at(6,58),"preserve later-first-leg fallback");
}
void leave_now_fastest() {
    auto d=provisional(at(6,20),at(6,30));
    seed(d,CommuteEtaKind::Bus8P,at(6,20),{at(6,46),at(6,52)}); plan(d,at(6,20),0);
    require(d.routeA.primary.connectionEpoch==at(6,46),"target-free uses nominal fastest");
    require(d.routeA.assessment==CommuteAssessment::Unavailable,"no target means no lateness");
    require(d.routeA.riskAssessment==CommuteAssessment::Unavailable,"no target risk assessment");
}
void ontime_risky_beats_late() {
    auto d=provisional(at(6,20),at(6,30));
    seed(d,CommuteEtaKind::Bus8P,at(6,20),{at(6,46),at(6,52)});
    seed(d,CommuteEtaKind::Bus118,at(6,20),{at(6,39)}); plan(d,at(6,20),at(7,5));
    require(d.routeA.primary.connectionEpoch==at(6,46) && d.recommendation==CommuteChoice::RouteA,"nominal-on-time outranks a late safer alternative");
    require(d.routeA.primary.connectionAtRisk,"do not hide selected risk");
}
void full_connection_horizon() {
    auto d=provisional(at(6,20),at(6,30));
    auto& e=commuteEta(d,CommuteEtaKind::Bus8P);
    for(size_t i=0;i<e.epochs.size();++i) {
        e.epochs[i]=at(6,44)+static_cast<int64_t>(i)*60;
        e.evidenceEpochs[i]=at(6,20); ++e.count;
    }
    CommutePlannerSettings s; s.route106RiskRideMinutes=20;
    plan(d,at(6,20),at(7,25),s);
    require(d.routeA.primary.connectionEpoch==at(6,55),"last of twelve is meaningful safer connection");
    require(d.routeA.nominalFastest.connectionEpoch==at(6,44),"bounded horizon preserves fastest too");
}
void preserve_existing_stale_policy() {
    auto d=provisional(at(6,20),at(6,30));
    seed(d,CommuteEtaKind::Bus106,at(6,20),{at(6,30),at(6,31)});
    auto& first=commuteEta(d,CommuteEtaKind::Bus106);
    first.rowStale[0]=true; first.stale=first.partial=true;
    seed(d,CommuteEtaKind::Bus8P,at(6,20),{at(6,55)});
    seed(d,CommuteEtaKind::Bus118,at(6,20),{at(6,45)}); plan(d,at(6,20));
    require(d.routeA.primary.firstBusEpoch==at(6,30) && d.routeA.stale,
            "do not silently redesign deferred stale-source policy");
    require(d.recommendation==CommuteChoice::RouteB,"existing route freshness priority retained");
}
}
int main() {
    const std::vector<std::pair<const char*,void(*)()>> tests={
        {"frozen_seconds",frozen_seconds},{"headway_seconds",headway_seconds},
        {"early_later_boundary",early_later_boundary},{"verified_band_end",verified_band_end},
        {"profile_boundary",profile_boundary},{"midnight_service_day",midnight_service_day},
        {"no_boarding_grace",no_boarding_grace},{"risk_arrival_changes_advice",risk_arrival_changes_advice},
        {"safer_same_first_leg",safer_same_first_leg},{"leave_now_fastest",leave_now_fastest},
        {"ontime_risky_beats_late",ontime_risky_beats_late},
        {"full_connection_horizon",full_connection_horizon},
        {"preserve_existing_stale_policy",preserve_existing_stale_policy}};
    int failures=0;
    for(const auto& test:tests) try { test.second(); std::cout<<"PASS "<<test.first<<'\n'; }
    catch(const std::exception& error) { ++failures; std::cout<<"FAIL "<<test.first<<": "<<error.what()<<'\n'; }
    std::cout<<tests.size()-failures<<'/'<<tests.size()<<" targeted checks passed\n";
    return failures?1:0;
}
