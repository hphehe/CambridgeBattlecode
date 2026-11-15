// The team opening, champion side: every body walks its squad's camp list from the draft (draft/opening.h).
// The list is common knowledge, so nothing has to be sent: each body moves along it by what it sees and by the allies' claim bytes.
#pragma once

static bool in_my_squad(int slot) { return (SQ_MASK >> (int)(slot == MY_SLOT ? ME.type : ALLY[slot].type)) & 1; }

// Who collects a cleared stop: the planner's rule, among the members still on the list (alive, and not on a home trip).
static int squad_collector(const Group& g) {
    OpBody b[OP_MAX_SQUADS];
    int slot[OP_MAX_SQUADS], k = 0;
    for (int s = 0; s < (int)ALLY.size() && s < OP_MAX_SQUADS; ++s) {
        if (!in_my_squad(s)) continue;
        if (s != MY_SLOT && (!ALLY[s].alive || claim_group(s) < 0)) continue;
        const CT t = s == MY_SLOT ? ME.type : ALLY[s].type;
        b[k] = OpBody{t, base_dps(t), 0.f};
        slot[k++] = s;
    }
    return slot[op_taker(b, k, g.gems, g.gold)];
}

static int squad_stop() { return SQ_ON ? (int)SQ_STOP[SQ_AT] : -1; }

// The list is a plan for a camp nobody contests. With enemy champions at my stop the question is the planner's own: does the squad
// still finish what is left of this camp, with their damage on top of the monsters'?
static bool squad_stop_lost(const Group& g) {
    if (g.st != ST_ALIVE) return false;
    float extra = 0.f;
    for (auto& e : ENEMY) {
        if (!e.vis || !e.alive || e.hp <= 0) continue;
        int d = 99;
        for (int k = 0; k < g.n; ++k) if (g.m[k].hp > 0) d = std::min(d, cheb(e.p, g.m[k].p));
        if (d <= OP_GATHER_NEAR) extra += ally_dps_est(e.type);
    }
    if (extra <= 0.f) return false;
    float mh[OP_MAX_MONSTERS], ms[OP_MAX_MONSTERS], harmless = 0.f;
    int nm = 0;
    for (int k = 0; k < g.n; ++k) if (g.m[k].hp > 0) op_add_monster(mh, ms, nm, harmless, g.m[k].hp, g.m[k].str);
    op_seal(mh, ms, nm, harmless);
    if (nm == 0) return false;
    OpBody b[OP_MAX_SQUADS];
    float avail[OP_MAX_SQUADS];
    int k = 0;
    for (int s = 0; s < (int)ALLY.size() && s < OP_MAX_SQUADS; ++s) {
        if (!in_my_squad(s)) continue;
        if (s != MY_SLOT && (!ALLY[s].alive || claim_group(s) != g.id)) continue;
        const CT t = s == MY_SLOT ? ME.type : ALLY[s].type;
        const float mhp = s == MY_SLOT ? ME.mhp : ALLY[s].mhp;
        const float hp = s == MY_SLOT ? ME.hp : (ALLY[s].vis ? ALLY[s].hp : mhp);
        b[k] = OpBody{t, ally_dps_est(t), hp};
        avail[k++] = std::max(0.f, hp - mhp * RETREAT_HEALTH);
    }
    int front[OP_MAX_SQUADS];
    op_front(b, k, front);
    float spend[OP_MAX_SQUADS], dps_of[OP_MAX_SQUADS];
    for (int i = 0; i < k; ++i) { spend[i] = avail[front[i]]; dps_of[i] = b[front[i]].dps; }
    float clear, hit;
    return !op_fight(nm, mh, ms, spend, dps_of, k, extra, clear, hit);
}

// Once a turn: past the stops that are done for me. The list ends at its last stop, at the horizon, or when under half the squad lives.
static void squad_tick(int r) {
    if (!SQ_ON) return;
    int alive = 0, size = 0;
    for (int s = 0; s < (int)ALLY.size() && s < OP_MAX_SQUADS; ++s) if (in_my_squad(s)) { ++size; alive += s == MY_SLOT || ALLY[s].alive; }
    if (r >= OP_HORIZON || 2 * alive < size) { SQ_ON = false; return; }
    for (; SQ_AT < SQ_N; ++SQ_AT) {
        const Group& g = G[SQ_STOP[SQ_AT]];
        const bool taken = g.st == ST_RESPAWN && g.ready_at - r > OP_MAX_WAIT;              // captured, by us or by them
        const bool walk_on = g.st == ST_REWARD && squad_collector(g) != MY_SLOT;            // cleared: the collector stays, I go on
        if (taken || walk_on) continue;
        if (squad_stop_lost(g)) { SQ_ON = false; JOB = -1; set_claim(-1, false); KNOW_CHANGED = true; }   // the job engine plays what is on the board
        return;
    }
    SQ_ON = false;
}

// A live monster stands on every way down to the stop: distance fields ignore monsters, so a plugged corridor reads as open.
// Its camp is the job until the way is free. -1 when nothing plugs the way.
static int squad_door(const Group& stop) {
    const Field* f = gfield(stop);
    if (!f) return -1;
    const int cur = f->at(ME.p);
    if (cur == UNREACH) return -1;
    int door = -1;
    for (auto d : DIRS) {
        const vec2 q = ME.p + d;
        if (!inb(q) || BLOCK[idx(q)] || f->at(q) == UNREACH || f->at(q) >= cur) continue;
        int on = -1;
        for (const auto& g : G) {
            if (g.id == stop.id) continue;
            for (int k = 0; k < g.n; ++k) if (g.m[k].vis && g.m[k].hp > 0 && g.m[k].p == q) on = g.id;
        }
        if (on < 0) return -1;
        door = on;
    }
    return door;
}

// The step in choose_job: the stop I am on, or what plugs the way to it; -1 when my list is over.
static int squad_job() {
    const int s = squad_stop();
    if (s < 0) return s;
    const int d = squad_door(G[s]);
    return d >= 0 ? d : s;
}

// A stop whose monsters hit: stand off until the members walking to it with me have arrived, unless the fight is already on.
static bool squad_gathers(Group& g, const std::vector<Mon*>& M, int dmin, const Field* f) {
    if (!SQ_ON || g.id != squad_stop() || g.str_total <= 0) return false;
    const int r = rnd();
    if (r - g.awake_at <= 2 || g.hp_left < g.hp_total - 1.f) { g.gather_since = -1; return false; }
    int coming = 0, here = 0;
    for (int s = 0; s < (int)ALLY.size() && s < OP_MAX_SQUADS; ++s) {
        if (s == MY_SLOT || !in_my_squad(s) || !ALLY[s].alive || claim_group(s) != g.id) continue;
        ++coming;
        if (!ALLY[s].vis || ALLY[s].channel) continue;
        int d = 99;
        for (auto* m : M) d = std::min(d, cheb(ALLY[s].p, m->p));
        here += d <= OP_GATHER_NEAR;
    }
    if (here >= coming) { g.gather_since = -1; return false; }
    if (g.gather_since < 0) g.gather_since = r;
    if (r - g.gather_since > OP_GATHER_WAIT) return false;
    stand_off(g, M, dmin, f);
    return true;
}
