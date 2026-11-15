#pragma once
// Included by brain.h after movement/teleport helpers.
// ---------------------------------------------------------------------------------------------
// Banking
// ---------------------------------------------------------------------------------------------
static bool altar_locked_by_enemy(vec2 a) {
    try {
        if (!bc::vision::has_altar_lock(a)) return false;
        for (auto& l : bc::vision::altar_locks(team::enemy)) if (l.position() == a) return true;
    } catch (const std::exception&) {}
    return false;
}

// An enemy champion (off its spawn) seen within d of p in the last t rounds.
static bool enemy_seen_near(vec2 p, int d, int t) {
    const int r = rnd();
    for (auto& e : ENEMY)
        if (e.alive && e.last_p.x >= 0 && r - e.seen_at <= t && cheb(e.last_p, p) <= d && !on_enemy_spawn(e.last_p)) return true;
    return false;
}
static vec2 nearest_altar() {
    vec2 best{-1, -1};
    int bd = 1 << 30;
    for (auto a : altars) if (cheb(a, ME.p) < bd) { bd = cheb(a, ME.p); best = a; }
    return best;
}
// an enemy was near the altar we would bank at (a long tribute there is a lock away from starting over)
static bool altar_threat_recent() {
    const vec2 a = nearest_altar();
    return a.x >= 0 && enemy_seen_near(a, 8, 20);
}
// LOCK_OWN_TRIBUTE_GEMS: our lock on the altar we are about to tribute at, while an enemy was near it (no ally channelling there)
static void lock_own_altar(vec2 a) {
    if (ME.gems < LOCK_OWN_TRIBUTE_GEMS || !altar_threat_recent()) return;
    for (auto& al : ALLY) if (al.vis && al.alive && al.channel && cheb(al.p, a) <= 1 && al.p != ME.p) return;
    try {
        if (bc::vision::has_altar_lock(a)) return;
        auto cons = bc::self().consumables()[bc::consumable_type::altar_lock];
        if (cons.count() > 0) cons.use(a);
    } catch (const std::exception&) {}
}

// ---------------------------------------------------------------------------------------------
// When a carrier banks. want_bank starts a trip, bank_on_hold stops one starting, bank_in_respawn_wait is the free trip of a
// respawn wait; bank_urgent is what overrides bag size and waits.
// ---------------------------------------------------------------------------------------------
static int tribute_turns(int gems) { return gems > 0 ? (gems + 9) / 10 : 0; }
static int bank_walk_distance();
// the game is about to end on the enemy's gems, or on the clock before a walk and tribute from here would finish
static bool game_end_forces_bank(int walk) {
    return (their_banked() >= ENEMY_NEAR_WIN_GEMS && ME.gems >= ENEMY_NEAR_WIN_BAG) ||
           rnd() + walk + tribute_turns(ME.gems) + CLOCK_MARGIN_ROUNDS >= (int)bc::constants::max_rounds;
}
// the bag goes now whatever its size: the gems we carry win the game, or the game is ending
static bool bank_urgent() {
    if (ME.gems <= 0) return false;
    const int d = bank_walk_distance();
    if (d == UNREACH) return false;
    return our_banked() + team_carried() >= (int)bc::constants::winning_gems || game_end_forces_bank(d);
}

#include "bank_engine.h"

static bool want_bank() {
    if (ME.gems <= 0) return false;
    const int dw = F_ALTAR.at(ME.p);
    const int da = lf::bank_trip(dw);   // less what the crystal or a recall saves (lfield.h)
    if (da == UNREACH) return false;
    if (bank_urgent()) return true;
    const float q = be_ideal(be_trip(dw, da, ME.gems));
    if ((float)ME.gems >= q) return true;
    return (float)ME.gems >= q * be_near_scale();   // enemies near: a smaller bag is worth the trip now
}

// a bank walk that stops getting nearer. F_ALTAR walks through camp tiles; with a live camp
// in the only gap of a wall (01the_farm) step_field's detour step is undone by the next downhill step, for good.
static int STALL_LAST_R = -100, STALL_BEST = 1 << 30, STALL_N = 0;
static int BANK_HOLD_UNTIL = -1;   // no new bank trip before this round, unless urgent
// the goal was blocked: drop the trip and farm for a while
static void hold_banking(int r) { BANK_HOLD_UNTIL = r + BANK_RETRY_ROUNDS; banking = false; KNOW_CHANGED = true; }
// A tribute given up under damage with nothing in view and no monster reaching us: an enemy shoots from a bush. The fight
// code runs only with an enemy in view, so it cannot make us leave, and a new tribute is longer than the one just given up
// (a Vesper restarted a 200-gem tribute 23 times beside the altar, then died with the bag). We go home and stay off the altar
// for a while. A last chance ends the wait, the enemy near the win or the game ending, but not on the turn it broke. Having
// enough gems to win does not: we are ahead, and carriers came back every other round. An ally beside us does not: it cannot
// see the shooter either, and two carriers side by side ended each other's waits.
static int HIDDEN_FIRE_AT = -1, HIDDEN_FIRE_UNTIL = -1;
static bool hidden_fire_holding() {
    const int r = rnd();
    if (r >= HIDDEN_FIRE_UNTIL) return false;
    if (r == HIDDEN_FIRE_AT) return true;
    const int d = bank_walk_distance();
    return !(ME.gems > 0 && d != UNREACH && game_end_forces_bank(d));
}
static void note_hidden_fire(int r) {
    for (auto& e : ENEMY) if (e.vis && e.alive && e.hp > 0) return;
    std::vector<vec2> hitters;
    hitters_near(hitters, 0);
    if (!hitters.empty()) return;
    HIDDEN_FIRE_AT = r;
    HIDDEN_FIRE_UNTIL = r + HIDDEN_FIRE_HOLD_ROUNDS;
    banking = false;
    going_home = true;
    set_claim(-1, false);
    JOB = -1;
    KNOW_CHANGED = true;
}
static bool bank_on_hold() { return (rnd() < BANK_HOLD_UNTIL && !bank_urgent()) || hidden_fire_holding(); }
// a walk to an altar exists and lfield.h can make the trip (a live camp ring round the altar leaves F_ALTAR but no trip)
static bool bank_reachable() {
    const int d = F_ALTAR.at(ME.p);
    return d != UNREACH && lf::bank_trip(d) != UNREACH;
}
// a respawn wait is free time: the trip costs only the farming the wait does not cover
static bool bank_in_respawn_wait(int wait_left) {
    const int t_left = 2 * F_ALTAR.at(ME.p) + ME.gems / 10 + 1 - wait_left;
    return ME.gems > 0 && bank_reachable() && (float)ME.gems >= be_ideal(t_left) && !bank_on_hold();
}
// do_bank, before the walk: true when banking stopped this turn
static bool bank_stall() {
    const int r = rnd();
    if (r - STALL_LAST_R > 3) { STALL_BEST = 1 << 30; STALL_N = 0; }
    STALL_LAST_R = r;
    const int da = F_ALTAR.at(ME.p);
    if (da <= 1 || da == UNREACH) return false;   // next to an altar: the tribute / lock code decides
    if (da < STALL_BEST) { STALL_BEST = da; STALL_N = 0; return false; }
    if (++STALL_N < BANK_NO_PROGRESS_TURNS) return false;
    STALL_N = 0;
    hold_banking(r);
    return true;
}

// One round of damage the visible enemies could deal at `at` during a tribute with `turn_ends` more turns: a cooldown
// that ends during the channel counts as ready, and the enemy gets one tile of walking in.
// A gem tribute is cancelled when this reaches TRIBUTE_CANCEL_BURST of our health, and the same test runs before
// starting one: Marcella started and cancelled a 50-gem tribute every other round for 80 rounds, an enemy at 5-7.
static float tribute_burst(vec2 at, int turn_ends) {
    float burst = 0.f;
    for (auto& e : ENEMY) {
        if (!targetable(e)) continue;
        const EnemyPower p = read_enemy_power(e, turn_ends, 1);   // the channel outlasts this round's turn order
        burst += danger_at(p, std::max(0, cheb(e.p, at) - 1));
    }
    return burst;
}
// an awake monster hits its nearest champion (rules 05): one standing nearer to it than `at` takes the hits
static bool other_nearer(vec2 mp, vec2 at) {
    const int d = cheb(mp, at);
    for (const auto& a : ALLY) if (a.vis && a.alive && a.hp > 0 && cheb(a.p, mp) < d) return true;
    for (const auto& e : ENEMY) if (e.vis && e.alive && e.hp > 0 && cheb(e.p, mp) < d) return true;
    return false;
}
// damage per round the awake monsters whose reach covers `at` deal to a champion there: only those with no
// champion nearer to them; counting every one kept a 1900-hp Marcella from a tribute for 40 rounds next to a camp that
// was busy hitting the allies fighting it (starfish, TRIBHOLD)
static float camp_damage_at(vec2 at) {
    float s = 0.f;
    for (const auto& g : G) {
        if (cheb(g.center, at) > 9 || !maybe_awake(g)) continue;
        for (int k = 0; k < g.n; ++k)
            if (monster_hits(g.m[k], at) && !other_nearer(g.m[k].p, at)) s += g.m[k].str;
    }
    return s;
}
// the tribute we would start here gets cancelled on our next turn (3+ turns left by then), or the awake
// camps reaching the altar tile would take our health over the channel and the walk out: Marcella tributed 3 turns in
// reach of four awake elks, left at 480 and died on the third step out (starvation, the same tile three times)
static bool tribute_unsafe(vec2 at) {
    const int turns = std::max(1, (ME.gems + 9) / 10);
    if (ME.gems > 0) {
        const float cd = camp_damage_at(at);
        if (cd > 0.f && cd * (turns + escape_rounds() + 1) >= ME.hp) return true;
    }
    return ME.gems >= TRIBUTE_CANCEL_GEMS && turns - 1 >= 3 && tribute_burst(at, turns - 1) >= ME.hp * TRIBUTE_CANCEL_BURST;
}

// A failed tribute floor needs a persistent destination. A one-step greedy
// sidestep followed by F_ALTAR returned to the same floor every other round.
static Field BANK_ALTERNATE;
static vec2 BANK_DEST{-1, -1};
static uint32_t BANK_ALT_KEY = 0;
static bool bank_alternate_active() {
    if (BANK_DEST.x < 0) return false;
    if (BANK_ALT_KEY != ALT_KEY_LIVE || altar_locked_by_enemy(BANK_DEST) ||
        BANK_ALTERNATE.at(ME.p) == UNREACH) {
        BANK_DEST = vec2{-1, -1};
        return false;
    }
    return true;
}
static int bank_walk_distance() {
    return bank_alternate_active() ? BANK_ALTERNATE.at(ME.p) : F_ALTAR.at(ME.p);
}
static bool alternate_fuel_ok() { return (long)bc::fuel_remaining() >= std::max(40000L, 6L * N + 16000L); }
static bool choose_bank_alternate() {
    if (!F_ALTAR.roots || !alternate_fuel_ok()) return false;
    std::vector<vec2> choices;
    for (auto a : altars) {
        if (cheb(a, ME.p) <= 1 || altar_locked_by_enemy(a) || enemy_seen_near(a, 6, 2)) continue;
        choices.push_back(a);
    }
    std::stable_sort(choices.begin(), choices.end(), [](vec2 a, vec2 b) {
        return cheb(a, ME.p) < cheb(b, ME.p);
    });
    for (auto a : choices) {
        if (!alternate_fuel_ok()) break;
        bc::bitboard roots;
        // Reuse legal tribute floors: do not reintroduce live gate-camp tiles.
        for (auto d : DIRS) {
            const vec2 q = a + d;
            if (inb(q) && F_ALTAR.at(q) == 0) roots.set(q, true);
        }
        if (roots.is_empty()) continue;
        BANK_ALTERNATE = make_field(STATIC_BB, roots);
        if (BANK_ALTERNATE.at(ME.p) == UNREACH) continue;
        BANK_DEST = a; BANK_ALT_KEY = ALT_KEY_LIVE;
        return true;
    }
    return false;
}

static bool submit_at_current_tile() {
    if (ME.gems <= 0) return false;
    auto& me = bc::self();
    // adjacent altar not locked by the enemy: submit
    for (auto a : altars) {
        if (cheb(a, ME.p) > 1 || altar_locked_by_enemy(a)) continue;
        if (tribute_unsafe(ME.p)) break;
        lock_own_altar(a);
        try { me.submit_gems(a); banking = false; BANK_DEST = vec2{-1, -1}; return true; }
        catch (const std::exception&) {}
    }
    return false;
}

// Walk to an altar and tribute. Returns true if we acted.
static bool do_bank() {
    auto& me = bc::self();
    if (submit_at_current_tile()) return true;
    // locked altar next to us: hit the lock if we can
    for (auto a : altars) {
        if (cheb(a, ME.p) > 3 || !ME.can_attack) continue;
        try {
            for (auto& l : bc::vision::altar_locks(team::enemy))
                if (l.position() == a && cheb(a, ME.p) <= attack_reach(ME.melee)) { me.attack(l); ME.can_attack = false; return true; }
        } catch (const std::exception&) {}
    }
    banking = true;
    if (bank_alternate_active()) {
        if (BANK_ALTERNATE.at(ME.p) > 0) {
            step_field(BANK_ALTERNATE, 0);
            submit_at_current_tile(); return true;
        }
        BANK_DEST = vec2{-1, -1};
    }
    if (F_ALTAR.at(ME.p) == 0 && choose_bank_alternate()) {
        step_field(BANK_ALTERNATE, 0); submit_at_current_tile(); return true;
    }
    // the crystal or a recall, when either beats the walk (lfield.h): before the stall check, which measures progress
    // on F_ALTAR
    if (lf::travel(F_ALTAR, 0)) return true;
    if (bank_stall()) return true;
    step_field(F_ALTAR, 0);
    // Moving and starting tribute are legal in the same turn.
    submit_at_current_tile();
    return true;
}
