// Channel safety, carrier policy, escape and teleport routing.
#pragma once
static bool on_own_spawn() { return inb(ME.p) && OWN_SPAWN[idx(ME.p)]; }

// a live monster reaches `at`: as far as its own attack range (elk and golem 4, monolith 5), not a fixed 3; `margin`
// counts tiles beyond that reach
ALWAYS_INLINE bool monster_hits(const Mon& m, vec2 at, int margin = 0) { return m.hp > 0 && m.str > 0 && cheb(m.p, at) <= m.reach + margin; }

static int awake_threat_near(vec2 at, int margin) {
    const int r = rnd();
    int s = 0;
    for (const auto& g : G) {
        if (r - g.awake_at > 2 || cheb(g.center, at) > margin + 9) continue;
        for (int k = 0; k < g.n; ++k) if (monster_hits(g.m[k], at, margin)) s += (int)g.m[k].str;
    }
    return s;
}

// Monsters that could be hitting us: awake ones, and any in reach while we are taking damage. Only damaged
// ones counted before; three full-health wolves woken out of our sight hit a low Ryth 4 rounds while it stood still.
// only while no monster known to be awake reaches us. A Marcella hit by three awake elks counted a sleeping
// monolith as a hitter too, so the short way out (3 steps) looked unsafe and the escape led 9 steps past the elks.
static bool known_hitter() {
    static int kr = -1; static bool kh = false;
    if (kr == rnd()) return kh;
    kr = rnd(); kh = false;
    for (const auto& g : G) {
        if (g.str_total <= 0 || rnd() - g.awake_at > 2 || cheb(g.center, ME.p) > 9) continue;
        for (int k = 0; k < g.n && !kh; ++k) if (monster_hits(g.m[k], ME.p)) kh = true;
    }
    return kh;
}
static bool maybe_awake(const Group& g) {
    if (g.str_total <= 0) return false;
    if (rnd() - g.awake_at <= 2) return true;
    return ME.hurt && (!known_hitter());
}
static void hitters_near(std::vector<vec2>& out, int margin) {
    for (const auto& g : G) {
        if (cheb(g.center, ME.p) > margin + 9 || !maybe_awake(g)) continue;
        for (int k = 0; k < g.n; ++k)
            if (monster_hits(g.m[k], ME.p, margin)) out.push_back(g.m[k].p);
    }
}

// the monsters hitters_near(_, 0) counts: `tot` their Strength, `open` the part no ally covers. An
// ally covers a monster when it is as near to it as we are and keeps health above its own heal line after two rounds of
// `tot` (it takes the hits we hand it)
static void camp_cover(float heal_frac, float& tot, float& open) {
    tot = open = 0.f;
    const Mon* hs[24]; int n = 0;
    for (const auto& g : G) {
        if (cheb(g.center, ME.p) > 9 || !maybe_awake(g)) continue;
        for (int k = 0; k < g.n && n < 24; ++k)
            if (monster_hits(g.m[k], ME.p)) { hs[n++] = &g.m[k]; tot += g.m[k].str; }
    }
    for (int i = 0; i < n; ++i) {
        bool cov = false;
        for (auto& a : ALLY)
            if (&a != &ALLY[MY_SLOT] && a.vis && a.alive && !a.channel && a.hp - 2.f * tot > a.mhp * heal_frac &&
                cheb(a.p, hs[i]->p) <= cheb(ME.p, hs[i]->p)) { cov = true; break; }
        if (!cov) open += hs[i]->str;
    }
}

// the walk out of every awake monster's reach. The greedy step_away maximised the distance to the nearest
// hitter one step at a time, so a champion in a pocket whose every exit passes the camp stood still until it died
// (Vesper 9 rounds at distance 2 from an elk). A search from the tiles no awake monster reaches (within 8) back to us
// gives the first step out and how many more rounds of camp damage we take on the way.
static int ESC_R = -1, ESC_N = 0;
static vec2 ESC_STEP{-1, -1};
static void escape_plan() {
    if (ESC_R == rnd()) return;
    ESC_R = rnd(); ESC_N = 0; ESC_STEP = {-1, -1};
    bc::bitboard walls = STATIC_BB, reach, roots;
    // a group counted as a hitter stays in the mask 3 more rounds. The "maybe awake" test flips from turn
    // to turn (a known hitter or not), so the way out flipped too: a low Varg stepped (28,1) -> (29,2) -> (28,1) between
    // a golem and two wolves and died (reverse_spiral, LH-ESC6/7/6)
    static std::vector<int> esc_seen;
    if (esc_seen.size() != G.size()) esc_seen.assign(G.size(), -100);
    for (const auto& g : G) {
        if (cheb(g.center, ME.p) > 17) continue;
        bool awake = maybe_awake(g);
        if (awake) esc_seen[g.id] = rnd(); else awake = g.str_total > 0 && rnd() - esc_seen[g.id] <= 3;
        for (int k = 0; k < g.n; ++k) if (g.m[k].hp > 0) {
            walls.set(g.m[k].p, true);
            if (awake && g.m[k].str > 0) reach.draw_chebyshev(g.m[k].p, (float)g.m[k].reach);
        }
    }
    if (!reach.is_set(ME.p)) return;
    for (auto q : OCC) if (inb(q)) walls.set(q, true);
    walls.set(ME.p, false);
    roots.draw_chebyshev(ME.p, 8.f);
    roots = roots & ~reach & ~walls;
    if (roots.is_empty()) { ESC_N = 3; return; }
    bc::bitboard dest, visited;
    dest.set(ME.p, true);
    bc::distance_field df;
    df.compute_to(walls, roots, dest, visited, 16);
    const int v = df.get(ME.p);
    if (v <= 0 || v > 16) { ESC_N = 3; return; }
    ESC_N = v;
    if (const auto mv = df.next_move_from(ME.p)) ESC_STEP = ME.p + *mv;
}
// rounds of camp damage still to take before we stand out of reach (0 when nothing awake reaches us)
static int escape_rounds() { escape_plan(); return ESC_N; }
static bool escape_step() {
    escape_plan();
    return ME.can_move && ESC_STEP.x >= 0 && walkable(ESC_STEP) && try_move_to(ESC_STEP);
}
static bool safe_to_channel() {
    // a recall is interrupted by any damage: nothing hitting us, no enemy close
    if (awake_threat_near(ME.p, 0) > 0) return false;
    if (ME.drop > 0.5f) return false;
    std::vector<vec2> h;
    hitters_near(h, 0);
    if (!h.empty()) return false;
    for (auto& e : ENEMY) if (e.vis && e.alive && cheb(e.p, ME.p) <= CHANNEL_SAFE_D) return false;
    return true;
}

// the next home step (lowest F_HOME neighbour) lies in reach of an awake monster
static bool home_step_in_reach() {
    vec2 best{-1, -1}; int bv = F_HOME.at(ME.p);
    for (auto d : DIRS) { const vec2 q = ME.p + d; if (walkable(q) && F_HOME.at(q) < bv) { bv = F_HOME.at(q); best = q; } }
    if (best.x < 0) return false;
    for (const auto& g : G) {
        if (cheb(g.center, best) > 9 || !maybe_awake(g)) continue;
        for (int k = 0; k < g.n; ++k) if (monster_hits(g.m[k], best)) return true;
    }
    return false;
}
static bool start_recall() {
    try { bc::self().recall(); } catch (const std::exception&) { return false; }
    return true;
}

// a crystal use that failed keeps the crystal (rules: forbidden or occupied landing, out of range): re-read it
static void tp_resync() {
    try { HAS_TP = bc::self().consumables()[bc::consumable_type::tp_crystal].count() > 0; } catch (const std::exception&) {}
}
// the 3-turn crystal channel cannot be cancelled (only a stun or death stops it): nothing close may hit us during it
static bool tp_safe() {
    if (ME.hp < ME.mhp * 0.35f || ME.cast_excl) return false;
    for (auto& e : ENEMY) if (e.vis && e.alive && cheb(e.p, ME.p) <= 7) return false;
    // the 3-turn channel cannot be cancelled: nothing awake or hitting us close by (as safe_to_channel),
    // and no enemy remembered near here in the last 3 rounds (a bush hides it)
    if (awake_threat_near(ME.p, 0) > 0) return false;
    std::vector<vec2> h;
    hitters_near(h, 0);
    if (!h.empty()) return false;
    const int r = rnd();
    for (auto& e : ENEMY) if (e.alive && e.last_p.x >= 0 && r - e.seen_at <= 3 && cheb(e.last_p, ME.p) <= 7) return false;
    return true;
}

#include "banking.h"
