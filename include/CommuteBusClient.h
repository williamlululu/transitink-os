#pragma once

#include <Arduino.h>

#include "CitybusClient.h"
#include "KmbClient.h"
#include "core/CommuteDashboardCore.h"

class CommuteBusClient {
public:
    CommuteBusClient(KmbClient& kmb, CitybusClient& citybus)
        : kmb_(kmb), citybus_(citybus) {}

    bool refresh(transitink::CommuteDashboardSnapshot& dashboard,
                 int64_t nowEpoch,
                 String& error);

private:
    KmbClient& kmb_;
    CitybusClient& citybus_;
};
