// wmodel.h: state-dependent prices for job choice from an observable-state win model (wmodel_coef.h).
// The model's own levels are not used (it cannot see the enemy's bag or gold); only how each price moves with the
// state at a round, against the median state of the ladder games at that round:
//   W x  (dG/dB) / median   gold's price in banked gems (multiplies the learned W(r) = gw_job)
//   carry x (dC/dB) / median   a carried gem lost on death
//   dc0 x  median dB / dB      JM_DC0, the rest of a death (respawn time) in banked gems
//   kill x (-dT/dB) / median   JM_KV, an enemy death (it slows their banking)
// dB, dT, dG, dC = dz/d(our banked, their banked, our earned gold, our carried). Each factor is clamped to [0.4, 1.6].
// At the median state every factor is 1 (the plain prices); near the goal gold gets cheaper and a carried bag dearer.
#pragma once
#include "wmodel_coef.h"
namespace wm {
static int AT = -1;
static float W_MOD = 1.f;
static inline float clampm(float x) { return std::max(0.4f, std::min(1.6f, x)); }
static void update(int r) {
    if (r == AT) return;
    AT = r;
    W_MOD = 1.f; groi::WM_GW = 1.f; JM_CARRY_K = 1.f; JM_DC0_NOW = JM_DC0; JM_KV_NOW = JM_KV;
    if (r < (int)WM_T[0][0]) return;
    int i = std::min(WM_N - 2, (int)((r - WM_T[0][0]) / 25.f));
    const float a = std::min(1.f, std::max(0.f, (r - WM_T[i][0]) / (WM_T[i + 1][0] - WM_T[i][0])));
    float c[11];
    for (int k = 0; k < 11; ++k) c[k] = WM_T[i][k] * (1.f - a) + WM_T[i + 1][k] * a;
    const float w1 = c[1], w2 = c[2], w3 = c[3], w5 = c[4], w6 = c[5], G = c[6];
    const float B = (float)our_banked(), Bt = (float)their_banked(), C = (float)team_carried();
    const float nu = std::max(20.f, 1000.f - B), nt = std::max(20.f, 1000.f - Bt);
    const float dB = w2 / nu + w3 * C / (nu * nu) + w5 * G / (nu * nu) + w6 / 100.f;
    if (!(dB > 0.f)) return;
    const float dT = -w2 / nt - w6 / 100.f, dG = w1 / 100.f + w5 / nu, dC = w3 / nu;
    W_MOD = clampm(dG / dB / c[7]);
    groi::WM_GW = W_MOD;
    JM_CARRY_K = clampm(dC / dB / c[8]);
    JM_DC0_NOW = JM_DC0 * clampm(c[9] / dB);
    JM_KV_NOW = JM_KV * clampm(-dT / dB / c[10]);
}
}  // namespace wm
