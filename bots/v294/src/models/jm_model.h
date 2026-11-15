// jm_model.h: the job model's math. The two logistic models fitted on
// ladder games, the camp terms (get / they take / kills) and the two decision rules. Pure: no game types, so
// jm_test.cpp checks it natively against the fit (jm_golden.h). The inputs come from jm.h.
#pragma once
#include <algorithm>
#include <cmath>
#include "jm_coef.h"
// the job score's death and kill prices, set each round from the game state (by default 1, JM_DC0, JM_KV)
static float JM_CARRY_K = 1.f, JM_DC0_NOW = JM_DC0, JM_KV_NOW = JM_KV;
static inline float jm_sig(float z) { return 1.f / (1.f + std::exp(-z)); }
// P(caught) on the way to a job and at it, from the champion's memory (jm_job_feats)
static inline float jm_pcaught(const float f[20]) {
    float z = JM_PC_W[0];
    for (int k = 0; k < 20; ++k) z += JM_PC_W[k + 1] * (f[k] - JM_PC_MU[k]) / JM_PC_SD[k];
    return jm_sig(z);
}
// a camp job's outcome as the bot sees it when it picks the job: w = JM_GET (our team takes the camp's next
// capture) or JM_THEY (they take it); con = contested (an enemy seen within 7 of it in the last 15 rounds), lbal = log
// camp Balance, S = the team switch, walk20 = steps / 20
static inline float jm_camp_p(const float w[5], bool con, float lbal, int S, float walk20) {
    const float c = con ? 1.f : 0.f;
    return jm_sig(w[0] + w[1] * c + w[2] * c * lbal + w[3] * S / 16.f + w[4] * walk20);
}
static inline float jm_pget(bool con, float lbal, int S, float walk20) { return jm_camp_p(JM_GET, con, lbal, S, walk20); }
static inline float jm_ptake(bool con, float lbal, int S, float walk20) { return jm_camp_p(JM_THEY, con, lbal, S, walk20); }
// enemy deaths near a contested job camp in its window, by camp Balance (exp(lbal)) < 0.5 / < 1 / < 2 / 2 and up
static inline float jm_pkill(float lbal) {
    return JM_PK[lbal < -0.693147182f ? 0 : lbal < 0.f ? 1 : lbal < 0.693147182f ? 2 : 3];
}
// a job's score: [what we get + the take we deny + the kills we expect - what dying costs] per round of the job. A camp
// job's value counts at P(get); a siege (camp false) keeps its whole value.
static inline float jm_score(float value, float share, float den_time, float pc, int gems, bool camp, bool contested, float lbal,
                             int S, float walk20, float xg = 1.f) {
    // xg = the job's length in fitted jobs (P(get) holds once per fitted job length)
    const float pg = camp ? std::pow(jm_pget(contested, lbal, S, walk20), xg) : 1.f;
    const float con = camp && contested ? value * pg * jm_ptake(true, lbal, S, walk20) + jm_pkill(lbal) * JM_KV_NOW : 0.f;
    return (pg * value * share + con - pc * (gems * JM_CARRY_K + JM_DC0_NOW)) / den_time;
}
// damage per round by champion type id: melee, focus / 8, extra str, focus share, the 1.125 rate, low-HP rage
struct JmDps { float m, f8, fs, sh, rs, hor; };
static constexpr JmDps JM_TAB[13] = {
    {1, 1.0f, 0, 0, 1, 0}, {0, 0, 0, 0, 1, 0}, {1, 0, .15f, 0, 1, 0}, {0, 2.0f, 0, 0, 0, 0}, {1, 0, 0, 0, 1, 0},
    {0, 3.4f, 0, 0, 0, 0}, {0, 2.5f, 0, 0, 0, 0}, {1, 2.0f, 0, 0, 1, 0}, {0, 0, 0, 0, 1, 0}, {1, 1.3f, 0, 0, 0, 0},
    {1, 0, 0, .3f, 1, 0}, {0, 1.6f, 0, 0, 1, 0}, {1, 1.5f, 0, 1.5f / 8, 0, 1}};
static inline float jm_dps(int type, float str, float focus, float hp, float mhp) {
    const JmDps t = type >= 0 && type < 13 ? JM_TAB[type] : JmDps{1, 0, 0, 0, 1, 0};
    float d = str * (t.m != 0.f ? 1.f : 0.9f) * (t.rs != 0.f ? 1.125f : 1.f) + focus * t.f8 / 8 + str * t.fs + focus * t.sh;
    if (t.hor != 0.f) d *= 1 + 0.5f * std::max(0.f, std::min(1.f, 1 - hp / std::max(1.f, mhp)));
    return d;
}
// log Balance (our time to die over theirs), clipped to [-3, 3]; no enemy damage = +3
static inline float jm_lbal(float hu, float du, float ht, float dt) {
    if (dt <= 0.f) return 3.f;
    const float b = (hu / std::max(1.f, dt)) / std::max(0.1f, ht / std::max(1.f, du));
    return std::max(-3.f, std::min(3.f, std::log(std::max(b, 1e-3f))));
}
