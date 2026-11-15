// Visible and remembered enemy burst estimates for movement.
#pragma once
static float step_burst(vec2 q) {
    float b = 0.f;
    for (auto& e : ENEMY) {
        // spawn campers count too (the step guard, recall cancel)
        if (!e.vis || !e.alive || e.hp <= 0 || standoff_ignored(e) || chan_ignored(e)) continue;
        b += danger_at(enemy_power(e), cheb(e.p, q));
    }
    return b;
}
static int SG_HOLDS = 0;   // turns in a row a plain walk was held
struct SgEnd { ~SgEnd() { if (SG_ON) SG_HOLDS = (SG_SKIP > 0 && !ME.walked) ? SG_HOLDS + 1 : 0; SG_ON = false; } };
// the other visible enemies' burst on tile q before our next turn (the trapped check's sum in brain_fight_turn.h, skipping one enemy)
static float fin_threat(vec2 q, const Champ* skip) {
    float b = 0.f;
    for (auto& e : ENEMY) {
        if (&e == skip || !targetable(e) || chan_ignored(e)) continue;
        b += danger_at(enemy_power(e), cheb(e.p, q));
    }
    return b;
}
#include "../models/flog.h"
#include "../models/fightmodel2.h"   // round-2 model (on-policy data, diverse opponent pool)
