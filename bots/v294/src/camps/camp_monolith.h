// Monolith positioning, commitment and quorum rules.
#pragma once
static inline int mono5_reach(const Group& g) {
    int r = 0;
    for (int k = 0; k < g.n; ++k) if (g.m[k].str > 0) r = std::max(r, g.m[k].reach);
    return r;
}
static inline bool mono5_on(const Group& g) { return g.monolith && mono5_reach(g) > 4; }
static inline int mono5_roster_need() {
    return std::max(1, std::min(MONOLITH_QUORUM, (int)ALLY.size()));
}
// since the 2026-10 patch the monolith reaches 5, past every spell (4), so the siege ring at 4 sits inside its
// reach and "never stand in its reach" means dealing no damage. It hits only its nearest champion each round and never
// heals: commit when the bodies here can absorb the whole clear (rounds to kill x its Strength fits in the health they
// have above the line where they leave), then fight it like any camp (melee adjacent, ranged at 3, basic attacks and
// spells); the one it hits steps out when low. Not enough bodies: wait just outside its reach, give up after a while.
static float champ_dps(CT t, float str, float focus);
static float mono5_str(const Group& g) {
    float s = 0;
    for (int k = 0; k < g.n; ++k) s = std::max(s, g.m[k].str);
    // monoliths side by side all hit the same nearest champion: their sum
    if (g.id == GATE_G) { s = 0; for (int k = 0; k < g.n; ++k) if (g.m[k].hp > 0) s += g.m[k].str; }
    return s;
}
static float mono5_pool(float hp, float mhp, float mstr) {
    return std::max(0.f, hp - std::max(2.f * mstr, mhp * RETREAT_HEALTH));
}
// an ally at monolith g that counts as a body, also one out of our sight that claims it and was last seen
// there: walls hide waiters from each other, so the count changed with our own tile (4 at (18,35) -> M5GO, step in,
// 2 at (19,34) -> M5WAIT, step out ...: 45 rounds on contest)
static bool m5_body(const Champ& a, const Group& g) {
    if (&a == &ALLY[MY_SLOT] || !a.alive || a.channel || a.hp <= 0) return false;
    if (a.vis) return cheb(a.p, g.center) <= 7;
    const int slot = (int)(&a - &ALLY[0]);
    return slot < 6 && claim_group(slot) == g.id && a.last_p.x >= 0 && rnd() - a.seen_at <= MONOLITH_HELPER_STALE &&
           cheb(a.last_p, g.center) <= 7;
}
static bool mono5_commit(const Group& g, float mstr, int& bodies) {
    float dps = champ_dps(ME.type, ME.str, ME.focus), pool = mono5_pool(ME.hp, ME.mhp, mstr);
    bodies = 1;
    for (auto& a : ALLY) {
        if (!m5_body(a, g)) continue;
        dps += champ_dps(a.type, a.str, a.focus); pool += mono5_pool(a.hp, a.mhp, mstr); ++bodies;
    }
    return g.hp_left / std::max(1.f, dps) * mstr <= pool;
}
static int m5_g = -1, m5_since = -1, m5_last = -100;
static bool m5_alone = false;   // the quorum wait has no one coming
static bool is_leader();
static bool fight_mono5(Group& g, std::vector<Mon*>& M) {
    const int mr = mono5_reach(g), r = rnd();
    const float mstr = mono5_str(g);
    const Field* f = gfield(g);
    // a group out of view keeps its members' last "visible" flags and health: walk closer until we see it (waiting
    // behind a wall at 6 we read 20000 for 200 rounds while it stood at 284)
    if (g.seen_at != r) {
        // a monolith never moves and never heals: seen within MONOLITH_HELPER_STALE rounds, wait by its remembered tiles and
        // health. Walking in until it was in sight each round fought the wait, which steps back out of its reach (5)
        // where walls hide it: M5WAIT / M5LOOK every other round (GATHER <-> LOOK 170 steps a suite)
        if (rnd() - g.seen_at <= MONOLITH_HELPER_STALE) {
            M.clear();
            for (int k = 0; k < g.n; ++k) if (g.m[k].hp > 0) M.push_back(&g.m[k]);
        }
        if (M.empty())
        {
            const int cur = f ? f->at(ME.p) : UNREACH;
            if (cur != UNREACH) step_field(*f, cur > mr + 1 ? mr + 1 : std::max(1, cur - 1));
            else step_greedy(g.center);
            return true;
        }
    }
    int dmin = 99;
    std::vector<vec2> th;
    for (auto* m : M) if (m->str > 0) { dmin = std::min(dmin, cheb(ME.p, m->p)); th.push_back(m->p); }
    int bodies = 1;
    bool commit = mono5_commit(g, mstr, bodies);
    m5_alone = false;
    // it never heals, so damage dealt is never lost: fight it whenever two of us are here (the strict
    // check passes only once it is below ~5000 hp, so we neither sieged nor farmed)
    // two or three weak champions chipped a 20000-hp monolith for 350 rounds (51 hp a round, 11 deaths) and
    // the enemy took the last hit; the team that waits and comes with five clears it in ~40 rounds. So: only
    // with MONOLITH_QUORUM bodies here (or the full roster when smaller), unless it is down to MONOLITH_LOW_HEALTH.
    // The quorum is based on roster size rather than ALIVE_ALLIES: shrinking it as allies died let a lone survivor keep
    // returning to the objective and feed. Not committed:
    // wait outside its reach, then leave it for gold.
    bool m5hold = false;
    if (bodies >= 2 && !commit) {
        if (bodies < mono5_roster_need() && g.hp_left > MONOLITH_LOW_HEALTH) {
            m5hold = true;
        }
    }
    if (!m5hold && bodies >= 2) commit = true;
    // a wait is only for bodies that are coming (claim it): when the bodies here and those coming are fewer
    // than the commit needs (the MONOLITH_QUORUM quorum while it is healthy, else two), leave now. The wait stood 30 rounds,
    // left for 60, came back and stood again all game while the others worked elsewhere (Ren and Marcella
    // ~700 rounds at M5WAIT1/2)
    if (!commit) {
        int coming = 0;
        for (int s = 0; s < (int)ALLY.size() && s < 6; ++s)
            if (s != MY_SLOT && ALLY[s].alive && claim_group(s) == g.id && !m5_body(ALLY[s], g)) ++coming;
        const int need = (g.hp_left > MONOLITH_LOW_HEALTH) ? mono5_roster_need() : 2;
        m5_alone = bodies + coming < need;
    }
    // rotation: low and in its reach -> step out, the next nearest takes the hits
    // at the gate only the nearest of us is hit; the others keep fighting behind it
    bool hit_me = true;
    if (g.id == GATE_G)
        for (auto& a : ALLY) {
            if (&a == &ALLY[MY_SLOT] || !a.vis || !a.alive || a.channel || a.hp <= 0) continue;
            int da = 99;
            for (auto* m : M) if (m->str > 0) da = std::min(da, cheb(a.p, m->p));
            if (da < dmin) hit_me = false;
        }
    if (dmin <= mr && ME.hp <= 2.f * mstr && hit_me && ME.can_move && !th.empty()) {
        step_away(th);
        return true;
    }
    if (commit) {
        m5_g = -1;
        const int reach = attack_reach(ME.melee);
        const float incoming = group_incoming(g, ME.p);
        int dall = 99;
        for (auto* m : M) dall = std::min(dall, cheb(ME.p, m->p));
        if (dall > reach) {
            bool moved = false;
            if (!moved) { if (f) step_field(*f, reach, dall > reach + 3); else step_greedy(g.center); }
        }
        camp_spells(M, incoming);
        Mon* t = pick_target(M, reach);
        if (t) basic_attack_camp(*t);
        camp_reset(M);
        return true;
    }
    if (m5_g != g.id || r - m5_last > 3) { m5_g = g.id; m5_since = r; }
    m5_last = r;
    if (dmin <= mr && ME.can_move && !th.empty()) step_away(th);
    // A wall can leave the live member only 6/7 tiles away while the route to
    // the remembered objective is much longer. Keep a stale solo leader
    // traveling through that outer ring instead of waiting until M5SKIP.
    else if (f && dmin > mr + 2) step_field(*f, mr + 1);
    else if (f && dmin > mr && bodies == 1 && g.seen_at != r &&
             gdist(g, ME.p) > mr + 2 && is_leader()) {
        step_field(*f, mr + 1);
    }
    // The remote leader has no local evidence for vetoing the shared target.
    // Preserve travel and local quorum decisions; only defer its stale solo skip.
    const bool remote_leader = bodies == 1 && g.seen_at != r && gdist(g, ME.p) > mr + 2 && is_leader();
    if (remote_leader) { m5_g = -1; }
    if (!remote_leader && (r - m5_since > MONOLITH_HOLD_MAX || m5_alone)) {
        g.skip_until = r + 2 * MONOLITH_HOLD_MAX; KNOW_CHANGED = true; m5_g = -1;
    }
    return true;
}

// Work on a camp group that is alive. Returns true if we did something.
// at our usual distance from a living job group and none of its tiles is in view (walls or a bush hide it):
// the usual "walk to distance 3/6 and look" never sees it, so we stood there for hundreds of rounds. Walk closer along
// its field; next to it and still blind, step into a bush next to a member (inside a bush we see the whole bush) or
// to another tile next to it; skip the group for a while if that does not help either.
static int blind_g = -1, blind_n = 0, blind_last = -100;
