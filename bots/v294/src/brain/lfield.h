#pragma once
#include <memory>
// exact travel time to a static goal (the altar's submit tiles, our spawn) under the movement rules: walk,
// dash (for dash champions), and the teleport crystal (every champion). Camp tiles are walls (a living camp blocks its
// tiles; a ring of camps round the altar is then a closed pocket, which the walk fields never saw).
// State (tile, layer). Dash champions: N layers c 0..7 = rounds until the dash is ready at the start of a turn, 8 =
// dashed this turn, the walk still to come. One turn from c: walk or wait -> (q, max(c-1, 0)); with c = 0 also dash then
// walk -> (q, 7), or walk then dash -> (b, 7). A dash is two king steps whose middle tile may be a hole or an altar.
// Other champions: one N layer (walk or wait).
// The C layer (last board): we carry a crystal. One turn: walk (the dash is not counted), or walk and then use the
// crystal: it lands on an open tile within Chebyshev 3 over any terrain and we stand there at the start of the third
// turn after (rules 03/04), without the crystal: C_k = walk(C_{k-1} | H_k), H_k = open & dil3(N_{k-3}[ready]). Keeping
// the crystal unused is allowed: C_k |= N_k[the slowest dash state].
// LV[k][layer] = the tiles whose best time to the goal from that state is <= k. Level k follows from level k-1 with
// bitboard ops (~1.2 fuel per 64-bit word each: about 170 ops a level with the dash, 35 without), one level at a time
// on spare fuel. The boards are a pool, written in place and never freed: a reset that freed and allocated ~600 boards
// cost ~90k fuel and suspended the turn.
// also one field per camp group (GF), capped at GCAP levels.
// Included by nav.h before step_field.

static bool HAS_TP = false;     // we carry a teleport crystal (bought in shop_now)
static bool tp_safe();          // brain_safety.h: nothing near enough to hit us during the 3-turn crystal channel
static bool safe_to_channel();
static bool start_recall();
static void tp_resync();
static bool on_own_spawn();

namespace lf {
using Lvl = std::vector<bc::bitboard>;
struct LField {
    std::vector<Lvl> lv;   // the pool; levels 0..n-1 are built
    int n = 0;
    int cap = 400;         // levels at most; a field that reaches it is done, states beyond it unreachable
    bc::bitboard roots, open;   // open: the walkable tiles and the roots (a root on a camp tile stays a goal)
    int goal = 0;               // level 0 = the tiles within this many walk steps of the roots (a camp field: our hit reach)
    bool have = false, done = false, logged = false;
    int spn = -2, spc = -2;     // once done: the best time from our spawn tiles, N layer 0 and C layer (-2 not yet)
};
static LField ALT, HOME;
static std::vector<std::unique_ptr<LField>> GL;   // by group field index (Group::fid); null until that field is built
                                                  // (GL.resize made 2 boards per group field in one turn)
static constexpr int GCAP = 40;
static constexpr int GAIN = 4;   // a crystal or a recall must save this many turns 
static bc::bitboard OPEN, MID, T1, T2, T3, T4, T5, T6;
static const bc::bitboard* OP = &OPEN;   // the open tiles of the field being built
static bool INIT = false;
static int NL = 1;          // N layers: 9 for a dash champion, else 1; the C layer is board NL
static int CL = 1;          // the C layer's index (= NL)
static int SLOW = 0;        // the N layer a kept crystal joins: the dash just used (7), or the only one
static size_t STRIDE = 0;   // words per chunk row of a board's raw buffer; 0 = read bits by host call
static long LCOST = 0;      // fuel of the dearest level built so far (not the last one: a new
                            // field's first levels allocate their boards and cost more, and overran the turn ~1 time a game)

static Lvl blank() {
    Lvl v;
    v.reserve((size_t)NL + 1);
    for (int j = 0; j <= NL; ++j) v.emplace_back(bc::uninitialized);
    return v;
}
// out = x grown by one king step, terrain ignored (out must not be T1/T2; out == x is fine)
static void grow(const bc::bitboard& x, bc::bitboard& out) {
    x.shift_right_into(1, T1); x.shift_right_into(-1, T2); T1 |= T2; T1 |= x;
    T1.shift_up_into(1, out); T1.shift_up_into(-1, T2); out |= T2; out |= T1;
}
// one walk or a wait: x and the open tiles next to it
static void walk(const bc::bitboard& x, bc::bitboard& out) { grow(x, out); out &= *OP; }
// the open tiles a dash leaves from to land in x
static void rdash(const bc::bitboard& x, bc::bitboard& out) { grow(x, T3); T3 &= MID; grow(T3, out); out &= *OP; }

// level k's boards, allocated the first time
static Lvl& slot(LField& L, int k) {
    if (L.lv.capacity() == 0) L.lv.reserve((size_t)L.cap + 1);   // never reallocates
    while ((int)L.lv.size() <= k) L.lv.push_back(blank());
    return L.lv[(size_t)k];
}
static void start(LField& L, const bc::bitboard& roots) {
    L.roots.clear(); L.roots |= roots;
    L.open.clear(); L.open |= OPEN; L.open |= roots;
    OP = &L.open;
    Lvl& Q = slot(L, 0);
    Q[0].clear(); Q[0] |= roots;
    for (int i = 0; i < L.goal; ++i) walk(Q[0], Q[0]);   // the stop: a camp trip ends in hit reach, not on its tiles
    for (int j = 1; j <= NL; ++j) { Q[j].clear(); Q[j] |= Q[0]; }
    if (NL == 9) walk(Q[7], Q[8]);
    L.n = 1;
    L.spn = L.spc = -2;
    L.have = true;
    L.done = false;
}
static void level(LField& L) {
    const int k = L.n;
    OP = &L.open;
    Lvl& Q = slot(L, k);
    const Lvl& P = L.lv[(size_t)k - 1];
    if (NL == 9) {
        for (int j = 1; j <= 7; ++j) walk(P[j - 1], Q[j]);   // cooling: walk or wait, one round nearer the dash
        walk(Q[7], Q[8]);                                     // dashed this turn: the walk is left
        walk(P[0], Q[0]);                                     // ready: walk and keep the dash ...
        rdash(P[8], T4); Q[0] |= T4;                          // ... dash, then walk
        rdash(P[7], T4); walk(T4, T5); Q[0] |= T5;            // ... walk, then dash
    } else {
        walk(P[0], Q[0]);
    }
    // crystal: hop from the tiles within 3 of a landing that is k-3 from the goal (dash ready on arrival), or walk on
    T6.clear();
    if (k >= 3) { T6 |= L.lv[(size_t)k - 3][0]; T6 &= OPEN; grow(T6, T6); grow(T6, T6); grow(T6, T6); T6 &= *OP; }   // lands on open tiles only
    T6 |= P[CL];
    walk(T6, Q[CL]);
    Q[CL] |= Q[SLOW];
    // done once nothing changed and the hop's input (layer 0, 3 levels back) has stopped too: an enclosed goal's N
    // layers stop at once while its C layer only starts at level 3
    bool same = k >= 3 && L.lv[(size_t)k - 3][0] == Q[0];
    for (int j = 0; j <= NL && same; ++j) same = Q[j] == P[j];
    if (!same) L.n = k + 1;
    if (same || L.n >= L.cap) L.done = true;
}

// A board's bit read from its raw buffer: one 64-bit word per 8x8 chunk, chunk rows STRIDE words apart, bit
// (y & 7) * 8 + (x & 7). A host is_set costs ~40 fuel more.
static inline bool bit(const bc::bitboard& b, vec2 p) {
    if (!STRIDE) return b.is_set(p);
    return (b.ptr()[(size_t)(p.y >> 3) * STRIDE + (size_t)(p.x >> 3)] >> (((p.y & 7) << 3) | (p.x & 7))) & 1;
}
// the layout above, checked against the host on a spread of tiles of the static walls
static void check_layout() {
    STRIDE = bc::detail::layout().chunk_stride;
    const int sy = std::max(3, H / 8), sx = std::max(5, W / 8);   // ~64 reads
    for (int y = 0; y < H && STRIDE; y += sy)
        for (int x = (y * 7) % sx; x < W; x += sx)
            if (bit(STATIC_BB, vec2{x, y}) != STATIC_BB.is_set(vec2{x, y})) { STRIDE = 0; }
}
static inline bool in(const LField& L, vec2 p, int c, int k) {
    return k >= 0 && k < L.n && inb(p) && bit(L.lv[(size_t)k][c], p);
}
static int val(const LField& L, vec2 p, int c) {
    const int n = L.n;
    if (!in(L, p, c, n - 1)) return UNREACH;
    int lo = 0, hi = n - 1;
    while (lo < hi) {
        const int m = (lo + hi) / 2;
        if (bit(L.lv[(size_t)m][c], p)) hi = m; else lo = m + 1;
    }
    return lo;
}

// the best time from our spawn tiles in layer c (0 or CL) of a built field
static int spawn_val(LField& L, int c) {
    int& v = c == CL ? L.spc : L.spn;
    if (v == -2) { v = UNREACH; for (auto s : own_spawn) v = std::min(v, val(L, s, c)); }
    return v;
}

static int dash_ab() {
    for (int s = 0; s < 2; ++s) if (ME.ab[s].dash) return s;
    return -1;
}
// our N layer now: the dash's rounds to ready (cast something else this turn: ready next turn at best)
static int now_layer() {
    const int s = dash_ab();
    if (s < 0) return 0;
    return std::clamp((int)ME.cd[s], ME.can_cast ? 0 : 1, 7);
}

// Build L's levels while the fuel lasts; true once it is done.
static bool build(LField& L, const bc::bitboard& roots) {
    if (L.done) return true;
    if ((long)bc::fuel_remaining() < LCOST + 15000) return false;
    if (!L.have) start(L, roots);
    while (!L.done && (long)bc::fuel_remaining() >= LCOST + 15000) {
        const long f0 = (long)bc::fuel_remaining();
        level(L);
        LCOST = std::max(LCOST, f0 - (long)bc::fuel_remaining());
    }
    return L.done;
}

// Spare fuel at the end of a turn: build the altar field, then the home one, then the camp ones, the
// job's (field index jf) first. The altar's submit tiles change when a camp on them dies or respawns: start that field
// again.
static void work(int jf) {
    if (!INIT && (long)bc::fuel_remaining() < 50000) return;   // the set-up costs up to 33k (was 30k)
    if (!INIT) {
        NL = dash_ab() >= 0 ? 9 : 1;
        CL = NL;
        SLOW = NL == 9 ? 7 : 0;
        OPEN = ~STATIC_BB;
        for (const auto& g : G) for (int k = 0; k < g.n; ++k) if (inb(g.m[k].p)) OPEN.set(g.m[k].p, false);
        MID = OPEN | bc::map::holes() | bc::map::altars();
        check_layout();
        INIT = true;
        LCOST = (NL == 9 ? 175L : 40L) * (long)(bc::bitboard::raw_buffer_size() / 8) * 13 / 10 + 3000;
    }
    for (int i = 0; i < 2; ++i) {
        LField& L = i == 0 ? ALT : HOME;
        const Field& f = i == 0 ? F_ALTAR : F_HOME;
        if (!f.roots) continue;
        if (L.have && !(L.roots == *f.roots)) { L.have = L.done = L.logged = false; }
        if (!build(L, *f.roots)) return;
        if (L.n > 0 && !L.logged) { L.logged = true; }
    }
    // the camp fields: the job's first, then the others in turn (a group's spawn tiles never change). Only the groups'
    // own fields: the other GF entries have extra walls (a siege ring's reach zone) that these levels would not see.
    if (GL.size() < GF.size()) GL.resize(GF.size());
    for (int k = -1; k < (int)G.size(); ++k) {
        const int fi = k < 0 ? jf : G[(size_t)k].fid;
        if (fi < 0 || !GF[fi].roots) continue;
        auto& P = GL[(size_t)fi];
        if (P && P->done) continue;
        if (!P) {
            if ((long)bc::fuel_remaining() < LCOST + 15000) return;   // build() would stop at once: no boards yet
            P = std::make_unique<LField>();
        }
        LField& L = *P;
        L.cap = GCAP;
        L.goal = attack_reach(ME.melee);   // camp_combat.h's reach: timed to the tiles we hit the camp from
        if (!build(L, *GF[fi].roots)) return;
    }
}

static bool use_dash(int s, vec2 b) {
    bool ok = false;
    try { ok = bc::self().abilities()[s].use(b); } catch (const std::exception&) { ok = false; }
    if (!ok) return false;
    prev_pos = ME.p;
    ME.p = b;
    ME.cd[s] = 8;
    ME.can_cast = false;
    return true;
}

// a dash from p can land on b (b != p, cheb(p, b) <= 2): a middle tile next to both may be passed
static bool dash_link(vec2 p, vec2 b) {
    for (auto d : DIRS) {
        const vec2 a = p + d;
        if (cheb(a, b) == 1 && dash_mid_ok(a)) return true;
    }
    return false;
}

// the layered field of walk field f, if there is one
static const LField* of(const Field& f) {
    if (&f == &F_ALTAR) return &ALT;
    if (&f == &F_HOME) return &HOME;
    if (!GF.empty() && &f >= GF.data() && &f < GF.data() + GF.size()) {
        const size_t fi = (size_t)(&f - GF.data());
        return fi < GL.size() ? GL[fi].get() : nullptr;
    }
    return nullptr;
}

// ---- the crystal and the recall

static bool use_crystal(vec2 b) {
    bool ok = false;
    try { ok = bc::self().consumables()[bc::consumable_type::tp_crystal].use(b); } catch (const std::exception&) { tp_resync(); }
    if (!ok) { OCC.push_back(b); return false; }   // a hidden occupant: another tile next time
    HAS_TP = false;
    ME.can_move = ME.can_attack = ME.can_cast = false;
    ME.walked = true;
    return true;
}
// the best landing within 3 of p for field L: the open tile with the lowest time on arrival (dash ready), below lim
static vec2 landing(const LField& L, vec2 p, int lim, int& bv) {
    vec2 best{-1, -1};
    bv = lim;
    if (lim <= 0) return best;
    for (int dy = -3; dy <= 3; ++dy)
        for (int dx = -3; dx <= 3; ++dx) {
            const vec2 b{p.x + dx, p.y + dy};
            if ((!dx && !dy) || !in(L, b, 0, bv - 1) || !bit(OPEN, b) || occupied(b)) continue;   // one bit before the search
            const int v = val(L, b, 0);
            if (v < bv) { bv = v; best = b; }
        }
    return best;
}
// our time to L's goal in layer c. A tile off the field (a live camp's tile when the field was built: we stand on it
// once the camp died) is one step more than its best neighbour, rather than no way at all
static int here(const LField& L, int c) {
    const int v = val(L, ME.p, c);
    if (v != UNREACH || !inb(ME.p) || bit(L.open, ME.p)) return v;
    const int cw = c == CL ? CL : std::max(0, c - 1);
    int w = UNREACH;
    for (auto d : DIRS) {
        const vec2 q = ME.p + d;
        if (inb(q)) w = std::min(w, val(L, q, cw));
    }
    return w == UNREACH ? UNREACH : w + 1;
}

static int HOLD = 0, HOLD_R = -10;   // turns held for a safe hop
// One turn of a trip down L carrying the crystal, when the crystal is worth it (C + GAIN <= N now): hop from here if
// that keeps the best time, else walk the C layer (and hop at once if the step reached the hop tile). False: not worth
// it, or no such move: the caller's walk runs.
static bool crystal_step(const LField& L, const Field& f, int goal_d) {
    if (!HAS_TP || !L.done || !ME.can_move || ME.cast_excl || !inb(ME.p)) return false;
    const int cc = here(L, CL), cn = here(L, now_layer());
    if (cc == UNREACH || (cn != UNREACH && cc + GAIN > cn)) return false;
    const bool safe = tp_safe();
    int bv;
    const vec2 b = landing(L, ME.p, cc - 2, bv);   // a hop now that keeps the best time lands at cc - 3 or less
    if (b.x >= 0) {
        if (safe && use_crystal(b)) { return true; }
        if (!safe) {
            const int r = rnd();
            if (r - HOLD_R > 1) HOLD = 0;
            HOLD_R = r;
            if (++HOLD <= 6) { ME.walked = true; return true; }
            return false;
        }
    }
    // walk on the C layer: to a tile nearer on it, or to one with a landing that keeps the best time (walk, then hop
    // this turn)
    const auto score = [&](vec2 q) { return f.at(q) * 16 + danger_cost(q) * 4; };
    vec2 best{-1, -1};
    int bs = 1 << 30;
    for (auto d : DIRS) {
        const vec2 q = ME.p + d;
        if (!inb(q) || !in(L, q, CL, cc) || f.at(q) < goal_d || !walkable(q)) continue;
        if (sg_bad(q)) continue;
        const bool h = !in(L, q, CL, cc - 1);   // as far on C: only its hop keeps the time
        if (h && (!safe || landing(L, q, cc - 2, bv).x < 0)) continue;
        const int sc = score(q);
        if (sc < bs) { bs = sc; best = q; }
    }
    if (best.x < 0 || !try_move_to(best)) { return false; }
    stuck_turns = 0;
    if (safe) {
        const vec2 b2 = landing(L, ME.p, cc - 2, bv);
        if (b2.x >= 0) use_crystal(b2);
    }
    return true;
}
// turns to L's goal by recalling now: 5 to our spawn, then the trip from there (with the crystal bought there when
// carried or affordable and worth it)
static bool tp_worth();
static int via_recall(LField& L) {
    const int v = spawn_val(L, HAS_TP || (ME.gold >= 10 && tp_worth()) ? CL : 0);
    return v == UNREACH ? UNREACH : 5 + v;
}
// One turn of travel down F_ALTAR or F_HOME by the crystal or a recall, when either saves GAIN turns over walking (or
// is the only way). False: walk as before (the dash field's step, then the walk field's).
static bool travel(const Field& f, int goal_d) {
    LField* Lp = const_cast<LField*>(of(f));
    if (!Lp || !Lp->done || !inb(ME.p) || !ME.can_move) return false;
    LField& L = *Lp;
    const int cn = here(L, now_layer());
    const int now = HAS_TP ? std::min(cn, here(L, CL)) : cn;
    if (now != UNREACH && now <= GAIN) return false;
    if (!on_own_spawn()) {
        const int vr = via_recall(L);
        if (vr != UNREACH && (now == UNREACH || vr + GAIN < now) && safe_to_channel()) {
            if (start_recall()) return true;
        }
    }
    return crystal_step(L, f, goal_d);
}
// the bank trip's length for want_bank: the walk field's distance da, less what the crystal or a recall saves; with no
// walk to the altar from here, the crystal or recall trip itself
static int bank_trip(int da) {
    if (!ALT.done || !inb(ME.p)) return da;
    const int cn = here(ALT, now_layer());
    int best = HAS_TP ? here(ALT, CL) : UNREACH;
    if (!on_own_spawn()) best = std::min(best, via_recall(ALT));
    if (cn == UNREACH) return best;
    if (best != UNREACH && da != UNREACH && best + GAIN <= cn) return std::max(0, da - (cn - best));
    return da;
}
// crystals worth buying: from our spawn the altar is GAIN nearer with one (or only reachable with one)
static bool tp_worth() {
    if (!ALT.done) return false;
    const int vn = spawn_val(ALT, 0), vc = spawn_val(ALT, CL);
    return vc != UNREACH && (vn == UNREACH || vc + GAIN <= vn);
}
// In the altar's pocket with no walk home (a ring of camps round it): out by the crystal, else a recall. True: acted
// or held.
static bool pocket() {
    if (!ALT.done || !HOME.done || !inb(ME.p) || !bit(OPEN, ME.p) || on_own_spawn()) return false;
    if (here(HOME, now_layer()) != UNREACH || here(ALT, now_layer()) == UNREACH) return false;
    if (crystal_step(HOME, F_HOME, 0)) return true;
    if (safe_to_channel()) start_recall();
    return true;
}

// One turn of travel down f by the layered field: an action that keeps the best time, landing no nearer than goal_d
// on f (the caller stops there). False: no field for f, or every such move is blocked (units): the caller's
// walk-field step runs instead. Fuel: every small tile check costs 40-90 (walkable() ~300), so the candidates are
// filtered by the field's bits first; a 512-tile walk-then-dash scan cost up to 70k and suspended turns.
static bool step(const Field& f, int goal_d) {
    const LField* Lp = of(f);
    if (!Lp || !Lp->done || !ME.can_move) return false;
    // a camp field times the trip to our hit reach: a dash that gets us there a turn sooner is taken, where the time to
    // the camp's own tiles was the same either way and the walk kept the dash. Another stop (waiting at 2 by a
    // respawn) is not what it times: the walk field's step
    if (Lp->goal > 0 && goal_d != Lp->goal) return false;
    const LField& L = *Lp;
    if (crystal_step(L, f, goal_d)) return true;
    const int s = dash_ab();
    if (s < 0) return false;
    const int c = now_layer();
    const int cur = here(L, c);
    if (cur == UNREACH || cur == 0) return false;
    const int t = cur - 1;
    const auto score = [&](vec2 q) { return f.at(q) * 16 + danger_cost(q) * 4; };
    // a walk that keeps the best time; the dash is kept whenever walking is as fast. Not on a camp trip: it ends in
    // the camp fight, whose approach within reach + 3 (camp_combat.h) walks only, so a dash kept for the next turn was
    // never used (the hits started 2 turns after the old dash_along's). A ready dash that keeps the best time goes first
    const int wl = c > 0 ? c - 1 : 0;
    const bool dash_first = L.goal > 0 && c == 0;
    const auto walk_step = [&]() {
        vec2 best{-1, -1};
        int bs = 1 << 30;
        for (auto d : DIRS) {
            const vec2 q = ME.p + d;
            if (f.at(q) < goal_d || !in(L, q, wl, t) || !walkable(q)) continue;
            if (sg_bad(q)) continue;
            const int sc = score(q);
            if (sc < bs) { bs = sc; best = q; }
        }
        if (best.x >= 0 && try_move_to(best)) { stuck_turns = 0; return true; }
        return false;
    };
    if (!dash_first && walk_step()) return true;
    if (c > 0) {
        if (in(L, ME.p, wl, t)) { return true; }   // waiting out the cooldown is as fast as any walk
        return false;
    }
    // dash, then walk: the landing tiles that keep the best time first (a bit read each), then a middle tile
    vec2 bb{-1, -1};
    int bs = 1 << 30;
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) {
            const vec2 b{ME.p.x + dx, ME.p.y + dy};
            if (b == ME.p || !in(L, b, 8, t) || f.at(b) < goal_d || !dash_link(ME.p, b) || !walkable(b)) continue;
            if (sg_bad(b)) continue;
            const int sc = score(b);
            if (sc < bs) { bs = sc; bb = b; }
        }
    if (bb.x >= 0 && use_dash(s, bb)) {
        stuck_turns = 0;
        vec2 bq{-1, -1};
        int qs = 1 << 30;
        for (auto d : DIRS) {
            const vec2 q = ME.p + d;
            if (f.at(q) < goal_d || !in(L, q, 7, t) || !walkable(q)) continue;
            if (sg_bad(q)) continue;
            const int sc = score(q);
            if (sc < qs) { qs = sc; bq = q; }
        }
        // a tie with standing still still walks on: the trip often changes job before it ends, and the walk keeps
        // its lead on any later plan (the stop left a camp hit a turn late after a job switch)
        if (bq.x >= 0 && f.at(ME.p) > goal_d && (!in(L, ME.p, 7, t) || qs < score(ME.p))) try_move_to(bq);
        return true;
    }
    // walk, then dash: only to a tile a dash leaves from to land at level t (one board op, rather than 512 tile checks)
    OP = &L.open;
    rdash(L.lv[(size_t)t][7], T4);
    for (auto d0 : DIRS) {
        const vec2 w = ME.p + d0;
        if (!inb(w) || !bit(T4, w) || f.at(w) < goal_d || !walkable(w)) continue;
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) {
                const vec2 b{w.x + dx, w.y + dy};
                if (b == w || !in(L, b, 7, t) || f.at(b) < goal_d || !dash_link(w, b) || !walkable(b)) continue;
                if (sg_bad(w) || sg_bad(b)) continue;
                if (!try_move_to(w)) goto next_w;
                stuck_turns = 0;
                use_dash(s, b);
                return true;
            }
    next_w:;
    }
    if (dash_first && walk_step()) return true;
    return false;
}
}   // namespace lf
