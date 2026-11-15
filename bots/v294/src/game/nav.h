// movement along cached distance fields, with dashes, sidesteps and a dynamic fallback.
#pragma once
#include "world.h"

static vec2 prev_pos{-1, -1};
static int stuck_turns = 0;
static const void* pl_f = nullptr;   // the field we are walking, its value last turn, turns since it dropped
static int pl_last = 1 << 30, pl_n = 0;
static bool SF_BLOCKED = false;   // UNJAM_BLOCKED_TURNS: step_field wanted a step this turn and none was possible
static bool SG_ON = false;          // think(): this turn's walk is a plain one (job, bank, home) with enemies in view
static float SG_HERE = 0.f, SG_LIM = 0.f;
static int SG_SKIP = 0;             // step tiles skipped this turn
static float step_burst(vec2 q);    // brain.h: the lethal burst on tile q
static bool sg_bad(vec2 q) {
    if (!SG_ON) return false;
    const float b = step_burst(q);
    if (b >= SG_LIM && b > SG_HERE) {
        ++SG_SKIP;
        return true;
    }
    return false;
}
static int AVOID_GROUP = -1;   // the group we are working on: its monsters are not "danger"

// Extra cost of standing on q at the end of our move: awake monsters of other groups and
// visible enemies that out-power us make a tile unattractive.
// reach = the monster's own attack range (3 before); w = danger weight for danger_cost; str = its Strength (
// the camp-aggro terms read it; they used (w - 2) x 20, Strength rounded down to 20s, 0 below 20)
struct Hazard { vec2 p; int w; int group; int reach; float str; };
static std::vector<Hazard> HAZ;   // awake monsters, rebuilt each turn
static void build_hazards() {
    HAZ.clear();
    const int r = rnd();
    for (const auto& g : G) {
        if (r - g.awake_at > 2 || g.str_total <= 0) continue;
        for (int k = 0; k < g.n; ++k)
            if (g.m[k].hp > 0 && g.m[k].str > 0) HAZ.push_back({g.m[k].p, 2 + (int)(g.m[k].str / 20), g.id, g.m[k].reach, g.m[k].str});
    }
}
static int danger_cost(vec2 q) {
    int c = 0;
    for (const auto& h : HAZ) if (h.group != AVOID_GROUP && cheb(h.p, q) <= h.reach) c += h.w;
    return c;
}

static bool try_move_to(vec2 q) {
    auto& me = bc::self();
    bool ok = false;
    try { ok = me.move_absolute(q); } catch (const std::exception&) { ok = false; }
    if (!ok) { OCC.push_back(q); return false; }
    prev_pos = ME.p;
    ME.p = q;
    ME.walked = true;
    ME.can_move = false;
    if (!ME.melee) ME.can_attack = false;
    return true;
}

static int dash_slot() {
    for (int s = 0; s < 2; ++s) if (ME.ab[s].dash && ready(s)) return s;
    return -1;
}

// walking greedily down f from here (open tiles only), is a bridge hole crossed within `steps`?
static bool hd_bridge_ahead(const Field& f, int goal_d, int steps) {
    vec2 p = ME.p;
    int c = f.at(p);
    for (int k = 0; k < steps && c > goal_d; ++k) {
        vec2 nb{-1, -1};
        int nv = c;
        for (auto d : DIRS) {
            const vec2 q = p + d;
            if (!inb(q) || BLOCK[idx(q)]) continue;
            const int v = f.at(q);
            if (v < nv) { nv = v; nb = q; }
        }
        if (nb.x < 0) return true;
        p = nb;
        c = nv;
    }
    return false;
}

// Dash up to two tiles along the field (does not use the walk). Returns true if dashed.
static bool dash_along(const Field& f, int goal_d, bool holes_only = false) {
    const int s = dash_slot();
    if (s < 0) return false;
    const int cur = f.at(ME.p);
    if (cur == UNREACH) return false;
    // near the goal (or when the caller walks only) just a hole crossing is worth the dash (no way round)
    const bool near = cur - goal_d < 3 || holes_only;
    if (near && !HD_ON) return false;
    vec2 best{-1, -1}, bmid{-1, -1};
    int bd = cur;
    for (auto d1 : DIRS) {
        const vec2 a = ME.p + d1;
        if (!dash_mid_ok(a)) continue;          // holes and altars may be passed over
        if (near && !BLOCK[idx(a)]) continue;
        for (auto d2 : DIRS) {
            const vec2 b = a + d2;
            if (!walkable(b) || b == ME.p) continue;
            const int v = f.at(b);
            // on a tie a hole crossing beats an open dash
            const bool better = v < bd - 1 || (best.x >= 0 && v == bd && BLOCK[idx(a)] && !BLOCK[idx(bmid)]);
            if (better && v >= goal_d && !sg_bad(b)) { bd = v; best = b; bmid = a; }
        }
    }
    if (best.x < 0) return false;
    // an open dash gains 2 steps; keep it when a bridge on the way would then wait out the cooldown
    if (HD_ON && HD_N > 0 && !BLOCK[idx(bmid)] && hd_bridge_ahead(f, goal_d, 10)) { return false; }
    bool ok = false;
    try { ok = bc::self().abilities()[s].use(best); } catch (const std::exception&) { ok = false; }
    if (!ok) return false;
    prev_pos = ME.p;
    ME.p = best;
    ME.cd[s] = 8;
    ME.can_cast = false;
    return true;
}

// the walk cannot get nearer to `target` (holes in the way): dash over a hole or altar to a tile from
// which target is nearer to being in `reach`
static bool hole_dash_to(vec2 target, int reach) {
    if (!HD_ON || HD_N == 0 || !ME.can_cast) return false;
    const int s = dash_slot();
    if (s < 0) return false;
    const int cur = std::max(0, cheb(ME.p, target) - reach);
    if (cur == 0) return false;
    vec2 best{-1, -1};
    int bd = cur;
    for (auto d1 : DIRS) {
        const vec2 a = ME.p + d1;
        if (!inb(a) || !BLOCK[idx(a)] || !HD_PASS[idx(a)]) continue;
        for (auto d2 : DIRS) {
            const vec2 b = a + d2;
            if (!walkable(b) || b == ME.p) continue;
            const int v = std::max(0, cheb(b, target) - reach);
            if (v < bd && !sg_bad(b)) { bd = v; best = b; }
        }
    }
    if (best.x < 0) return false;
    bool ok = false;
    try { ok = bc::self().abilities()[s].use(best); } catch (const std::exception&) { ok = false; }
    if (!ok) return false;
    prev_pos = ME.p;
    ME.p = best;
    ME.cd[s] = 8;
    ME.can_cast = false;
    return true;
}

#include "../brain/lfield.h"   // the fields are built in main.cpp (lf::work); HAS_TP lives there
// One walking step that lowers f, avoiding occupied and dangerous tiles. Stops once f <= goal_d.
static bool step_field(const Field& f, int goal_d, bool allow_dash = true) {
    if (!f.ok()) return false;
    int cur = f.at(ME.p);
    if (cur == UNREACH) return false;
    if (cur <= goal_d) return false;
    // a landing dash (Heart of Ruin) is a camp hit; near the goal it is kept for the landing
    // (camp_combat.h), far away a travel dash is back off cooldown (8) before the walk ends
    // NCP: only on a camp group's field (GF). On the altar or home walk it also skipped lf::step's crystal hop,
    // the only way into an altar pocket ringed by camps
    const bool camp_walk = !GF.empty() && &f >= GF.data() && &f < GF.data() + GF.size();
    if (allow_dash && camp_walk) for (int s = 0; s < 2; ++s) if (ME.ab[s].land > 0 && cur - goal_d < 10) allow_dash = false;
    if (allow_dash && lf::step(f, goal_d)) return true;
    if (allow_dash && ME.can_cast) { if (dash_along(f, goal_d)) cur = f.at(ME.p); }
    else if (HD_ON && HD_N > 0 && ME.can_cast) {
        // a walk-only caller whose way down crosses a hole (no open tile lower than here) dashes over it
        bool down = false;
        for (auto d : DIRS) { const vec2 q = ME.p + d; if (inb(q) && !BLOCK[idx(q)] && f.at(q) < cur) { down = true; break; } }
        if (!down && dash_along(f, goal_d, true)) { cur = f.at(ME.p); }
    }
    if (!ME.can_move || cur <= goal_d) return ME.walked;
    // the way down is a bridge but the dash is cooling: walk round when that is no slower than waiting
    if (HD_ON && HD_N > 0 && f.roots) {
        int ds = -1;
        for (int k = 0; k < 2; ++k) if (ME.ab[k].dash) ds = k;
        if (ds >= 0 && !ready(ds)) {
            bool down = false;
            for (auto d : DIRS) { const vec2 q = ME.p + d; if (inb(q) && !BLOCK[idx(q)] && f.at(q) < cur) { down = true; break; } }
            if (!down) {
                const int rem = std::max(1, (int)ME.cd[ds]);
                bc::distance_field df;
                bc::bitboard visited;
                df.compute(STATIC_BB, *f.roots, visited, (uint16_t)std::min<int>(UNREACH - 1, cur + rem));
                const int wd = visited.is_set(ME.p) ? (int)df.get(ME.p) : UNREACH;
                if (wd != UNREACH && wd <= cur + rem) {
                    vec2 bq{-1, -1};
                    int bv = wd;
                    for (auto d : DIRS) {
                        const vec2 q = ME.p + d;
                        if (!walkable(q) || !visited.is_set(q)) continue;
                        const int v = df.get(q);
                        if (v < bv) { bv = v; bq = q; }
                    }
                    if (bq.x >= 0 && try_move_to(bq)) { stuck_turns = 0; return true; }
                }
            }
        }
    }
    // a sidestep is not progress. With no free downhill tile for 2 turns, path around the units first.
    if (pl_f != (const void*)&f || cur < pl_last) { pl_f = &f; pl_n = 0; }
    else ++pl_n;
    pl_last = cur;
    bool down_free = false, down_any = false;
    for (auto d : DIRS) {
        const vec2 q = ME.p + d;
        if (!inb(q) || BLOCK[idx(q)]) continue;
        const int v = f.at(q);
        if (v == UNREACH || v >= cur) continue;
        down_any = true;
        if (!occupied(q)) down_free = true;   // a tile skipped for danger counts as free: danger keeps priority
    }
    if (down_any && !down_free && pl_n >= 2 && f.roots && (long)bc::fuel_remaining() > std::max(30000L, 6L * N + 8000L)) {
        bc::bitboard walls = STATIC_BB;
        for (auto q : OCC) if (inb(q)) walls.set(q, true);
        walls.set(ME.p, false);
        bc::bitboard roots = *f.roots & ~walls;
        if (!roots.is_empty()) {
            bc::bitboard dest;
            dest.set(ME.p, true);
            bc::distance_field df;
            bc::bitboard visited;
            df.compute_to(walls, roots, dest, visited);
            const auto mv = df.next_move_from(ME.p);
            if (mv) {
                const vec2 q = ME.p + *mv;
                if (walkable(q) && try_move_to(q)) { return true; }
            }
        }
    }
    for (int attempt = 0; attempt < 3; ++attempt) {
        vec2 best{-1, -1};
        int bs = 1 << 30;
        for (auto d : DIRS) {
            const vec2 q = ME.p + d;
            if (!walkable(q)) continue;
            const int v = f.at(q);
            if (v == UNREACH) continue;
            if (v <= cur && sg_bad(q)) continue;
            int s;
            if (v < cur) s = v * 16 + danger_cost(q) * 4;
            else if (v == cur && q != prev_pos) s = (v + 2) * 16 + danger_cost(q) * 4 + 8;   // sidestep
            else continue;
            if (s < bs) { bs = s; best = q; }
        }
        if (best.x < 0) break;
        if (try_move_to(best)) { stuck_turns = 0; return true; }
    }
    if (SG_ON && SG_SKIP > 0) { return false; }   // hold: no detour, not blocked
    // Dynamic fallback: path around visible units.
    // (the detour is a whole-map field, ~6 fuel per tile: 57k on 100x100)
    if (++stuck_turns >= 2 && f.roots && (long)bc::fuel_remaining() > std::max(30000L, 6L * N + 8000L)) {
        bc::bitboard walls = STATIC_BB;
        for (auto q : OCC) if (inb(q)) walls.set(q, true);
        walls.set(ME.p, false);
        bc::bitboard roots = *f.roots & ~walls;
        if (roots.is_empty()) return false;
        bc::bitboard dest;
        dest.set(ME.p, true);
        bc::distance_field df;
        bc::bitboard visited;
        df.compute_to(walls, roots, dest, visited);
        const auto mv = df.next_move_from(ME.p);
        if (mv) {
            const vec2 q = ME.p + *mv;
            if (walkable(q) && try_move_to(q)) { return true; }
        }
    }
    SF_BLOCKED = true;
    return false;
}

// No enemy within this distance before a recall channel starts (safe_to_channel).
static constexpr int CHANNEL_SAFE_D = 6;
// Step to the neighbour that is furthest from the given threats, preferring our spawn.
// the distance counts up to one past the recall-safe distance. Capped at 6, a low champion with an enemy
// at 6 between it and home stood still for 17 rounds: no tile scored better and the recall never became safe.
static bool step_away(const std::vector<vec2>& threats) {
    if (!ME.can_move) return false;
    int best_s = -(1 << 30);
    vec2 best{-1, -1};
    auto score = [&](vec2 q) {
        int dmin = 99;
        for (auto t : threats) dmin = std::min(dmin, cheb(t, q));
        const int h = F_HOME.at(q);
        return std::min(dmin, CHANNEL_SAFE_D + 1) * 100 - (h == UNREACH ? 500 : h) * (8) - danger_cost(q) * 10;
    };
    const int here = score(ME.p);
    for (auto d : DIRS) {
        const vec2 q = ME.p + d;
        if (!walkable(q)) continue;
        const int s = score(q);
        if (s > best_s) { best_s = s; best = q; }
    }
    // a dash gets further first
    const int ds = dash_slot();
    bool dashed = false;
    if (ds >= 0 && ME.can_cast) {
        vec2 b2{-1, -1};
        // The dash is an independent retreat action.  When no adjacent tile
        // improves the score, compare it with the current tile instead of
        // rejecting it before the dash search starts.
        int bs2 = std::max(best_s, here);
        for (auto d1 : DIRS) for (auto d2 : DIRS) {
            const vec2 a = ME.p + d1, b = a + d2;
            if (!dash_mid_ok(a) || !walkable(b) || b == ME.p) continue;
            const int s = score(b);
            if (s > bs2 + 50) { bs2 = s; b2 = b; }
        }
        if (b2.x >= 0) {
            bool ok = false;
            try { ok = bc::self().abilities()[ds].use(b2); } catch (const std::exception&) { ok = false; }
            if (ok) {
                prev_pos = ME.p;
                ME.p = b2;
                ME.cd[ds] = 8;
                ME.can_cast = false;
                dashed = true;
            }
        }
    }
    best_s = -(1 << 30); best = {-1, -1};
    for (auto d : DIRS) {
        const vec2 q = ME.p + d;
        if (!walkable(q)) continue;
        const int s = score(q);
        if (s > best_s) { best_s = s; best = q; }
    }
    // A successful dash is already a successful retreat. Only spend the walk
    // when it improves the landed tile's score; a pocket with no legal walk
    // must still report the dash to its caller.
    const int landed = dashed ? score(ME.p) : here;
    if (best.x >= 0 && best_s > landed) return try_move_to(best) || dashed;
    return dashed;
}

// Walk directly towards a tile (short range, no field): greedy Chebyshev with obstacle check.
static bool step_greedy(vec2 target) {
    if (!ME.can_move || ME.p == target) return false;
    vec2 best{-1, -1};
    auto score = [&](vec2 q) {
        // Compare the current tile and candidates with the same metric. The
        // previous current score omitted the Manhattan tie-break, so a
        // diagonal move from (0,0) toward (10,10) could score 162 versus the
        // current 160 and be rejected despite reducing distance.
        return cheb(q, target) * 16 + danger_cost(q) * 4 +
               std::abs(q.x - target.x) + std::abs(q.y - target.y);
    };
    int bd = score(ME.p);
    for (auto d : DIRS) {
        const vec2 q = ME.p + d;
        if (!walkable(q)) continue;
        const int s = cheb(q, target) * 16 + danger_cost(q) * 4 + (std::abs(q.x - target.x) + std::abs(q.y - target.y));
        if (s < bd) { bd = s; best = q; }
    }
    if (best.x < 0) return hole_dash_to(target, 1);
    return try_move_to(best);
}
