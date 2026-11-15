// Camp retaliation estimates and siege approach.
#pragma once
static float group_incoming(const Group& g, vec2 at) {
    float s = 0;
    for (int k = 0; k < g.n; ++k) if (g.m[k].hp > 0 && cheb(g.m[k].p, at) <= g.m[k].reach) s += g.m[k].str;
    return s;
}

// ---------------------------------------------------------------------------------------------
// Siege: casters fight dangerous camps from distance 4, outside every monster's reach (3).
// ---------------------------------------------------------------------------------------------
static float SIEGE_DPS = 0;
static int siege_dps_round = -100;
static float siege_dps() {
    if (rnd() - siege_dps_round < 20) return SIEGE_DPS;
    siege_dps_round = rnd();
    float d = 0;
    for (int s = 0; s < 2; ++s) {
        const Ab& a = ME.ab[s];
        if (!(a.dmg || a.aoe || a.cc) || !(a.camps || a.ground)) continue;
        try { d += bc::self().abilities()[s].damage_amount() / 8.f; } catch (const std::exception&) {}
    }
    SIEGE_DPS = d;
    return d;
}
// the team can clear the jackpot in an ordinary fight (live stats, rechecked in brain.h's ALLIN block); then
// it is fought like any team camp instead of a spell siege from distance 5 (starvation: 4 elks, 20 hp a round)
static bool JP_FIGHT = false;
static int squad_stop();   // brain/squad.h
static bool siege_camp(const Group& g) { return g.id == DOM_G && !JP_FIGHT && g.id != squad_stop(); }   // a squad's stop is fought, the plan counted on it
// a member we see right now within spell range 4
static bool siege_sees(const std::vector<Mon*>& M, int r = 4) {
    for (auto* m : M) if (!m->unseen && cheb(ME.p, m->p) <= r) return true;
    return false;
}

static bool fight_siege(Group& g, std::vector<Mon*>& M) {
    if (g.id == DOM_G) {
        static int jg_id = -1, jg_since = 0, jg_last = -100;
        static vec2 jg_p{-1, -1};
        static float jg_hp = 0;
        // an enemy champion in sight is a contest (spells are saved for it), not a freeze
        bool foe = false;
        for (auto& e : ENEMY) if (e.vis && e.alive && cheb(e.p, ME.p) <= SAVE_CAST_RADIUS) foe = true;
        if (foe || jg_id != g.id || ME.p != jg_p || g.hp_left < jg_hp - 1 || rnd() - jg_last > 2) {
            jg_id = g.id; jg_p = ME.p; jg_hp = g.hp_left; jg_since = rnd();
        } else if (rnd() - jg_since >= JACKPOT_STILL_ROUNDS) { g.skip_until = rnd() + JACKPOT_SKIP_ROUNDS; jg_id = -1; }
        jg_last = rnd();
    }
    if (g.id == DOM_G) SIEGE_R = rnd();   // the jackpot siege also casts with an enemy in sight (spells are its only damage)
    auto min_danger = [&](vec2 q) {
        int d = 99;
        for (auto* m : M) if (m->str > 0) d = std::min(d, cheb(q, m->p));
        return d;
    };
    auto near_target = [&](vec2 q) {
        int d = 99;
        for (auto* m : M) d = std::min(d, cheb(q, m->p));
        return d;
    };
    // a tile is safe from a monster beyond its own reach (engine: elk/Golem 4); stand one tile past the largest
    // reach, where area spells (range 4 + radius 1) still land and nothing hits us
    int SD = 4;
    for (auto* m : M) if (m->str > 0) SD = std::max(SD, m->reach + 1);
    auto unsafe = [&](vec2 q) { for (auto* m : M) if (m->str > 0 && cheb(q, m->p) <= m->reach) return true; return false; };
    const int here_t = near_target(ME.p);
    if (!unsafe(ME.p)) camp_spells(M, 0.f);   // safe here: cast first, a walk would forbid most spells
    // 1. never stand in reach of a dangerous monster
    if (unsafe(ME.p) && ME.can_move) {
        vec2 best{-1, -1}; int bs = -(1 << 30);
        for (auto d : DIRS) {
            const vec2 q = ME.p + d;
            if (!walkable(q)) continue;
            const int md = min_danger(q);
            const int sc = (unsafe(q) ? 0 : 10000) + std::min(md, SD) * 100 - std::abs(near_target(q) - SD) * 10;
            if (sc > bs) { bs = sc; best = q; }
        }
        if (best.x >= 0) try_move_to(best);
    } else if (ME.can_move && (here_t > SD ||
                               (g.id == DOM_G && !siege_sees(M, SD)))) {
        // 2. approach to distance 4 without entering anyone's reach
        const Field* f = gfield(g);
        const bool far = here_t > 8 && f;
        vec2 best{-1, -1};
        if (!far) {
            int bs = here_t * 10;
            for (auto d : DIRS) {
                const vec2 q = ME.p + d;
                if (!walkable(q) || unsafe(q)) continue;
                const int sc = near_target(q) * 10 + danger_cost(q);
                if (sc < bs) { bs = sc; best = q; }
            }
        }
        if (far) step_field(*f, 6, true);
        // the jackpot: walls make the path much longer than the straight line, or we are in range with no member in
        // sight: walk the real path (c1f5: the greedy step parked the team behind a wall for 1,500 rounds)
        else if (g.id == DOM_G && f && f->at(ME.p) != UNREACH &&
                 (f->at(ME.p) > here_t + 2 || (here_t <= SD && !siege_sees(M, SD)))) { step_field(*f, SD, true); }
        else if (here_t > SD && best.x >= 0) try_move_to(best);
    }
    // 3. spells on whatever is in range (they never need a walk unless we walked)
    camp_spells(M, 0.f);
    // 4. basic attacks only on harmless monsters (clusters) in reach
    if (ME.can_attack) {
        Mon* t = nullptr;
        for (auto* m : M) if (m->str <= 0 && cheb(ME.p, m->p) <= attack_reach(ME.melee) && (!t || m->hp < t->hp)) t = m;
        if (t) basic_attack_camp(*t);
    }
    return true;
}
