#pragma once
#include "core/CommuteContextCore.h"
class CommuteContextClient {
public:
    void refresh(transitink::CommuteContextSnapshot& snapshot, int64_t now);
private:
    transitink::ContextPollState poll_;
};
