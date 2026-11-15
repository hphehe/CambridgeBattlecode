// Team counter: is this enemy committed to camp contests, or does it play
// safe? Each champion turns its own contests into evidence (what happened minus the ladder's pooled chance at the camp
// Balance when the contest began; + = committed) and pushes it in quarter steps to a 5-bit shared score (tc_get).
#pragma once
// they hold when we stay / they take the camp when we back off, by camp Balance < 0.5, 0.5-1, 1-2, 2+ (48 ladder games)
static constexpr float TC_PHOLD[4] = {0.602f, 0.429f, 0.463f, 0.368f};
static constexpr float TC_PTAKE[4] = {0.419f, 0.355f, 0.333f, 0.282f};
struct TcContest { int g = -1, b = 0, k0 = 0, last_e = -1000; float e_hp = 1.f; bool active = false; };
struct TcPending { int g = -1, b = 0, k0 = 0; bool ally_cap = false, live = false; };
static TcContest TC_CUR;      // my contest now
static TcPending TC_PB;       // my back-off waiting for its result
static float TC_ACC = 0.f;    // my evidence not yet pushed (under a quarter)
static int TC_OBS_R = -1;     // the round observe() last ran: the tick runs at the end of such turns (main.cpp)
// the 5 bits: slot 4's claim bits when the team has no slot 4, else bit 7 (DANGER) of each slot (world.h TC_DB)
static inline int tc_off(int i) { return TC_DB() ? i * SLOT_BITS + 7 : 4 * SLOT_BITS + i; }
static int tc_get() { int v = 0; for (int i = 0; i < 5; ++i) v |= sm_get(tc_off(i), 1) << i; return v >= 16 ? v - 32 : v; }
static void tc_put(int s) { const int v = s & 31; for (int i = 0; i < 5; ++i) sm_set(tc_off(i), 1, (v >> i) & 1); }
// steps from p to the camp's nearest member tile
static int tc_gd(const Group& g, vec2 p) { int d = 999; for (int k = 0; k < g.n; ++k) d = std::min(d, cheb(g.m[k].p, p)); return d; }
static bool tc_open(const Group& g) { return g.st == ST_ALIVE || g.st == ST_REWARD; }
static bool tc_foe(const Champ& e) { return e.vis && e.alive && e.hp > 0 && !on_enemy_spawn(e.p); }
// the camp Balance: me and the allies I see within 7 of the camp or within FIGHT_ALLY_RADIUS of me, against the enemies I see
// within 7 of it (from its centre less its radius: cheap, job_score calls it)
static float tc_camp_bal(const Group& g) {
    const int rad = g.id < (int)G_RAD.size() ? G_RAD[g.id] : 2;
    Balance cb;
    cb.add_us(ME.hp + my_shield_hp(), my_dps_c());   // HoR passive; shields as hp
    for (int s = 0; s < (int)ALLY.size(); ++s) {
        const Champ& a = ALLY[s];
        if (s == MY_SLOT || !a.vis || !a.alive || a.hp <= 0) continue;
        if (cheb(a.p, g.center) - rad <= 7 || cheb(a.p, ME.p) <= FIGHT_ALLY_RADIUS) cb.add_us(a.hp + shield_hp(a), champ_dps_c(a));
    }
    for (const auto& e : ENEMY) if (tc_foe(e) && cheb(e.p, g.center) - rad <= 7) cb.add_th(e.hp + shield_hp(e) + heal_hp(e), champ_dps_c(e));
    return cb.th_n ? cb.adv() : 99.f;
}
static int tc_bucket(float b) { return b < 0.5f ? 0 : b < 1.f ? 1 : b < 2.f ? 2 : 3; }
static void tc_evidence(float x) {
    TC_ACC += x;
    const int q = (int)(TC_ACC * 4.f);   // whole quarters, toward zero
    if (q != 0) { TC_ACC -= q * 0.25f; tc_put(std::max(-16, std::min(15, tc_get() + q))); }
}
// a contest where we stayed is recorded once: by the lowest slot of us at the camp that I can see
static bool tc_lowest_here(const Group& g) {
    for (int s = 0; s < MY_SLOT && s < (int)ALLY.size(); ++s) {
        const Champ& a = ALLY[s];
        if (a.vis && a.alive && a.hp > 0 && tc_gd(g, a.p) <= 5) return false;
    }
    return true;
}
// an ally I know of stays at the camp (in view within 5, or claiming it)
static bool tc_ally_stays(const Group& g) {
    for (int s = 0; s < (int)ALLY.size(); ++s) {
        if (s == MY_SLOT) continue;
        const Champ& a = ALLY[s];
        if ((a.vis && a.alive && a.hp > 0 && tc_gd(g, a.p) <= 5) || claim_group(s) == g.id) return true;
    }
    return false;
}
static void tc_tick(int r) {
    // a back-off waiting for its result: did they take the camp we left, within 20 rounds?
    if (TC_PB.live) {
        const Group& g = G[TC_PB.g];
        for (int s = 0; s < (int)ALLY.size(); ++s)
            if (s != MY_SLOT && claim_group(s) == g.id && claim_capturing(s)) TC_PB.ally_cap = true;
        if (g.seen_at == r && (g.st == ST_RESPAWN || r >= TC_PB.k0 + 20)) {
            const int capt = g.st == ST_RESPAWN ? g.ready_at - g.respawn : 1 << 30;   // the capture round
            const bool took = !TC_PB.ally_cap && capt >= TC_PB.k0 - 1 && capt <= TC_PB.k0 + 20;
            tc_evidence(took ? 1.f - TC_PTAKE[TC_PB.b] : -TC_PTAKE[TC_PB.b]);
            TC_PB.live = false;
        } else if (r > TC_PB.k0 + 80) TC_PB.live = false;   // never seen again: no evidence
    }
    if (TC_CUR.active) {
        const Group& g = G[TC_CUR.g];
        bool en = false; float ehp = 1.f;
        for (const auto& e : ENEMY)
            if (tc_foe(e) && tc_gd(g, e.p) <= 5) { en = true; ehp = std::min(ehp, e.hp / std::max(1.f, e.mhp)); }
        if (en) { TC_CUR.last_e = r; TC_CUR.e_hp = ehp; }
        const int b = TC_CUR.b;
        if (!tc_open(g)) {   // captured while we were there: if they were still there, they held
            if (r - TC_CUR.last_e <= 2 && tc_lowest_here(g)) tc_evidence(1.f - TC_PHOLD[b]);
            TC_CUR.active = false;
        } else if (tc_gd(g, ME.p) > 5) {   // I left: if they were still there and none of us stays, a back-off
            if (r - TC_CUR.last_e <= 2 && !tc_ally_stays(g)) TC_PB = TcPending{TC_CUR.g, b, r, false, true};
            TC_CUR.active = false;
        } else if (r - TC_CUR.last_e >= 3) {   // they left while I stayed (last seen nearly dead: maybe a kill, no evidence)
            if (TC_CUR.e_hp >= 0.25f && tc_lowest_here(g)) tc_evidence(-TC_PHOLD[b]);
            TC_CUR.active = false;
        }
        if (TC_CUR.active) return;
    }
    // a new contest: the nearest open camp within 5 of me with an enemy within 5 of it, me or that enemy working it (2)
    int best = -1, bd = 99;
    for (const auto& g : G) {
        if (!tc_open(g) || cheb(g.center, ME.p) > 5 + (g.id < (int)G_RAD.size() ? G_RAD[g.id] : 3)) continue;
        const int dme = tc_gd(g, ME.p);
        if (dme > 5 || dme >= bd) continue;
        bool en = false, work = dme <= 2;
        for (const auto& e : ENEMY)
            if (tc_foe(e)) { const int de = tc_gd(g, e.p); if (de <= 5) { en = true; if (de <= 2) work = true; } }
        if (en && work) { best = g.id; bd = dme; }
    }
    if (best >= 0) TC_CUR = TcContest{best, tc_bucket(tc_camp_bal(G[best])), r, r, 1.f, true};
}
// our dead turns: dying at a contest means they held
static void tc_dead() {
    if (TC_CUR.active) {
        if (tc_lowest_here(G[TC_CUR.g])) tc_evidence(1.f - TC_PHOLD[TC_CUR.b]);
        TC_CUR.active = false;
    }
}
