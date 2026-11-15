// Camp approach and combat orchestration.
#pragma once
static bool squad_gathers(Group& g, const std::vector<Mon*>& M, int dmin, const Field* f);   // brain/squad.h
static void blind_approach(Group& g, const Field& f) {
    const int r = rnd();
    if (blind_g != g.id || r - blind_last > 2) { blind_g = g.id; blind_n = 0; }
    blind_last = r;
    bool moved = false;
    if (f.at(ME.p) > 1) moved = step_field(f, 1);
    else if (ME.can_move) {
        vec2 best{-1, -1};
        int bs = 1 << 30;
        for (auto d : DIRS) {
            const vec2 q = ME.p + d;
            if (!walkable(q)) continue;
            int md = 99;
            for (int k = 0; k < g.n; ++k) md = std::min(md, cheb(q, g.m[k].p));
            if (md > 1) continue;
            int s = (BUSH[idx(q)] ? 0 : 10) + (q == prev_pos ? 5 : 0);
            if (s < bs) { bs = s; best = q; }
        }
        if (best.x >= 0) moved = try_move_to(best);
    }
    if (!moved || f.at(ME.p) <= 1) ++blind_n;
    if (blind_n > BLIND_GIVE_UP_TURNS) {
        g.skip_until = r + 40; blind_n = 0; KNOW_CHANGED = true;
    }
}

// One step towards the live members. The group field is rooted at every spawn tile, dead members' too, so on a long
// strip a Vesper 3 steps from a dead tile read as "in reach" and stood 146 rounds, the nearest live one 8 away. With a
// reach the search ends on the tiles from which we can hit a live member (a ranged champion is led to range 3, not next
// to the camp), else on the members' own tiles.
static bool step_to_live(const std::vector<Mon*>& M, int reach = 0) {
    if (M.empty() || !ME.can_move) return false;
    bc::bitboard walls = STATIC_BB, roots, dest, visited;
    for (auto q : OCC) if (inb(q)) walls.set(q, true);
    if (reach > 0) {
        for (auto* m : M) roots.draw_chebyshev(m->p, (float)reach);
        roots = roots & ~walls;
        if (roots.is_set(ME.p)) return false;
    } else for (auto* m : M) { walls.set(m->p, false); roots.set(m->p, true); }
    walls.set(ME.p, false);
    dest.set(ME.p, true);
    bc::distance_field df;
    df.compute_to(walls, roots, dest, visited);
    const auto mv = df.next_move_from(ME.p);
    if (!mv) return false;
    const vec2 q = ME.p + *mv;
    return walkable(q) && try_move_to(q);
}
// the group field when it still leads somewhere, else the live members (the field says "here" by a dead member's tile)
static void approach_step(const Field* f, const std::vector<Mon*>& M, int reach, bool dash) {
    if (f && f->at(ME.p) > reach) step_field(*f, reach, dash);
    else step_to_live(M, reach);
}
// every living member of g (all of them while it respawns) can be hit at `reach` from one of our
// spawn tiles, and the hitters together cannot kill a body of `mhp` in one round (each hits its nearest champion; standing
// on our spawn at turn end heals to full, rules 06). 03homonoliths: three Monoliths (200 Strength, reach 5) behind the spawn.
// monsters do not move (rules: "do not chase"; Mon::p is the spawn position), so whether any member of g stands
// within a ranged reach (3) of a spawn tile is fixed for the game. With none, spawn_siege_ok is false for every reach:
// decided once per group. The job score asked for every group on every turn, ~0.7-1.7k fuel a turn on 02arena, where
// no group qualifies and the job scoring's fuel cut (jobcut) then dropped jobs (HoR's round-23 job).
static bool spawn_siege_near(const Group& g) {
    static std::vector<int8_t> near;
    if (g.id < 0) return true;
    if ((size_t)g.id >= near.size()) near.resize(g.id + 1, -1);
    if (near[g.id] < 0) {
        bool any = false;
        for (int k = 0; k < g.n && !any; ++k) for (auto q : own_spawn) if (cheb(q, g.m[k].p) <= 3) { any = true; break; }
        near[g.id] = any ? 1 : 0;
    }
    return near[g.id] == 1;
}
static bool spawn_siege_ok(const Group& g, int reach, float mhp) {
    if (own_spawn.empty() || g.st == ST_REWARD) return false;
    if (!spawn_siege_near(g)) return false;
    // This runs for every scored job. Reject camps outside the spawn's expanded bounds before scanning its tiles.
    // The bounds only reject impossible hits; the exact tile check below still handles irregular spawn shapes.
    static const auto bounds = [] {
        std::array<int, 4> b{1 << 20, 1 << 20, -1, -1};
        for (auto q : own_spawn) {
            b[0] = std::min(b[0], q.x); b[1] = std::min(b[1], q.y);
            b[2] = std::max(b[2], q.x); b[3] = std::max(b[3], q.y);
        }
        return b;
    }();
    float sum = 0.f; bool any = false;
    for (int k = 0; k < g.n; ++k) {
        const Mon& m = g.m[k];
        if (g.st == ST_ALIVE && m.hp <= 0) continue;
        if (m.p.x < bounds[0] - reach || m.p.y < bounds[1] - reach ||
            m.p.x > bounds[2] + reach || m.p.y > bounds[3] + reach) return false;
        bool ok = false;
        for (auto q : own_spawn) if (cheb(q, m.p) <= reach) { ok = true; break; }
        if (!ok) return false;
        sum += std::max(0.f, m.str); any = true;
    }
    return any && sum < 0.8f * mhp;
}
// wait just out of the living members' reach (live attack ranges: a fixed range-three gate left gatherers inside elk reach)
static void stand_off(const Group& g, const std::vector<Mon*>& M, int dmin, const Field* f) {
    int wnear = 0;
    for (auto* m : M) if (m->str > 0) wnear = std::max(wnear, m->reach);
    const int wfar = wnear + 2;
    if (dmin <= wnear) {
        std::vector<vec2> th;
        for (auto* m : M) th.push_back(m->p);
        step_away(th);
    } else if (dmin > wfar) {
        if (f) step_field(*f, wfar - 1); else step_greedy(g.center);
    }
}
static bool fight_group(Group& g) {
    std::vector<Mon*> M;
    for (int k = 0; k < g.n; ++k) if (g.m[k].vis && g.m[k].hp > 0) M.push_back(&g.m[k]);
    AVOID_GROUP = g.id;
    const Field* f = gfield(g);
    if (M.empty()) {
        // not in view yet: walk there (a besieged group whose last members we cannot see from 6: go and look)
        const bool look = g.id == DOM_G && gdist(g, ME.p) <= 6;
        // alive, yet nothing of it in view from close by: a member hides in a bush; walk next to it
        if (gdist(g, ME.p) <= 6 && rnd() - g.state_at <= 1) {
            const Mon* hid = nullptr;
            for (int k = 0; k < g.n; ++k)
                if (g.m[k].unseen && (!hid || cheb(g.m[k].p, ME.p) < cheb(hid->p, ME.p))) hid = &g.m[k];
            // walk there by the group field. The straight-line step went to a wall between us and the group,
            // out of the 6 steps LOOK needs, and the field step came back (Marcella, 70 rounds on the_farm)
            if (hid && cheb(hid->p, ME.p) > 1) {
                // the straight step to the hidden member, unless it walks further from the group by the field
                // (then the field step). Walking by the field alone (1) lost 6/180 on the gather maps: it led to the
                // nearest member tile, not the hidden one
                if (f) {
                    vec2 best{-1, -1}; int bd = cheb(ME.p, hid->p);
                    for (auto d : DIRS) {
                        const vec2 q = ME.p + d;
                        if (walkable(q) && cheb(q, hid->p) < bd && f->at(q) <= f->at(ME.p)) { bd = cheb(q, hid->p); best = q; }
                    }
                    if (best.x >= 0) try_move_to(best); else step_field(*f, 1);
                } else step_greedy(hid->p);
                return true;
            }
            if (hid && !BUSH[idx(ME.p)]) {
                // next to it and still blind: step into the bush it hides in (inside a bush we see the whole bush)
                for (auto d : DIRS) {
                    const vec2 q = ME.p + d;
                    if (walkable(q) && BUSH[idx(q)] && cheb(q, hid->p) <= 1) {
                        // A rejected tile is recorded in OCC by try_move_to;
                        // try the remaining legal bush tiles before giving up.
                        if (try_move_to(q)) return true;
                    }
                }
            }
        }
        if (f) {
            const int goal = siege_camp(g) && !look ? 6 : attack_reach(ME.melee);
            const int cur = f->at(ME.p);
            if (g.vis_now == 0 && cur != UNREACH && cur <= goal) blind_approach(g, *f);
            else if (cur != UNREACH && cur <= goal) {
                // arrived by the field (a dead member's tile) with none alive in sight: walk to the ones we remember alive
                std::vector<Mon*> known;
                for (int k = 0; k < g.n; ++k) if (g.m[k].hp > 0 && cheb(g.m[k].p, ME.p) > goal) known.push_back(&g.m[k]);
                step_to_live(known);
            }
            else step_field(*f, goal);
        }
        else step_greedy(g.center);
        return true;
    }
    if (g.id == GATE_G && mono5_on(g)) { return fight_mono5(g, M); }   // gate rotation
    if (siege_camp(g)) {
        bool danger = false;
        for (auto* m : M) if (m->str > 0) danger = true;
        if (danger || !(g.id == DOM_G)) return fight_siege(g, M);
        g.kmin = 1;   // only harmless clusters left: finish them normally
    }
    const int reach = attack_reach(ME.melee);
    int dmin = 99;
    for (auto* m : M) dmin = std::min(dmin, cheb(ME.p, m->p));
    const float incoming = group_incoming(g, ME.p);
    // team camp: wait out of reach (distance 4-5) until enough allies are here, unless the fight is on
    if (g.kmin > 1 && g.id == DOM_G) {   // only the all-in group still gathers; the calculator prices the rest
        int here = 1;
        for (auto& a : ALLY) {
            if (!a.vis || !a.alive || a.channel) continue;
            // "here" by the same measure the wait uses (distance to the nearest living member). By the
            // distance to the centre, two champions waiting 2 apart at a long group each counted only itself and waited
            // for the other 250 rounds (fracture, GATHER1/2 both)
            int da = 99;
            for (auto* m : M) da = std::min(da, cheb(a.p, m->p));
            if (da > 6) continue;
            ++here;
        }
        // the all-in group never heals, so "already hurt" says nothing: join only a fight that is on, with company
        bool engaged = (g.id == DOM_G) ? (rnd() - g.awake_at <= 2 && here >= 2)
                                                  : (rnd() - g.awake_at <= 2 || g.hp_left < g.hp_total - 1);
        if (here < g.kmin && !engaged) {
            if (g.gather_since < 0) g.gather_since = rnd();
            int limit = 14;
            if (g.id == DOM_G) {
                // the all-in group is never given up; after a long wait any two of us start
                // a team fight on the jackpot keeps its damage (camps never heal): any two start soon
                if (JP_FIGHT && rnd() - g.gather_since > JACKPOT_WAIT_ROUNDS && here >= 2) { g.kmin = here; }
                else
                if (rnd() - g.gather_since > 40 && here >= std::min(3, ALIVE_ALLIES)) g.kmin = here;
            } else if (rnd() - g.gather_since > limit) {
                // allies who claim this camp are on their way: wait for them a while longer
                int coming = 0;
                for (int s = 0; s < (int)ALLY.size() && s < 6; ++s) if (s != MY_SLOT && claim_group(s) == g.id) ++coming;
                if (!(coming + 1 >= g.kmin && coming + 1 > here && rnd() - g.gather_since <= 40)) {
                    g.skip_until = rnd() + (60); g.gather_since = -1;
                }
            }
            stand_off(g, M, dmin, f);
            return true;
        }
        g.gather_since = -1;
    }
    if (squad_gathers(g, M, dmin, f)) return true;
    // the dash landing on the camp first (hop or approach), then the attack from the landing tile
    if (hor_land(M)) { dmin = 99; for (auto* m : M) if (m->vis && m->hp > 0) dmin = std::min(dmin, cheb(ME.p, m->p)); }
    if (dmin > reach) {
        // ranged with a spell ready at range 4: cast instead of walking in
        if (!ME.melee && dmin <= 4) {
            camp_spells(M, incoming);
            if (ME.cast_excl) return true;
        }
        // approach: melee to an adjacent tile of the weakest monster, ranged to distance 3
        Mon* t = nullptr; float bk = 1e9f;
        for (auto* m : M) {
            float key = m->hp + cheb(ME.p, m->p) * 300.f + cluster_pen(*m, M);
            if (champ(ME.type).splash) key -= near_count(m->p, M) * 400.f;
            if (key < bk) { bk = key; t = m; }
        }
        if (cheb(ME.p, t->p) <= reach + 3) {
            // local greedy step towards a tile in reach of t
            bool moved = false;
            vec2 best{-1, -1}; int bs = 1 << 30;
            for (auto d : DIRS) {
                const vec2 q = ME.p + d;
                if (!walkable(q)) continue;
                const int dd = cheb(q, t->p);
                int s = std::max(0, dd - reach) * 100 + danger_cost(q) * 5;
                for (auto* m : M) if (m != t && m->str > 0 && cheb(q, m->p) <= 3) s += 30;   // each one hits the nearest of us
                if (champ(ME.type).splash && dd <= 1) s -= near_count(q, M) * 10;
                if (!ME.melee && dd < 2) s += 50;
                const int fv = f ? f->at(q) : 0;
                s += fv;
                if (s < bs) { bs = s; best = q; }
            }
            // never a step that walks further from the group by its field. Behind a wall the greedy step went back to the
            // wall and the field step away from it again (Vendetta 12 rounds, Ryth 270 rounds of ping-pong).
            // A step that brings the target into reach is always taken: that is the approach's goal (the field also
            // counts dead members' tiles, so a step towards a live one can read as "further")
            if (best.x >= 0 && cheb(best, t->p) < cheb(ME.p, t->p) &&
                (!f || f->at(best) <= f->at(ME.p) || cheb(best, t->p) <= reach)) {
                moved = try_move_to(best);
            }
            // a greedy step that found nothing (or was refused) walks by the field, or to the live members when the
            // field already says "here": a melee champion two tiles from an elk, the one adjacent tile taken, stood
            // 16-31 rounds (FGSTILL1/2)
            if (!moved && !hole_dash_to(t->p, reach)) approach_step(f, M, reach, false);
        } else approach_step(f, M, reach, true);
        if (ME.melee) {
            Mon* tt = pick_target(M, 1);
            if (tt) { basic_attack_camp(*tt); camp_spells(M, incoming); camp_reset(M); }
        } else if (!ME.cast_excl) {
            camp_spells(M, incoming);
        }
        return true;
    }
    // in reach: spells first (they never need a walk), then attack, then reset
    camp_spells(M, incoming);
    Mon* t = pick_target(M, reach);
    if (t) basic_attack_camp(*t);
    camp_reset(M);
    // melee may still use the walk to improve its tile (Varg: touch more monsters)
    if (ME.melee && ME.can_move && champ(ME.type).splash && !ME.cast_excl) {
        const int here = near_count(ME.p, M);
        for (auto d : DIRS) {
            const vec2 q = ME.p + d;
            if (walkable(q) && near_count(q, M) > here) { try_move_to(q); break; }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// Enemy champions
// ---------------------------------------------------------------------------------------------
// Sustained damage per round of a champion against another champion.
