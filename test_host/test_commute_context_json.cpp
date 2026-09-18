#include "core/CommuteContextJson.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <ctime>
#define CHECK(x) do { if(!(x)) {std::cerr<<"FAIL "<<__LINE__<<'\n';return 1;} } while(false)
using namespace transitink;
int main(int argc, char** argv) {
    const std::string body=R"JSON({"schema":1,"generated_at":1894018800,"sources":[{"source":"citybus","coverage":"partial","fetched_at":1894018800},{"source":"kmb","coverage":"partial","fetched_at":1894018800},{"source":"lcsd","coverage":"supported","fetched_at":1894018800}],"events":[{"id":"synthetic-ctb","source":"citybus","source_url":"https://mobile.citybus.com.hk/NWST/special-arrangement-listing.php","route":"106","direction":"toward_yue_wan","operator_mask":1,"scope":"transfer","effect":"stop_omitted","start":1894010000,"end":1894100000,"fetched_at":1894018800,"verified":true,"severity":"hard_block","message_tc":"城巴106維園轉車站暫停"}]})JSON";
    CommuteContextSnapshot result;
    CHECK(parseCommuteContext(body,1894018800,result));CHECK(result.count==1);
    CHECK(result.events[0].severity==ContextSeverity::HardBlock);
    CHECK(!parseCommuteContext(body,1894010000,result));
    for (const auto& mutation: {std::pair<std::string,std::string>{"\"schema\":1","\"schema\":2"},
        {"mobile.citybus.com.hk/","mobile.citybus.com.hk.invalid/"},
        {"\"operator_mask\":1","\"operator_mask\":3"},
        {"\"verified\":true","\"verified\":\"true\""},
        {"\"end\":1894100000","\"end\":1894000000"},
        {"\"scope\":\"transfer\"","\"scope\":\"boarding\""}}) {
        auto copy=body;copy.replace(copy.find(mutation.first),mutation.first.size(),mutation.second);
        if (mutation.first.find("scope")!=std::string::npos) CHECK(parseCommuteContext(copy,1894018800,result));
        else CHECK(!parseCommuteContext(copy,1894018800,result));
    }
    CHECK(!parseCommuteContext(std::string(24577,' '),1894018800,result));
    CHECK(!parseCommuteContext("{}",1894018800,result));
    CHECK(!parseCommuteContext(body.substr(0,100),1894018800,result));
    auto physical=body;
    physical.replace(physical.find("\"operator_mask\":1"),17,"\"operator_mask\":3");
    physical.replace(physical.find("\"effect\":\"stop_omitted\""),23,"\"effect\":\"physical_stop_closure\",\"operator_independent\":true");
    CHECK(parseCommuteContext(physical,1894018800,result));CHECK(result.events[0].operatorMask==3);
    if(argc==2) {
        std::ifstream f(argv[1]);std::string live((std::istreambuf_iterator<char>(f)),{});
        CHECK(parseCommuteContext(live,std::time(nullptr),result));
        std::cout<<"Actual official normalized feed parsed; events="<<result.count<<'\n';
    }
    std::cout<<"Context JSON bounds/types/provenance checks PASS\n";
}
