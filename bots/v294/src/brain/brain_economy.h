// Shopping value and camp staffing feasibility.
#pragma once
// ---------------------------------------------------------------------------------------------
// Shopping trips: worth it when the stat gain over the rest of the game beats the time away.
// ---------------------------------------------------------------------------------------------
static float stat_growth_per_shard() {
    if (build_pos >= BUILD.size()) return 0;
    const IT it = BUILD[build_pos];
    if (it.focus_points() > 0) return ME.g_focus / std::max(30.f, ME.focus) * 0.7f;
    if (it.strength_points() > 0) return ME.g_str / std::max(25.f, ME.str);
    return ME.g_hp / std::max(300.f, ME.mhp) * 0.5f;
}
static int rounds_left_est() {
    const int r = rnd();
    const int have = our_banked() + team_carried();
    const float rate = std::max(1.2f, have / (float)std::max(60, r));
    const int need = std::max(0, (int)bc::constants::winning_gems - have);
    return std::clamp((int)(need / rate), 30, (int)bc::constants::max_rounds - r);
}
// the price of the next item on the build (shop_now may swap a Power shard for the Bruiser one: same tier)
static float next_item_price() {
    float c = 30.f;
    if (build_pos < BUILD.size()) { try { c = (float)BUILD[build_pos].cost(); } catch (const std::exception&) {} }
    return std::max(1.f, c);
}
static bool want_shop() {
    if (champ(ME.type).remote_shop || build_pos >= BUILD.size()) return false;
    const int hd = F_HOME.at(ME.p);
    if (hd == UNREACH) return false;
    // within this many steps of our spawn the floor is the next item's price, not 60, and the
    // shards count at that price (03homonoliths: Vesper held 53 gold from round 24, a 35-gold shard,
    // walked past the spawn a step away and first bought at round 56). Farther out the old floor keeps one-shard trips home.
    const bool near = hd <= SHOP_NEAR_SPAWN_STEPS;
    const float price = near ? next_item_price() : 30.f;
    if (ME.gold < (near ? price : 60.f)) return false;
    const float shards = ME.gold / price;
    const float delta = shards * stat_growth_per_shard();
    float gain = rounds_left_est() * 0.45f * delta / (1.f + delta);
    // SHOP_GAIN_CASTER: a caster's Focus pays twice (clearing and fights): shop sooner
    if (champ(ME.type).shop_eager) gain *= SHOP_GAIN_CASTER;
    const float cost = (hd <= 6 ? hd * 2.f : 6.f + hd) + 2.f;
    return gain > cost * SHOP_TRIP_MARGIN;
}
// the job a shopping trip interrupted (JOB is cleared for the trip), for the tile choice below
static int SHOP_NEXT_G = -1;
// One step towards the spawn tile that costs least for the whole trip: steps to it plus the walk from it to the next
// job (JOB, else the one the trip interrupted). Override ordinary home navigation only for a known job and a strictly
// shorter estimated trip than the best free nearest spawn tile; no job or a tie must preserve the ordinary route.
static bool shop_tile_step() {
    if (own_spawn.empty() || !ME.can_move) return false;
    const int jn = JOB >= 0 && JOB < (int)G.size() ? JOB : (SHOP_NEXT_G >= 0 && SHOP_NEXT_G < (int)G.size() ? SHOP_NEXT_G : -1);
    if (jn < 0) return false;
    vec2 best{-1, -1}; int best_trip = 1 << 30, best_d = 1 << 30;
    int near_d = 1 << 30, near_trip = 1 << 30;
    for (auto q : own_spawn) {
        if (q != ME.p && (!walkable(q) || occupied(q))) continue;
        const int d = cheb(q, ME.p), trip = d + gdist(G[jn], q);
        if (d < near_d) { near_d = d; near_trip = trip; }
        else if (d == near_d) near_trip = std::min(near_trip, trip);
        if (trip < best_trip || (trip == best_trip && d < best_d)) { best_trip = trip; best_d = d; best = q; }
    }
    if (best.x < 0 || best == ME.p || best_trip >= near_trip) return false;
    vec2 step{-1, -1}; int sd = 1 << 30;
    for (auto d : DIRS) {
        const vec2 n = ME.p + d;
        if (!inb(n) || !walkable(n) || occupied(n) || sg_bad(n)) continue;
        const int k = cheb(n, best) * 100 + F_HOME.at(n);
        if (k < sd) { sd = k; step = n; }
    }
    if (step.x < 0 || cheb(step, best) >= cheb(ME.p, best)) return false;
    return try_move_to(step);
}

// ---------------------------------------------------------------------------------------------
// Job choice
// ---------------------------------------------------------------------------------------------
// Allies' claims, tallied once per planning pass.
// the champions that take the team's capture rewards (champions.h)
static bool is_taker(CT t) { return champ(t).taker; }
// who takes a cleared group's reward. A higher class takes it from a lower one (steps on, the lower one
// leaves); within a class the wealth rules decide (share_target, should_hand_off). Ordinary rewards: takers (1) over the
// rest (0). Big rewards (BIG_REWARD_GEMS+ gems, not a monolith on a monolith map): a dash champion adds 2, so it
// carries the bag (it runs home and escapes), then the takers.
static bool big_reward(const Group& g) { return g.gems >= BIG_REWARD_GEMS; }
static int cap_class(CT t, const Group& g) {
    const int tk = is_taker(t) ? 1 : 0;
    // the reward goes to the body whose passive gains most from it: the extra gold if t takes it,
    // tribute_gold x gems (Marcella 2 a gem, paid when she banks them) + (gold_bonus - 1) x gold (Vesper +33%). Both
    // count at the same gold weight, so the gold itself orders them. The top teams route this way (replays: their
    // Marcella takes 30% of capture gems, 122 a game by r500, their Vesper 12% of the gems and 24% of the gold); ours
    // took 23%. A dasher still carries a big reward first among bodies with no passive for it, then the takers.
    const float v = tribute_gold(t) * (float)std::max(0, g.gems) + (champ(t).gold_bonus - 1.f) * (float)std::max(0, g.gold);
    const int dash = big_reward(g) && has_dash(t) && !champ(t).tank ? 2 : 0;
    return 4 * (int)std::lround(v) + dash + tk;
}
// the highest class among the allies capturing g
static int cap_top_class(const Group& g) {
    int top = -1;
    for (int s = 0; s < (int)ALLY.size() && s < 6; ++s)
        if (s != MY_SLOT && ALLY[s].alive && claim_group(s) == g.id && claim_capturing(s)) top = std::max(top, cap_class(ALLY[s].type, g));
    return top;
}
struct Help { int n = 0, melee = 0; float dps = 0; bool cap = false; };
static std::vector<Help> HELP;
static void tally_help() {
    HELP.assign(G.size(), Help{});
    ALIVE_ALLIES = 1;
    for (int s = 0; s < (int)ALLY.size(); ++s) {
        if (s == MY_SLOT) continue;
        try { ALLY[s].alive = ALLY[s].h.is_alive(); } catch (const std::exception&) {}
        if (ALLY[s].alive) ++ALIVE_ALLIES;
    }
    for (int s = 0; s < (int)ALLY.size() && s < 6; ++s) {
        if (s == MY_SLOT || !ALLY[s].alive) continue;
        const int g = claim_group(s);
        if (g < 0 || g >= (int)G.size()) continue;
        Help& h = HELP[g];
        ++h.n;
        h.dps += ally_dps_est(ALLY[s].type);
        if (is_melee(ALLY[s].type)) ++h.melee;
        if (claim_capturing(s)) h.cap = true;
    }
}
// no enemy seen within 7 of the group lately and none visible within 8 of it now. A non-taker hands a
// cleared group to a taker only then; at a contested group its body is worth more than who gets paid.
static bool take_quiet(const Group& g) {
    if (rnd() - g.enemy_at < TAKE_SAFE_ROUNDS) return false;
    for (auto& e : ENEMY) if (e.vis && e.alive && gdist(g, e.p) <= 8) return false;
    return true;
}
// A safe capture with a live allied countdown no longer needs travelling helpers.
// Keep participants and contested/monolith guards; share_target handles deliberate sharing.
static bool reward_reserved(const Group& g) {
    if (g.st != ST_REWARD || group_at[idx(ME.p)] == g.id) return false;
    bool claimed = false;
    for (int s = 0; s < (int)ALLY.size() && s < 6; ++s)
        if (s != MY_SLOT && ALLY[s].alive && claim_group(s) == g.id && claim_capturing(s)) claimed = true;
    if (!claimed || !take_quiet(g)) return false;
    try {
        return (*GROUPS)[g.id].capture_state().state == bc::map::camp_group_capture_type::ally_capturing;
    } catch (const std::exception&) { return false; }
}
// a taker ally that claims this cleared group is close to it, and no enemy is near us: a non-taker leaves it.
static bool taker_coming(const Group& g) {
    if (g.st != ST_REWARD) return false;
    const int mc = cap_class(ME.type, g);   // an ally of a higher capture class (was: a taker, we are not)
    bool near = false;
    for (int s = 0; s < (int)ALLY.size() && s < 6 && !near; ++s) {
        if (s == MY_SLOT || !ALLY[s].alive || !ALLY[s].vis || cap_class(ALLY[s].type, g) <= mc || ALLY[s].channel) continue;
        if (claim_group(s) == g.id && gdist(g, ALLY[s].p) <= TAKER_MAX_STEPS) near = true;
    }
    if (!near) return false;
    for (auto& e : ENEMY) if (e.vis && e.alive && cheb(e.p, ME.p) <= 8) return false;
    if (!take_quiet(g)) { return false; }
    return true;
}
static float MYDPS = 50;

// Champions needed so that each soaks an acceptable share of the camp's damage while it dies.
static int team_needed(const Group& g, float hp_left, float dps_each) {
    if (g.str_total <= 0) return 1;
    const float hpme = std::max(ME.mhp * 0.8f, ME.hp);
    float incoming = g.str_total;
    // shields soak part of it, and the Jungle emblem (our second five shards) cuts camp damage by 30%
    // each champion's own shield, its kit's multiplier (HoR 1.5 x Focus, Varg 2.5), as the camp engine
    for (int s = 0; s < 2; ++s) if (ME.ab[s].shield) incoming -= ME.focus * shield_k(ME.type) / 8.f;
    if (has_jungle()) incoming *= 0.7f;
    incoming = std::max(incoming, g.str_total * 0.15f);
    float soak_frac = CAMP_SOAK_SHARE;
    if (has_jungle() && rnd() - g.enemy_at > CAMP_SOAK_QUIET_ROUNDS) soak_frac = std::max(soak_frac, CAMP_SOAK_SHARE_JUNGLE);
    const float x = incoming * hp_left / (soak_frac * hpme * std::max(10.f, dps_each));
    int k = std::clamp((int)std::ceil(std::sqrt(x)), 1, 6);
    return k;
}

static bool ally_on_tiles(const Group& g);
// every gem our team carries (read from the allies in view; the shared counter stops at 255)
static int gate_bags() {
    int s = ME.gems;
    for (int k = 0; k < (int)ALLY.size(); ++k) if (k != MY_SLOT && ALLY[k].alive && ALLY[k].vis) s += ALLY[k].gems;
    return s;
}
// Gear up first, or start the big job now? A job of H damage started after w rounds, while our damage d grows by a a round, is
// done in w + H / (d + a w) rounds. Waiting shortens that only while H a > d^2: while the job takes longer than our damage
// needs to grow by its own size. a / d is read from this champion: the share of its damage that gear has added, per round
// of the game so far (none yet: waiting gains nothing).
static float MY_BASE_DPS = 0.f;
static bool gearing_is_over(float job_rounds) {
    const float now = champ_dps(ME.type, ME.str, ME.focus);
    if (MY_BASE_DPS <= 0.f) MY_BASE_DPS = now;
    const float added = 1.f - MY_BASE_DPS / std::max(1.f, now);
    return job_rounds * added <= (float)rnd();
}
// the gate is the whole team's job only once opening it pays: we carry TRIBUTE_PAYS_BAG gems, or banking
// what we carry would (nearly) finish the game. Every champion reads the same shared counter, so they turn together;
// once the group is hurt the job stays on until it dies (it never heals).
static bool GATE_ACT = false;
static int gate_team_gems() { return std::max(team_carried(), gate_bags()); }
static void gate_tick() {
    if (GATE_G < 0 || own_spawn.empty()) { GATE_ACT = false; return; }
    const bool closed = F_ALTAR.at(own_spawn[0]) == UNREACH;
    const Group& g = G[GATE_G];
    const int tg = gate_team_gems();
    const bool pay = tg >= TRIBUTE_PAYS_BAG || our_banked() + tg >= TRIBUTE_PAYS_TOTAL;
    const bool act = closed && (pay || (GATE_ACT && g.st == ST_ALIVE && g.hp_left < g.hp_total - 1.f));
    GATE_ACT = act;
}
// the camp standing on every route to an altar (its unlock value is in the job score); no GATE_ACT carry thresholds
// Gate trigger (GATE_ACT: closed and opening it pays; the team turns together on the shared counter)
static inline bool gate_on(const Group& g) { return g.id == GATE_G && GATE_ACT; }
