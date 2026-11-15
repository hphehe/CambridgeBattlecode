// What one gold is worth in gems for job choice: the exchange rate learned from a logistic win model on 4602 top-30
// ladder games, W(r) = GOLD_WEIGHT_START * exp(-r / GOLD_WEIGHT_DECAY), times the state factor from wmodel.h.
#pragma once

namespace groi {
static int GWL_AT = -1;
static float GWL = 0;
static float gw_learned(int r) {   // one exp a round
    if (r != GWL_AT) { GWL_AT = r; GWL = GOLD_WEIGHT_START * std::exp(-(float)r / GOLD_WEIGHT_DECAY); }
    return GWL;
}
static float WM_GW = 1.f;   // set by wm::update
static float gw_job(int r) {
    return gw_learned(r) * WM_GW;
}
}  // namespace groi
