#include "core/CommuteDashboardCore.h"
#include <cassert>
#include <iostream>
#include <cstdlib>
#undef assert
#define assert(value) do { if (!(value)) { std::cerr << "FAILED line " << __LINE__ << ": " << #value << '\n'; std::exit(1); } } while (false)
using namespace transitink;
constexpr int64_t now = 1894018800; // synthetic instant; no private trip data
CommuteDashboardSnapshot fixture() {
    CommuteDashboardSnapshot d;
    const int rows[3][3]={{10,16,22},{20,26,32},{19,26,32}};
    d.context.generatedEpoch=now;
    for(auto& source:d.context.sources) {source.fetchedEpoch=now;source.coverage=1;}
    for(unsigned stream=0;stream<3;++stream) {
        auto& eta=d.etas[stream];eta.count=3;eta.fetchedAtEpoch=now;
        for(unsigned i=0;i<3;++i) {eta.epochs[i]=now+rows[stream][i]*60;eta.evidenceEpochs[i]=now;eta.operators[i]=2;}
    }
    return d;
}
void plan(CommuteDashboardSnapshot& d,int64_t target=now+65*60) {
    planCommuteDashboard(d,now,target,true,{},CommuteSessionMode::Manual,380);
}
CommuteContextEvent omission() {
    CommuteContextEvent e;e.id="synthetic-omission";e.route=0;e.operatorMask=1;
    e.scope=ContextScope::Transfer;e.severity=ContextSeverity::HardBlock;
    e.startEpoch=now-3600;e.endEpoch=now+86400;e.fetchedEpoch=now;
    e.towardYueWan=e.verified=true;e.messageTc="城巴106維園轉車站暫停";return e;
}
int main() {
    auto original=fixture();plan(original);
    assert(original.routeA.primary.valid && original.routeA.primary.connectionEpoch==now+32*60);
    auto blocked=fixture();blocked.context.events[0]=omission();blocked.context.count=1;plan(blocked);
    assert(!blocked.routeA.primary.valid && blocked.recommendation==CommuteChoice::RouteB);
    assert(blocked.etas[0].epochs==original.etas[0].epochs); // official ETA unshifted
    auto joint=fixture();joint.etas[0].operators[1]=0;joint.context=blocked.context;plan(joint);
    assert(joint.routeA.primary.valid && joint.routeA.primary.firstOperator==0);
    joint.context.events[0].operatorMask=3;plan(joint);assert(!joint.routeA.primary.valid);
    auto relocation=fixture();relocation.context=blocked.context;
    relocation.context.events[0].severity=ContextSeverity::Degraded;plan(relocation);
    assert(relocation.routeA.primary.valid && relocation.routeA.primary.contextDegraded);
    assert(relocation.routeA.primary.transferReadyEpoch==original.routeA.primary.transferReadyEpoch);
    assert(relocation.recommendation==CommuteChoice::RouteB);
    for(int variant=0;variant<5;++variant) {
        auto d=fixture();d.context=blocked.context;auto& e=d.context.events[0];
        if(variant==0)e.endEpoch=now;
        if(variant==1)e.towardYueWan=false;
        if(variant==2)e.scope=ContextScope::Unknown;
        if(variant==3)e.verified=false;
        if(variant==4)e.operatorMask=2;
        plan(d);assert(d.routeA.primary.valid);
    }
    auto stale=fixture();stale.context=blocked.context;stale.context.generatedEpoch=now-172800;
    stale.context.events[0].fetchedEpoch=now-172800;plan(stale);
    assert(!stale.routeA.primary.valid); // explicit verified validity survives stale fetch
    stale.context.events[0].endEpoch=0;plan(stale);assert(stale.routeA.primary.valid);
    auto future=fixture();future.context=blocked.context;
    future.context.events[0].startEpoch=now+21*60;plan(future);
    assert(!future.routeA.primary.valid); // active at modeled transfer, not just fetch time
    auto second=fixture();second.context.events[0]=omission();second.context.count=1;
    second.context.events[0].route=1;second.context.events[0].scope=ContextScope::Boarding;
    plan(second);assert(!second.routeA.primary.valid); // no timetable escape from block
    auto meet=fixture();auto& m=meet.context.events[0];m=omission();m.athleticMeet=true;
    m.route=1;m.severity=ContextSeverity::Advisory;m.startEpoch=(now+28800)/86400*86400-28800;
    m.endEpoch=m.startEpoch+86400;m.fetchedEpoch=m.startEpoch;
    meet.context.sources[2].fetchedEpoch=m.startEpoch;meet.context.count=1;plan(meet);
    assert(meet.crowding8p && meet.recommendation==original.recommendation);
    assert(meet.routeA.primary.arrivalEpoch==original.routeA.primary.arrivalEpoch);
    assert(athleticMeetToday(meet.context,m.startEpoch));assert(athleticMeetToday(meet.context,m.endEpoch-1));
    assert(!athleticMeetToday(meet.context,m.endEpoch));
    m.startEpoch+=86400;m.endEpoch+=86400;plan(meet);assert(!meet.crowding8p);
    m.startEpoch-=86400;m.endEpoch-=86400;meet.context.sources[2].fetchedEpoch=now-172800;
    plan(meet);assert(!meet.crowding8p);
    ContextPollState poll;assert(contextPollDue(poll,0));recordContextPoll(poll,0,true);
    assert(!contextPollDue(poll,6*3600*1000-1));assert(contextPollDue(poll,6*3600*1000));
    for(int i=0;i<10;++i) {recordContextPoll(poll,0,false);assert(poll.nextMs>=900000 && poll.nextMs<=21600000);}
    recordContextPoll(poll,UINT32_MAX-1000,true);assert(!contextPollDue(poll,100));
    assert(contextPollDue(poll,poll.nextMs));
    CommutePlannerSettings settings;assert(settings.route106EarlyRideMinutes==12 && settings.route8pEarlyRideMinutes==17 && settings.route118RideMinutes==35);
    assert(settings.routeAWalkMinutes==4 && settings.routeBWalkMinutes==13 && settings.boardingBufferMinutes==2 && settings.transferBufferMinutes==2);
    auto leave=fixture();plan(leave,0);assert(leave.routeA.assessment==CommuteAssessment::Unavailable);
    std::cout<<"Context planner: scoped/operator/time/stale/fallback/crowding/cache/calibration checks PASS\n";
}
