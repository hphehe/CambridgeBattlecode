// Camp engine: one calculator for every camp, closed form.
// Per hit (checked on 22 replays, 9,360 Monolith hits within 2%): dealt = (Strength | k x Focus) x 1.15 Monster x
// (1 + 1% a family) Collectors x HoR passive; a camp's hit = Strength x 0.7 Jungle x 0.9 Durability. Rules 03 / 05:
// camps act at round start on the nearest champion in reach, a stun skips one camp attack, a camp never heals.
// A body's damage over tau rounds on the camp = Strength x m x tau + every cast its cooldowns allow (8-turn cooldowns;
// the HoR's landing is left out: the bot does not land on camps it fights). Check (22 games): damage in the next 8
// rounds and rounds to clear both 1.00 of the replays (median) for wolves, elks, clusters and small groups.
// the job score's clear time, soak test and banked progress (a camp never heals: work on it is kept).
#pragma once
#include "../brain/coop.h"
struct CBody { int t; float s, f, m, hp, mhp, red, shield; int cd[2]; int eta; bool act[2], st[2]; float sd[2]; };   // act / sd / st from cc_prep
struct CCalc { int T = 9999; float raw = 0, pool = 0, d8 = 0; int n = 0, e0 = 0; bool ok = false; };   // e0: the first arrival
static int cc_fams(const bc::enum_map<IT>& ic, bool& mon, bool& col, bool& jun, bool& dur) {
    static const IT E[14][3] = {
        {IT::monster_emblem_of_strength, IT::monster_emblem_of_focus, IT::monster_emblem_of_health},
        {IT::power_emblem_of_strength, IT::power_emblem_of_focus, IT::power_emblem_of_health},
        {IT::durability_emblem_of_strength, IT::durability_emblem_of_focus, IT::durability_emblem_of_health},
        {IT::treasure_emblem_of_strength, IT::treasure_emblem_of_focus, IT::treasure_emblem_of_health},
        {IT::jungle_emblem_of_strength, IT::jungle_emblem_of_focus, IT::jungle_emblem_of_health},
        {IT::home_emblem_of_strength, IT::home_emblem_of_focus, IT::home_emblem_of_health},
        {IT::bruiser_emblem_of_strength, IT::bruiser_emblem_of_focus, IT::bruiser_emblem_of_health},
        {IT::tribute_emblem_of_strength, IT::tribute_emblem_of_focus, IT::tribute_emblem_of_health},
        {IT::capture_emblem_of_strength, IT::capture_emblem_of_focus, IT::capture_emblem_of_health},
        {IT::prospector_emblem_of_strength, IT::prospector_emblem_of_focus, IT::prospector_emblem_of_health},
        {IT::hunter_emblem_of_strength, IT::hunter_emblem_of_focus, IT::hunter_emblem_of_health},
        {IT::skirmish_emblem_of_strength, IT::skirmish_emblem_of_focus, IT::skirmish_emblem_of_health},
        {IT::greed_emblem_of_strength, IT::greed_emblem_of_focus, IT::greed_emblem_of_health},
        {IT::collectors_emblem_of_strength, IT::collectors_emblem_of_focus, IT::collectors_emblem_of_health}};
    int n = 0; bool has[14];
    for (int f = 0; f < 14; ++f) { has[f] = ic[E[f][0]] + ic[E[f][1]] + ic[E[f][2]] > 0; n += has[f]; }
    mon = has[0]; dur = has[2]; jun = has[4]; col = has[13];
    return n;
}
static inline int cc_casts(int cd, int tau) { return cd < tau ? 1 + (tau - 1 - cd) / 8 : 0; }
static inline bool cc_skip_slot(const CBody& b, const SlotK& sl) {
    return sl.kind == SK_NONE || sl.kind == SK_DASH || (champ((CT)b.t).missing_hp && sl.kind == SK_AREA);
}
static inline float cc_slot_dmg(const CBody& b, const SlotK& sl) { return sl.kind == SK_RESET ? b.s * b.m : sl.mult * b.f * b.m; }
// a body's casts, their damage and stuns, once (cc_refresh)
static void cc_prep(CBody& b) {
    const Kit& k = champ((CT)b.t).kit;
    for (int s = 0; s < 2; ++s) {
        const SlotK& sl = k.s[s];
        b.act[s] = !cc_skip_slot(b, sl);
        b.sd[s] = b.act[s] ? cc_slot_dmg(b, sl) : 0.f;
        b.st[s] = b.act[s] && sl.stun;
    }
}
// damage one body deals in its first tau rounds at the camp, and the stuns it casts
static float cc_dealt(const CBody& b, int tau, int* stuns = nullptr) {
    if (tau <= 0) return 0.f;
    float d = b.s * b.m * tau;
    for (int s = 0; s < 2; ++s) {
        if (!b.act[s]) continue;
        const int c = cc_casts(b.cd[s], tau);
        d += c * b.sd[s];
        if (stuns && b.st[s]) *stuns += c;
    }
    return d;
}
// sustained damage a round (basic + casts every 8) and the casts ready at once
static void cc_rate(const CBody& b, float& rate, float& burst) {
    rate = b.s * b.m; burst = 0;
    for (int s = 0; s < 2; ++s) if (b.act[s]) { rate += b.sd[s] / 8.f; burst += b.sd[s]; }
}
static float cc_team(const std::vector<CBody>& B, int t, int* stuns = nullptr) {
    float d = 0; for (auto& b : B) d += cc_dealt(b, t - b.eta, stuns); return d;
}
// first round the team's damage reaches H (9999: not within 1,000 rounds); the search runs between the bounds the
// sustained rate gives (the damage by round t lies within [rate x (t - eta) - one cycle, rate x (t - eta) + burst])
static int cc_rounds(const std::vector<CBody>& B, float H) {
    if (H <= 0) return 0;
    float rate = 0, burst = 0, lag = 0, slack = 0;
    int emin = 1 << 30;
    for (auto& b : B) {
        float r, u; cc_rate(b, r, u);
        rate += r; burst += u; lag += r * b.eta; emin = std::min(emin, b.eta);
        for (int s = 0; s < 2; ++s) if (b.act[s]) slack += b.sd[s] * b.cd[s] / 8.f;
    }
    if (rate <= 0) return 9999;
    // nothing before the first body arrives, then at most rate x (t - emin) + burst: T >= lo. A cast every 8
    // rounds from its cooldown gives at least rate x t - lag - slack by round t: T <= hi
    int lo = std::max(1, emin + (int)((H - burst) / rate));
    int hi = (int)std::ceil((H + lag + slack) / rate) + 1;
    if (hi > 1000) { if (cc_team(B, 1000) < H) return 9999; hi = 1000; }
    if (lo > hi) lo = hi;
    while (lo < hi) { const int mid = (lo + hi) / 2; if (cc_team(B, mid) >= H) hi = mid; else lo = mid + 1; }
    return lo;
}
static int CC_CR = 3;   // the camp's reach (cc_job sets it)
// tiles between a body's attack spot and the camp's reach edge; the round starts it spends in reach walking out
// (a tile a turn) and walking in (a dash turn covers 3)
static inline int cc_steps(const CBody& b) { return std::max(0, CC_CR + 1 - attack_reach(champ((CT)b.t).melee)); }
static inline int cc_out_hits(const CBody& b) { return std::max(0, cc_steps(b) - 1); }
static inline int cc_in_hits(const CBody& b) { return std::max(0, cc_steps(b) - (champ((CT)b.t).dash ? 3 : 1)); }
// mons = the members alive (health, Strength), killed weakest first. raw = the hits they land before they die (each
// member's time spread by its share of the health), less one hit per stun; pool = what the bodies can take above their
// leave line (RETREAT_HEALTH of max health, or two rounds of the camp's hits), as raw hits after each body's cuts, plus
// its shields over the clear
static CCalc cc_calc(const std::vector<CBody>& B, std::vector<std::pair<float, float>>& mons) {
    CCalc c; c.n = (int)B.size();
    float H = 0, S = 0, smax = 0; for (auto& m : mons) { H += m.first; S += m.second; smax = std::max(smax, m.second); }
    if (B.empty() || H <= 0) return c;
    c.T = cc_rounds(B, H);
    c.e0 = 1 << 30; for (auto& b : B) c.e0 = std::min(c.e0, b.eta);
    if (c.T >= 9999) return c;
    if (S > 0) {
        std::sort(mons.begin(), mons.end());
        float cum = 0;
        for (auto& m : mons) { cum += m.first; c.raw += m.second * (c.T - c.e0) * (cum / H); }
        int st = 0;   // the stuns cast by round T, in closed form
        for (auto& b : B) for (int s = 0; s < 2; ++s) if (b.st[s]) st += cc_casts(b.cd[s], c.T - b.eta);
        c.raw = std::max(0.f, c.raw - std::min(st, c.T) * smax);
    }
    // a body in front takes the camp's S a round after its cuts, less its shield (one cast a cooldown) while it stands
    // there: the raw hits it soaks = S x (health above its line) / (S x cut - shield / 8). (An earlier version counted every shield of
    // the whole clear: a lone HoR, Strength 30, read a 15,720 pool at a 20,000 Monolith, took it at r47, died at r61.)
    for (auto& b : B) {
        if (b.eta >= c.T || S <= 0) continue;
        const float line = std::max(2.f * S, b.mhp * RETREAT_HEALTH);
        const float net = std::max(0.05f * S * b.red, S * b.red - b.shield / 8.f);
        c.pool += S * std::max(0.f, b.hp - line) / net;
    }
    if (S > 0) {   // the first arrival walks in alone, the one with the most health walks out last, alone
        int fi = -1, la = -1;
        for (int i = 0; i < (int)B.size(); ++i) {
            if (B[i].eta >= c.T) continue;
            if (fi < 0 || B[i].eta < B[fi].eta || (B[i].eta == B[fi].eta && cc_in_hits(B[i]) < cc_in_hits(B[fi]))) fi = i;
            if (la < 0 || B[i].hp > B[la].hp) la = i;
        }
        if (fi >= 0) c.pool = std::max(0.f, c.pool - S * (float)(cc_in_hits(B[fi]) + std::max(0, cc_out_hits(B[la]) - 1)));
    }
    c.ok = c.pool >= c.raw;
    return c;
}
// ---- the job score's view: bodies read once a turn
static int CC_AT = -1;
static CBody CC_B[6];
static vec2 CC_P[6];
static bool CC_ON[6];
static int CC_CL[6];   // each ally's claimed group, read once a turn
static float CC_RATE_ALL = 1.f, CC_BURST_ALL = 0.f;   // every live body of ours: an upper bound on a camp's clear speed
// per slot, the emblem families of the last item read and the cooldowns read then
static int CC_IR[6] = {-1000, -1000, -1000, -1000, -1000, -1000}, CC_ICD[6][2];
struct CFam { bool mon = false, col = false, jun = false, dur = false; int nf = 0; };
static CFam CC_FM[6];
// cc_body with the families already counted (the same body)
static CBody cc_body_f(CT type, float s, float f, float hp, float mhp, const CFam& fm, int cd0, int cd1) {
    CBody b{}; b.t = (int)type; b.s = s; b.f = f; b.hp = hp; b.mhp = mhp; b.eta = 0;
    b.m = (fm.mon ? 1.15f : 1.f) * (fm.col ? 1.f + 0.01f * fm.nf : 1.f);
    if (champ(type).missing_hp) b.m *= 1.f + 0.5f * std::max(0.f, std::min(1.f, 1.f - hp / std::max(1.f, mhp)));
    b.red = (fm.jun ? 0.7f : 1.f) * (fm.dur ? 0.9f : 1.f);
    const qm::Kit& q = champ(type).qkit;
    b.shield = 0; for (int k = 0; k < 2; ++k) if (q.s[k].k == qm::KS) b.shield = q.s[k].m * f;
    b.cd[0] = std::max(0, cd0); b.cd[1] = std::max(0, cd1);
    return b;
}
static void cc_refresh() {
    if (CC_AT == rnd()) return;
    CC_AT = rnd(); CC_RATE_ALL = 0; CC_BURST_ALL = 0;
    const int r = rnd();
    for (int s = 0; s < 6; ++s) CC_ON[s] = false;
    for (int s = 0; s < (int)ALLY.size() && s < 6; ++s) {
        const bool stale = r - CC_IR[s] >= CALC_REREAD_ROUNDS;
        if (s == MY_SLOT) {
            if (stale) {
                CFam& fm = CC_FM[s]; fm = CFam{};
                try { const auto ic = bc::self().item_count(); fm.nf = cc_fams(ic, fm.mon, fm.col, fm.jun, fm.dur); } catch (const std::exception&) {}
                CC_IR[s] = r;
            }
            CC_B[s] = cc_body_f(ME.type, ME.str, ME.focus, ME.hp, ME.mhp, CC_FM[s], ME.cd[0], ME.cd[1]);
            CC_P[s] = ME.p; CC_ON[s] = true;
        } else {
            Champ& a = ALLY[s];
            if (!a.alive || a.hp <= 0 || a.channel) continue;
            if (stale && a.vis) {
                try {
                    const auto ic = a.h.item_count();
                    CFam fm; fm.nf = cc_fams(ic, fm.mon, fm.col, fm.jun, fm.dur);
                    const auto ab = a.h.abilities(); CC_ICD[s][0] = ab[0].cooldown(); CC_ICD[s][1] = ab[1].cooldown();
                    CC_FM[s] = fm; CC_IR[s] = r;
                } catch (const std::exception&) {}
            }
            const int el = r - CC_IR[s];
            const bool known = CC_IR[s] > -1000;
            const int cd0 = known ? std::max(0, CC_ICD[s][0] - el) : -1, cd1 = known ? std::max(0, CC_ICD[s][1] - el) : -1;
            CC_B[s] = cc_body_f(a.type, a.str, a.focus, a.hp, a.mhp, known ? CC_FM[s] : CFam{}, cd0, cd1);
            CC_P[s] = a.vis ? a.p : a.last_p; CC_ON[s] = true;
        }
        cc_prep(CC_B[s]);
        CC_CL[s] = s == MY_SLOT ? -2 : claim_group(s);
        float rate, burst; cc_rate(CC_B[s], rate, burst);
        // HoR's passive grows to x1.5 as it is hurt: the bound takes its top
        if (champ((CT)CC_B[s].t).missing_hp) { rate *= 1.5f; burst *= 1.5f; }
        CC_RATE_ALL += rate; CC_BURST_ALL += burst;
    }
    CC_RATE_ALL = std::max(1.f, CC_RATE_ALL);
}
// a lower bound on any clear of health H by us (the early bound needs one)
static inline float cc_clear_lb(float H) { cc_refresh(); return std::max(0.f, (H - CC_BURST_ALL) / CC_RATE_ALL); }
static void cc_members(const Group& g, std::vector<std::pair<float, float>>& mons) {
    mons.clear();
    for (int k = 0; k < g.n; ++k) {
        const float hp = g.st == ST_ALIVE ? g.m[k].hp : g.m[k].mhp;
        if (hp > 0) mons.push_back({hp, g.m[k].str});
    }
}
static int CC_MI = -1;   // my index in cc_job_bodies' list
static void cc_job_bodies(const Group& g, std::vector<CBody>& B) {
    cc_refresh(); B.clear(); CC_MI = -1;
    for (int s = 0; s < 6; ++s) {
        if (!CC_ON[s] || (s != MY_SLOT && CC_CL[s] != g.id)) continue;
        CBody b = CC_B[s];
        const CT ty = (CT)b.t;
        b.eta = CC_P[s].x < 0 ? 30 : std::max(0, gdist(g, CC_P[s]) - attack_reach(champ(ty).melee));
        if (s == MY_SLOT) CC_MI = (int)B.size();
        B.push_back(b);
    }
}
// rounds the bodies work before every one is at its leave line (when they cannot finish): the raw hits, spread over
// the clear as in cc_calc, reach the pool
static int cc_work_rounds(const CCalc& c) {
    if (c.raw <= 0 || c.T >= 9999) return c.T >= 9999 ? 200 : c.T;
    return std::max(c.e0, std::min(c.T, c.e0 + (int)((c.T - c.e0) * c.pool / c.raw)));
}
// a camp never heals, so bodies at their leave line
// go home, heal (a turn on the spawn) and come back: the camp takes longer, it is never refused. A trip: home on foot
// (or a recall when the walk is longer: the LOWHP rule recalls past 6), the turn there, the walk back
static inline int cc_trip(const Group& g) {
    const int dh = std::min(g.d_home, 200);
    const int out = dh <= 6 ? dh : std::min(dh, (int)bc::constants::recall_time + 1);
    return out + 1 + dh;
}
struct CJob { int T = 9999, k = 0, trips = 0, recruits = 0; bool heal = false; CCalc c;
    int T0 = 9999, nc = 0;   // rounds the claimers need without me (9999: they cannot / none), how many claim it
};
static std::vector<char> CC_HEALF;   // the job needs me healed first (job_score sets it)
// rounds from now until the bodies B finish the camp (members mons), the trips home included. The first stint runs until
// every body is at its leave line; each later one starts from full health with the abilities ready, everyone back
static int cc_finish(const std::vector<CBody>& B, const std::vector<std::pair<float, float>>& mons0, int trip, CJob& j) {
    static std::vector<std::pair<float, float>> mons, rest; static std::vector<CBody> F;
    mons = mons0;
    float H = 0; for (auto& m : mons) H += m.first;
    j.k = (int)B.size(); j.trips = 0;
    j.c = cc_calc(B, mons);   // sorts mons, weakest first (the order they die)
    if (j.c.T >= 9999) return 9999;
    if (j.c.ok) return j.c.T;
    const int W1 = cc_work_rounds(j.c);
    float rem = std::min(H, cc_team(B, W1));
    rest.clear();
    for (auto& m : mons) { if (rem >= m.first) { rem -= m.first; continue; } rest.push_back({m.first - rem, m.second}); rem = 0; }
    float H2 = 0; for (auto& m : rest) H2 += m.first;
    if (H2 <= 0) return W1;
    F = B;
    for (auto& b : F) {
        // HoR's passive is at its floor again at full health
        if (champ((CT)b.t).missing_hp) b.m /= 1.f + 0.5f * std::max(0.f, std::min(1.f, 1.f - b.hp / std::max(1.f, b.mhp)));
        b.hp = b.mhp; b.eta = 0; b.cd[0] = 0; b.cd[1] = 0;
        cc_prep(b);
    }
    const CCalc f = cc_calc(F, rest);
    if (f.T >= 9999) return 9999;
    if (f.ok) { j.trips = 1; return W1 + trip + f.T; }
    // the same stint again until the rest is gone (the first one's members: later stints face fewer)
    const int W = cc_work_rounds(f);
    const float D = std::max(1.f, cc_team(F, W));
    const int n = (int)std::ceil(H2 / D);
    const int Wl = std::max(1, (int)std::ceil(W * (H2 - (n - 1) * D) / D));
    j.trips = n;
    const long long T = (long long)W1 + (long long)n * trip + (long long)(n - 1) * W + Wl;
    return T > 5000 ? 9999 : (int)T;
}
// the job: me + the allies that claim it, then (while it needs trips home) our other bodies by arrival, keeping the set
// with the fewest body-rounds k x (rounds after my arrival + fixed); fixed1 / fixedk = the score's other time for one
// body / for a team (the capture). kmin: the bodies it must have. An ally on another job comes after it
struct CJobKeep { int r = -1000, kmin = 0, after = 0, cl = 0; float hpc = -1, hpme = -1; bool rec = false; CJob j; };
static std::vector<CJobKeep> CC_KEEP;
static CJob cc_job_new(const Group& g, int t, float fixed1, float fixedk, int kmin, bool recruits);
static CJob cc_job(const Group& g, int t, float fixed1, float fixedk, int kmin, bool recruits = true) {
    if ((int)CC_KEEP.size() < (int)G.size()) CC_KEEP.resize(G.size());
    cc_refresh();
    int cl = 0;   // who claims it (a bit a slot)
    for (int s = 0; s < 6; ++s) if (CC_ON[s] && s != MY_SLOT && CC_CL[s] == g.id) cl |= 1 << s;
    const float hpc = g.st == ST_ALIVE ? g.hp_left : -1.f;
    if (g.id >= 0 && g.id < (int)CC_KEEP.size()) {
        CJobKeep& q = CC_KEEP[(size_t)g.id];
        const bool low = (long)bc::fuel_remaining() < JOB_SCAN_FUEL_FLOOR;
        if (low) recruits = false;
        if ((rnd() - q.r <= CALC_KEEP_ROUNDS && q.cl == cl && q.hpc == hpc && q.kmin == kmin && (q.rec || !recruits) &&
             std::fabs(q.hpme - ME.hp) <= 0.1f * ME.mhp) ||
            (low && rnd() - q.r <= CALC_KEEP_ROUNDS_LOW_FUEL && q.cl == cl && q.kmin == kmin)) {
            CJob j = q.j; if (j.T < 9999) j.T = t + q.after;
            if (j.T0 < 9999) j.T0 = std::max(1, j.T0 - (rnd() - q.r));   // the claimers' finish moves with the clock
            return j;
        }
        CJob j = cc_job_new(g, t, fixed1, fixedk, kmin, recruits);
        q.r = rnd(); q.cl = cl; q.hpc = hpc; q.kmin = kmin; q.rec = recruits; q.hpme = ME.hp; q.j = j; q.after = j.T - t;
        return j;
    }
    return cc_job_new(g, t, fixed1, fixedk, kmin, recruits);
}
static CJob cc_job_new(const Group& g, int t, float fixed1, float fixedk, int kmin, bool recruits) {
    static std::vector<CBody> B; static std::vector<std::pair<float, float>> mons;
    cc_job_bodies(g, B); cc_members(g, mons);
    CC_CR = 0;
    for (int m = 0; m < g.n; ++m) if (g.m[m].str > 0) CC_CR = std::max(CC_CR, g.m[m].reach);
    float S = 0;
    for (auto& m : mons) S += m.second;
    // my part: under one hit above my line now, home first (the walk or a recall, a turn on the spawn, the walk here)
    // at full health; under one hit at full health, not a camp I can work
    bool heal = false;
    if (S > 0 && CC_MI >= 0) {
        CBody& me = B[(size_t)CC_MI];
        const float hit = S * me.red, line = std::max(2.f * S, me.mhp * RETREAT_HEALTH);
        if (me.mhp - line < hit) { CJob no; no.T = 9999; return no; }
        if (me.hp - line < hit && !on_own_spawn()) {
            const int hd = F_HOME.at(ME.p);
            if (hd == UNREACH) { CJob no; no.T = 9999; return no; }
            const int out = hd <= 6 ? hd : std::min(hd, (int)bc::constants::recall_time + 1);
            if (champ((CT)me.t).missing_hp) me.m /= 1.f + 0.5f * std::max(0.f, std::min(1.f, 1.f - me.hp / std::max(1.f, me.mhp)));
            me.hp = me.mhp; me.cd[0] = 0; me.cd[1] = 0; cc_prep(me);
            me.eta = out + 1 + std::max(0, std::min(g.d_home, 200) - attack_reach(ME.melee));
            heal = true;
        }
    }
    const int trip = cc_trip(g);
    CJob best; best.T = cc_finish(B, mons, trip, best); best.heal = heal;
    // the claimers alone (me out), for my share of what we add
    best.nc = (int)B.size() - (CC_MI >= 0 ? 1 : 0);
    if (best.nc > 0 && CC_MI >= 0 && best.T < 9999) {
        static std::vector<CBody> C0; C0 = B; C0.erase(C0.begin() + CC_MI);
        CJob j0; best.T0 = cc_finish(C0, mons, trip, j0);
    }
    // fewest trips home first (a camp never heals: each trip is a walk out under fire and a longer job),
    // then the fewest body-rounds
    auto cost = [&](const CJob& j) {
        if (j.T >= 9999) return 1e30f;
        return (float)j.trips * 1e6f + j.k * (std::max(1.f, (float)(j.T - t)) + (j.k > 1 ? fixedk : fixed1));
    };
    if ((best.trips == 0 && best.k >= kmin) || !recruits) return best;
    const int keep_T0 = best.T0, keep_nc = best.nc;
    int cs[6], ce[6], nc = 0;
    for (int s = 0; s < 6; ++s) {
        if (!CC_ON[s] || s == MY_SLOT || CC_CL[s] == g.id || CC_P[s].x < 0) continue;
        const CT ty = (CT)CC_B[s].t;
        int e = gdist(g, CC_P[s]);
        if (e >= 900) continue;
        e = std::max(0, e - attack_reach(champ(ty).melee));
        const int h = CC_CL[s];
        if (h >= 0 && h < (int)G.size()) {
            const Group& o = G[(size_t)h];
            float r, u; cc_rate(CC_B[s], r, u);
            const float oh = o.st == ST_ALIVE ? o.hp_left : o.st == ST_REWARD ? 0.f : o.hp_total;
            e += (int)(oh / std::max(1.f, r)) + capture_rounds();
        }
        int i = nc++;
        for (; i > 0 && ce[i - 1] > e; --i) { cs[i] = cs[i - 1]; ce[i] = ce[i - 1]; }
        cs[i] = s; ce[i] = e;
    }
    const int base = (int)B.size();
    // k bodies need at least k x (their clear with no trips, from the first arrival: damage by round x is at most
    // rate x (x - first arrival) + every cast ready at once) - my arrival + the fixed time): no calculator run when that
    // cannot beat the best set so far
    float H = 0; for (auto& m : mons) H += m.first;
    float rate = 0, burst = 0; int e0 = 1 << 30;
    for (auto& b : B) { float rr, uu; cc_rate(b, rr, uu); rate += rr; burst += uu; e0 = std::min(e0, b.eta); }
    for (int i = 0; i < nc; ++i) {
        if ((long)bc::fuel_remaining() < JOB_SCAN_FUEL_FLOOR) break;   // the best set so far
        CBody b = CC_B[cs[i]]; b.eta = ce[i]; B.push_back(b);
        { float rr, uu; cc_rate(b, rr, uu); rate += rr; burst += uu; e0 = std::min(e0, b.eta); }
        const int kk = (int)B.size();
        if (best.k >= kmin && best.T < 9999 && rate > 0) {
            const float tlb = (float)e0 + std::max(0.f, (H - burst) / rate);
            if (kk * (std::max(1.f, tlb - t) + (kk > 1 ? fixedk : fixed1)) >= cost(best)) continue;
        }
        CJob j; j.T = cc_finish(B, mons, trip, j); j.recruits = (int)B.size() - base; j.heal = heal;
        j.T0 = keep_T0; j.nc = keep_nc;
        if (best.k < kmin || cost(j) < cost(best)) best = j;
        if (j.trips == 0 && j.k >= kmin) break;
    }
    return best;
}
// my share of camp g in the job cj (me + its claimers + the recruits it books): what my body and those recruits add to
// the claimers' own finish, split equally between us, with the contest priced (coop.h); never below the average 1/k.
// at = the round we would arrive
static float coop_share(const Group& g, const CJob& cj, int k, int at) {
    float m = 1.f;
    if (cj.nc > 0 && cj.T0 < 9999 && cj.T < 9999) {
        float psi = 1.f;
        const int ne = coop_enemies(g, at);
        if (ne > 0) psi = coop_ptake(cj.nc - ne) / coop_ptake(cj.k - ne);
        m = std::clamp(1.f - psi * (float)cj.T / (float)std::max(1, cj.T0), 0.f, 1.f);
    }
    return std::max(m / (float)(1 + cj.recruits), 1.f / (float)std::max(1, k));
}
