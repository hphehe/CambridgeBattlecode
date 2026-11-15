// Capture, gem sharing, warding and enclosed objectives.
#pragma once
// ---------------------------------------------------------------------------------------------
// Capture: stand on a spawn tile of a cleared group until the reward arrives
// ---------------------------------------------------------------------------------------------
static bool ally_on_tiles(const Group& g) {
    for (auto& a : ALLY) if (a.vis && a.alive)
        for (int k = 0; k < g.n; ++k) if (a.p == g.m[k].p) return true;
    for (int s = 0; s < (int)ALLY.size() && s < 6; ++s)
        if (s != MY_SLOT && claim_group(s) == g.id && claim_capturing(s)) return true;
    return false;
}
static bool enemy_on_tiles(const Group& g) {
    for (auto& e : ENEMY) if (e.vis && e.alive)
        for (int k = 0; k < g.n; ++k) if (e.p == g.m[k].p) return true;
    return false;
}

static bool do_capture(Group& g) {
    const bool on = group_at[idx(ME.p)] == g.id;
    if (on) {
        if (cap_group != g.id) { cap_group = g.id; cap_start_gems = ME.gems; cap_since = rnd(); }
        set_claim(g.id, true);
        // hit something while waiting
        for (auto& e : ENEMY) if (e.vis && e.alive && cheb(e.p, ME.p) <= attack_reach(ME.melee)) { attack_champ(e); break; }
        return true;
    }
    // walk onto the nearest free spawn tile
    vec2 best{-1, -1}; int bd = 999;
    for (int k = 0; k < g.n; ++k) {
        const vec2 q = g.m[k].p;
        if (occupied(q)) continue;
        const int d = cheb(q, ME.p);
        if (d < bd) { bd = d; best = q; }
    }
    if (best.x < 0) return false;
    if (bd == 1) { if (try_move_to(best)) { cap_group = g.id; cap_start_gems = ME.gems; cap_since = rnd(); set_claim(g.id, true); return true; } }
    const Field* f = gfield(g);
    if (f) step_field(*f, 0); else step_greedy(best);
    if (group_at[idx(ME.p)] == g.id) { cap_group = g.id; cap_start_gems = ME.gems; cap_since = rnd(); set_claim(g.id, true); }
    return true;
}

// ---------------------------------------------------------------------------------------------
// Capture sharing: the reward is split among allies on the tiles when it pays, and whoever steps on
// first used to take all of it, so the melee farmers ended with 60 items and the casters (Cassi above all, whose
// death kills Issac too) with almost none. A poorer ally next to a capture steps on; the richer one then leaves.
// ---------------------------------------------------------------------------------------------
struct BaseSt { bool ok = false, tried = false; float bh = 0, gh = 1, bs = 0, gs = 1, bf = 0, gf = 1; };
static BaseSt ALLY_BASE[6];
// Shards a champion owns, from its stats over its design base (the Power emblem counts as six).
static float shards_from(float mhp, float str, float focus, const BaseSt& b) {
    const float n = (mhp - b.bh) / std::max(1.f, b.gh) + (str - b.bs) / std::max(0.1f, b.gs) + (focus - b.bf) / std::max(0.1f, b.gf);
    return std::max(0.f, n);
}
static bool ally_base(int s) {
    if (s < 0 || s >= 6 || s >= (int)ALLY.size()) return false;
    BaseSt& b = ALLY_BASE[s];
    if (!b.tried) {
        b.tried = true;   // one attempt per slot: a failure is not retried (each throw costs fuel)
        try {
            const auto st = ALLY[s].h.base_stats();
            b.bh = st.base_health; b.gh = st.health_growth; b.bs = st.base_strength; b.gs = st.strength_growth;
            b.bf = st.base_focus; b.gf = st.focus_growth; b.ok = true;
        } catch (const std::exception&) {}
    }
    return b.ok;
}
// the carries count as poorer and take the shared captures (champions.h)
static float gold_priority(CT t) { return champ(t).gold_prio; }
static float ally_wealth(int s) {
    if (!ally_base(s)) return -1;
    const Champ& a = ALLY[s];
    return shards_from(a.mhp, a.str, a.focus, ALLY_BASE[s]) / gold_priority(a.type);
}
static float my_wealth() {
    float w = (float)build_pos;
    if (ally_base(MY_SLOT)) w = shards_from(ME.mhp, ME.str, ME.focus, ALLY_BASE[MY_SLOT]);
    return (w + ME.gold / 30.f) / gold_priority(ME.type);
}
// The ally standing on g's tiles (visible), or -1.
static int capturer_on(const Group& g) {
    for (int s = 0; s < (int)ALLY.size(); ++s) {
        if (s == MY_SLOT) continue;
        const Champ& a = ALLY[s];
        if (!a.vis || !a.alive || a.hp <= 0) continue;
        if (inb(a.p) && group_at[idx(a.p)] == g.id) return s;
    }
    return -1;
}
// A cleared group an ally is capturing that we should step onto too.
static int share_target() {
    const float mine = my_wealth();
    int best = -1, bd = 999;
    for (auto& g : G) {
        if (g.st != ST_REWARD || (g.gold < 30 && !big_reward(g))) continue;
        const int d = gdist(g, ME.p);
        if (d > (cap_class(ME.type, g) > 0 ? std::max(SHARE_MAX_STEPS, TAKER_MAX_STEPS) : SHARE_MAX_STEPS) || d >= bd) continue;
        if (enemy_on_tiles(g)) continue;
        const int c = capturer_on(g);
        if (c < 0) continue;
        bool free_tile = false;
        for (int k = 0; k < g.n; ++k) if (!occupied(g.m[k].p) || g.m[k].p == ME.p) { free_tile = true; break; }
        if (!free_tile) continue;
        // a higher capture class steps onto a lower one's capture whatever their wealth (the lower one then
        // leaves); a lower class never steps onto a higher one's capture
        const int mc = cap_class(ME.type, g), cc = cap_class(ALLY[c].type, g);
        if (mc < cc) continue;
        if (mc > cc) { best = g.id; bd = d; continue; }
        if (mc == 0 && d > SHARE_MAX_STEPS) continue;
        const float theirs = ally_wealth(c);
        if (theirs < 0) continue;
        if (mine + SHARE_SHARD_GAP > theirs) continue;
        best = g.id; bd = d;
    }
    return best;
}
// We are capturing and a poorer ally (not our linked partner) is on the tiles too: leave the reward to it.
static int should_hand_off(const Group& g) {
    if ((g.gold < 30 && !big_reward(g)) || rnd() - cap_since < 1) return -1;
    for (auto& e : ENEMY) if (e.vis && e.alive && cheb(e.p, ME.p) <= 8) return -1;
    const float mine = my_wealth();
    const int mc = cap_class(ME.type, g);
    for (int s = 0; s < (int)ALLY.size() && s < 6; ++s) {
        if (s == MY_SLOT) continue;
        const Champ& a = ALLY[s];
        if (!a.vis || !a.alive || a.hp <= 0 || !inb(a.p) || group_at[idx(a.p)] != g.id) continue;
        if (!(claim_group(s) == g.id && claim_capturing(s))) continue;
        if (g.n > 1) {
            // a lower capture class leaves a capture a higher one has joined (it stays on, so the timer keeps
            // running); a higher class never leaves one to a lower one
            const int ac = cap_class(a.type, g);
            if (mc < ac && !take_quiet(g)) { continue; }
            if (mc < ac) { return s; }
            if (mc > ac) continue;
        }
        const float theirs = ally_wealth(s);
        if (theirs >= 0 && mine >= theirs + HANDOFF_SHARD_GAP) return s;
    }
    return -1;
}

// ---------------------------------------------------------------------------------------------
// One turn
// ---------------------------------------------------------------------------------------------
static vec2 my_ward{-1, -1};      // our current ward (bush-ward bookkeeping)
static int my_ward_at = -1000, my_ward_cap = 0;
static void leave_group_with_ward(const Group& g) {
    try {
        auto& me = bc::self();
        if (me.ward_cooldown() == 0 && me.place_ward(ME.p)) { my_ward = ME.p; my_ward_at = rnd(); my_ward_cap = 1; QM_WARD_ON = true; }
    } catch (const std::exception&) {}
    (void)g;
}

// Bush ward: bushes hide champions from anyone outside them, and vision is personal, so each champion
// keeps its own ward in the bush next to where it works. Placed after the turn's move, within 2 tiles, on the bush
// tile nearest to where we are heading; only when the current ward is somewhere else.
static void bush_ward() {
    if (!ANY_BUSH) return;
    const int r = rnd();
    if (my_ward_cap && r - my_ward_at < BUSH_WARD_KEEP_ROUNDS) return;
    vec2 tgt = ME.p;
    if (JOB >= 0 && JOB < (int)G.size()) tgt = G[JOB].center;
    int ne = 99;
    for (auto& e : ENEMY) if (e.vis && e.alive) { const int d = cheb(e.p, ME.p); if (d < ne) { ne = d; tgt = e.p; } }
    vec2 best{-1, -1};
    int bs = 1 << 30;
    for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx) {
        const vec2 q{ME.p.x + dx, ME.p.y + dy};
        if (!inb(q) || !BUSH[idx(q)]) continue;
        if (my_ward.x >= 0 && cheb(q, my_ward) <= BUSH_WARD_MIN_GAP) continue;
        const int sc = cheb(q, tgt) * 4 - cheb(q, ME.p);
        if (sc < bs) { bs = sc; best = q; }
    }
    if (best.x < 0) return;
    try {
        auto& me = bc::self();
        if (me.ward_cooldown() != 0) return;
        const Bits ok = unpack(me.ward_placement_tiles());
        if (!ok[idx(best)]) return;
        if (me.place_ward(best)) { my_ward = best; my_ward_at = r; my_ward_cap = 0; QM_WARD_ON = true; }
    } catch (const std::exception&) {}
}

// Step to a neighbour that is not one of g's spawn tiles (prefer distance 2 from them).
static bool step_off_group(const Group& g) {
    if (!ME.can_move) return false;
    const Field* f = gfield(g);
    vec2 best{-1, -1};
    int bs = -1;
    for (auto d : DIRS) {
        const vec2 q = ME.p + d;
        if (!walkable(q) || group_at[idx(q)] == g.id) continue;
        const int v = f ? std::min<int>(f->at(q), 3) : 1;
        const int s = (v == 2 ? 10 : v) - danger_cost(q);
        if (s > bs) { bs = s; best = q; }
    }
    return best.x >= 0 && try_move_to(best);
}

// The group the whole team opens on: the best value per round for all of us, counted from our spawn so that every
// champion picks the same one; once picked it is kept until it is no longer alive or waiting for a capture.
static int TEAM_G = -1;
static int TEAM_PREV = -1;   // the team's previous group (the next one is picked from there)
static bool ally_on_tiles(const Group& g);
static int team_camp_group() {
    const int r = rnd();
    if (TEAM_G >= 0 && (G[TEAM_G].st == ST_ALIVE || G[TEAM_G].st == ST_REWARD) && r >= G[TEAM_G].skip_until) {
        // an ally is taking the reward: the rest of us move on to the next group
        if (!(G[TEAM_G].st == ST_REWARD && cap_group != TEAM_G && ally_on_tiles(G[TEAM_G]))) return TEAM_G;
    }
    if (TEAM_G >= 0) TEAM_PREV = TEAM_G;
    float H, D;
    lineup_body(H, D);
    const float team_dps = std::max(1, ALIVE_ALLIES) * D;
    int best = -1;
    float bs = 0;
    for (const auto& g : G) {
        if (g.monolith || g.st == ST_RESPAWN || r < g.skip_until) continue;
        if (g.id == TEAM_PREV) continue;
        if (g.d_away * 10 < g.d_home * 8) continue;                 // never on their side
        const float value = g.gems + 0.5f * g.gold;                   // early gold buys the first items
        if (value <= 0) continue;
        const float hp = g.st == ST_ALIVE ? g.hp_left : 0.f;
        const float clear = hp / team_dps;
        if (clear > 60.f) continue;
        // the whole team must be enough to soak it, and it must need more than one of us (team_needed with our
        // line-up's average body, which every one of us shares; a reward waiting for a capture keeps its full size)
        const int need = party_need(g, g.st == ST_ALIVE ? g.hp_left : g.hp_total);
        if (need > std::max(1, ALIVE_ALLIES) || need < TEAM_CAMP_MIN_NEED) continue;
        // path steps from the last group's spawn tiles (every one of us worked there, so its field exists and
        // we all compute the same pick), else from home
        int from = std::min(g.d_home, 200);
        if (TEAM_PREV >= 0) {
            const Group& pg = G[TEAM_PREV];
            const int fd = pg.fid >= 0 ? GF[pg.fid].at(g.m[0].p) : UNREACH;
            from = fd != UNREACH ? fd : cheb(pg.center, g.center) * 5 / 4;
        }
        const float s = value / (from + clear + 8.f);
        if (s > bs) { bs = s; best = g.id; }
    }
    TEAM_G = best;
    return best;
}

// ---- rally beacon (bit 8 of the holder's slot, the group in beacon_gid): the group nearest to a battle we are winning,
// so free allies close by come and make it decisive. Vision is not shared, so this is the only way the others learn
// about the fight. The team has one beacon: a champion raises it only when no other slot holds it.
static int beacon_quiet = 0;      // our turns next to the beacon without an enemy in sight
static int nearest_group(vec2 p) {
    int best = -1, bd = 1 << 30;
    for (const auto& g : G) { const int d = cheb(g.center, p); if (d < bd) { bd = d; best = g.id; } }
    return best;
}
static int beacon_group() { return beacon_gid(); }
static void raise_beacon() {
    if (!RALLY_ON() || !slot_ok(MY_SLOT)) return;
    const int hs = beacon_slot();
    if (hs >= 0 && beacon_mine_at < 0) return;   // someone else's fight already has it
    if (hs >= 0 && hs != MY_SLOT) clear_beacon_gid(hs);   // ours was retired and taken since: take it back, as before
    const int g = nearest_group(ME.p);
    if (g >= 0 && g < 63) set_beacon_gid(g);
    else if (hs == MY_SLOT) clear_beacon_gid(hs);   // a group past 62 cannot be named
    beacon_mine_at = rnd();
    call_beacon = false;
}
// whoever holds it (the holder may be dead, or we may be retiring an ally's beacon)
static void drop_beacon() {
    const int hs = beacon_slot();
    if (hs >= 0) clear_beacon_gid(hs);
    call_beacon = false; beacon_mine_at = -1; beacon_quiet = 0;
}
static void beacon_upkeep(bool enemies_near) {
    if (!RALLY_ON()) return;
    const int cur = beacon_group();
    if (cur < 0 || cur >= (int)G.size()) { beacon_mine_at = -1; return; }
    if (beacon_mine_at >= 0 && !enemies_near && rnd() - beacon_mine_at > 3) { drop_beacon(); return; }
    // whoever stands at the beacon and sees nobody for a while retires it (its owner may be dead)
    if (cheb(G[cur].center, ME.p) <= 4) {
        beacon_quiet = enemies_near ? 0 : beacon_quiet + 1;
        if (beacon_quiet >= 3) drop_beacon();
    }
}
static bool beacon_near(int r8) { const int sq = beacon_group(); return sq >= 0 && sq < (int)G.size() && cheb(G[sq].center, ME.p) <= r8; }

// low on health next to a camp that is nearly dead, and no enemy champion in sight: keep hitting if the camp
// dies well before we would, and finish a capture we are on (or next to) while nothing hits us. Walking home and back
// costs far more than the few rounds left, and a retreat hands the reward to whoever comes next.
static bool finish_camp(int enemies_seen) {
    if (enemies_seen > 0) return false;
    for (auto& e : ENEMY) if (e.vis && e.alive && cheb(e.p, ME.p) <= 9) return false;
    const int gi = cap_group >= 0 ? cap_group : JOB;
    if (gi < 0 || gi >= (int)G.size()) return false;
    const Group& g = G[gi];
    if (g.monolith) return false;
    // damage per round on us: every monster of an awake group within its reach (an upper bound: each hits its nearest).
    // it counted monsters within 3 (elks and golems reach 4) and took the Jungle
    // emblem's 30% off after any 10 purchases (Vesper's build has no Jungle; Marcella's completes it at 15)
    float inc = 0;
    for (const auto& h : G) {
        if (h.str_total <= 0 || cheb(h.center, ME.p) > 7) continue;
        const bool awake = rnd() - h.awake_at <= 2 || (h.id == g.id && g.st == ST_ALIVE);
        if (!awake) continue;
        for (int k = 0; k < h.n; ++k) if (h.m[k].hp > 0 && cheb(h.m[k].p, ME.p) <= (h.m[k].reach)) inc += h.m[k].str;
    }
    if (has_jungle()) inc *= 0.7f;
    if (g.st == ST_REWARD) {
        if (gdist(g, ME.p) > 2 || inc > 0 || ME.hurt) return false;
        return true;
    }
    if (g.st != ST_ALIVE) return false;
    const int reach = attack_reach(ME.melee);
    int dmin = 99;
    for (int k = 0; k < g.n; ++k) if (g.m[k].vis && g.m[k].hp > 0) dmin = std::min(dmin, cheb(g.m[k].p, ME.p));
    if (dmin > reach) return false;
    float dps = my_dps_est();
    for (auto& a : ALLY) if (a.vis && a.alive && !a.channel && cheb(a.p, g.center) <= 4) dps += ally_dps_est(a.type) * 0.8f;
    const float t_kill = g.hp_left / std::max(10.f, dps) + 1.f;
    const float t_die = ME.hp / std::max(1.f, inc);
    if (t_kill * 1.6f + 1.f >= t_die) return false;
    return true;
}

// ---------------------------------------------------------------------------------------------
// A harmless gem group no one can walk to (ambush: two gem clusters behind a line of holes) is reached
// with a dash over the holes: uncontested gems for a dash champion that is not tied to a partner.
// ---------------------------------------------------------------------------------------------
static int ENC_G = -2;          // -2 not computed yet, -1 none
static vec2 ENC_A{-1, -1}, ENC_B{-1, -1};
static int ENC_FA = -1;         // GF index of the field to ENC_A
static int ENC_LF = -1;         // GF index of a short field (10 steps) from the enclosed group
static int ENC_FAIL = 0;        // dashes in that failed with the dash ready
static bool encl_setup() {
    if (ENC_G != -2) return ENC_G >= 0;
    bool dash = false;
    for (int s = 0; s < 2; ++s) if (ME.ab[s].dash) dash = true;
    if (!dash) { ENC_G = -1; return false; }
    bool any = false;
    for (auto& g : G) if (!g.monolith && g.gems > 0 && g.str_total <= 0 && g.d_home >= 999) any = true;
    if (!any) { ENC_G = -1; return false; }
    if (rnd() < 6 || (long)bc::fuel_remaining() < 70000) return false;   // after the staged set-up, with room
    // holes next to the group's region: a dash crosses a hole from an outside neighbour to an inside one
    static Bits HOLEM;
    static bool holem_ok = false;
    if (!holem_ok) { HOLEM = unpack(bc::map::holes()); holem_ok = true; }
    int best = 1 << 30, found = -1;
    for (auto& g : G) {
        if (g.monolith || g.gems <= 0 || g.str_total > 0 || g.d_home < 999) continue;
        bc::bitboard roots;
        for (int k = 0; k < g.n; ++k) roots.set(g.m[k].p, true);
        GF.push_back(make_field(STATIC_BB, roots, 10));
        const int lf = (int)GF.size() - 1;
        const Field* gf = &GF[lf];
        for (int dx = -7; dx <= 7; ++dx) for (int dy = -7; dy <= 7; ++dy) {
            const vec2 h{g.center.x + dx, g.center.y + dy};
            if (!inb(h) || !HOLEM[idx(h)]) continue;
            for (auto d1 : DIRS) {
                const vec2 b = h + d1;
                if (!inb(b) || BLOCK[idx(b)] || gf->at(b) == UNREACH || F_HOME.at(b) != UNREACH) continue;
                if (group_at[idx(b)] >= 0) continue;   // a camp stands there: a dash cannot land
                for (auto d2 : DIRS) {
                    const vec2 a = h + d2;
                    if (!inb(a) || BLOCK[idx(a)] || cheb(a, b) != 2 || F_HOME.at(a) == UNREACH) continue;
                    if (group_at[idx(a)] >= 0) continue;
                    const int d = F_HOME.at(a) + gf->at(b);
                    if (d < best) { best = d; found = g.id; ENC_A = a; ENC_B = b; ENC_LF = lf; }
                }
            }
        }
    }
    ENC_G = found;
    return false;   // found or not: nothing more this turn (the field to the entry tile is built on a later one)
}
static bool encl_inside() {
    if (ENC_G < 0 || ENC_LF < 0 || F_HOME.at(ME.p) != UNREACH) return false;
    return GF[ENC_LF].at(ME.p) != UNREACH;
}
// Inside the enclosure: work the group, wait out a short respawn, else walk to the exit tile and dash out.
static bool encl_inside_turn() {
    if (!encl_inside()) return false;
    Group& g = G[ENC_G];
    const int r = rnd();
    const bool stay = ME.gems < ENCLAVE_BANK_GEMS && ME.hp > ME.mhp * 0.35f &&
                      (g.st != ST_RESPAWN || g.ready_at - r <= ENCLAVE_WAIT_ROUNDS);
    if (stay) {
        JOB = g.id;
        set_claim(g.id, g.st == ST_REWARD);
        if (g.st == ST_REWARD) { do_capture(g); return true; }
        if (g.st == ST_ALIVE) { fight_group(g); return true; }
        if (group_at[idx(ME.p)] == g.id) step_greedy(ENC_B);   // never block the respawn
        return true;
    }
    JOB = -1;
    set_claim(-1, false);
    if (ME.p != ENC_B) { step_greedy(ENC_B); if (ME.p != ENC_B) return true; }
    const int ds = dash_slot();
    if (ds >= 0 && cast_slot_tile(ds, ENC_A)) { prev_pos = ME.p; ME.p = ENC_A; if (ME.gems > 0) banking = true; }
    return true;
}
// Outside: when nothing else is pressing, walk to the entry tile and dash in.
static bool encl_outside_turn() {
    if (!encl_setup() || encl_inside()) return false;
    Group& g = G[ENC_G];
    const int r = rnd();
    if (ME.gems >= ENCLAVE_BANK_GEMS || ME.hp < ME.mhp * 0.6f) return false;
    for (auto& e : ENEMY) if (e.vis && e.alive && cheb(e.p, ME.p) <= 8) return false;
    if (ENC_FA < 0) {
        if ((long)bc::fuel_remaining() < 75000) return false;
        bc::bitboard root; root.set(ENC_A, true);
        GF.push_back(make_field(STATIC_BB, root));
        ENC_FA = (int)GF.size() - 1;
    }
    const Field& fa = GF[ENC_FA];
    const int t = fa.at(ME.p);
    if (t == UNREACH || t > ENCLAVE_MAX_STEPS) return false;
    if (g.st == ST_RESPAWN && g.ready_at - r > t + ENCLAVE_WAIT_ROUNDS) return false;
    JOB = -1;
    set_claim(g.id, false);
    if (ME.p != ENC_A) { step_field(fa, 0, false); if (ME.p != ENC_A) return true; }
    const int ds = dash_slot();
    if (ds >= 0 && cast_slot_tile(ds, ENC_B)) { prev_pos = ME.p; ME.p = ENC_B; ENC_FAIL = 0; }
    else if (ds >= 0) {
        // the landing tile is taken (a unit we cannot see, or one we can): try a few times, then drop the group
        if (ENC_FAIL >= 3) { ENC_G = -1; set_claim(-1, false); }
        return false;
    }
    return true;
}
