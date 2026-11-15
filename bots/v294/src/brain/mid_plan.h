// mid_plan.h - the respawn appointment for rich contested camps.
#pragma once

static bool MR_INIT = false;
static std::vector<int> MR_G;                       // rich contested groups
static std::vector<int> MR_ENN, MR_ENR;             // per group: most enemy bodies I saw within 8 of it lately, and when

static void mr_init() {
    MR_INIT = true;
    if (own_spawn.empty() || en_spawn.empty()) return;
    for (const auto& g : G) {
        if (g.gems < MID_RICH_GEMS) continue;
        MR_G.push_back(g.id);   // no distance test; the game's captures decide (mr_ceded)
    }
}

// arrival at group g for a body at p (its health share hf): the walk to its reach, or a heal trip first when low
static int mr_eta(const Group& g, vec2 p, CT ty, float hf) {
    const int walk = std::max(0, gdist(g, p) - attack_reach(champ(ty).melee));
    if (hf >= MID_MIN_HEALTH) return walk;
    const int dh = F_HOME.at(p);
    const int out = dh == UNREACH ? (int)bc::constants::recall_time + 1 : std::min(dh, (int)bc::constants::recall_time + 1);
    return out + 1 + std::min(g.d_home, 200);
}

static void mr_check(int r);
static void mr_tick(int r) {
    if (!MR_INIT) mr_init();
    if (MR_G.empty()) return;
    if (MR_ENN.size() != G.size()) { MR_ENN.assign(G.size(), 0); MR_ENR.assign(G.size(), -1000); }
    for (int gid : MR_G) {
        const Group& g = G[gid];
        int en = 0;
        for (const auto& e : ENEMY) {
            if (!e.vis || !e.alive || on_enemy_spawn(e.p)) continue;
            int d = 99;
            for (int k = 0; k < g.n; ++k) d = std::min(d, cheb(e.p, g.m[k].p));
            if (d <= 8) ++en;
        }
        if (en > 0) { MR_ENN[gid] = r - MR_ENR[gid] <= 40 ? std::max(MR_ENN[gid], en) : en; MR_ENR[gid] = r; }
    }
    mr_check(r);   // the appointment's releases, after every kind of turn
}

// ---------------------------------------------------------------------------------------------
// Step 1: the appointment itself. Vision is not shared (rules 04), so the squad forms through the claims:
//   leader   - a body that knows a rich contested group is due (respawning, back within MID_LEADER_LEAD rounds, or up at full
//              health) and can walk there within MID_MAX_WALK claims it;
//   follower - a body within MID_MAX_WALK that reads an ally's claim on such a group joins while fewer than MID_SQUAD_SIZE claim it
//              (a write is seen at once by the next champion that acts, so the count does not overshoot).
// A committed body keeps the group as its job (no re-plan) until the cycle ends, the enemy has MID_ABORT_EDGE more bodies
// there, or MID_TIMEOUT rounds pass.

static int MR_C = -1, MR_C_R = -1000, MR_C_UP = 0;   // committed group, round, seen alive / cleared since
static int MR_C_WAIT = -1;                           // first round I waited next to it up at full health, unopened
static std::vector<int> MR_SKIP;                     // per group: no commitment before this round
static int MR_T = -1;    // the appointment's respawn round (the leader's own knowledge; the record's for a follower)

// the last capture of it that I know of was the enemy's (we are ceding it now; a camp we hold is the job engine's)
static bool mr_ceded(const Group& g) { return g.cap_them_at > -1000 && g.cap_them_at > g.cap_us_at; }
// the jackpot (DOM_G: its own all-in team mode) and the gate group are not appointments
static bool mr_skip_kind(int gid) { return G[gid].monolith || gid == DOM_G || gid == GATE_G; }
static bool mr_group(int gid) {
    for (int g : MR_G) if (g == gid) return !mr_skip_kind(gid);
    return false;
}
static int mr_claims(int gid) {
    int n = 0;
    for (int s = 0; s < SM_SLOTS && s < (int)ALLY.size(); ++s) if (s != MY_SLOT && claim_group(s) == gid) ++n;
    return n;
}
// our bodies within 6 of the group's tiles (me included) and enemy bodies within 8, as I see them now
static void mr_count(const Group& g, int& ours, int& en) {
    auto dmin = [&](vec2 p) { int d = 99; for (int k = 0; k < g.n; ++k) d = std::min(d, cheb(p, g.m[k].p)); return d; };
    ours = dmin(ME.p) <= 6 ? 1 : 0; en = 0;
    for (int s = 0; s < (int)ALLY.size(); ++s) if (s != MY_SLOT && ALLY[s].vis && ALLY[s].alive && dmin(ALLY[s].p) <= 6) ++ours;
    for (const auto& e : ENEMY) if (e.vis && e.alive && !on_enemy_spawn(e.p) && dmin(e.p) <= 8) ++en;
}
// the team's appointment record (world.h SM_AP_*): the leader's slot + 1 (0 none) and the
// respawn round in 5-round buckets mod 8 (read back as the bucket's middle, within the 40 rounds from 3 before now). Its
// group is the leader's claim; while the leader captures something the record cannot be read.
static int mr_rec_slot() { const int v = sm_get(SM_AP_WHO, 3); return v >= 1 && v <= SM_SLOTS ? v - 1 : -1; }
static int mr_rec_T(int r) {
    const int q = sm_get(SM_AP_WHEN, 3), b = std::max(0, r - 3) / 5;
    for (int k = b; k < b + 8; ++k) if ((k & 7) == q) return 5 * k + 2;
    return -1;
}
// the record's group: -1 none (or void: its leader claims no rich group now), -2 unreadable now (its leader captures)
static int mr_rec_gid() {
    const int s = mr_rec_slot();
    if (s < 0 || s >= (int)ALLY.size()) return -1;
    if (claim_capturing(s)) return -2;
    const int g = claim_group(s);
    return g >= 0 && g < (int)G.size() && mr_group(g) ? g : -1;
}
static bool mr_rec_free() { return mr_rec_gid() == -1; }
static void mr_rec_write(int T) {
    if (!slot_ok(MY_SLOT)) return;
    sm_set(SM_AP_WHO, 3, MY_SLOT + 1); sm_set(SM_AP_WHEN, 3, (T / 5) & 7);
}
static void mr_rec_clear() {
    if (!slot_ok(MY_SLOT) || mr_rec_slot() != MY_SLOT) return;
    sm_set(SM_AP_WHO, 3, 0); sm_set(SM_AP_WHEN, 3, 0);
}
// at the start of every turn (alive or dead, before any early return): my record ends with my appointment or my death;
// from 2 rounds before the respawn it names "now + 5", so it stays readable while the appointment lasts
static void mr_rec_tick(int r, bool alive) {
    if (mr_rec_slot() != MY_SLOT) return;
    if (!alive || MR_C < 0 || MR_T < 0) { mr_rec_clear(); return; }
    if (r >= MR_T - 2) { const int q = ((r + 5) / 5) & 7; if (sm_get(SM_AP_WHEN, 3) != q) sm_set(SM_AP_WHEN, 3, q); }
}
static void mr_release(int r, int skip) {
    if (skip > 0 && MR_C >= 0 && MR_C < (int)MR_SKIP.size()) MR_SKIP[MR_C] = r + skip;
    if (JOB == MR_C) JOB = -1;
    MR_C = -1; MR_C_UP = 0;
    MR_T = -1; mr_rec_clear();
}
// bodies the appointment wants: what the camp needs (team_needed, as the job score counts it), and one more than the
// enemy bodies I saw at it in the last 40 rounds
static int mr_k(const Group& g, int r) {
    const float hp = g.st == ST_ALIVE ? g.hp_left : g.hp_total;
    int k = std::clamp(team_needed(g, hp, my_dps_est()), 1, MID_SQUAD_SIZE);
    if (g.id < (int)MR_ENR.size() && r - MR_ENR[g.id] <= 40) k = std::max(k, std::min(MR_ENN[g.id] + 1, MID_SQUAD_SIZE + 1));
    return std::min(k, std::max(1, ALIVE_ALLIES));
}
// the price of an appointment body, in the job engine's currency (a rate x rounds against a value)
static float mr_pwin(float edge) { return 1.f / (1.f + std::exp(-(MID_WIN_INTERCEPT + MID_WIN_SLOPE * edge))); }
// their bodies expected at the respawn: the most I saw at it in the last 40 rounds; a retake at least the prior
static float mr_their(const Group& g, int r, bool retake) {
    const float seen = g.id < (int)MR_ENR.size() && r - MR_ENR[g.id] <= 40 ? (float)MR_ENN[g.id] : 0.f;
    return retake ? std::max(seen, MID_ENEMY_PRIOR) : seen;
}
// what the next capture swings: its value to us as job_score counts it, plus what it pays them
static float mr_swing(const Group& g, int r) {
    const float gw = ANY_GEM_GROUP ? groi::gw_job(r) : 1.f;
    return grole_value(ME.type, r, (float)g.gems, (float)g.gold, gw) + (float)g.gems + gw * (float)g.gold;
}
// rounds the appointment holds me: the walk or the wait, the clear with the squad, the capture
static float mr_hold_rounds(const Group& g, int r, int t, int eta, int bodies) {
    const float hp = g.st == ST_ALIVE ? g.hp_left : g.hp_total;
    return (float)std::max(eta, t - r) + hp / std::max(1.f, (float)bodies * MYDPS) + (float)bc::constants::capture_time + 1.f;
}
// keep or release the appointment: every turn (mr_tick, after fight and flee turns too) and before the farming plan
static void mr_check(int r) {
    if (MR_C < 0) return;
    if (MR_SKIP.size() != G.size()) MR_SKIP.assign(G.size(), -1000);
    {
        const Group& g = G[MR_C];
        int ours = 0, en = 0;
        mr_count(g, ours, en);
        // paid (to anyone) or found respawning for longer than the wait: the cycle is over; a respawn within the wait
        // (a stale "up" belief, corrected on arrival) is a wait
        if (g.st == ST_RESPAWN && g.ready_at - r > MID_MAX_WAIT) mr_release(r, 0);
        else if (g.st == ST_RESPAWN) MR_C_UP = 0;
        else MR_C_UP = 1;
        if (MR_C >= 0 && ours > 0 && en >= ours + MID_ABORT_EDGE) mr_release(r, 40);
        if (MR_C >= 0 && r - MR_C_R > MID_TIMEOUT) mr_release(r, 40);
        if (MR_C >= 0 && ME.hp < ME.mhp * RETREAT_HEALTH * 1.3f) mr_release(r, 0);
        if (MR_C >= 0 && g.st == ST_ALIVE && g.hp_left >= g.hp_total - 0.5f && gdist(g, ME.p) <= 6) {
            if (MR_C_WAIT < 0) MR_C_WAIT = r;
            else if (r - MR_C_WAIT > 25) mr_release(r, 30);
        } else MR_C_WAIT = -1;
        // too far now (a recall home, a long flight): the appointment was for the bodies near it
        if (MR_C >= 0 && mr_eta(g, ME.p, ME.type, ME.hp / std::max(1.f, ME.mhp)) > MID_MAX_WALK + 10) mr_release(r, 0);
    }
}
// once a turn, before the farming plan: keep, release or take an appointment
static void mr_decide(int r) {
    if (!MR_INIT || MR_G.empty()) return;
    if (MR_SKIP.size() != G.size()) MR_SKIP.assign(G.size(), -1000);
    mr_check(r);
    if (MR_C >= 0) return;
    const int rg = mr_rec_gid(), rT = rg >= 0 ? mr_rec_T(r) : -1;   // the team's appointment and its round
    int best = -1, bkind = 0, bt = 0;
    // my best and second best job rates in the last job pass (the best other job for any group)
    float alt1 = 0.f, alt2 = 0.f;
    int alt1g = -1;
    for (int i = 0; i < (int)SC.size(); ++i) {
        if (SC[i] > alt1) { alt2 = alt1; alt1 = SC[i]; alt1g = i; }
        else if (SC[i] > alt2) alt2 = SC[i];
    }
    float bkey = 1e9f;
    const float hf = ME.hp / std::max(1.f, ME.mhp);
    for (int gid : MR_G) {
        const Group& g = G[gid];
        if (mr_skip_kind(gid) || r < MR_SKIP[gid]) continue;
        const int n = mr_claims(gid);
        const bool rec = gid == rg;
        const int k = rec ? std::max(2, mr_k(g, r)) : mr_k(g, r);   // the team's appointment is for 2 or more
        if (n >= k) continue;
        const int eta = mr_eta(g, ME.p, ME.type, hf);
        if (eta > MID_MAX_WALK) continue;
        int kind = 0, t = r;
        // follower: an ally works it and it wants more bodies; leader: it is due and needs a squad (a group up now, or one
        // a single body clears, is the job engine's to price)
        if (rec && n >= 1) { kind = 2; t = std::max(r, rT); }   // the team's appointment, whatever my own view says
        else if (n >= 1 && g.st == ST_RESPAWN) { kind = 2; t = g.ready_at; }
        else if (k >= 2 && g.st == ST_RESPAWN && g.ready_at - r <= MID_LEADER_LEAD && mr_ceded(g) && mr_rec_free()) { kind = 1; t = g.ready_at; }
        if (!kind) continue;
        if (g.st == ST_RESPAWN && g.ready_at - r > MID_MAX_WAIT) continue;
        float gain = 0.f, cost = 0.f;
        {   // priced (a follower: the chance its body adds; a leader: its squad of k over nobody, for k bodies' time)
            const float e = mr_their(g, r, rec || kind == 1 || mr_ceded(g));
            if (e <= 0.f) continue;
            const float sw = mr_swing(g, r), alt = gid == alt1g ? alt2 : alt1;
            if (kind == 1) {
                gain = (mr_pwin((float)k - e) - mr_pwin(-e)) * sw;
                cost = (float)k * alt * mr_hold_rounds(g, r, t, eta, k);
            } else {
                gain = (mr_pwin((float)(n + 1) - e) - mr_pwin((float)n - e)) * sw;
                cost = alt * mr_hold_rounds(g, r, t, eta, n + 1);
            }
            if (gain <= cost) continue;
        }
        const float key = (float)std::max(t, r + eta) - 0.5f * (float)n;   // soonest start, then the most claimed
        if (key < bkey) { bkey = key; best = gid; bkind = kind; bt = t; }
    }
    if (best < 0) return;
    MR_C = best; MR_C_R = r; MR_C_UP = G[best].st != ST_RESPAWN ? 1 : 0; MR_C_WAIT = -1;
    MR_T = bt;
    if (bkind == 1) mr_rec_write(bt);     // the team's appointment
}
