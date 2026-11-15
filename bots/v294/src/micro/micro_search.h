#pragma once

// rounds of our last flights (the job walk reads them)
static int FLEE_R[4] = {-100, -100, -100, -100}, FLEE_I = 0;
static int FIGHT_R = -100, FIGHT_E = -1;   // last round we fought (not fled) and its focus (ENEMY index)
static int flees_since(int r0) { int n = 0; for (int x : FLEE_R) n += x >= r0; return n; }

static void remember_fight_or_flee(MicroMode mode, Champ* focus) {
    if (mode == MM_FLEE && FLEE_R[(FLEE_I + 3) & 3] != rnd()) FLEE_R[FLEE_I++ & 3] = rnd();
    if (mode != MM_FLEE && focus && focus >= &ENEMY[0] && focus < &ENEMY[0] + ENEMY.size()) { FIGHT_R = rnd(); FIGHT_E = (int)(focus - &ENEMY[0]); }
}

// a fight where we neither hit, nor took damage, nor moved for STANDOFF_TURNS rounds is a stalemate. Each side's
// micro found staying best, so it never ends: Marcella at 142 hp and a 249-hp Tavi stood 5 apart for 305 rounds
// (FINM), a healthy Varg 166 rounds in BATTLE. Leave those enemies out of the fight logic while
// they keep their distance, so the job / low-health logic runs again.
static void watch_standoff() {
    static int so_since = 0, so_last = -10;
    static vec2 so_p{-1, -1};
    const int r = rnd();
    if (ME.hurt || r - LAST_CHAMP_HIT <= 1 || ME.p != so_p || r - so_last > 1) so_since = r;
    so_p = ME.p; so_last = r;
    if (r - so_since >= STANDOFF_TURNS) {
        for (auto& e : ENEMY) if (e.vis && e.alive && cheb(e.p, ME.p) <= 7) e.so_until = r + STANDOFF_IGNORE_ROUNDS;
        so_since = r;
    }
}

static void read_walk_window(vec2 p0) {
    for (int k = 0; k < 49; ++k) { C_INC_OK[k] = false; C_BAS_OK[k] = false; C_SP_OK[0][k] = false; C_SP_OK[1][k] = false; C_WALK[k] = false; }
    // walkable tiles of the window, with one pass over the occupied list
    bool occ[49] = {};
    for (auto q : OCC) if (cheb(q, p0) <= 3) occ[wi(q)] = true;
    if (p0.x >= 3 && p0.y >= 3 && p0.x + 3 < W && p0.y + 3 < H && BLOCK.b.size() * 8 >= (size_t)W * H) {
        // the window is on the map: the wall bits are read directly, column by column
        const uint8_t* bb = BLOCK.b.data();
        for (int dx = 0; dx < 7; ++dx) {
            int i = (p0.y - 3) * W + p0.x - 3 + dx;
            for (int dy = 0; dy < 7; ++dy, i += W) C_WALK[dx * 7 + dy] = !((bb[i >> 3] >> (i & 7)) & 1) && !occ[dx * 7 + dy];
        }
    } else
        for (int dx = -3; dx <= 3; ++dx)
            for (int dy = -3; dy <= 3; ++dy) {
                const vec2 q{p0.x + dx, p0.y + dy};
                C_WALK[wi(q)] = inb(q) && !BLOCK[idx(q)] && !occ[wi(q)];
            }
}

// ulw: walk to the focus once no step gets closer to it in a straight line (a search costs ~15k fuel, so not on every
// fight turn near a wall: 920 a game), then for HIT_KEEP rounds: the way out of a pocket leads away from it first
static void plan_way_to_focus(MicroMode mode, Champ* focus, vec2 p0) {
    C_HIT_ON = false;
    if (mode != MM_FLEE && focus && M_PULL >= 1.f) {   // POKE pulls 0.01
        static const Champ* hit_foc = nullptr;
        static int hit_until = -100;
        const int reach = attack_reach(ME.melee), c0 = cheb(p0, focus->p);
        bool step = c0 <= reach;
        for (auto d : DIRS) { const vec2 q = p0 + d; step = step || (inb(q) && !BLOCK[idx(q)] && cheb(q, focus->p) < c0); }
        if (!step) { hit_foc = focus; hit_until = rnd() + HIT_KEEP; }
        if (hit_foc == focus && rnd() <= hit_until && (long)bc::fuel_remaining() > 40000) hit_steps(p0, focus->p, reach);
    }
}

static void read_mates_and_hazards(vec2 p0) {
    MATES_N = 0; SCREENED_N = 0;
    for (auto& a : ALLY)
        if (a.vis && a.alive && !a.channel && cheb(a.p, p0) <= 8 && MATES_N < 6) {
            MATES[MATES_N++] = a.p;
            if (&a != &ALLY[MY_SLOT] && !champ(a.type).tank && cheb(a.p, p0) <= 6 && SCREENED_N < 6) SCREENED[SCREENED_N++] = a.p;
        }
    LHAZ_N = 0;
    for (const auto& h : HAZ) if (cheb(h.p, p0) <= 6 && LHAZ_N < 24) LHAZ[LHAZ_N++] = h;
    camp_fixed();
    wake_tiles(p0);
}

static void read_my_spells() {
    for (int s = 0; s < 2; ++s) {
        const Ab& a = ME.ab[s];
        SP_OK[s] = ready(s) && (a.dmg || a.aoe || a.cc) && (a.enemies || a.ground) && !a.dash && !a.reset && !a.shield;
        SP_DMG[s] = 0;
        if (SP_OK[s] && !read_spell_damage(s, a)) SP_OK[s] = false;
    }
}

static void read_enemies(vec2 p0) {
    TH_N = 0; EN_N = 0;
    for (auto& e : ENEMY) {
        if (!e.alive || e.hp <= 0) continue;
        if (!e.vis) continue;
        if (cheb(e.p, p0) > 10) continue;
        int enemy_i = -1;
        if (targetable(e) && EN_N < 6) { enemy_i = EN_N; EC_G[EN_N] = &e; EP[EN_N] = e.p; KV_B[EN_N] = basic_value(e); ++EN_N; }
        if (chan_ignored(e) || TH_N >= 6) continue;
        Threat& t = TH[TH_N++];
        const float hor = is_hor(e) ? hor_mult(e) : 1.f;   // Heart of Ruin's passive, on all its damage
        t.p = e.p;
        t.ei = enemy_i;
        t.power = enemy_power(e);
        const float sp_avg = 1.5f * spell_avg(e.type, e.focus) * hor, sp_burst = spell_burst(e.type, e.focus) * hor;
        t.spell_inc = t.power.spell > 0.f ? sp_avg : 0.f;
        t.area_inc = sp_burst > 0.f ? sp_avg * t.power.area / sp_burst : 0.f;
        t.mn = 0;
        for (int a = 0; a < MATES_N; ++a) t.md[t.mn++] = cheb(MATES[a], e.p);
    }
}

static void read_spell_values(vec2 p0) {
    for (int s = 0; s < 2; ++s) {
        if (!SP_OK[s]) continue;
        const bool cc = ME.ab[s].cc;
        for (int i = 0; i < EN_N; ++i) KV_S[s][i] = kill_value(*EC_G[i], SP_DMG[s]) + (cc ? stun_value(*EC_G[i]) : 0.f);
        for (int i = 0; i < EN_N; ++i) {
            KV_A[s][i] = 0;
            for (int j = 0; j < EN_N; ++j) if (cheb(EP[i], EP[j]) <= 1) KV_A[s][i] += KV_S[s][j];
        }
        AC_N[s] = 0;
        if (spell_area(s)) {
            // damaging spells exclude walking and a dash spends the turn's cast: they go off from p0 only, so the
            // centres within 4 of it are all there is (deduplicated on a 9x9 grid around p0: fuel)
            bool seen_c[81] = {};
            for (int i = 0; i < EN_N; ++i)
                for (int dx = -1; dx <= 1; ++dx) for (int dy = -1; dy <= 1; ++dy) {
                    const vec2 c{EP[i].x + dx, EP[i].y + dy};
                    if (!inb(c) || AC_N[s] >= 60 || cheb(c, p0) > 4) continue;
                    const int gi = (c.x - p0.x + 4) * 9 + (c.y - p0.y + 4);
                    if (seen_c[gi]) continue;
                    seen_c[gi] = true;
                    float v = 0;
                    for (int j = 0; j < EN_N; ++j) if (cheb(EP[j], c) <= 1) v += KV_S[s][j];
                    AC_P[s][AC_N[s]] = c; AC_V[s][AC_N[s]] = v; ++AC_N[s];
                }
            // keep the 16 best centres: each window tile scans them for every plan (fuel)
            if (AC_N[s] > 16) {
                for (int i = 0; i < 16; ++i) {
                    int bi = i;
                    for (int j = i + 1; j < AC_N[s]; ++j) if (AC_V[s][j] > AC_V[s][bi]) bi = j;
                    std::swap(AC_V[s][i], AC_V[s][bi]); std::swap(AC_P[s][i], AC_P[s][bi]);
                }
                AC_N[s] = 16;
            }
        }
    }
}

static uint64_t R12_MASK = 0, R3_MASK = 0;   // window tiles at ring 1-2 / ring 3 from the centre

// the dash tiles in the order the (step, step) double loop first met them, from a table of the 64 pairs built once
// (the ring-2 offset of each pair)
static int8_t DASH_PR[64], DASH_RK[16];
static vec2 DASH_RO[16];
// the first-met order of the ring-2 tiles depends only on the 8-bit mask of passable middle steps: one list per mask
static const int8_t* dash_ring_order(uint32_t mid) {
    static bool pr_init = false;
    if (!pr_init) {
        pr_init = true;
        int nr = 0;
        for (int i1 = 0; i1 < 8; ++i1)
            for (int i2 = 0; i2 < 8; ++i2) {
                const vec2 o = DIRS[i1] + DIRS[i2];
                int r = -1;
                if (std::max(std::abs(o.x), std::abs(o.y)) == 2) {
                    for (int j = 0; j < nr; ++j) if (DASH_RO[j] == o) r = j;
                    if (r < 0) { r = nr; DASH_RO[nr] = o; DASH_RK[nr] = (int8_t)((o.x + 3) * 7 + (o.y + 3)); ++nr; }
                }
                DASH_PR[i1 * 8 + i2] = (int8_t)r;
            }
    }
    static int8_t ORD[256][17];
    static bool ORD_OK[256];
    if (!ORD_OK[mid]) {
        ORD_OK[mid] = true;
        uint32_t rs2 = 0; int on = 0;
        for (int p = 0; p < 64; ++p) {
            if (!(mid >> (p >> 3) & 1)) continue;
            const int r = DASH_PR[p];
            if (r < 0 || (rs2 >> r & 1)) continue;
            rs2 |= 1u << r; ORD[mid][1 + on++] = (int8_t)r;
        }
        ORD[mid][0] = (int8_t)on;
    }
    return ORD[mid];
}

// Every plan the turn can make is scored here and the best kept: stand, walk, dash and the landing dash, cheapest and
// likeliest first, so the plans that matter are scored before the fuel gates close.
struct PlanSearch {
    MicroMode mode;
    vec2 p0;
    int rs = reset_slot(), ds = dash_slot();
    // a dash with a landing (Heart of Ruin: damage and a stun on everything within 1 of the landing
    // tile) scores what it lands on; landing was 0 here, so the dash only moved us
    float land_dmg = 0.f;
    // cached per window tile for this call (the landing-priority and the dash-walk lists read the same tiles)
    bool lv_ok[49] = {};
    float lv_c[49];
    float woff = 1.f;
    Plan best;
    bool land_done[49] = {};
    // window tiles within 1 of an enemy (bit wi): only there can land_value be above 0, so the landing
    // scans below test this first and skip the rest of their checks (all pure) for every other tile
    uint64_t lmask = 0;
    // a 2-tile dash from p0 to the ring-2 tile q has a free middle tile: cached per tile (both landing scans ask)
    int8_t mid_c[49];
    bool dash_done[49] = {};

    bool walk_ok(vec2 q) const { return cheb(q, p0) <= 3 && C_WALK[wi(q)]; }

    float land_value(vec2 q) {
        if (land_dmg <= 0.f) return 0.f;
        const bool inw = cheb(q, p0) <= 3;
        const int k = inw ? wi(q) : 0;
        if (inw && lv_ok[k]) return lv_c[k];
        float v = 0.f;
        for (int i = 0; i < EN_N; ++i) if (cheb(EP[i], q) <= 1) v += kill_value(*EC_G[i], land_dmg) + stun_value(*EC_G[i]);
        if (inw) { lv_ok[k] = true; lv_c[k] = v; }
        return v;
    }

    void set_weights() {
        const float hpf = ME.hp / std::max(1.f, ME.mhp);
        M_LAM = MICRO_DAMAGE_WEIGHT * (1.f + 1.2f * (1.f - hpf));
        // A retreat is primarily an escape plan.  The old 0.35 attack weight still
        // made a low-health unit spend its turn trading damage instead of opening
        // distance; the replay losses show this repeatedly on Treasure Box and Packs.
        if (mode == MM_FLEE) { M_LAM = 2.f * (1.f + (1.f - hpf)); woff = 0.10f; }
        if (mode == MM_HOLD) M_LAM = 1.2f * (1.f + (1.f - hpf));
        if (mode == MM_POKE) M_LAM = POKE_DAMAGE_WEIGHT * (1.f + (1.f - hpf));
        if (champ(ME.type).tank && mode != MM_FLEE) M_LAM *= TANK_LAM;   // a tank takes the hits
    }

    // order 0: hit, dash, walk, hit (each part optional); 1: dash, hit on the dash tile, walk on; 2: walk, then dash
    // from there (the landing and the hit on the end tile)
    void consider(vec2 dash_to, vec2 walk_to, int order = 0) {
        const bool dashed = dash_to.x >= 0, walked = walk_to.x >= 0;
        const vec2 end = order == 2 ? dash_to : walked ? walk_to : dashed ? dash_to : p0;
        if (mode == MM_HOLD && !OWN_SPAWN[idx(end)]) return;
        // the basic attack: melee before or after moving; ranged only without walking (a dash keeps it)
        // scored in locals; the Plan is written only when it beats the best so far
        float atk_v = 0;
        int atk = 0, cast = -1;
        bool cast_first = false;
        if (ME.can_attack && order == 1) { const float v = basic_best(dash_to); if (v > 0) { atk_v = v; atk = 2; } }
        else if (ME.can_attack) {
            if (ME.melee || !walked) { const float v = basic_best(p0); if (v > 0) { atk_v = v; atk = 1; } }
            if (ME.melee || (dashed && !walked)) { const float v = basic_best(end); if (v > atk_v) { atk_v = v; atk = 3; } }
        }
        // the one cast
        float cast_v = 0;
        if (dashed) {
            cast = 3;
            cast_v = land_value(dash_to);
        } else if (ME.can_cast) {
            for (int s = 0; s < 2; ++s) {
                if (!SP_OK[s] || (ME.ab[s].excl && walked)) continue;
                float v = spell_cached(s, p0);
                bool first = true;
                if (walked) { const float v2 = spell_cached(s, end); if (v2 > v) { v = v2; first = false; } }
                if (v > cast_v) { cast_v = v; cast = s; cast_first = first; }
            }
            if (rs >= 0 && ME.can_attack) {
                float rv = 0;
                int code = atk;
                if (atk) {
                    rv = 0.9f * atk_v;                                          // a second hit
                    // Marcella's enhanced reset makes that hit stun for a round (rules 08); it was
                    // scored as a plain hit. The reset hit lands from the start tile (atk 1: mreset0) or the end tile.
                }
                else if (!ME.melee && walked) { rv = basic_best(end); code = 3; }  // walk, reset, attack
                if (rv > cast_v) { cast_v = rv; cast = 2; atk = code; cast_first = false; }
            }
        }
        float pos = tile_score(end);
        if (dashed) pos -= 25.f + (land_dmg > 0.f ? wake_pen(dash_to) : 0.f);   // the dash has an 8-turn cooldown
        if (walked || dashed) pos -= 1.f; // ties: stay
        // The burst check is only one turn deep. Promoting a flee spell to
        // full value can keep us trading in place across successive turns.
        // Keep free attacks/casts legal, with the same escape-first weight.
        float score = woff * (atk_v + cast_v) + pos;
        if (FIN_END.x >= 0 && end == FIN_END && atk_v + cast_v > 0) score += SOLO_KILL_BONUS;
        if (score > best.score) {
            best = Plan{};
            best.dash = dash_to; best.walk = walk_to; best.end = end; best.walk_first = order == 2;
            best.atk = atk; best.cast = cast; best.cast_first = cast_first;
            if (dashed) best.dash_damage = land_dmg;
            best.score = score; best.val = atk_v + cast_v;
        }
    }

    // the landing plans scored directly, each with consider()'s terms for its kind:
    //  dash only (order 0, no walk), dash-hit-walk (order 1), walk-dash (order 2, the landing on the end tile)
    void plan_out(float score, float val, vec2 dsh, vec2 wlk, vec2 end, bool wfirst, int atk) {
        if (FIN_END.x >= 0 && end == FIN_END && val > 0) score += SOLO_KILL_BONUS;
        if (score > best.score) {
            best = Plan{};
            best.dash = dsh; best.walk = wlk; best.end = end; best.walk_first = wfirst;
            best.atk = atk; best.cast = 3; best.dash_damage = land_dmg;
            best.score = score; best.val = val;
        }
    }
    void plan_dash(vec2 b) {             // consider(b, {-1, -1})
        if (mode == MM_HOLD && !OWN_SPAWN[idx(b)]) return;
        float atk_v = 0; int atk = 0;
        if (ME.can_attack) {
            const float v0 = basic_best(p0); if (v0 > 0) { atk_v = v0; atk = 1; }
            const float v = basic_best(b); if (v > atk_v) { atk_v = v; atk = 3; }
        }
        const float cast_v = land_value(b);
        float pos = tile_score(b); pos -= 25.f + wake_pen(b); pos -= 1.f;
        plan_out(woff * (atk_v + cast_v) + pos, atk_v + cast_v, b, vec2{-1, -1}, b, false, atk);
    }
    void plan_daw(vec2 b, vec2 w) {      // consider(b, w, 1)
        if (mode == MM_HOLD && !OWN_SPAWN[idx(w)]) return;
        float atk_v = 0; int atk = 0;
        if (ME.can_attack) { const float v = basic_best(b); if (v > 0) { atk_v = v; atk = 2; } }
        const float cast_v = land_value(b);
        float pos = tile_score(w); pos -= 25.f + wake_pen(b); pos -= 1.f;
        plan_out(woff * (atk_v + cast_v) + pos, atk_v + cast_v, b, w, w, false, atk);
    }
    void plan_wd(vec2 b, vec2 w) {       // consider(b, w, 2)
        if (mode == MM_HOLD && !OWN_SPAWN[idx(b)]) return;
        float atk_v = 0; int atk = 0;
        if (ME.can_attack && ME.melee) {
            const float v0 = basic_best(p0); if (v0 > 0) { atk_v = v0; atk = 1; }
            const float v = basic_best(b); if (v > atk_v) { atk_v = v; atk = 3; }
        }
        const float cast_v = land_value(b);
        float pos = tile_score(b); pos -= 25.f + wake_pen(b); pos -= 1.f;
        plan_out(woff * (atk_v + cast_v) + pos, atk_v + cast_v, b, w, b, true, atk);
    }

    // the walk tiles scored in SIMD; the plans below read the caches
    void score_walks_simd() {
        // the walk tiles in at most 2 SIMD groups; the stand tile on the scalar path (a third group for it alone
        // cost more than tile_score)
        if (ME.can_move && (long)bc::fuel_remaining() > 25000) {
            uint32_t ok = 0;
            for (int i = 0; i < 8; ++i) { const vec2 q = p0 + DIRS[i]; if (walk_ok(q) && (mode != MM_HOLD || OWN_SPAWN[idx(q)])) ok |= 1u << i; }
            walk8_simd(ok);
        }
    }

    void score_stand() {
        if (mode != MM_HOLD || OWN_SPAWN[idx(p0)]) {
            // the stand plan scored directly: consider() for no move and no dash, term by term (a melee
            // champion's end-tile hit is the start-tile hit there, never strictly better, so it is not read again)
            float atk_v = 0;
            int atk = 0, cast = -1;
            bool cast_first = false;
            if (ME.can_attack) { const float v = basic_best(p0); if (v > 0) { atk_v = v; atk = 1; } }
            float cast_v = 0;
            if (ME.can_cast) {
                for (int s = 0; s < 2; ++s) {
                    if (!SP_OK[s]) continue;
                    const float v = spell_cached(s, p0);
                    if (v > cast_v) { cast_v = v; cast = s; cast_first = true; }
                }
                if (rs >= 0 && ME.can_attack && atk) {
                    float rv = 0.9f * atk_v;
                    if (rv > cast_v) { cast_v = rv; cast = 2; cast_first = false; }
                }
            }
            float score = woff * (atk_v + cast_v) + tile_score(p0);
            if (FIN_END.x >= 0 && p0 == FIN_END && atk_v + cast_v > 0) score += SOLO_KILL_BONUS;
            if (score > best.score) {
                best = Plan{};
                best.end = p0;
                best.atk = atk; best.cast = cast; best.cast_first = cast_first;
                best.score = score; best.val = atk_v + cast_v;
            }
        }
    }

    // the kill plan itself, whatever the fuel gates below allow (low fuel in fights skipped 26 of 49 kill plans)
    void score_kill_plan() {
        if (FIN_END.x >= 0) {
            if (FIN_DASH.x < 0) { if (ME.can_move) consider({-1, -1}, FIN_END); }
            else if (ds >= 0) consider(FIN_DASH, FIN_END == FIN_DASH ? vec2{-1, -1} : FIN_END);
        }
    }

    void read_landing_mask() {
        if (!R12_MASK) for (int k = 0; k < 49; ++k) { const int r = std::max(std::abs(k / 7 - 3), std::abs(k % 7 - 3)); if (r == 1 || r == 2) R12_MASK |= 1ull << k; if (r == 3) R3_MASK |= 1ull << k; }
        if (ds >= 0 && land_dmg > 0.f) std::memset(mid_c, -1, sizeof mid_c);
        if (ds >= 0 && land_dmg > 0.f)
            for (int i = 0; i < EN_N; ++i)
                for (int dx = -1; dx <= 1; ++dx)
                    for (int dy = -1; dy <= 1; ++dy) {
                        const vec2 c{EP[i].x + dx, EP[i].y + dy};
                        if (cheb(c, p0) <= 3) lmask |= 1ull << wi(c);
                    }
    }

    bool mid2(vec2 q) {
        const int k = wi(q);
        if (mid_c[k] < 0) {
            bool mid = false;
            for (auto d : DIRS) {
                const vec2 a = p0 + d;
                if (cheb(a, q) == 1 && dash_mid_ok(a) && (BLOCK[idx(a)] || walk_ok(a))) { mid = true; break; }
            }
            mid_c[k] = mid ? 1 : 0;
        }
        return mid_c[k] == 1;
    }

    // a landing dash (Heart of Ruin) scored first. The dash plans below wait for the walks and
    // a 45k fuel gate: in 8 logged games 30% of the HoR turns with a landing on an enemy came in under it (no
    // landing ever chosen there) and most of the rest ran out of fuel partway through the dash tiles. Here the land
    // value of every tile a dash of 1 or 2 reaches is read first (cheap), and only the best few are scored.
    void score_landings() {
        if (ds >= 0 && land_dmg > 0.f && mode != MM_FLEE && (long)bc::fuel_remaining() > 20000) {
            vec2 cq[24]; float cv[24]; int cn = 0;
            // the mask's tiles in window-index order (= the old dx-outer, dy-inner order), ring 1 and 2 only
            for (uint64_t m = lmask & R12_MASK; m; m &= m - 1) {
                const int k = __builtin_ctzll(m);
                const vec2 q{p0.x + k / 7 - 3, p0.y + k % 7 - 3};
                const int c = cheb(q, p0);
                if (!walk_ok(q)) continue;
                if (c == 2 && !mid2(q)) continue;   // a free middle tile (a unit on it blocks the dash), or a hole or an altar
                const float v = land_value(q);
                if (v <= 0.f || cn >= 24) continue;
                cq[cn] = q; cv[cn] = v; ++cn;
            }
            for (int k = 0; k < HOR_LANDING_CANDIDATES && k < cn; ++k) {
                int bi = k;
                for (int j = k + 1; j < cn; ++j) if (cv[j] > cv[bi]) bi = j;
                std::swap(cq[k], cq[bi]); std::swap(cv[k], cv[bi]);
                plan_dash(cq[k]);
                land_done[wi(cq[k])] = true;
            }
        }
    }

    // A landing dash with a walk in the same turn (the dash does not use up the walk, rules Q13), both orders:
    //  - dash, land and hit on the dash tile, then step to the best of its 8 neighbours (out of reach of what is left);
    //  - step first, then dash 2 from there: the landing reaches enemies 4 away (3 from a dash alone).
    // The hit and the landing are the same for every step, so the step is picked by tile_score alone (the plain scorer's
    // position term) and one plan per dash tile is scored. Both kinds in one list, best landing value first, 2 x HOR_LANDING_CANDIDATES.
    void score_landing_walks() {
        if (ds >= 0 && land_dmg > 0.f && ME.can_move && mode != MM_FLEE && mode != MM_HOLD) {
            // Q is gated off on almost every landing turn (fuel), so these plans keep only the walks' room: the walks' 20k
            // gate plus their 8 plans (~9k)
            auto fuel_nb = [&]() { return (long)bc::fuel_remaining() > 30000; };
            auto mid_ok = [&](vec2 from, vec2 to) {   // a 2-tile dash from `from` to `to` has a free middle tile
                for (auto d : DIRS) {
                    const vec2 a = from + d;
                    if (cheb(a, to) == 1 && cheb(a, p0) <= 3 && dash_mid_ok(a) && (BLOCK[idx(a)] || walk_ok(a))) return true;
                }
                return false;
            };
            // dash, hit, walk
            vec2 bq[24]; float bv[24]; int bn = 0;
            for (uint64_t m = lmask & R12_MASK; m; m &= m - 1) {   // as the landing-priority scan
                const int k = __builtin_ctzll(m);
                const vec2 b{p0.x + k / 7 - 3, p0.y + k % 7 - 3};
                const int c = cheb(b, p0);
                if (!walk_ok(b) || (c == 2 && !mid2(b))) continue;
                const float v = land_value(b);
                if (v > 0.f && bn < 24) { bq[bn] = b; bv[bn] = v; ++bn; }
            }
            // walk, dash: landing tiles 3 away, each with one walk tile it is reached from
            vec2 wq[24], wb[24]; float wv[24]; int wn = 0;
            for (uint64_t m = lmask & R3_MASK; m; m &= m - 1) {   // ring-3 tiles of the mask, window-index order
                    const int k = __builtin_ctzll(m);
                    const vec2 b{p0.x + k / 7 - 3, p0.y + k % 7 - 3};
                    if (!walk_ok(b)) continue;
                    const float v = land_value(b);
                    if (v <= 0.f || wn >= 24) continue;
                    for (auto d : DIRS) {
                        const vec2 q = p0 + d;
                        if (cheb(q, b) == 2 && walk_ok(q) && mid_ok(q, b)) { wb[wn] = b; wq[wn] = q; wv[wn] = v; ++wn; break; }
                    }
            }
            // one list, best landing first: a dash-hit-walk and a walk-dash plan compete for the same fuel
            bool used_b[24] = {}, used_w[24] = {};
            for (int n = 0; n < 2 * HOR_LANDING_CANDIDATES && fuel_nb(); ++n) {
                int bi = -1, wi_ = -1; float bvv = 0.f;
                for (int i = 0; i < bn; ++i) if (!used_b[i] && bv[i] > bvv) { bvv = bv[i]; bi = i; wi_ = -1; }
                for (int i = 0; i < wn; ++i) if (!used_w[i] && wv[i] > bvv) { bvv = wv[i]; wi_ = i; bi = -1; }
                if (bi >= 0) {
                    used_b[bi] = true;
                    vec2 bs{-1, -1}; float bsv = -1e30f;
                    vec2 cl[8]; int cn2 = 0;
                    for (auto d : DIRS) { const vec2 c = bq[bi] + d; if (cheb(c, p0) <= 3 && walk_ok(c)) cl[cn2++] = c; }
                    tiles_simd(cl, cn2);   // one SIMD pass, then the same choice from the cache
                    for (int u = 0; u < cn2; ++u) {
                        const float sc = tile_score(cl[u]);
                        if (sc > bsv) { bsv = sc; bs = cl[u]; }
                    }
                    if (bs.x >= 0) plan_daw(bq[bi], bs);
                } else if (wi_ >= 0) {
                    used_w[wi_] = true;
                    plan_wd(wb[wi_], wq[wi_]);
                } else break;
            }
        }
    }

    // the same for the other dash champions (Vesper, Vendetta). In 8 logged games their dash
    // plans were never scored on 17-27% of battle turns with the dash ready and ran out partway on many more (Vendetta's
    // battle turns: 27% none, 42% cut), and 17-19% of their flee turns had no escape dash to choose. The best few dash
    // moves by a cheap read are scored here, before the walks: in a fight the value of the hit from the end tile (a dash,
    // or a dash and a walk for melee), in a retreat the distance gained from the nearest threat. The loop below still
    // lists these tiles (flee walks on from its best one) but does not score them twice.
    void score_priority_dashes() {
        if (ds >= 0 && land_dmg <= 0.f && (long)bc::fuel_remaining() > 20000) {
            auto th_d = [&](vec2 q) { int d = 99; for (int i = 0; i < TH_N; ++i) d = std::min(d, cheb(TH[i].p, q)); return d; };
            const int dh = th_d(p0);
            // 16 dash tiles plus 56 dash-and-walk routes to the outer ring. Do not discard the late directions.
            constexpr int dash_candidates = 72;
            vec2 cd[dash_candidates], cw[dash_candidates]; float cv[dash_candidates]; int cn = 0;
            auto add = [&](vec2 b, vec2 c, float v) { if (cn < dash_candidates) { cd[cn] = b; cw[cn] = c; cv[cn] = v; ++cn; } };
            for (int dx = -2; dx <= 2; ++dx)
                for (int dy = -2; dy <= 2; ++dy) {
                    const vec2 b{p0.x + dx, p0.y + dy};
                    if (cheb(b, p0) != 2 || !walk_ok(b)) continue;
                    bool mid = false;   // a free middle tile (a unit on it blocks the dash), or a hole or an altar
                    for (auto d : DIRS) {
                        const vec2 a = p0 + d;
                        if (cheb(a, b) == 1 && dash_mid_ok(a) && (BLOCK[idx(a)] || walk_ok(a))) { mid = true; break; }
                    }
                    if (!mid) continue;
                    if (mode == MM_FLEE) {
                        const int d = th_d(b);
                        if (d > dh) {
                            add(b, {-1, -1}, (float)d);
                            // The priority pass can consume the later 45k gate. Include the escape's walk here too.
                            if (ME.can_move) for (auto d3 : DIRS) {
                                const vec2 c = b + d3;
                                if (cheb(c, p0) == 3 && walk_ok(c)) add(b, c, (float)th_d(c));
                            }
                        }
                        continue;
                    }
                    const float v = basic_best(b);   // a ranged dash keeps the attack
                    if (v > 0.f) add(b, {-1, -1}, v);
                    if (ME.melee && ME.can_move)
                        for (auto d3 : DIRS) {
                            const vec2 c = b + d3;
                            if (cheb(c, p0) != 3 || !walk_ok(c)) continue;
                            const float w = basic_best(c);
                            if (w > 0.f) add(b, c, w);
                        }
                }
            int taken = 0;
            for (int k = 0; k < cn && taken < DASH_CANDIDATES && (long)bc::fuel_remaining() > 20000; ++k) {
                int bi = k;
                for (int j = k + 1; j < cn; ++j) if (cv[j] > cv[bi]) bi = j;
                std::swap(cd[k], cd[bi]); std::swap(cw[k], cw[bi]); std::swap(cv[k], cv[bi]);
                const vec2 end = cw[k].x >= 0 ? cw[k] : cd[k];
                if (dash_done[wi(end)]) continue;   // the same end tile by another middle step
                consider(cd[k], cw[k]);
                dash_done[wi(end)] = true;
                ++taken;
            }
        }
    }

    void score_walks() {
        if (ME.can_move && (long)bc::fuel_remaining() > 20000) {
            // the walk plans scored here instead of through consider(): the same terms in the same order as
            // consider() for a walk (no dash, order 0), the tile and hit values read from the caches the SIMD pass filled,
            // the same strict > against best in DIRS order (ties keep the earlier plan)
            const bool ca = ME.can_attack, ml = ME.melee, cc = ME.can_cast, hold = mode == MM_HOLD;
            const float b0 = ca && ml ? basic_best(p0) : 0.f;   // a melee champion's hit from the start tile
            bool any_sp = false;
            if (cc) for (int s = 0; s < 2; ++s) any_sp = any_sp || (SP_OK[s] && !ME.ab[s].excl);
            const bool rs_on = cc && rs >= 0 && ca;
            for (auto d : DIRS) {
                const vec2 q = p0 + d;
                if (!walk_ok(q) || (hold && !OWN_SPAWN[idx(q)])) continue;
                const int k = wi(q);
                float atk_v = 0;
                int atk = 0, cast = -1;
                bool cast_first = false;
                if (ca && ml) {
                    if (b0 > 0) { atk_v = b0; atk = 1; }
                    const float v = C_BAS_OK[k] ? C_BAS[k] : basic_best(q);
                    if (v > atk_v) { atk_v = v; atk = 3; }
                }
                float cast_v = 0;
                if (cc) {
                    if (any_sp)
                        for (int s = 0; s < 2; ++s) {
                            if (!SP_OK[s] || ME.ab[s].excl) continue;
                            float v = spell_cached(s, p0);
                            bool first = true;
                            const float v2 = spell_cached(s, q);
                            if (v2 > v) { v = v2; first = false; }
                            if (v > cast_v) { cast_v = v; cast = s; cast_first = first; }
                        }
                    if (rs_on) {
                        float rv = 0;
                        int code = atk;
                        if (atk) {
                            rv = 0.9f * atk_v;
                        }
                        else if (!ml) { rv = basic_best(q); code = 3; }
                        if (rv > cast_v) { cast_v = rv; cast = 2; atk = code; cast_first = false; }
                    }
                }
                float pos = C_INC_OK[k] ? C_INC[k] : tile_score(q);
                pos -= 1.f;
                float score = woff * (atk_v + cast_v) + pos;
                if (FIN_END.x >= 0 && q == FIN_END && atk_v + cast_v > 0) score += SOLO_KILL_BONUS;
                if (score > best.score) {
                    best = Plan{};
                    best.walk = q; best.end = q;
                    best.atk = atk; best.cast = cast; best.cast_first = cast_first;
                    best.score = score; best.val = atk_v + cast_v;
                }
            }
        }
    }

    bool fuel_ok() const { return (long)bc::fuel_remaining() > 45000; }

    void score_dash(vec2 b, float b0d) {
        float atk_v = 0;
        int atk = 0;
        if (ME.can_attack) {
            if (b0d > 0) { atk_v = b0d; atk = 1; }
            const float v = C_BAS[wi(b)];
            if (v > atk_v) { atk_v = v; atk = 3; }
        }
        const float cast_v = land_value(b);
        float pos = tile_score(b);
        pos -= 25.f + (land_dmg > 0.f ? wake_pen(b) : 0.f);
        pos -= 1.f;
        float score = woff * (atk_v + cast_v) + pos;
        if (FIN_END.x >= 0 && b == FIN_END && atk_v + cast_v > 0) score += SOLO_KILL_BONUS;
        if (score > best.score) {
            best = Plan{};
            best.dash = b; best.end = b;
            best.atk = atk; best.cast = 3; best.dash_damage = land_dmg;
            best.score = score; best.val = atk_v + cast_v;
        }
    }

    void score_dash_then_walk(const vec2* dt, int dn, int flee_best) {
        bool seen[49] = {};     // dash-and-walk end tiles (ring 3): each end tile once
        // dash then walk: melee reaching an attack, or a retreat going on from its best dash tile
        // melee, not fleeing: a ring-3 end tile hits someone only if an enemy with a basic value stands 2 to 4 from p0;
        // with none the loop below adds no plan (every candidate fails its basic_best filter), so it is skipped
        bool dw_none = false;
        if (ME.melee && mode != MM_FLEE) {
            dw_none = true;
            for (int i = 0; i < EN_N; ++i) { const int de = cheb(EP[i], p0); if (de >= 2 && de <= 4 && KV_B[i] > 0) { dw_none = false; break; } }
        }
        if (ME.can_move && (ME.melee || mode == MM_FLEE) && !dw_none)
            for (int i = 0; i < dn; ++i) {
                if (mode == MM_FLEE && i != flee_best) continue;
                for (auto d3 : DIRS) {
                    const vec2 c = dt[i] + d3;
                    if (cheb(c, p0) < 3 || !walk_ok(c) || seen[wi(c)] || dash_done[wi(c)]) continue;
                    if (mode != MM_FLEE && basic_best(c) <= 0) continue;
                    if (!fuel_ok()) break;
                    seen[wi(c)] = true;
                    consider(dt[i], c);
                }
            }
    }

    // Dash plans cost about 1k fuel per new end tile, so only promising ones are scored: in a fight the dash must
    // set up a hit, in a retreat it must gain distance (and only the best dash tile is walked on from).
    // Whatever the case, stop while the turn still has a wide margin.
    void score_dashes() {
        auto dist_th = [&](vec2 q) { int d = 99; for (int i = 0; i < TH_N; ++i) d = std::min(d, cheb(TH[i].p, q)); return d; };
        const int d_here = mode == MM_FLEE ? dist_th(p0) : 0;   // read only when fleeing
        vec2 dt[24];
        int dn = 0, flee_best = -1;
        float flee_bs = -1e30f;
        uint32_t mid = 0;
        for (int i = 0; i < 8; ++i) {   // a unit on an open middle tile blocks the dash
            const vec2 a = p0 + DIRS[i];
            if (dash_mid_ok(a) && (BLOCK[idx(a)] || walk_ok(a))) mid |= 1u << i;
        }
        const int8_t* ord = dash_ring_order(mid);
        vec2 cl[16]; int cn = 0;
        for (int j = 0; j < ord[0]; ++j) {
            const int r = ord[1 + j];
            if (!C_WALK[DASH_RK[r]] || land_done[DASH_RK[r]]) continue;
            cl[cn++] = p0 + DASH_RO[r];
        }
        basics_any(cl, cn);
        vec2 cd[16]; int cdn = 0;
        for (int j = 0; j < cn; ++j) {
            const vec2 b = cl[j];
            if (mode == MM_FLEE) { if (dist_th(b) <= d_here) continue; }
            else if (!ME.melee && C_BAS[wi(b)] <= 0) continue;    // ranged: dash, then attack
            cd[cdn++] = b;
        }
        tiles_simd(cd, cdn);
        const float b0d = ME.can_attack ? basic_best(p0) : 0.f;
        for (int j = 0; j < cdn; ++j) {
            const vec2 b = cd[j];
            if (!dash_done[wi(b)] && !fuel_ok()) break;
            dt[dn] = b;
            if (!dash_done[wi(b)] && (mode != MM_HOLD || OWN_SPAWN[idx(b)])) score_dash(b, b0d);   // the dash priority pass scored it already
            if (mode == MM_FLEE) { const float sc = tile_score(b); if (sc > flee_bs) { flee_bs = sc; flee_best = dn; } }
            ++dn;
        }
        score_dash_then_walk(dt, dn, flee_best);
    }

    // trust region: the legacy plan stands unless the model rates its own plan clearly higher (both scored on this turn)
    void apply_model() {
        if (mode != MM_HOLD && mode != MM_FLEE) {
            // fuel gate: the model's fixed stages run unguarded, so start it only when the fuel left pays for its predicted
            // cost (about 26k + 2.5k per enemy) and the rest of the turn; otherwise the legacy plan stands
            const long Fq = (long)bc::fuel_remaining();
            const long need = QMODEL_TILE_FUEL_GATE + 2500L * std::max(0, EN_N - 1);
            Plan qp;
            if (Fq < need) { QM_NOSH = false; }
            else if (qm::decide(mode, qp) && qm::trust_gain(best) >= QMODEL_TRUST_GAIN) { best = qp; }
            else QM_NOSH = false;
        }
    }

    void run() {
        if (ds >= 0) { try { land_dmg = bc::self().abilities()[ds].damage_amount(); } catch (const std::exception&) {} }
        set_weights();
        QM_NOSH = false;
        score_walks_simd();
        score_stand();
        score_kill_plan();
        read_landing_mask();
        score_landings();
        score_landing_walks();
        score_priority_dashes();
        score_walks();
        if (ds >= 0 && fuel_ok()) score_dashes();
        apply_model();
    }
};

// One turn of fighting. Returns true if we damaged an enemy champion.
static bool micro_fight(MicroMode mode, Champ* focus) {
    remember_fight_or_flee(mode, focus);
    const vec2 p0 = ME.p;
    watch_standoff();
    MFOCUS = focus;
    shield_ahead();
    MP0 = p0;
    M_PULL = champ(ME.type).tank ? TANK_PULL : 18.f;   // a tank goes in first
    if (M_PULL_NEXT > 0.f) { M_PULL = M_PULL_NEXT; M_PULL_NEXT = 0.f; }
    M_MODE = mode;
    read_walk_window(p0);
    plan_way_to_focus(mode, focus, p0);
    read_mates_and_hazards(p0);
    read_my_spells();
    read_enemies(p0);
    camp_fixed();   // again: this round's enemy priorities are read above
    read_spell_values(p0);
    PlanSearch search{mode, p0};
    search.run();
    if (FIN_END.x >= 0) { FIN_END = {-1, -1}; FIN_DASH = {-1, -1}; }
    return micro_exec(search.best);
}

// The team's focus: the enemy we can kill soonest (health over type priority), weighed by distance, carried gems
// and channels. Every ally sees roughly the same picture, so the team converges on one target.
static Champ* pick_focus(int reach) {
    Champ* focus = nullptr;
    float fk = 1e18f;
    for (auto& e : ENEMY) {
        if (!targetable(e)) continue;
        const int d = cheb(e.p, ME.p);
        if (d > reach + 3) continue;
        if (in_es_zone(e.p) && d > reach) continue;   // no chase near its own spawn
        // an enemy that just lost health (an ally hit it this round) stays the focus
        float key = e.hp / type_mult(e.type) + d * 40.f - e.gems * 4.f - (e.channel ? 300.f : 0.f) - (e.hurt ? FOCUS_HURT_BONUS : 0.f);
        if (key < fk) { fk = key; focus = &e; }
    }
    return focus;
}
