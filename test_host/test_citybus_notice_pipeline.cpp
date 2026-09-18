#include "core/CommuteContextJson.h"
#include "core/CommuteDashboardCore.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#define CHECK(x) do { if(!(x)) { std::cerr << "FAIL " << __LINE__ << '\n'; return 1; } } while(false)
using namespace transitink;
int main(int argc, char** argv) {
    CHECK(argc==2);
    std::ifstream f(argv[1]);
    std::string body((std::istreambuf_iterator<char>(f)),{});
    // Synthetic dates; a real Citybus ETA remains live in the omitted-stop case.
    const int64_t now=1894018800;
    CommuteContextSnapshot context;
    CHECK(parseCommuteContext(body,now,context));
    CHECK(context.count==1 && context.events[0].operatorMask==1);
    CHECK(context.events[0].route==0 && context.events[0].scope==ContextScope::Transfer);
    CHECK(context.events[0].severity==ContextSeverity::HardBlock);
    CommuteDashboardSnapshot original;
    const int minutes[3][3]={{10,16,22},{20,26,32},{19,26,32}};
    for(unsigned stream=0;stream<3;++stream) {
        auto& eta=original.etas[stream];eta.count=3;eta.fetchedAtEpoch=now;
        for(unsigned i=0;i<3;++i) {
            eta.epochs[i]=now+minutes[stream][i]*60;
            eta.evidenceEpochs[i]=now;eta.operators[i]=2;eta.scheduled[i]=false;
        }
    }
    auto plan=[&](CommuteDashboardSnapshot& d) {
        planCommuteDashboard(d,now,now+65*60,true,{},CommuteSessionMode::Manual,380);
    };
    plan(original);CHECK(original.routeA.primary.valid && original.routeA.primary.connectionConfirmed);
    auto blocked=original;blocked.context=context;plan(blocked);
    CHECK(!blocked.routeA.primary.valid && blocked.recommendation==CommuteChoice::RouteB);
    CHECK(!blocked.routeA.primary.provisional);
    for(unsigned i=0;i<3;++i) {
        CHECK(blocked.etas[i].epochs==original.etas[i].epochs);
        CHECK(blocked.etas[i].operators==original.etas[i].operators);
        CHECK(blocked.etas[i].scheduled==original.etas[i].scheduled);
    }
    auto joint=blocked;joint.etas[0].operators[1]=0;plan(joint);
    CHECK(joint.routeA.primary.valid && joint.routeA.primary.firstOperator==0);
    auto fallback=blocked;fallback.etas[1].count=0;plan(fallback);
    CHECK(!fallback.routeA.primary.valid && !fallback.routeA.primary.provisional);
    auto expired=original;expired.context=context;expired.context.events[0].endEpoch=now;plan(expired);
    CHECK(expired.routeA.primary.valid);
    auto future=original;future.context=context;future.context.events[0].startEpoch=now+86400;plan(future);
    CHECK(future.routeA.primary.valid);
    CommutePlannerSettings settings;
    CHECK(settings.route106EarlyRideMinutes==12 && settings.route8pEarlyRideMinutes==17 && settings.route118RideMinutes==35);
    std::cout << "Synthetic official discovery/PDF text -> JSON -> production parser/planner: PASS\n";
}
