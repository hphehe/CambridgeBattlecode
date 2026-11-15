// Camp damage, target refresh, spell aiming and attack execution.
#pragma once
static bool mon_alive(const Mon& m) { return m.c && m.c->is_visible() && m.c->health() > 0; }

// Actions can kill a target or deal a different amount than our estimate (stuns,
// shields and champion passives). Keep this turn's target list consistent with
// the engine before choosing the next basic attack or reset.
static void refresh_camp_health(Mon& m) {
    if (!m.c) { m.hp = 0; m.vis = false; return; }
    // One native snapshot replaces the is_visible()+health() pair. This is
    // called after AoE casts, where the old per-field accessors multiplied
    // fuel by the number of monsters in the radius.
    bc::detail::camp_state cs{};
    const int32_t code = bc::detail::sdk_camp(
        static_cast<uint32_t>(static_cast<bc::handle_camp_t>(*m.c)), &cs);
    if (code == 0) {
        m.hp = cs.health;
        m.vis = m.hp > 0;
        return;
    }
    if (code == (int32_t)bc::error_code::api_not_in_vision ||
        code == (int32_t)bc::error_code::api_not_found) {
        m.hp = 0;
        m.vis = false;
        return;
    }
    bc::detail::check(code);
}
static void refresh_camp_area(const std::vector<Mon*>& M, vec2 at) {
    // Camp spells consume our one cast. Only a still-available basic attack
    // can use post-cast HP this turn; observe() refreshes the others next turn.
    // Ranged siege AoE often hits nine monsters from outside attack range.
    if (!ME.can_attack) return;
    const int reach = attack_reach(ME.melee);
    for (auto* m : M)
        if (m->vis && cheb(m->p, at) <= 1 && cheb(m->p, ME.p) <= reach)
            refresh_camp_health(*m);
}

static int near_count(vec2 c, const std::vector<Mon*>& M) {
    int n = 0;
    for (auto* m : M) if (m->vis && m->hp > 0 && cheb(c, m->p) <= 1) ++n;
    return n;
}

// a Vendetta kill (champion or camp monster) resets all her cooldowns (rules 08). ME.cd is read at the
// turn's start, so the reset stayed "on cooldown" for the rest of a turn whose hit killed: read them again after each hit.
static void kill_reset_reread() {
    return;
    try { const auto ab = bc::self().abilities(); ME.cd[0] = ab[0].cooldown(); ME.cd[1] = ab[1].cooldown(); } catch (const std::exception&) {}
}
static bool basic_attack_camp(Mon& m) {
    if (!ME.can_attack || cheb(ME.p, m.p) > attack_reach(ME.melee)) return false;
    if (!mon_alive(m)) { m.hp = 0; m.vis = false; return false; }
    try { bc::self().attack(*m.c); } catch (const std::exception&) { ME.can_attack = false; return false; }
    ME.can_attack = false;
    LAST_CAMP_HIT = rnd();
    refresh_camp_health(m);
    if (m.hp <= 0) kill_reset_reread();
    return true;
}

// Harmless clusters wait while anything that hits is alive.
static float cluster_pen(const Mon& m, const std::vector<Mon*>& M) {
    if (m.str > 0) return 0.f;
    for (auto* o : M) if (o->vis && o->hp > 0 && o->str > 0) return 3000.f;
    return 0.f;
}
// Choose the monster to hit: lowest health in reach (fewer monsters alive = less damage taken).
static Mon* pick_target(std::vector<Mon*>& M, int range) {
    Mon* best = nullptr;
    float bk = 1e9f;
    for (auto* m : M) {
        if (!m->vis || m->hp <= 0 || cheb(ME.p, m->p) > range) continue;
        float key = m->hp + cluster_pen(*m, M);
        if (champ(ME.type).splash) key -= near_count(m->p, M) * 400.f;
        if (key < bk) { bk = key; best = m; }
    }
    return best;
}

static int reset_slot() { for (int s = 0; s < 2; ++s) if (ME.ab[s].reset && ready(s)) return s; return -1; }
static int shield_slot() { for (int s = 0; s < 2; ++s) if (ME.ab[s].shield && ready(s)) return s; return -1; }

static bool cast_slot_unit(int s, const bc::camp& c) {
    bool ok = false;
    try { ok = bc::self().abilities()[s].use(c); } catch (const std::exception&) { ok = false; }
    if (ok) LAST_CAMP_HIT = rnd();
    if (ok) { ME.cd[s] = 8; ME.can_cast = false; if (ME.ab[s].excl) { ME.cast_excl = true; ME.can_move = false; } }
    return ok;
}
static bool cast_slot_champ(int s, const bc::champion& c) {
    bool ok = false;
    try { ok = bc::self().abilities()[s].use(c); } catch (const std::exception&) { ok = false; }
    if (ok) { ME.cd[s] = 8; ME.can_cast = false; if (ME.ab[s].excl) { ME.cast_excl = true; ME.can_move = false; } LAST_CHAMP_HIT = rnd(); }
    return ok;
}
static bool cast_slot_tile(int s, vec2 t) {
    bool ok = false;
    try { ok = bc::self().abilities()[s].use(t); } catch (const std::exception&) { ok = false; }
    if (ok) LAST_CAMP_HIT = rnd();   // a ground spell counts as acting (sieges cast at a tile)
    if (ok) { ME.cd[s] = 8; ME.can_cast = false; if (ME.ab[s].excl) { ME.cast_excl = true; ME.can_move = false; } }
    return ok;
}
static bool cast_self(int s) {
    bool ok = false;
    try { ok = bc::self().abilities()[s].use(); } catch (const std::exception&) { ok = false; }
    if (ok) { ME.cd[s] = 8; ME.can_cast = false; }
    return ok;
}

// A self-shield (Varg 2.5 x Focus, Heart of Ruin 1.5 x Focus) lasts 5 turns on an 8-turn cooldown: up once an enemy
// champion is within 6 (a walk and a dash from us), before the contact, instead of as the turn's spare cast after an attack
// reset (ladder: our Varg shielded 3.1 times per 100 rounds and never before contact, top-team Varg 9.1 with 38% before).
// one cast a turn (rules 08), so while a dash is ready (HoR: the
// landing and its stun, or the escape) the shield waits for the turn's actions; main.cpp casts it after think() with the same
// test (shield_late) if the cast is still unused and no channel started.
static bool SHA_LATE = false;
static void shield_ahead() {
    if (!ME.can_cast) return;
    const int sh = shield_slot();
    if (sh < 0) return;
    for (const auto& e : ENEMY)
        if (e.vis && e.alive && e.hp > 0 && !on_enemy_spawn(e.p) && cheb(e.p, ME.p) <= 6) {
            for (int s = 0; s < 2; ++s) if (ME.ab[s].dash && ready(s)) { SHA_LATE = true; return; }
            cast_self(sh);
            return;
        }
}
// no second test at the end of the turn: the shield goes up whenever shield_ahead held it for the dash and the turn left
// the cast unused (re-testing an enemy within 6 from the end tile, so a walk out to 7 also dropped the shield)
static void shield_late() {
    if (!ME.can_cast) return;
    const int sh = shield_slot();
    if (sh < 0) return;
    cast_self(sh);
}

// Each legal cast center appears beside several monsters. Evaluate it once,
// retaining first-encounter order so ties choose the identical destination.
static vec2 camp_area_target(const std::vector<Mon*>& M, int& count) {
    vec2 best{-1, -1}; count = 0;
    bool seen[81] = {};
    for (auto* m : M) for (int dx = -1; dx <= 1; ++dx) for (int dy = -1; dy <= 1; ++dy) {
        const vec2 c{m->p.x + dx, m->p.y + dy};
        if (!inb(c) || cheb(ME.p, c) > 4) continue;
        const int key = (c.x - ME.p.x + 4) * 9 + (c.y - ME.p.y + 4);
        if (seen[key]) continue;
        seen[key] = true;
        const int n = near_count(c, M);
        if (n > count) { count = n; best = c; }
    }
    return best;
}

static int SIEGE_R = -10;        // round of our last siege turn
// Offensive spells against monsters (damage, area, stun). Respects the walk exclusion.
static void camp_spells(std::vector<Mon*>& M, float incoming) {
    if (!ME.can_cast) return;
    // Varg / HoR: shield when the camp is really hurting us (its damage over the shield's five turns fills half of it,
    // shield_mult x Focus; the champion's own shield size, 2.5 x Focus was Varg's for both);
    // otherwise keep the cast for the reset (Varg) or the shield for an enemy who turns up (HoR: the landing has its own slot).
    const int sh = shield_slot();
    const bool hurting = incoming * 5.f >= 0.5f * shield_mult(ME.type) * ME.focus;
    if (sh >= 0 && (hurting || ME.hp < ME.mhp * 0.6f) && incoming > 0) { if (cast_self(sh)) { return; } }
    // an enemy champion close by will likely fight us before an 8-turn cooldown ends
    bool save = false;
        for (const auto& e : ENEMY)
            if (e.alive && e.vis && cheb(e.p, ME.p) <= SAVE_CAST_RADIUS) { save = true; break; }
    for (int s = 0; s < 2; ++s) {
        const Ab& a = ME.ab[s];
        if (!ready(s) || !(a.dmg || a.aoe || a.cc) || !a.camps && !a.ground) continue;
        if (save) {
            // a hold makes no progress while sieging, and a spell that ends the camp ends the reason to stay
            bool go = SIEGE_R == rnd();
            if (!go && (a.dmg || a.aoe)) {
                float dmg = 0, left = 0; bool inr = true;
                try { dmg = bc::self().abilities()[s].damage_amount(); } catch (const std::exception&) {}
                for (auto* m : M) if (mon_alive(*m)) { left += std::max(0.f, m->hp); if (cheb(ME.p, m->p) > 4) inr = false; }
                go = inr && left > 0 && left <= dmg && (a.aoe || M.size() == 1);
            }
            if (!go) { return; }
        }
        if (a.excl && ME.walked) continue;
        if (a.aoe || (a.ground && !a.camps)) {
            int bn = 0;
            const vec2 best = camp_area_target(M, bn);
            bool other_idle = false;   // the other (single-target) slot has nothing within its range 4
            other_idle = true;
            for (auto* m : M) if (cheb(ME.p, m->p) <= 4) other_idle = false;
            if (best.x >= 0 && (bn >= 2 || M.size() == 1 || ME.cd[1 - s] > 0 || !(ME.ab[1 - s].dmg) || other_idle)) {
                if (cast_slot_tile(s, best)) { refresh_camp_area(M, best); return; }
            }
            continue;
        }
        // single target: stuns go on the hardest hitter, damage on the weakest
        Mon* t = nullptr; float bk = 1e9f;
        for (auto* m : M) {
            if (!m->vis || m->hp <= 0 || cheb(ME.p, m->p) > 4) continue;
            // Marcella's damage spell (2 x Focus, 8-turn cooldown) took the lowest health in range 4,
            // so a 120-health cluster: 20 of 44 casts in one ladder game, 15 with a wolf or elk in range. It now passes
            // over a harmless cluster while a monster that hits is alive (pick_target's rule) and over one a basic attack kills.
            if (ME.type == CT::marcella && a.dmg && !a.cc && m->str <= 0 && (cluster_pen(*m, M) > 0 || m->hp <= ME.str)) continue;
            const float key = a.cc && !a.dmg ? -m->str * 100 + m->hp : (a.cc ? -m->str * 10 + m->hp : m->hp);
            if (key < bk) { bk = key; t = m; }
        }
        if (!t) continue;
        if (a.cc && t->str <= 0 && !a.dmg) continue;    // stunning a cluster is pointless
        if (!mon_alive(*t)) continue;
        if (cast_slot_unit(s, *t->c)) { refresh_camp_health(*t); return; }
    }
}

// After the basic attack is spent: reset and hit again.
static void camp_reset(std::vector<Mon*>& M) {
    if (ME.can_attack || !ME.can_cast) return;
    const int s = reset_slot();
    if (s < 0) return;
    Mon* t = pick_target(M, attack_reach(ME.melee));
    if (!t || !mon_alive(*t)) return;
    // Marcella's reset makes her next hit stun. With only a harmless cluster in reach while a monster
    // that hits is alive, the stun and the 8-turn cooldown went to the cluster (14 of 46 in one ladder game).
    if (ME.type == CT::marcella && cluster_pen(*t, M) > 0) return;
    if (cast_self(s)) { ME.can_attack = true; basic_attack_camp(*t); }
}

// the dash landing as a camp hit (Heart of Ruin: 1.5 x Focus x passive and a one-turn stun within
// radius 1). Top teams fire it the moment it is off cooldown while farming, on the free tile within 2 (54% of landings a
// 1-tile hop), then attack from there: 28.5 camp landings a game, ~70% of their HoR's camp damage. Candidates: walkable
// tiles within 2 that a dash reaches (land_path), scored by the landing damage they deal (min(hp, landing) summed over live
// monsters within 1, clusters x 0.6) minus half the tile's danger; ties keep more monsters. Skipped below 0.4 x the landing
// damage, and while an enemy champion (spawn campers too) is within 5.
static int land_slot() { for (int s = 0; s < 2; ++s) if (ME.ab[s].dash && ME.ab[s].land > 0 && ready(s)) return s; return -1; }
// a 2-tile dash needs a middle tile next to both ends that is free (a camp or a unit on it blocks the path: engine error
// 32 in the HoR_v6 probe) or a hole or altar it may pass over
static bool land_path(vec2 b) {
    for (auto d : DIRS) {
        const vec2 a = ME.p + d;
        if (cheb(a, b) != 1 || !inb(a)) continue;
        if (BLOCK[idx(a)] ? (HD_ON && HD_PASS[idx(a)]) : !occupied(a)) return true;
    }
    return false;
}
static int LAND_FAIL_R = -1;
static bool hor_land(std::vector<Mon*>& M) {
    if (!ME.can_cast || LAND_FAIL_R == rnd()) return false;
    const int s = land_slot();
    if (s < 0) return false;
    // an enemy champion within a walk, a dash and a hit keeps the dash for the fight (micro) or the way out
    for (const auto& e : ENEMY)
        if (e.alive && e.vis && cheb(e.p, ME.p) <= 5) { return false; }
    float dmg = ME.focus * ME.ab[s].land;
    try { dmg = bc::self().abilities()[s].damage_amount(); } catch (const std::exception&) {}
    vec2 best{-1, -1};
    float bv = 0.f;
    int bn = 0;
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) {
            const vec2 b{ME.p.x + dx, ME.p.y + dy};
            if (b == ME.p || !walkable(b)) continue;
            float v = 0.f;
            int n = 0;
            for (auto* m : M) {
                if (!m->vis || m->hp <= 0 || cheb(m->p, b) > 1) continue;
                v += std::min(m->hp, dmg) * (m->str > 0 ? 1.f : 0.6f);
                ++n;
            }
            if (n == 0) continue;
            if (cheb(b, ME.p) == 2 && !land_path(b)) continue;
            v -= danger_cost(b) * 0.5f;
            if (v > bv || (v == bv && n > bn)) { bv = v; bn = n; best = b; }
        }
    // a landing that only finishes a nearly dead monster leaves it to the basic attack
    if (best.x < 0 || bv < 0.4f * dmg) return false;
    bool ok = false;
    try { ok = bc::self().abilities()[s].use(best); } catch (const std::exception&) { ok = false; }
    if (!ok) { LAND_FAIL_R = rnd(); return false; }
    prev_pos = ME.p;
    ME.p = best;
    ME.cd[s] = 8;
    ME.can_cast = false;
    LAST_CAMP_HIT = rnd();
    for (auto* m : M) if (m->vis && cheb(m->p, best) <= 1) refresh_camp_health(*m);
    return true;
}
