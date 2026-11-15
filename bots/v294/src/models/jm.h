// jm.h: the job model's inputs from this champion's own view and memory.
// Training builds the same quantities from ladder recordings as this champion saw them:
// its own sight and ward (vision is not shared), real channels only (our bait recall is none),
// enemy deaths out of view unknown (an enemy's memory runs on after it dies), side from F_HOME / F_AWAY. Distances are
// Chebyshev; Balances are jm_lbal over jm_dps.
#pragma once
#include "jm_model.h"

static constexpr int JM_T = 30;           // rounds of memory
static int JM_CONTACT[8] = {-1000, -1000, -1000, -1000, -1000, -1000, -1000, -1000};   // last round each enemy was within 4
static bool JM_INIT = false;
static vec2 JM_SO{-1, -1}, JM_ST{-1, -1}; // spawn centres (ours, theirs): only where F_HOME / F_AWAY do not reach
static std::vector<float> JM_SIDE_G;      // per camp group: side_c (-1 = not computed yet)

static float jm_dps_of(const Champ& c) { return jm_dps((int)c.type, c.str, c.focus, c.hp, c.mhp); }
static float jm_dps_me() { return jm_dps((int)ME.type, ME.str, ME.focus, ME.hp, ME.mhp); }
static vec2 jm_centre(const std::vector<vec2>& t) {
    long sx = 0, sy = 0; for (auto p : t) { sx += p.x; sy += p.y; }
    return t.empty() ? vec2{-1, -1} : vec2{(int)std::lround((double)sx / t.size()), (int)std::lround((double)sy / t.size())};
}
// once per turn, before the decisions (the only pre-decision cost: a pass over the enemies)
static void jm_observe(int r) {
    if (!JM_INIT && !own_spawn.empty()) { JM_SO = jm_centre(own_spawn); JM_ST = jm_centre(en_spawn); JM_INIT = true; }
    for (int i = 0; i < (int)ENEMY.size() && i < 8; ++i) if (ENEMY[i].vis && cheb(ENEMY[i].p, ME.p) <= 4) JM_CONTACT[i] = r;
}
// map side: our share of the walk between the two spawns (0 at ours, 1 at theirs) = F_HOME / (F_HOME + F_AWAY), as
// where either does not reach (or before F_AWAY exists), Chebyshev to the spawn centres
static float jm_side_hd(int h, int a, vec2 p) {
    if (h == UNREACH || a == UNREACH) {
        const float dh = (float)cheb(p, JM_SO), da = (float)cheb(p, JM_ST);
        return dh / std::max(1.f, dh + da);
    }
    return (float)h / (float)std::max(1, h + a);
}
// F_AWAY, or our own copy of it: with a draft plan (PLAN_OK) the bot never builds F_AWAY (the plan carries each group's
// d_away), and every side here fell back to Chebyshev
static Field JM_AW;
static const Field& jm_awf() { return F_AWAY.ok() ? F_AWAY : JM_AW; }
static int jm_away(vec2 p) { const Field& f = jm_awf(); return f.ok() ? (int)f.at(p) : (int)UNREACH; }
// end of the turn, from spare fuel: build JM_AW once, as init_away_field builds F_AWAY (the groups' d_away untouched).
// A field costs ~6 fuel per tile (world.h ensure_field); running out here would lose the next round.
static void jm_spare() {
    if (F_AWAY.ok() || JM_AW.ok() || en_spawn.empty() || (long)bc::fuel_remaining() < 6L * N + 12000L) return;
    bc::bitboard er;   // walkable tiles touching the enemy spawn
    for (auto s : en_spawn)
        for (auto d : DIRS) { vec2 q = s + d; if (inb(q) && !BLOCK[idx(q)]) er.set(q, true); }
    JM_AW = make_field(STATIC_BB, er);
}
// ... of a camp: the nearest member tile to each spawn, kept once the away field exists
static float jm_side_g(const Group& g) {
    if ((int)JM_SIDE_G.size() < (int)G.size()) JM_SIDE_G.assign(G.size(), -1.f);
    if (g.id >= 0 && g.id < (int)JM_SIDE_G.size() && JM_SIDE_G[g.id] >= 0.f) return JM_SIDE_G[g.id];
    if (g.n <= 0) return 0.5f;
    int bh = (int)UNREACH, ba = (int)UNREACH;
    for (int k = 0; k < g.n; ++k) { bh = std::min(bh, (int)F_HOME.at(g.m[k].p)); ba = std::min(ba, jm_away(g.m[k].p)); }
    const float s = jm_side_hd(bh, ba, g.m[0].p);
    if (jm_awf().ok() && g.id >= 0 && g.id < (int)JM_SIDE_G.size()) JM_SIDE_G[g.id] = s;
    return s;
}
// Chebyshev distance from q to the straight walk p0 -> p1 sampled once per step (ns samples). Along the
// walk the distance max(|a + b t|, |c + d t|) is convex, so the nearest sample sits next to one of the continuous
// candidates (the ends, where either axis passes q, where the two axes tie): at most 12 samples, not ns. The full loop
// over every sample cost a re-plan so much fuel that 3.8% of turns ran out (base 0.1%).
static float jm_route_d(float qx, float qy, float x0, float y0, float x1, float y1, int ns) {
    const float a = x0 - qx, b = x1 - x0, c = y0 - qy, d = y1 - y0;
    if (ns <= 1) return std::max(std::fabs(a), std::fabs(c));
    float cand[6]; int nc = 0;
    cand[nc++] = 0.f; cand[nc++] = 1.f;
    if (b != 0.f) cand[nc++] = -a / b;
    if (d != 0.f) cand[nc++] = -c / d;
    if (b != d) cand[nc++] = (c - a) / (b - d);
    if (b != -d) cand[nc++] = -(a + c) / (b + d);
    float best = 1e9f;
    for (int i = 0; i < nc; ++i) {
        const int k0 = (int)std::floor(std::max(0.f, std::min(1.f, cand[i])) * (ns - 1));
        for (int k = k0; k <= k0 + 1 && k < ns; ++k) {
            const float t = (float)k / (ns - 1);
            best = std::min(best, std::max(std::fabs(a + b * t), std::fabs(c + d * t)));
        }
    }
    return best;
}
// The parts of the job features that are the same for every camp, built once per round and tile: the enemies
// remembered or fought (with HP and damage), the allies on the map and not channelling, the Balance around me. A re-plan
// scores 20-30 camps, and computing these for each camp cost ~1.7k fuel a camp.
struct JmFE { vec2 lp; bool rec, fo, r15; float w, hp, dps; };
struct JmFA { vec2 p; float hp, dps; };
struct JmFT {
    int r = -1; vec2 mp{-1, -1}; float hp = -1; int gems = -1;   // key: round, my tile, HP and gems
    std::vector<JmFE> e;
    std::vector<JmFA> a;
    int fought = 0, in_view = 0, unseen = 0, ally_me = 0, n15 = 0;   // n15: seen in the last 15 rounds
    float du = 0, lbal_me = 0, lgems = 0;
};
static JmFT JMFT;
static const JmFT& jm_feat_turn(int r) {
    JmFT& T = JMFT;
    if (T.r == r && T.mp.x == ME.p.x && T.mp.y == ME.p.y && T.hp == ME.hp && T.gems == ME.gems) return T;
    T.r = r; T.mp = ME.p; T.hp = ME.hp; T.gems = ME.gems;
    T.e.clear(); T.a.clear();
    T.fought = T.in_view = T.unseen = T.ally_me = T.n15 = 0;
    float hv = 0, dv = 0;
    for (int i = 0; i < (int)ENEMY.size(); ++i) {
        const Champ& e = ENEMY[i];
        const bool rec = e.seen_at >= 0 && r - e.seen_at <= JM_T;
        const bool fo = i < 8 && JM_CONTACT[i] >= 0 && r - JM_CONTACT[i] <= JM_T;
        if (e.vis && cheb(e.p, ME.p) <= 8) { ++T.in_view; hv += e.hp; dv += jm_dps_of(e); }
        if (!rec) ++T.unseen;
        if (fo) ++T.fought;
        if (!rec && !fo) continue;
        if (rec && r - e.seen_at <= 15) ++T.n15;
        T.e.push_back({e.last_p, rec, fo, rec && r - e.seen_at <= 15, rec ? 1.f - (float)(r - e.seen_at) / (JM_T + 1) : 0.f,
                       e.hp, jm_dps_of(e)});
    }
    // me + allies within 5 of me
    T.du = jm_dps_me();
    float hm = ME.hp, dm = T.du;
    for (int i = 0; i < (int)ALLY.size(); ++i) {
        if (i == MY_SLOT) continue;
        const Champ& a = ALLY[i];
        if (!a.vis || a.channel) continue;
        const float ad = jm_dps_of(a);
        T.a.push_back({a.p, a.hp, ad});
        if (cheb(a.p, ME.p) <= 5) { hm += a.hp; dm += ad; ++T.ally_me; }
    }
    T.lbal_me = jm_lbal(hm, dm, hv, dv);
    T.lgems = std::log1p((float)ME.gems);
    return T;
}
// the box around a camp's monsters; the Chebyshev distance from p to it is at most tc_gd(g, p)
struct JmBox { int x0, y0, x1, y1; };
static inline JmBox jm_box(const Group& g) {
    JmBox b{1 << 20, 1 << 20, -(1 << 20), -(1 << 20)};
    for (int k = 0; k < g.n; ++k) {
        b.x0 = std::min(b.x0, g.m[k].p.x); b.x1 = std::max(b.x1, g.m[k].p.x);
        b.y0 = std::min(b.y0, g.m[k].p.y); b.y1 = std::max(b.y1, g.m[k].p.y);
    }
    return b;
}
static inline int jm_box_d(const JmBox& b, vec2 p) { return std::max(std::max(b.x0 - p.x, p.x - b.x1), std::max(b.y0 - p.y, p.y - b.y1)); }
// the 20 features of a camp job for P(caught), walk = steps to the camp
static void jm_job_feats(const Group& g, int walk, float f[20]) {
    const int r = rnd();
    const JmFT& T = jm_feat_turn(r);
    // the straight walk from me to the camp's centre, one sample per step
    const float x0 = ME.p.x, y0 = ME.p.y, x1 = g.center.x, y1 = g.center.y;
    const int ns = (int)std::max(std::fabs(x1 - x0), std::fabs(y1 - y0)) + 1;
    float mem_camp = 0, mem_route = 0, ht = 0, dt = 0, ht15 = 0, dt15 = 0;
    int fought_near = 0;
    const JmBox bx = jm_box(g);
    for (const JmFE& q : T.e) {
        // dcamp is only compared with 7, 10 and 12, so past 12 the box distance stands in for it
        const int dlb = jm_box_d(bx, q.lp);
        const int dcamp = dlb > 12 ? dlb : tc_gd(g, q.lp);
        // the walk only counts past 10 of the camp (memory on the route) and past 12 (a fight near the job)
        // it is only compared with 12 (a fought enemy past 12 of the camp) and 6 (a remembered one); every sample lies
        // in the box around the walk, so the Chebyshev distance to that box bounds it below
        float droute = 0.f;
        if (dcamp > 10) {
            const float lb = std::max(std::max(std::min(x0, x1) - q.lp.x, q.lp.x - std::max(x0, x1)),
                                      std::max(std::min(y0, y1) - q.lp.y, q.lp.y - std::max(y0, y1)));
            droute = (q.fo && dcamp > 12 && lb <= 12.f) || (q.rec && lb <= 6.f) ? jm_route_d((float)q.lp.x, (float)q.lp.y, x0, y0, x1, y1, ns)
                                                                                 : std::max(lb, 13.f);
        }
        if (q.fo && (dcamp <= 12 || droute <= 12.f)) ++fought_near;
        if (!q.rec) continue;
        if (dcamp <= 10) mem_camp += q.w; else if (droute <= 6.f) mem_route += q.w;
        if (dcamp <= 7) {
            ht += q.hp; dt += q.dps;
            if (q.r15) { ht15 += q.hp; dt15 += q.dps; }
        }
    }
    // our side: me + allies (on the map, not channelling) within 7 of the camp
    float hu = ME.hp, du = T.du;
    int ally_camp = 0;
    for (const JmFA& a : T.a) if (jm_box_d(bx, a.p) <= 7 && tc_gd(g, a.p) <= 7) { hu += a.hp; du += a.dps; ++ally_camp; }
    f[0] = walk / 20.f; f[1] = jm_side_g(g); f[2] = r / 1000.f; f[3] = mem_camp; f[4] = mem_route;
    f[5] = (float)fought_near; f[6] = (float)T.fought; f[7] = (float)T.in_view;
    f[8] = jm_lbal(hu, du, ht, dt); f[9] = T.lbal_me; f[10] = (float)ally_camp; f[11] = (float)T.ally_me;
    f[12] = ME.hp / std::max(1.f, ME.mhp); f[13] = T.lgems; f[14] = (float)T.unseen;
    // alive_them: deaths out of view are unknown, so their team size; alive_us: tally_help's count of
    // our living bodies (is_alive needs no sight), fresh in choose_job
    f[15] = (float)ENEMY.size(); f[16] = (float)ALIVE_ALLIES; f[17] = (float)ALLY.size();
    f[18] = jm_lbal(hu, du, ht15, dt15); f[19] = tc_get() / 16.f;
}
