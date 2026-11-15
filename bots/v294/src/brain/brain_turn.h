// Turn priorities: safety, combat, delivery, recovery, then objectives.
#pragma once
#include "brain_fight_turn.h"

// the trip home after a capture starts in its wait. A recall started on our turn arrives at turn start
// recall_time rounds later, after that round's capture phase: with timer <= recall_time left we are paid before we leave
// (a channeller holds the capture). Only when the turn after the payout would recall home to shop: no enemy or flight
// near, no bank walk with the paid bag, a shop worth it with the paid gold, home out of walking range.
static bool rcov_start(Group& g) {
    const int r = rnd();
    if (on_own_spawn() || F_HOME.at(ME.p) <= 7 || F_HOME.at(ME.p) == UNREACH) return false;
    if (champ(ME.type).remote_shop || build_pos >= BUILD.size()) return false;
    bc::map::camp_group_capture_state cs{};
    try { cs = (*GROUPS)[g.id].capture_state(); } catch (const std::exception&) { return false; }
    if (cs.state != bc::map::camp_group_capture_type::ally_capturing || cs.timer < 1 || cs.timer > (int)bc::constants::recall_time)
        return false;
    if (flees_since(r - FIGHT_MEMORY_ROUNDS) > 0) return false;
    for (const auto& e : ENEMY)
        if (e.alive && e.last_p.x >= 0 && r - e.seen_at <= FIGHT_MEMORY_ROUNDS && !on_enemy_spawn(e.last_p) && cheb(e.vis ? e.p : e.last_p, ME.p) <= 10)
            return false;
    if (r > (int)bc::constants::max_rounds - 150 || our_banked() + team_carried() > 900) return false;
    if (!safe_to_channel()) return false;
    // our share of the reward: split among the allies on its tiles, then our gold passive
    int n = 1;
    for (int i = 0; i < (int)ALLY.size(); ++i)
        if (i != MY_SLOT && ALLY[i].vis && ALLY[i].alive && inb(ALLY[i].p) && group_at[idx(ALLY[i].p)] == g.id) ++n;
    const int gems0 = ME.gems, gold0 = ME.gold;
    ME.gems += g.gems / n; ME.gold += (int)(g.gold / n * champ(ME.type).gold_bonus);   // the passive applies after the split
    const bool go = !want_bank() && want_shop();
    ME.gems = gems0; ME.gold = gold0;
    if (!go) return false;
    leave_group_with_ward(g);   // the camp ward the payout branch drops on leaving (prc without it: neutral)
    if (!start_recall()) return false;
    RCOV_G = g.id; RCOV_PAY = r + cs.timer;
    going_home = true;
    return true;
}

// ---- capture in progress: did it pay?
static void update_capture(int r) {
    if (cap_group >= 0) {
        Group& g = G[cap_group];
        g.cap_claim_at = r;   // my own capture in progress
        // Kill income or transferred gems are not proof that this camp paid.
        // observe() reads the group's lifecycle while we stand on its tile.
        // recalled home during the wait; the payout came at the round start (gems in the bag say so)
        const bool rc_away = RCOV_G == g.id && group_at[idx(ME.p)] != g.id;
        const bool rc_paid = rc_away && r >= RCOV_PAY && (g.gems <= 0 || ME.gems > cap_start_gems);
        const bool paid = g.st == ST_RESPAWN || rc_paid;
        if (paid || group_at[idx(ME.p)] != g.id) {
            if (paid) {
                if (g.id == OPEN_G) opening_done = true;
                g.cap_us_at = r;   // my own payout
                if (g.st != ST_RESPAWN) { g.st = ST_RESPAWN; g.ready_at = r + g.respawn; }
                if (!rc_away) leave_group_with_ward(g);   // rcov_start put it down before the recall
                if (JOB == g.id) JOB = -1;
            }
            cap_group = -1;
            set_claim(-1, false);
        }
    }
    if (cap_group < 0) RCOV_G = -1;
}

// ---- an enemy tributing at an altar within our reach: lock it (cancels the channel)
static void lock_winning_tribute() {
    auto& me = bc::self();
    int have = 0;
    try { have = (int)me.consumables()[bc::consumable_type::altar_lock].count(); } catch (const std::exception&) { have = 0; }
    // only a tribute (a recall or a teleport by the altar is not stopped by a lock),
    // and any tribute that would win the game (its bag on top of their score reaches the winning total), not just 36+ gems
    int their_sub = -1;
    for (auto& e : ENEMY) {
        if (have <= 0 || !e.vis || !e.alive || !e.channel) continue;
        if (!e.tribute) continue;
        if (e.gems < LOCK_MIN_TRIBUTE_GEMS) {
            if (their_sub < 0) their_sub = their_banked();
            if (their_sub + e.gems < (int)bc::constants::winning_gems) continue;
        }
        for (auto a : altars) {
            if (cheb(a, e.p) > 1) continue;
            if (cheb(a, ME.p) > 2) continue;
            try {
                auto cons = me.consumables()[bc::consumable_type::altar_lock];
                if (cons.count() > 0 && cons.use(a)) { have = 0; }
            } catch (const std::exception&) {}
            break;
        }
    }
}

// The enemies this turn can see and the balance of the fight around us.
struct EnemyRead {
    Balance bal;
    std::vector<vec2> threats;
    Champ* target = nullptr;
    int campers = 0;   // enemies counted only because they can hit us from their own spawn
    int dmin = 99;

    void read() {
        bal.add_us(ME.hp + my_shield_hp(), my_dps_c());   // HoR passive; my shield as hp
        vec2 bal_ap[8];   // where the allies the balance counts stand
        int bal_an = 0;
        // the allies in this fight: near us or near the enemies we see, in sight or seen within FIGHT_MEMORY_ROUNDS rounds. By "within
        // FIGHT_ALLY_RADIUS of us, in sight" alone the count flipped with our own step: A=2 -> BATTLE, step in, A=1 -> FLEE, step back,
        // every other round (quadrants: a Vesper 10 rounds between (10,36) and (9,35); FIGHT <-> FLEE 157 steps a suite)
        for (int i = 0; i < (int)ALLY.size(); ++i) {
            const Champ& al = ALLY[i];
            if (i == MY_SLOT || !al.alive || al.hp <= 0 || al.channel || al.last_p.x < 0 || rnd() - al.seen_at > FIGHT_MEMORY_ROUNDS) continue;
            const vec2 ap = al.vis ? al.p : al.last_p;
            bool in = cheb(ap, ME.p) <= FIGHT_ALLY_RADIUS;
            for (const auto& e : ENEMY) in = in || (e.vis && e.alive && e.hp > 0 && cheb(e.p, ME.p) <= 7 && cheb(ap, e.p) <= FIGHT_ALLY_RADIUS);
            if (in) { bal.add_us(al.hp + shield_hp(al), champ_dps_c(al)); if (bal_an < 8) bal_ap[bal_an++] = ap; }
        }
        float tk = 1e18f;
        for (auto& e : ENEMY) {
            if (!e.vis || !e.alive || e.hp <= 0 || standoff_ignored(e)) continue;
            const int d = cheb(e.p, ME.p);
            // a spawn camper only counts while it can reach us, and is never a target
            if (on_enemy_spawn(e.p)) {
                const float cd = camper_dps(e, d);   // what it hurts us with from its spawn, no health
                if (cd > 0.f) { bal.add_th(0.f, cd); threats.push_back(e.p); dmin = std::min(dmin, d); ++campers; }
                continue;
            }
            if (d <= 7) {
                bal.add_th(e.hp + shield_hp(e) + heal_hp(e), champ_dps_c(e));   // its shield and a ready heal as hp
                threats.push_back(e.p);
                dmin = std::min(dmin, d);
            }
            // not a target to walk to near its own spawn (three of ours chased a Vendetta on 72
            // and a Marcella on 10 health along the edge to 4-9 of their spawn; both healed there, our Marcella died at 6)
            if (d <= 6 && !(in_es_zone(e.p) && d > attack_reach(ME.melee))) {
                const float key = e.hp - e.gems * 8.f + d * 80.f - (e.channel ? 300.f : 0.f) - (e.hurt ? FOCUS_HURT_BONUS : 0.f);
                if (key < tk) { tk = key; target = &e; }
            }
        }
        // camp aggro by the rules (05-camps.md): an awake monster hits the nearest champion of either team each round, so
        // in a fight beside it the side standing nearest loses its Strength a round, as if the camp fought for the other
        // side (a tie counts against us: "do not count on a tie"; the monolith is kept away from, not fought beside)
        if (!threats.empty()) {
            for (const auto& h : HAZ) {
                if (h.group < 0 || cheb(h.p, ME.p) > 8) continue;
                int du = cheb(h.p, ME.p), dt = 99;
                for (int a = 0; a < bal_an; ++a) du = std::min(du, cheb(h.p, bal_ap[a]));
                for (const auto& tp : threats) dt = std::min(dt, cheb(h.p, tp));
                const float s = h.str;   // its Strength (was (w - 2) x 20)
                if (du <= h.reach && du <= dt) bal.th_dps += s;
                else if (dt <= h.reach && dt < du) bal.us_dps += s;
            }
        }
    }
};

// the plain walks below (job, bank, home) do not step into a ready burst that kills us
static void start_walk_guard() {
    SG_SKIP = 0; SG_ON = false;
    bool sg_seen = false;
    if (!on_own_spawn()) for (auto& e : ENEMY) if (e.vis && e.alive && e.hp > 0 && !standoff_ignored(e) && cheb(e.p, ME.p) <= 8) { sg_seen = true; break; }   // campers too
    if (!sg_seen) SG_HOLDS = 0;
    else if (SG_HOLDS >= 8) { SG_HOLDS = 0; }
    else {
        SG_ON = true;
        SG_HERE = step_burst(ME.p);
        // a plain walk does not step where the burst before our next turn would take us below the heal line (LOWHP's
        // RETREAT_HEALTH of max health), which also covers the lethal case it guarded before (0.9 x health)
        SG_LIM = std::max(0.f, ME.hp - ME.mhp * RETREAT_HEALTH);
    }
}

// ---- health
static bool try_low_health(const EnemyRead& seen) {
    const Balance& bal = seen.bal;
    const std::vector<vec2>& threats = seen.threats;
    const float heal_frac = RETREAT_HEALTH;
    // three elk next to one champion take 180 a round, so 34% of 1000 left one turn to walk out of a reach
    // of 3 (Varg died at 170 on highlands): against camps, leave while three more rounds of the damage just taken
    // still fit in our health
    float heal_line = ME.mhp * heal_frac;
    // the hits an ally takes off us count only while it can keep taking them: health above its own
    // heal line after two rounds of every hitter's Strength (was: above 2 x our last drop). The monsters it does not
    // cover hit us on the walk out whatever we took last round. A 490-health HoR one tile nearer to three elks took
    // their hits for a round, the line fell to 2 x 60, Marcella (660) walked back in, HoR left, and she died on the
    // walk out (official 00eight r23-27)
    float hh_str = 0.f, hh_open = 0.f;
    if (bal.th_n == 0) camp_cover(heal_frac, hh_str, hh_open);
    if ((ME.drop > 0 || hh_open > 0) && bal.th_n == 0) {
        std::vector<vec2> hh;
        hitters_near(hh, 0);
        // the rounds we still take this damage on the walk out of reach, plus the one we are in
        // a camp hits only the nearest champion in its reach. With a healthy ally in view already as
        // near to every monster that reaches us, one step hands the hits over: two rounds of this damage, not the walk out
        bool handed = !hh.empty();
        handed = handed && hh_open <= 0.f;
        const float per = handed ? ME.drop : std::max(ME.drop, hh_open);
        if (!hh.empty()) heal_line = std::max(heal_line, (handed ? 2 : escape_rounds() + 1) * per);
    }
    if (!(ME.hp <= heal_line && !on_own_spawn() && !finish_camp(bal.th_n))) return false;   // at the line, not below it
    set_claim(-1, false);
    JOB = -1; cap_group = -1;
    // home only when it stops the camp damage no later than walking out of reach. The walk home past
    // three elks killed a Varg and a Marcella one step short of it (fracture)
    const int esc_n = escape_rounds();   // 0: nothing awake reaches us
    if (F_HOME.at(ME.p) <= 6 && (esc_n == 0 || F_HOME.at(ME.p) <= esc_n)) {
        // out of reach, a home step back into an awake monster's reach undid the escape every other round
        // (LOWHP HOME / ESC1 ping-pong, 20 rounds): recall from here instead when it is safe
        if (esc_n == 0 && home_step_in_reach() && safe_to_channel() && start_recall()) return true;
        step_field(F_HOME, 0); return true;
    }
    if (safe_to_channel()) { if (start_recall()) return true; }
    // get out of reach of whatever is hitting us
    std::vector<vec2> th = threats;
    hitters_near(th, 1);
    if (th.size() > threats.size()) {
        const bool ok = escape_step();
        if (ok) return true;
    }
    if (!th.empty()) step_away(th); else step_field(F_HOME, 0);
    return true;
}

// ---- on spawn: wait for full health before leaving (healing happens at turn end)
static bool try_wait_on_spawn() {
    // Spawn heals to full at the end of a turn, so one turn here is all that waiting can buy. If I already ended my last turn
    // here and am hurt again, something hit me since (a camp beside the spawn hits before my turn) and will every round: go on.
    static int last_on_spawn = -2;
    if (!on_own_spawn()) return false;
    const bool healed_last_turn = last_on_spawn == rnd() - 1;
    last_on_spawn = rnd();
    if (!healed_last_turn && ME.hp < ME.mhp * 0.9f) {
        // Camps hit before our turn, so a spawn-side siege can keep us below this line forever.
        // Continue an already-damaged assigned camp from this tile; keep its opening/quorum gate and never move.
        if (ME.can_attack && JOB >= 0 && JOB < (int)G.size() && G[JOB].st == ST_ALIVE && G[JOB].hp_left < G[JOB].hp_total) {
            std::vector<Mon*> M;
            for (int k = 0; k < G[JOB].n; ++k)
                if (G[JOB].m[k].vis && G[JOB].m[k].hp > 0) M.push_back(&G[JOB].m[k]);
            Mon* t = pick_target(M, attack_reach(ME.melee));
            if (t) basic_attack_camp(*t);
        }
        return true;
    }
    return false;
}

// ---- capturing has priority over everything but danger
static void capture_turn() {
    const int heir = should_hand_off(G[cap_group]);
    if (heir >= 0) {
        const int gid = cap_group;
        cap_group = -1; set_claim(-1, false);
        if (JOB == gid) JOB = -1;
        step_off_group(G[gid]);
        return;
    }
    if (rcov_start(G[cap_group])) return;
    do_capture(G[cap_group]);
    return;
}

// a flight decided last round holds for FIGHT_MEMORY_ROUNDS rounds: keep away from the enemies we fled, seen or not.
// The flight ends just past the distance that triggers it (or on a tile out of its sight), the job walk stepped
// straight back, and the micro fled again: 1140 such A-B-A steps in 80 suite games (packs: a Varg 12 rounds between
// 6 and 7 tiles of a 2700-hp Vesper; monolith: in and out of a Heart of Ruin's sight). The flights also count for
// the flee-job rule, which drops a job whose way an enemy holds. Before banking too: a carrier fled and walked back to the
// altar the same way (BANK <-> FLEE, 177 steps).
// The same for a fight: the enemy we went for last round is out of sight now (a bush, a corner). Walk to where we saw
// it instead of the job: the job walk stepped back into sight, FINM stepped in again, and a Prism went to and fro
// 92 rounds after a 268-hp enemy Prism (stampede; FIGHT <-> JOB 450 steps in the suite)
static bool try_hold_flight_or_chase(int r) {
    if (flees_since(r - FIGHT_MEMORY_ROUNDS) > 0) {
        std::vector<vec2> fled;
        for (const auto& e : ENEMY) {
            if (!e.alive || e.last_p.x < 0 || on_enemy_spawn(e.last_p) || r - e.seen_at > FIGHT_MEMORY_ROUNDS) continue;
            const vec2 ep = e.vis ? e.p : e.last_p;
            if (cheb(ep, ME.p) <= 8) fled.push_back(ep);
        }
        if (!fled.empty()) { step_away(fled); return true; }
    }
    if (!banking && !want_bank() && r - FIGHT_R <= FIGHT_MEMORY_ROUNDS && FIGHT_E >= 0 && FIGHT_E < (int)ENEMY.size()) {
        const Champ& e = ENEMY[FIGHT_E];
        if (e.alive && !e.vis && e.last_p.x >= 0 && r - e.seen_at <= FIGHT_MEMORY_ROUNDS && !on_enemy_spawn(e.last_p) && !in_es_zone(e.last_p) && cheb(e.last_p, ME.p) <= 8) {
            step_greedy(e.last_p); return true;
        }
    }
    return false;
}

// ---- banking
// a nearly dead camp we are fighting, or a reward beside us, comes before the walk unless the bag is urgent
static bool finish_job_before_bank() {
    if (JOB < 0) return false;
    const Group& g = G[JOB];
    const bool near_done = (g.st == ST_ALIVE && g.hp_left < BANK_FINISH_CAMP_HP && gdist(g, ME.p) <= BANK_FINISH_CAMP_STEPS && ME.gems < BANK_FINISH_MAX_BAG) ||
                           (g.st == ST_REWARD && gdist(g, ME.p) <= BANK_FINISH_REWARD_STEPS);
    return near_done && !bank_urgent();
}
static bool try_bank() {
    if (!(banking || want_bank()) || bank_on_hold() || finish_job_before_bank()) return false;
    set_claim(-1, false);
    do_bank();
    return true;
}

// ---- shopping trip
static bool try_shop_trip(int r, bool late) {
    if (going_home || (!late && want_shop())) {
        bool busy = JOB >= 0 && G[JOB].st == ST_ALIVE && G[JOB].hp_left < G[JOB].hp_total * 0.5f && gdist(G[JOB], ME.p) <= 4;
        if (!busy) {
            set_claim(-1, false);
            if (JOB >= 0) SHOP_NEXT_G = JOB;   // where we go on to after shopping
            JOB = -1;
            going_home = true;
            if (F_HOME.at(ME.p) <= 7) { if (!shop_tile_step()) step_field(F_HOME, 0); return true; }
            if (safe_to_channel()) { if (start_recall()) return true; }
            std::vector<vec2> th;
            for (const auto& g : G) if (r - g.awake_at <= 2) for (int k = 0; k < g.n; ++k) if (g.m[k].hp > 0 && cheb(g.m[k].p, ME.p) <= 4) th.push_back(g.m[k].p);
            if (!th.empty()) step_away(th); else step_field(F_HOME, 0);
            return true;
        }
    }
    return false;
}

// ---- a fight nearby that allies are winning: join it
static bool try_rally() {
    // RALLY guard: not off my camp about to fall (<= 2 of my hits, in reach) or a reward next to me
    bool rally_hold = false;
    if (JOB >= 0 && JOB < (int)G.size()) {
        const Group& jg = G[JOB];
        int dm = 99;
        for (int k = 0; k < jg.n; ++k) if (jg.st != ST_ALIVE || jg.m[k].hp > 0) dm = std::min(dm, cheb(jg.m[k].p, ME.p));
        if (jg.st == ST_ALIVE) rally_hold = dm <= attack_reach(ME.melee) + 1 && jg.hp_left <= 2.f * my_dps_est();
        else if (jg.st == ST_REWARD) rally_hold = dm <= 2;
    }
    if (RALLY_ON() && !rally_hold && beacon_mine_at < 0 && ME.hp >= ME.mhp * 0.6f && ME.gems < 40) {
        const int sq = beacon_group();
        if (sq >= 0 && sq < (int)G.size()) {
            const Group& bg = G[sq];
            // "near" also by the live members (the fight), not only by the group field, which also ends at dead members'
            // tiles: a Vesper 15 tiles from the fight on a long strip read 3 and stood there (RALLY <-> JOB, 245 A-B-A
            // steps in 80 suite games)
            int dl = 99;
            for (int k = 0; k < bg.n; ++k) if (bg.m[k].hp > 0) dl = std::min(dl, cheb(bg.m[k].p, ME.p));
            const int d = std::max(gdist(bg, ME.p), dl);
            if (d <= CALL_ANSWER_RADIUS && cheb(bg.center, ME.p) > 3) {
                const Field* f = gfield(bg);
                if (!(f && step_field(*f, 3))) step_greedy(bg.center);
                return true;
            }
        }
    }
    return false;
}

// ---- an ally is capturing next to us and we are poorer: step on and share the gold
static bool try_share_capture() {
    if (!SQ_ON && ME.hp >= ME.mhp * 0.5f) {
        const int sg = share_target();
        if (sg >= 0) {
            if (do_capture(G[sg])) { JOB = sg; return true; }
        }
    }
    return false;
}

// ---- farming (re-plan on new information or every few turns)
static int last_plan = -100;
static int planned_ready = -1;   // the round the last plan expected its job to respawn (-1: it was not respawning)

// A plan (choose_job + the risk model) costs ~35-55k and was in most mid-game fuel exhaustions (a turn that runs out finishes
// its move a round late, on stale state). A plan that is only due (new information, or the periodic one) waits for a turn
// with REPLAN_GUARD_FUEL left at this point; KNOW_CHANGED stays set and the period stays over, so the next turn asks again.
// Never when the job is bad (or none), never more than REPLAN_GUARD_ROUNDS rounds after the first wait.
static bool plan_due(int r, bool job_bad) {
    static int waiting_since = -1;   // first round of the current chain of waits (-1: none)
    const bool due = job_bad || (KNOW_CHANGED && r - last_plan >= REPLAN_NEWS_ROUNDS) || r - last_plan >= REPLAN_EVERY_ROUNDS;
    if (!due) { waiting_since = -1; return false; }
    if (!job_bad && (long)bc::fuel_remaining() < REPLAN_GUARD_FUEL) {
        if (waiting_since < 0) waiting_since = r;
        if (r - waiting_since < REPLAN_GUARD_ROUNDS) return false;
    }
    waiting_since = -1;
    return true;
}

// Two of us kept swapping the same two jobs every other plan (A, B, A, B...): going back to the job we just left needs it to
// be clearly better than the one we hold.
static bool hold_against_swap_back(int pick, int prev_job, int r, bool job_bad) {
    if (pick < 0 || pick != prev_job || JOB < 0 || r - job_since >= SWAP_BACK_ROUNDS || job_bad) return false;
    const float cur = job_score(G[JOB]), alt = job_score(G[pick]);
    return cur > 0 && alt < cur * SWAP_BACK_MARGIN;
}

static void update_job(int r) {
    // mid_plan.h: a respawn appointment holds the job. The plan still runs on its usual cadence, because it also refreshes
    // team state such as JP_FIGHT; its pick is overridden.
    squad_tick(r);
    if (!SQ_ON) mr_decide(r);
    const bool mr_job = MR_C >= 0;
    if (mr_job && JOB != MR_C) { JOB = MR_C; job_since = r; }
    // A long respawn makes the job bad only if the plan did not already accept that wait: job_score takes waits of 40 rounds
    // after the walk (60 for sieges), and calling those bad re-planned every turn for the same answer.
    const bool job_bad = !mr_job && (JOB < 0 || (G[JOB].st == ST_RESPAWN && G[JOB].ready_at - r > JOB_DROP_WAIT && G[JOB].ready_at != planned_ready) || reward_reserved(G[JOB]) || (SQ_ON && JOB != squad_job()));
    if (!plan_due(r, job_bad)) return;
    int pick = choose_job();
    // mid_plan.h: the appointment keeps its group, except against the jackpot's all-in (the whole team's plan)
    if (MR_C >= 0) { if (DOM_G >= 0 && pick == DOM_G) mr_release(r, 0); else pick = MR_C; }
    last_plan = r;
    KNOW_CHANGED = false;
    static int prev_job = -1;
    if (pick != JOB && !hold_against_swap_back(pick, prev_job, r, job_bad)) { prev_job = JOB; JOB = pick; job_since = r; }
    planned_ready = JOB >= 0 && G[JOB].st == ST_RESPAWN ? G[JOB].ready_at : -1;
}

// the job needs me healed first (its time has the trip home in it): home now, by the shop trip's way. Only on the plan's own
// turn: the flag is as old as the plan that set it, and the next plan reads my health again.
static bool try_heal_before_job(int r) {
    if (last_plan == r && JOB >= 0 && JOB < (int)CC_HEALF.size() && CC_HEALF[(size_t)JOB] && !on_own_spawn()) {
        going_home = true; set_claim(-1, false); JOB = -1;
        if (F_HOME.at(ME.p) <= 7) step_field(F_HOME, 0);
        else if (!(safe_to_channel() && start_recall())) step_field(F_HOME, 0);
        return true;
    }
    return false;
}

static void reward_turn(Group& g, int r) {
    if (group_at[idx(ME.p)] != g.id && taker_coming(g)) {
        // a team plan (TEAMHEAVY) kept sending us back to it for several idle turns: skip it
        g.skip_until = r + 10;
        JOB = -1; set_claim(-1, false); return;
    }
    const int mc = cap_class(ME.type, g);
    if (mc > 0 && !enemy_on_tiles(g)) {
        // a higher capture class takes it even when a lower one stands on it (that one leaves when we join),
        // unless an ally of our class or higher holds it already
        bool taker_on = false, free_tile = group_at[idx(ME.p)] == g.id;
        for (auto& a : ALLY) if (&a != &ALLY[MY_SLOT] && a.vis && a.alive && cap_class(a.type, g) >= mc && inb(a.p) && group_at[idx(a.p)] == g.id) taker_on = true;
        for (int k = 0; k < g.n && !free_tile; ++k) if (!occupied(g.m[k].p)) free_tile = true;
        if (!taker_on && free_tile) { do_capture(g); return; }
    }
    if (ally_on_tiles(g) && !enemy_on_tiles(g)) { JOB = -1; set_claim(-1, false); return; }
    do_capture(g);
    return;
}

// wait near a group, off its spawn tiles: a champion standing on one blocks the whole group's respawn
static void wait_near_group(Group& g) {
    const Field* f = gfield(g);
    if (f && f->at(ME.p) > 2) step_field(*f, 2);
    else if (f && f->at(ME.p) < 2) step_off_group(g);
}

// home and back if the round trip fits into the wait: a walk each way up to 7 tiles, else a recall and the walk back
static bool try_home_in_wait(int hd, int wait_left) {
    const int trip = hd <= 7 ? 2 * hd : (int)bc::constants::recall_time + 1 + hd;
    if (trip > wait_left + 6) return false;
    going_home = true; set_claim(-1, false); JOB = -1;
    if (hd <= 7) step_field(F_HOME, 0); else if (safe_to_channel()) start_recall(); else step_field(F_HOME, 0);
    return true;
}

static void respawn_turn(Group& g, int r, bool late) {
    // a respawn wait is free time: bank or shop if the round trip fits into it
    const int wait_left = g.ready_at - r;
    if (bank_in_respawn_wait(wait_left)) { banking = true; do_bank(); return; }
    const int hd = F_HOME.at(ME.p);
    // to shop, or to heal: a hurt champion keeps farming at low health for hundreds of rounds (and then fails every
    // team camp's soak check), so a long wait is the time to go home
    const bool to_shop = ME.gold >= RESPAWN_SHOP_GOLD && build_pos < BUILD.size(), to_heal = ME.hp < ME.mhp * RESPAWN_HEAL_HEALTH;
    if ((to_shop || to_heal) && !champ(ME.type).remote_shop && hd != UNREACH && !late && try_home_in_wait(hd, wait_left)) return;
    wait_near_group(g);
}

// a camp job turn that neither walked nor hit, six times in a row: skip the group for a while (Vesper stood 30-59 rounds at
// distance 3 of a group an ally was killing, Vendetta 29 rounds next to a gold group)
static void camp_turn(Group& g, int r) {
    JOB_TURN = true;
    fight_group(g);
    if (g.st == ST_ALIVE && !ME.walked && ME.can_attack && !ME.cast_excl) {
        int nv = 0, dv = 99;
        for (int k = 0; k < g.n; ++k) if (g.m[k].vis && g.m[k].hp > 0) { ++nv; dv = std::min(dv, cheb(g.m[k].p, ME.p)); }
        static int fgs_g = -1, fgs_last = -10, fgs_n = 0;
        if (nv > 0 && dv > attack_reach(ME.melee) && ME.can_cast && g.gather_since < 0 && !siege_camp(g) && g.id != GATE_G) {
            if (fgs_g == g.id && r - fgs_last <= 1) ++fgs_n; else { fgs_g = g.id; fgs_n = 1; }
            fgs_last = r;
            if (fgs_n >= 6) {
                g.skip_until = r + 40; JOB = -1; set_claim(-1, false); KNOW_CHANGED = true; fgs_n = 0;
            }
        }
    }
}

static void farm_turn(int r, bool late) {
    update_job(r);
    if (try_heal_before_job(r)) return;
    if (JOB < 0) {
        set_claim(-1, false);
        // no job: wait by the altar
        step_field(F_ALTAR, 2);
        return;
    }
    Group& g = G[JOB];
    ensure_field(g, 40000);
    set_claim(JOB, false);
    if (g.st == ST_REWARD) { reward_turn(g, r); return; }
    if (g.st == ST_RESPAWN) { respawn_turn(g, r, late); return; }
    camp_turn(g, r);
}

static void think() {
    const int r = rnd();
    if (DANGER) set_danger(false);
    update_capture(r);
    if (encl_inside_turn()) return;
    lock_winning_tribute();

    EnemyRead seen;
    seen.read();
    const bool capturing_now = cap_group >= 0 && group_at[idx(ME.p)] == cap_group;
    const int reach = attack_reach(ME.melee);
    beacon_upkeep(seen.bal.th_n > 0);
    // no special branch for spawn campers (it stepped away whenever every counted enemy was a camper: captures dropped,
    // walks ping-ponged in and out of their reach). They are threats like any other: the Balance counts their damage from the
    // spawn, LETHAL and the step guard their burst; they are never a focus or target.
    if (seen.bal.th_n > 0) {
        FightTurn fight{seen.bal, seen.target, seen.campers, seen.dmin, capturing_now, reach, r};
        if (fight.play()) return;
    }

    start_walk_guard();
    SgEnd sg_end;
    if (try_low_health(seen)) return;
    if (try_wait_on_spawn()) return;
    if (capturing_now) { capture_turn(); return; }
    if (try_hold_flight_or_chase(r)) return;
    if (try_bank()) return;
    if (try_break_capture()) return;
    // in the altar's walled pocket (a ring of camps) with no walk home: out by the crystal or a recall
    if (lf::pocket()) return;
    const bool late = r > (int)bc::constants::max_rounds - 150 || our_banked() + team_carried() > 900;
    if (try_shop_trip(r, late)) return;
    if (try_rally()) return;
    if (try_share_capture()) return;
    if (encl_outside_turn()) return;
    farm_turn(r, late);
}
