#include "core/CommuteDashboardCore.h"
#include "core/CommuteBusCore.h"
#include <cassert>
#include <iostream>
using namespace transitink;
constexpr int64_t now=1894018800;
CommuteDashboardSnapshot input() {
    CommuteDashboardSnapshot d;d.context.generatedEpoch=now;d.context.alternateCheckedMask=3;
    for(auto& s:d.context.sources){s.coverage=1;s.fetchedEpoch=now;}
    const int offsets[4][3]={{10,16,22},{26,32,38},{19,26,32},{26,31,37}};
    for(int s=0;s<4;++s){auto& e=d.etas[s];e.count=3;e.fetchedAtEpoch=now;
        for(int i=0;i<3;++i){e.epochs[i]=now+offsets[s][i]*60;e.evidenceEpochs[i]=now;e.operators[i]=2;}}
    return d;
}
CommuteContextEvent notice(ContextScope scope=ContextScope::Transfer,uint8_t route=0,uint8_t mask=1) {
    CommuteContextEvent e;e.id="synthetic-disruption";e.scope=scope;e.route=route;e.operatorMask=mask;
    e.severity=ContextSeverity::HardBlock;e.verified=e.towardYueWan=true;
    e.startEpoch=now-3600;e.endEpoch=now+86400;e.fetchedEpoch=now;return e;
}
void plan(CommuteDashboardSnapshot& d,int64_t target=now+65*60){planCommuteDashboard(d,now,target,true,{},CommuteSessionMode::Manual,380);}
int main(){
    auto base=input();plan(base);assert(base.routeA.primary.valid&&!base.routeA2.primary.valid);
    assert(base.routeA.primary.firstLegRideMinutes==12&&base.routeA.primary.connectionRideMinutes==17);
    assert(base.routeB.primary.firstLegRideMinutes==35);
    auto a=input();a.context.events[0]=notice();a.context.count=1;plan(a);
    assert(!a.routeA.primary.valid&&a.routeA2.primary.valid&&a.recommendation==CommuteChoice::RouteA2);
    const auto& t=a.routeA2.primary;
    assert(t.alternateTransfer&&t.firstOperator==2&&t.firstLegRideMinutes==12&&t.connectionRideMinutes==12);
    assert(t.transferArrivalEpoch==now+22*60&&t.transferReadyEpoch==now+26*60&&t.riskTransferReadyEpoch==now+28*60);
    assert(t.connectionEpoch==now+31*60&&!t.connectionAtRisk&&t.riskArrivalEpoch==now+45*60);
    assert(a.routeA2.nominalFastest.connectionEpoch==now+26*60&&a.routeA2.nominalFastest.connectionAtRisk);
    assert(a.routeA2.fallback.connectionEpoch==now+37*60);
    assert(a.etas[3].epochs==base.etas[3].epochs&&a.etas[0].epochs==base.etas[0].epochs);
    auto partial=a;partial.etas[0].partial=partial.etas[0].stale=true;
    plan(partial);assert(partial.routeA2.primary.valid); // Other operator failure does not age this row.
    partial.etas[0].rowStale.fill(true);plan(partial);assert(!partial.routeA2.primary.valid);
    auto joint=a;joint.etas[0].operators[1]=0;plan(joint);
    assert(joint.routeA.primary.valid&&joint.routeA.primary.firstOperator==0);
    assert(joint.routeA2.primary.firstOperator==2);
    auto kmbOnly=a;kmbOnly.context.alternateCheckedMask=2;kmbOnly.context.events[0].operatorMask=2;
    kmbOnly.etas[0].operators.fill(0);plan(kmbOnly);assert(kmbOnly.routeA2.primary.valid&&kmbOnly.routeA2.primary.firstOperator==0);
    auto normalEight=input();normalEight.context.events[0]=notice(ContextScope::Boarding,1);normalEight.context.count=1;
    plan(normalEight);assert(!normalEight.routeA.primary.valid&&normalEight.routeA2.primary.valid);
    auto restored=a;restored.context.events[0].endEpoch=now+60;plan(restored);assert(!restored.routeA2.primary.valid&&restored.routeA.primary.valid);
    auto unrelated=input();unrelated.context.events[0]=notice(ContextScope::AlternateAlighting);unrelated.context.count=1;
    plan(unrelated);assert(unrelated.routeA.primary.valid&&!unrelated.routeA2.primary.valid);
    auto overlap=a;overlap.etas[0].count=1;overlap.context.events[1]=notice(ContextScope::AlternateAlighting);
    overlap.context.events[1].startEpoch=now+23*60;overlap.context.events[1].endEpoch=now+23*60+30;overlap.context.count=2;
    plan(overlap);assert(!overlap.routeA2.primary.valid);
    for(auto scope:{ContextScope::AlternateAlighting,ContextScope::AlternateBoarding}){
        auto blocked=a;blocked.context.events[1]=notice(scope,scope==ContextScope::AlternateAlighting?0:1);blocked.context.count=2;plan(blocked);
        assert(!blocked.routeA2.primary.valid);
        blocked.context.events[1].severity=ContextSeverity::Degraded;plan(blocked);assert(!blocked.routeA2.primary.valid);
    }
    for(int variation=0;variation<8;++variation){auto stale=a;
        if(variation==0)stale.context.generatedEpoch=now-172800;
        if(variation==1)stale.context.alternateCheckedMask=0;
        if(variation==2)stale.context.lastFetchFailed=true;
        if(variation==3)stale.context.events[0].fetchedEpoch=now-172800;
        if(variation==4)stale.context.events[0].severity=ContextSeverity::Advisory;
        if(variation==5)stale.context.events[0].towardYueWan=false;
        if(variation==6)stale.context.events[0].startEpoch=now+1;
        if(variation==7)stale.context.events[0].endEpoch=now;
        plan(stale);assert(!stale.routeA2.primary.valid);
    }
    auto fasterB=a;for(auto& e:fasterB.etas[3].epochs)if(e)e+=40*60;plan(fasterB);
    assert(fasterB.recommendation==CommuteChoice::RouteB);
    auto meet=a;auto& e=meet.context.events[1];e=notice(ContextScope::Route,1);e.athleticMeet=true;
    e.severity=ContextSeverity::Advisory;e.startEpoch=(now+28800)/86400*86400-28800;e.endEpoch=e.startEpoch+86400;meet.context.count=2;
    plan(meet);assert(meet.crowding8p&&meet.routeA2.primary.valid);
    plan(a,0);assert(a.routeA2.primary.valid&&a.routeA2.assessment==CommuteAssessment::Unavailable);
    assert(a.routeA2.primary.connectionEpoch==now+26*60&&a.routeA2.primary.connectionAtRisk);
    const auto& cfg=commuteBusFeedConfigs()[3];assert(cfg.citybus.stopId=="001251"&&cfg.citybus.directionId=="I"&&cfg.citybusBoardingSequence==6);
    BusEtaRecord row;row.operatorId=BusOperator::Citybus;row.routeId="8P";row.directionId="I";row.stopId="001251";row.stopSequence=6;
    assert(commuteEtaRecordMatches(CommuteEtaKind::Bus8PHingFat,row));assert(!commuteEtaRecordMatches(CommuteEtaKind::Bus8P,row));
    row.directionId="O";assert(!commuteEtaRecordMatches(CommuteEtaKind::Bus8PHingFat,row));
    // Synthetic replay variants: successful short rides, four-minute connection,
    // useful alternative to 118, faster alternative, and first-leg-only evidence.
    for(int shift:{0,3,7,11,14}){auto replay=input();replay.context.events[0]=notice();replay.context.count=1;
        for(auto& stream:replay.etas)for(auto& epoch:stream.epochs)if(epoch)epoch+=shift*60;
        plan(replay,now+100*60);assert(replay.routeA2.primary.valid);
        assert(replay.routeA2.primary.transferReadyEpoch-replay.routeA2.primary.transferArrivalEpoch==240);
    }
    auto incomplete=input();incomplete.context.events[0]=notice();incomplete.context.count=1;incomplete.etas[3]={};plan(incomplete);
    assert(incomplete.routeA2.primary.firstLegValid&&!incomplete.routeA2.primary.valid&&!incomplete.routeA2.primary.connectionConfirmed);
    std::cout<<"A2 scoped activation, risk, ETA identity and synthetic replay checks passed\n";
}
