#pragma once

// ---- the turn's picture
struct Threat {
    vec2 p; int md[6]; int mn;
    EnemyPower power;
    int ei;                 // targetable-enemy index, or -1 (used by the scratch rollout)
    // expected spell damage on a tile in spell range / one further: its average spell rate while ready, nothing on cooldown
    float spell_inc, area_inc;
};
static Threat TH[6];
static int TH_N = 0;
static vec2 MATES[6];
static int MATES_N = 0;
static vec2 EP[6];               // targetable enemies near us
static float KV_B[6], KV_S[2][6], KV_A[2][6];   // basic value, spell value, area spell value centred on it
static int EN_N = 0;
static Champ* EC_G[6];           // the enemies behind EP
static Hazard LHAZ[24];
static int LHAZ_N = 0;

// Plans end within 3 tiles of the start: per-tile caches over a 7x7 window.
static vec2 MP0;
static inline int wi(vec2 q) { return (q.x - MP0.x + 3) * 7 + (q.y - MP0.y + 3); }
static bool C_WALK[49];
static float C_INC[49], C_BAS[49], C_SP[2][49];
static bool C_INC_OK[49], C_BAS_OK[49], C_SP_OK[2][49];

// ulw: walking steps from each window tile to the nearest tile we can hit the focus from. The straight-line
// pull holds a body behind a wall: on the tank Varg stood at (18,26) 10 rounds, 3 from a 107-hp Vendetta,
// every tile toward it a wall, while a Varg one fight_r lower walked round on RALLY's path field and killed it.
static int8_t C_HIT[49];
static int C_HIT_FAR = 0;     // ... for a tile it does not reach: one past the farthest it does
static bool C_HIT_ON = false; // off: no wall in the box, the straight line is exact
static constexpr int HIT_MAXD = 12;   // ... counted this far (one 7x7 window and a detour); the rest are far
static constexpr int HIT_KEEP = 8;    // rounds the walk stays on for a focus once the straight line stalls
static void hit_steps(vec2 p0, vec2 f, int reach) {
    C_HIT_ON = false;
    // a box over the window and the hit tiles (the core) with 3 tiles of room for a detour, one bit mask per row (bit k = x0 + k)
    const int cx0 = std::min(p0.x - 3, f.x - reach), cx1 = std::max(p0.x + 3, f.x + reach);
    const int cy0 = std::min(p0.y - 3, f.y - reach), cy1 = std::max(p0.y + 3, f.y + reach);
    if (cx1 - cx0 + 1 > 32 || cy1 - cy0 + 1 > 32 || BLOCK.b.size() * 8 < (size_t)W * H) return;
    const int mx = std::min(3, (32 - (cx1 - cx0 + 1)) / 2), my = std::min(3, (32 - (cy1 - cy0 + 1)) / 2);
    const int x0 = std::max(0, cx0 - mx), x1 = std::min(W - 1, cx1 + mx);
    const int y0 = std::max(0, cy0 - my), y1 = std::min(H - 1, cy1 + my);
    const int bw = x1 - x0 + 1, bh = y1 - y0 + 1;
    const uint32_t full = bw == 32 ? 0xffffffffu : (1u << bw) - 1;
    const uint8_t* bb = BLOCK.b.data();
    const size_t nb = BLOCK.b.size();
    uint32_t walk[32], seen[32], fr[32], grow[32];
    for (int r = 0; r < bh; ++r) {
        const int i = (y0 + r) * W + x0;
        uint64_t v = 0;
        for (int k = 0; k < 5 && (size_t)((i >> 3) + k) < nb; ++k) v |= (uint64_t)bb[(i >> 3) + k] << (8 * k);
        walk[r] = ~(uint32_t)(v >> (i & 7)) & full;
    }
    // no wall in the core: every straight path stays in it, so the straight line is the walk
    const uint32_t core = (2u << (std::min(cx1, x1) - x0)) - (1u << (std::max(cx0, x0) - x0));
    bool wall = false;
    for (int y = std::max(cy0, y0); y <= std::min(cy1, y1); ++y) wall |= (walk[y - y0] & core) != core;
    if (!wall) return;
    for (int k = 0; k < 49; ++k) C_HIT[k] = -1;
    int left = 0, far = -1;
    for (int k = 0; k < 49; ++k) left += C_WALK[k];
    const int wl = std::max(p0.x - 3, x0) - x0, wh = std::min(p0.x + 3, x1) - x0;
    const uint32_t wmask = (2u << wh) - (1u << wl);
    auto mark = [&](int d) {
        for (int y = std::max(p0.y - 3, y0); y <= std::min(p0.y + 3, y1); ++y)
            for (uint32_t m = fr[y - y0] & wmask; m; m &= m - 1) {
                const int k = wi(vec2{x0 + __builtin_ctz(m), y});
                if (C_WALK[k] && C_HIT[k] < 0) { C_HIT[k] = (int8_t)d; far = d; --left; }
            }
    };
    const int rl = std::max(f.x - reach, x0) - x0, rh = std::min(f.x + reach, x1) - x0;
    const uint32_t rmask = (2u << rh) - (1u << rl);
    for (int r = 0; r < bh; ++r) { fr[r] = std::abs(y0 + r - f.y) <= reach ? walk[r] & rmask : 0; seen[r] = fr[r]; }
    mark(0);
    for (int d = 1; left > 0 && d <= HIT_MAXD; ++d) {
        for (int r = 0; r < bh; ++r) grow[r] = fr[r] | fr[r] << 1 | fr[r] >> 1;
        bool any = false;
        for (int r = 0; r < bh; ++r) {
            fr[r] = (grow[r] | (r > 0 ? grow[r - 1] : 0) | (r + 1 < bh ? grow[r + 1] : 0)) & walk[r] & ~seen[r];
            seen[r] |= fr[r];
            any |= fr[r] != 0;
        }
        if (!any) break;
        mark(d);
    }
    if (far < 0) return;   // no window tile reaches it inside the box: keep the straight line
    C_HIT_FAR = far + 1;
    C_HIT_ON = true;
}

static float basic_best(vec2 from) {
    const int k = wi(from);
    if (C_BAS_OK[k]) return C_BAS[k];
    const int reach = attack_reach(ME.melee);
    float v = 0;
    for (int i = 0; i < EN_N; ++i) if (cheb(from, EP[i]) <= reach) v = std::max(v, KV_B[i]);
    C_BAS[k] = v; C_BAS_OK[k] = true;
    return v;
}
static float spell_cached(int s, vec2 from) {
    const int k = wi(from);
    if (C_SP_OK[s][k]) return C_SP[s][k];
    float v = 0;
    if (spell_area(s)) {
        for (int c = 0; c < AC_N[s]; ++c) if (cheb(from, AC_P[s][c]) <= 4) v = std::max(v, AC_V[s][c]);
        C_SP[s][k] = v; C_SP_OK[s][k] = true;
        return v;
    }
    const float* kv = spell_area(s) ? KV_A[s] : KV_S[s];
    for (int i = 0; i < EN_N; ++i) if (cheb(from, EP[i]) <= 4) v = std::max(v, kv[i]);
    C_SP[s][k] = v; C_SP_OK[s][k] = true;
    return v;
}

// Everything about an end tile that does not depend on how we got there: minus the expected damage, plus position.
static MicroMode M_MODE = MM_BATTLE;
static float M_LAM = 1.f;
static float M_PULL = 18.f;
static float M_PULL_NEXT = 0.f;
static constexpr float TANK_LAM = 0.6f;    // a tank's weight on the damage it would take (x M_LAM)
static constexpr float TANK_PULL = 30.f;   // ... its pull to the focus per tile (others 18)
static constexpr float SCREEN_B = 20.f;    // ... per (enemy, ally) pair it stands between, at most 3 pairs
static vec2 SCREENED[6];                   // non-tank allies within 6 a tank stands in front of
static int SCREENED_N = 0;   // one micro call with this pull to the focus (then reset)
static vec2 FIN_END{-1, -1};     // the end tile of the solo kill plan (bonus in micro_fight)
static vec2 FIN_DASH{-1, -1};    // ... and its dash tile (-1: a walk only)
// Camp monsters hit the nearest champion of either team at the next round start (05-camps.md). Standing there then:
// the champions that cannot move before it (allies ranked before us this round, enemies that already acted, anyone
// channelling or stunned). Per local monster: the nearest such ally (not us) and enemy (99: none), that enemy's health
// and damage weight as in kill_value.
static int LHAZ_FA[24], LHAZ_FE[24];
static float LHAZ_FH[24], LHAZ_FW[24];
static void camp_fixed() {
    for (int i = 0; i < LHAZ_N; ++i) { LHAZ_FA[i] = LHAZ_FE[i] = 99; LHAZ_FH[i] = 0.f; LHAZ_FW[i] = 1.f; }
    if (LHAZ_N == 0 || MY_SLOT >= 6) return;
    own_prios();
    for (int k = 0; k < (int)ALLY.size() && k < 6; ++k) {
        Champ& a = ALLY[k];
        if (k == MY_SLOT || !a.vis || !a.alive || a.hp <= 0) continue;
        read_ecd(a);
        const bool before = OWN_P[0][k] > OWN_P[0][MY_SLOT] || (OWN_P[0][k] == OWN_P[0][MY_SLOT] && k < MY_SLOT);
        if (!before && !a.channel && !a.stunned) continue;
        for (int i = 0; i < LHAZ_N; ++i) LHAZ_FA[i] = std::min(LHAZ_FA[i], cheb(LHAZ[i].p, a.p));
    }
    for (auto& e : ENEMY) {
        if (!e.vis || !e.alive || e.hp <= 0) continue;
        read_ecd(e);
        if (!e.channel && !e.stunned && enemy_later(e) != 0) continue;
        for (int i = 0; i < LHAZ_N; ++i) {
            const int d = cheb(LHAZ[i].p, e.p);
            if (d < LHAZ_FE[i]) { LHAZ_FE[i] = d; LHAZ_FH[i] = e.hp; LHAZ_FW[i] = 1.f + 0.6f * (1.f - e.hp / std::max(1.f, e.mhp)); }
        }
    }
}
// a landing (Heart of Ruin: damage within 1 of the landing tile) next to a sleeping monster wakes its
// group, which then hits the nearest champion every round one stands in its reach. Per window tile (bit wi): the Strength
// a landing there wakes. LHAZ holds awake monsters only, so the landing looked free (official 00eight r202: a 150-health
// HoR fled by a landing next to two sleeping elks and walked on; they killed the Marcella fleeing behind it, 71 gems)
static uint64_t WAKE_M = 0;
static float WAKE_S[49];
static void wake_tiles(vec2 p0) {
    WAKE_M = 0;
    const int r = rnd();
    for (const auto& g : G) {
        if (g.st != ST_ALIVE || g.str_total <= 0 || r - g.awake_at <= 2 || cheb(g.center, p0) > 16) continue;
        float s = 0.f;
        uint64_t gm = 0;
        for (int k = 0; k < g.n; ++k) {
            const Mon& m = g.m[k];
            if (m.hp <= 0 || cheb(m.p, p0) > 4) continue;
            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy) {
                    const vec2 c{m.p.x + dx, m.p.y + dy};
                    if (cheb(c, p0) <= 3) gm |= 1ull << wi(c);
                }
        }
        if (!gm) continue;
        for (int k = 0; k < g.n; ++k) if (g.m[k].hp > 0) s += g.m[k].str;
        for (uint64_t b = gm; b; b &= b - 1) {
            const int k = __builtin_ctzll(b);
            if (!(WAKE_M >> k & 1)) WAKE_S[k] = 0.f;
            WAKE_S[k] += s;
        }
        WAKE_M |= gm;
    }
}
// the score a dash landing on q pays for what it wakes: WAKE_COST_ROUNDS rounds of the woken Strength on us or an ally near it
static float wake_pen(vec2 q) {
    if (!WAKE_M || cheb(q, MP0) > 3 || !(WAKE_M >> wi(q) & 1)) return 0.f;
    return M_LAM * WAKE_COST_ROUNDS * WAKE_S[wi(q)];
}
// the camp damage tile q takes (a tie with a fixed champion counts as us: "do not count on a tie"); `credit` gets the
// hits a fixed enemy takes instead while we stand here. The monolith (group -2) keeps its flat keep-away charge.
static float camp_hit(vec2 q, float& burst, float& credit) {
    float inc = 0.f;
    for (int i = 0; i < LHAZ_N; ++i) {
        const Hazard& h = LHAZ[i];
        const float s = h.str;   // its Strength (was (w - 2) x 20)
        const int d = cheb(h.p, q);
        if (h.group < 0) { if (d <= h.reach) { inc += s; burst += s; } continue; }
        if (d <= h.reach && d <= std::min(LHAZ_FA[i], LHAZ_FE[i])) { inc += s; burst += s; }
        else if (LHAZ_FE[i] <= h.reach && LHAZ_FE[i] < LHAZ_FA[i] && LHAZ_FE[i] < d) credit += std::min(s, LHAZ_FH[i]) * LHAZ_FW[i];
    }
    return inc;
}
static float tile_score(vec2 q) {
    const int k = wi(q);
    if (C_INC_OK[k]) return C_INC[k];
    float inc = 0, burst = 0;
    int dn = 99;
    for (int i = 0; i < TH_N; ++i) {
        const Threat& t = TH[i];
        const int d0 = cheb(t.p, q);
        dn = std::min(dn, d0);
        // an enemy that does not act before our next turn is no threat to this tile;
        // one that acts twice can hit twice, or walk and then hit
        const EnemyPower& pw = t.power;
        if (pw.turns == 0) continue;
        const bool twice = pw.turns == 2;
        burst += danger_at(pw, d0);
        float h = 0;
        if (d0 <= pw.reach) h = (twice ? 2.f : 1.f) * pw.hit;
        else if (twice && d0 <= pw.reach + 1) h = pw.hit;
        else if (pw.dash_ready && d0 <= pw.reach + 2) h = 0.35f * pw.hit;
        if (h > 0) {
            // allies at least as close to it as this tile may take the hit instead
            int others = 0;
            for (int a = 0; a < t.mn; ++a) if (t.md[a] <= d0) ++others;
            inc += h / (1.f + 0.7f * others);
        }
        if (d0 <= (twice ? 5 : 4)) inc += t.spell_inc;
        else if (d0 <= (twice ? 6 : 5)) inc += t.area_inc;
    }
    float camp_cr = 0.f;
    inc += camp_hit(q, burst, camp_cr);
    const bool spawn = OWN_SPAWN[idx(q)];
    // a turn that ends on our spawn heals us to full (rules 03/06: at our turn end), so there only a burst of our full health
    // kills before our next turn: no damage counts there below that, and the lethal test below reads the full health
    const float lethal_at = spawn ? ME.mhp : ME.hp;
    if (spawn && burst < lethal_at) inc = 0.f;
    float pos = 0;
    if (M_MODE == MM_FLEE) {
        pos += 45.f * std::min(dn, 6);
        const int h = F_HOME.at(q);
        pos -= (8.f) * (h == UNREACH ? 60 : std::min<int>(h, 60));
    } else if (MFOCUS) {
        const int dt = cheb(q, MFOCUS->p);
        // ulw: walking steps to a tile we can hit it from (the straight line when no wall is near)
        const int steps = !C_HIT_ON ? std::max(0, dt - attack_reach(ME.melee)) : C_HIT[k] >= 0 ? C_HIT[k] : C_HIT_FAR;
        pos -= M_PULL * steps;
        if (!ME.melee && dt < 2) pos -= 25.f;
    }
    // a tank stands between an enemy and an ally: closer to the enemy than that ally, at most 2 from it
    if (champ(ME.type).tank && M_MODE != MM_FLEE) {
        int pairs = 0;
        for (int i = 0; i < TH_N && pairs < 3; ++i)
            for (int c = 0; c < SCREENED_N && pairs < 3; ++c)
                if (cheb(q, SCREENED[c]) <= 2 && cheb(TH[i].p, SCREENED[c]) <= 5 && cheb(q, TH[i].p) < cheb(SCREENED[c], TH[i].p)) ++pairs;
        pos += SCREEN_B * pairs;
    }
    int mates = 0;
    for (int a = 0; a < MATES_N; ++a) { const int dm = cheb(MATES[a], q); if (dm <= 3) ++mates; }
    pos += 10.f * std::min(mates, 3);
    if (spawn) pos += M_MODE == MM_FLEE ? 80.f : 25.f;
    if (burst >= lethal_at) pos -= M_MODE == MM_FLEE ? 1500.f : 500.f;   // on our spawn too, against our full health
    const float sc = pos - M_LAM * inc;
    C_INC[k] = sc; C_INC_OK[k] = true;
    return sc;
}

#include <wasm_simd128.h>
// tile_score for up to 12 window tiles in G (1..3) groups of 4 SIMD lanes. Every lane does the operations of
// tile_score in the same order; where tile_score skips an addition the lane adds +0 (x + 0 == x for every value that
// occurs here), so the results are bit-identical (shadow-checked on 270k tiles). Threats, hazards and allies are the
// outer loops, so each one's fields are read once for all tiles. Lookups that need a tile index (own spawn bit, home
// distance, ulw walk steps) are read per lane; the spawn bits only when the own spawn's box touches the window.
// The lanes: qx/qy per group, kk the window index of each lane, mask the lanes to store (bit 4g + l).
template <int G>
static void tiles_core(const v128_t* qx, const v128_t* qy, const int8_t* kk, uint32_t mask) {
    const bool flee = M_MODE == MM_FLEE, melee = ME.melee;
    const v128_t zero = wasm_i32x4_splat(0);
    auto cheb4 = [](v128_t ax, v128_t ay, v128_t bx, v128_t by) {
        return wasm_i32x4_max(wasm_i32x4_abs(wasm_i32x4_sub(bx, ax)), wasm_i32x4_abs(wasm_i32x4_sub(by, ay)));
    };
    auto fsel = [](v128_t m, v128_t v) { return wasm_v128_and(m, v); };   // v where m, else +0
    v128_t inc[G], burst[G], dn[G];
#pragma clang loop unroll(full)
    for (int g = 0; g < G; ++g) { inc[g] = wasm_f32x4_splat(0.f); burst[g] = wasm_f32x4_splat(0.f); dn[g] = wasm_i32x4_splat(99); }
    for (int i = 0; i < TH_N; ++i) {
        const Threat& t = TH[i];
        const v128_t px = wasm_i32x4_splat(t.p.x), py = wasm_i32x4_splat(t.p.y);
        const EnemyPower& pw = t.power;
        if (pw.turns == 0) {
#pragma clang loop unroll(full)
            for (int g = 0; g < G; ++g) dn[g] = wasm_i32x4_min(dn[g], cheb4(qx[g], qy[g], px, py));
            continue;
        }
        const bool twice = pw.turns == 2, dsh = pw.dash_ready, area = pw.area > 0;
        const float hit2 = (twice ? 2.f : 1.f) * pw.hit;
        const v128_t r1 = wasm_i32x4_splat(pw.reach), r2 = wasm_i32x4_splat(pw.reach + 1), r3 = wasm_i32x4_splat(pw.reach + 2);
        const v128_t rs = wasm_i32x4_splat(twice ? 5 : 4), ra = wasm_i32x4_splat(twice ? 6 : 5);
        const v128_t vh2 = wasm_f32x4_splat(hit2), vh1 = wasm_f32x4_splat(pw.hit), vh3 = wasm_f32x4_splat(0.35f * pw.hit);
        const v128_t vsa = wasm_f32x4_splat(t.spell_inc);
        v128_t run_hi[9], run_v[9];
        for (int r = 0; r < pw.runs; ++r) { run_hi[r] = wasm_i32x4_splat(pw.run_hi[r]); run_v[r] = wasm_f32x4_splat(pw.run_v[r]); }
        const v128_t vai = wasm_f32x4_splat(t.area_inc);
        const v128_t one = wasm_f32x4_splat(1.f), c07 = wasm_f32x4_splat(0.7f);
        const int mn = t.mn;
#pragma clang loop unroll(full)
        for (int g = 0; g < G; ++g) {
            const v128_t d0 = cheb4(qx[g], qy[g], px, py);
            dn[g] = wasm_i32x4_min(dn[g], d0);
            const v128_t m1 = wasm_i32x4_le(d0, r1);
            const v128_t m2 = twice ? wasm_v128_andnot(wasm_i32x4_le(d0, r2), m1) : zero;
            const v128_t m3 = dsh ? wasm_v128_andnot(wasm_v128_andnot(wasm_i32x4_le(d0, r3), m1), m2) : zero;
            const v128_t hb = wasm_v128_or(fsel(m1, vh2), fsel(m2, vh1));
            const v128_t h = wasm_v128_or(hb, fsel(m3, vh3));
            v128_t seen = zero, danger = zero;   // danger_at: each lane takes the value of the first run that holds it
            for (int r = 0; r < pw.runs; ++r) {
                const v128_t m = wasm_i32x4_le(d0, run_hi[r]);
                danger = wasm_v128_or(danger, fsel(wasm_v128_andnot(m, seen), run_v[r]));
                seen = m;
            }
            burst[g] = wasm_f32x4_add(burst[g], danger);
            v128_t cnt = zero;   // allies at least as close to it (the mask is -1 per hit)
            for (int a = 0; a < mn; ++a) cnt = wasm_i32x4_sub(cnt, wasm_i32x4_le(wasm_i32x4_splat(t.md[a]), d0));
            inc[g] = wasm_f32x4_add(inc[g], wasm_f32x4_div(h, wasm_f32x4_add(one, wasm_f32x4_mul(c07, wasm_f32x4_convert_i32x4(cnt)))));
            const v128_t ms = wasm_i32x4_le(d0, rs);
            const v128_t ma = area ? wasm_v128_andnot(wasm_i32x4_le(d0, ra), ms) : zero;
            inc[g] = wasm_f32x4_add(inc[g], wasm_v128_or(fsel(ms, vsa), fsel(ma, vai)));
        }
    }
    if (LHAZ_N > 0) {
        v128_t cinc[G];
#pragma clang loop unroll(full)
        for (int g = 0; g < G; ++g) cinc[g] = wasm_f32x4_splat(0.f);
        for (int i = 0; i < LHAZ_N; ++i) {
            const Hazard& hz = LHAZ[i];
            const v128_t lim = wasm_i32x4_splat(hz.group < 0 ? hz.reach : std::min(hz.reach, std::min(LHAZ_FA[i], LHAZ_FE[i])));
            const v128_t hx = wasm_i32x4_splat(hz.p.x), hy = wasm_i32x4_splat(hz.p.y), st = wasm_f32x4_splat(hz.str);
#pragma clang loop unroll(full)
            for (int g = 0; g < G; ++g) {
                const v128_t m = wasm_i32x4_le(cheb4(qx[g], qy[g], hx, hy), lim);
                cinc[g] = wasm_f32x4_add(cinc[g], fsel(m, st));
                burst[g] = wasm_f32x4_add(burst[g], fsel(m, st));
            }
        }
#pragma clang loop unroll(full)
        for (int g = 0; g < G; ++g) inc[g] = wasm_f32x4_add(inc[g], cinc[g]);
    }
    // per-lane lookups read the coordinates back from memory, only when needed
    alignas(16) int lx[4 * G], ly[4 * G];
    const bool sp_any = OSP_X1 >= MP0.x - 3 && OSP_X0 <= MP0.x + 3 && OSP_Y1 >= MP0.y - 3 && OSP_Y0 <= MP0.y + 3;
    const bool hit_on = C_HIT_ON;
    if (sp_any || flee || hit_on) {
#pragma clang loop unroll(full)
        for (int g = 0; g < G; ++g) { wasm_v128_store(lx + 4 * g, qx[g]); wasm_v128_store(ly + 4 * g, qy[g]); }
    }
    v128_t spawn[G];
#pragma clang loop unroll(full)
    for (int g = 0; g < G; ++g) {
        if (!sp_any) { spawn[g] = zero; continue; }
        int b[4];
        for (int l = 0; l < 4; ++l) b[l] = OWN_SPAWN[idx(vec2{lx[4 * g + l], ly[4 * g + l]})] ? -1 : 0;
        spawn[g] = wasm_i32x4_make(b[0], b[1], b[2], b[3]);
    }
    const v128_t vhp = wasm_f32x4_splat(ME.hp), vmhp = wasm_f32x4_splat(ME.mhp), vlam = wasm_f32x4_splat(M_LAM);
    const v128_t vsb = wasm_f32x4_splat(flee ? 80.f : 25.f), vlp = wasm_f32x4_splat(flee ? 1500.f : 500.f);
    const Champ* foc = MFOCUS;
    const v128_t fx = wasm_i32x4_splat(foc ? foc->p.x : 0), fy = wasm_i32x4_splat(foc ? foc->p.y : 0);
    const v128_t vpull = wasm_f32x4_splat(M_PULL), rme = wasm_i32x4_splat(attack_reach(melee));
    const int hit_far = C_HIT_FAR;
#pragma clang loop unroll(full)
    for (int g = 0; g < G; ++g) {
        const v128_t lethal_at = wasm_v128_bitselect(vmhp, vhp, spawn[g]);
        const v128_t it = wasm_v128_andnot(inc[g], wasm_v128_and(spawn[g], wasm_f32x4_lt(burst[g], lethal_at)));
        v128_t pos = wasm_f32x4_splat(0.f);
        if (flee) {
            pos = wasm_f32x4_add(pos, wasm_f32x4_mul(wasm_f32x4_splat(45.f), wasm_f32x4_convert_i32x4(wasm_i32x4_min(dn[g], wasm_i32x4_splat(6)))));
            int hv[4];
            for (int l = 0; l < 4; ++l) { const int h = F_HOME.at(vec2{lx[4 * g + l], ly[4 * g + l]}); hv[l] = h == UNREACH ? 60 : std::min<int>(h, 60); }
            pos = wasm_f32x4_sub(pos, wasm_f32x4_mul(wasm_f32x4_splat(8.f), wasm_f32x4_convert_i32x4(wasm_i32x4_make(hv[0], hv[1], hv[2], hv[3]))));
        } else if (foc) {
            const v128_t dt = cheb4(qx[g], qy[g], fx, fy);
            v128_t steps;
            if (!hit_on) steps = wasm_i32x4_max(zero, wasm_i32x4_sub(dt, rme));
            else {
                int sv[4];
                for (int l = 0; l < 4; ++l) { const int k = kk[4 * g + l]; sv[l] = k < 0 ? 0 : C_HIT[k] >= 0 ? C_HIT[k] : hit_far; }
                steps = wasm_i32x4_make(sv[0], sv[1], sv[2], sv[3]);
            }
            pos = wasm_f32x4_sub(pos, wasm_f32x4_mul(vpull, wasm_f32x4_convert_i32x4(steps)));
            if (!melee) pos = wasm_f32x4_sub(pos, fsel(wasm_i32x4_lt(dt, wasm_i32x4_splat(2)), wasm_f32x4_splat(25.f)));
        }
        v128_t mc = zero;
        for (int a = 0; a < MATES_N; ++a)
            mc = wasm_i32x4_sub(mc, wasm_i32x4_le(cheb4(qx[g], qy[g], wasm_i32x4_splat(MATES[a].x), wasm_i32x4_splat(MATES[a].y)), wasm_i32x4_splat(3)));
        pos = wasm_f32x4_add(pos, wasm_f32x4_mul(wasm_f32x4_splat(10.f), wasm_f32x4_convert_i32x4(wasm_i32x4_min(mc, wasm_i32x4_splat(3)))));
        pos = wasm_f32x4_add(pos, fsel(spawn[g], vsb));
        pos = wasm_f32x4_sub(pos, fsel(wasm_f32x4_ge(burst[g], lethal_at), vlp));
        const v128_t sc = wasm_f32x4_sub(pos, wasm_f32x4_mul(vlam, it));
        alignas(16) float out[4];
        wasm_v128_store(out, sc);
        for (int l = 0; l < 4; ++l) if (mask >> (4 * g + l) & 1) { const int k = kk[4 * g + l]; C_INC[k] = out[l]; C_INC_OK[k] = true; }
    }
}
template <int G>
static void tiles_simd_g(const int* qxa, const int* qya, const int8_t* kk, int n) {
    int la[4 * G];
    for (int l = 0; l < 4 * G; ++l) la[l] = l < n ? l : n - 1;
    v128_t qx[G], qy[G];
    int8_t kl[4 * G];
#pragma clang loop unroll(full)
    for (int g = 0; g < G; ++g) {
        qx[g] = wasm_i32x4_make(qxa[la[4 * g]], qxa[la[4 * g + 1]], qxa[la[4 * g + 2]], qxa[la[4 * g + 3]]);
        qy[g] = wasm_i32x4_make(qya[la[4 * g]], qya[la[4 * g + 1]], qya[la[4 * g + 2]], qya[la[4 * g + 3]]);
    }
    for (int l = 0; l < 4 * G; ++l) kl[l] = kk[la[l]];
    tiles_core<G>(qx, qy, kl, n >= 32 ? 0xffffffffu : (1u << n) - 1u);
}
// The 8 tiles round the start tile (DIRS order, lanes 0-7): the coordinates are p0 plus constant offsets, so no list is
// gathered; every lane is computed, the walkable ones (bit i of ok) are stored. melee: their basic_best too.
static void walk8_simd(uint32_t ok) {
    if (ok == 0) return;
    if (champ(ME.type).tank && M_MODE != MM_FLEE) { for (int i = 0; i < 8; ++i) if (ok >> i & 1) tile_score(MP0 + DIRS[i]); return; }
    static v128_t OX[2], OY[2];
    static int8_t OK8[8];
    static bool init = false;
    if (!init) {
        init = true;
        for (int g = 0; g < 2; ++g) {
            OX[g] = wasm_i32x4_make(DIRS[4 * g].x, DIRS[4 * g + 1].x, DIRS[4 * g + 2].x, DIRS[4 * g + 3].x);
            OY[g] = wasm_i32x4_make(DIRS[4 * g].y, DIRS[4 * g + 1].y, DIRS[4 * g + 2].y, DIRS[4 * g + 3].y);
        }
        for (int i = 0; i < 8; ++i) OK8[i] = (int8_t)((DIRS[i].x + 3) * 7 + (DIRS[i].y + 3));
    }
    const v128_t px = wasm_i32x4_splat(MP0.x), py = wasm_i32x4_splat(MP0.y);
    v128_t qx[2] = {wasm_i32x4_add(px, OX[0]), wasm_i32x4_add(px, OX[1])}, qy[2] = {wasm_i32x4_add(py, OY[0]), wasm_i32x4_add(py, OY[1])};
    uint32_t todo = 0;
    for (int i = 0; i < 8; ++i) if ((ok >> i & 1) && !C_INC_OK[OK8[i]]) todo |= 1u << i;
    if (todo) tiles_core<2>(qx, qy, OK8, todo);
    if (ME.can_attack && ME.melee) {
        uint32_t bt = 0;
        for (int i = 0; i < 8; ++i) if ((ok >> i & 1) && !C_BAS_OK[OK8[i]]) bt |= 1u << i;
        if (bt) {
            v128_t v[2] = {wasm_f32x4_splat(0.f), wasm_f32x4_splat(0.f)};
            const v128_t one = wasm_i32x4_splat(1);
            for (int e = 0; e < EN_N; ++e) {
                const v128_t ex = wasm_i32x4_splat(EP[e].x), ey = wasm_i32x4_splat(EP[e].y), kv = wasm_f32x4_splat(KV_B[e]);
                for (int g = 0; g < 2; ++g) {
                    const v128_t d = wasm_i32x4_max(wasm_i32x4_abs(wasm_i32x4_sub(ex, qx[g])), wasm_i32x4_abs(wasm_i32x4_sub(ey, qy[g])));
                    v[g] = wasm_v128_bitselect(wasm_f32x4_pmax(v[g], kv), v[g], wasm_i32x4_le(d, one));
                }
            }
            alignas(16) float out[8];
            wasm_v128_store(out, v[0]); wasm_v128_store(out + 4, v[1]);
            for (int i = 0; i < 8; ++i) if (bt >> i & 1) { C_BAS[OK8[i]] = out[i]; C_BAS_OK[OK8[i]] = true; }
        }
    }
}
// basic_best for up to 16 given tiles, any champion (reach 1 or 3), 4 tiles per SIMD lane group; pmax ==
// std::max here (no NaN; KV_B >= 0), enemies in the same order as basic_best
static void basics_any(const vec2* qs, int n_in) {
    int8_t kk[16]; int qxa[16], qya[16]; int n = 0;
    for (int j = 0; j < n_in && n < 16; ++j) {
        const int k = wi(qs[j]);
        if (C_BAS_OK[k]) continue;
        kk[n] = (int8_t)k; qxa[n] = qs[j].x; qya[n] = qs[j].y; ++n;
    }
    if (n == 0) return;
    const v128_t reach = wasm_i32x4_splat(attack_reach(ME.melee));
    for (int g = 0; g < n; g += 4) {
        const int l1 = std::min(g + 1, n - 1), l2 = std::min(g + 2, n - 1), l3 = std::min(g + 3, n - 1);
        const v128_t qx = wasm_i32x4_make(qxa[g], qxa[l1], qxa[l2], qxa[l3]), qy = wasm_i32x4_make(qya[g], qya[l1], qya[l2], qya[l3]);
        v128_t v = wasm_f32x4_splat(0.f);
        for (int i = 0; i < EN_N; ++i) {
            const v128_t dd = wasm_i32x4_max(wasm_i32x4_abs(wasm_i32x4_sub(wasm_i32x4_splat(EP[i].x), qx)), wasm_i32x4_abs(wasm_i32x4_sub(wasm_i32x4_splat(EP[i].y), qy)));
            v = wasm_v128_bitselect(wasm_f32x4_pmax(v, wasm_f32x4_splat(KV_B[i])), v, wasm_i32x4_le(dd, reach));
        }
        alignas(16) float out[4];
        wasm_v128_store(out, v);
        for (int l = 0; l < 4 && g + l < n; ++l) { const int k = kk[g + l]; C_BAS[k] = out[l]; C_BAS_OK[k] = true; }
    }
}
static void tiles_simd(const vec2* qs, int n_in) {
    int8_t kk[16]; int qxa[16], qya[16]; int n = 0;
    for (int j = 0; j < n_in && n < 16; ++j) {
        const int k = wi(qs[j]);
        if (C_INC_OK[k]) continue;
        kk[n] = (int8_t)k; qxa[n] = qs[j].x; qya[n] = qs[j].y; ++n;
    }
    if (n == 0) return;
    if (champ(ME.type).tank && M_MODE != MM_FLEE) { for (int t = 0; t < n; ++t) tile_score(vec2{qxa[t], qya[t]}); return; }   // screening term: scalar
    if (n <= 4) tiles_simd_g<1>(qxa, qya, kk, n);
    else if (n <= 8) tiles_simd_g<2>(qxa, qya, kk, n);
    else if (n <= 12) tiles_simd_g<3>(qxa, qya, kk, n);
    else tiles_simd_g<4>(qxa, qya, kk, n);
}
