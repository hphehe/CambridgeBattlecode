#include "config.h"

// Cambridge Battlecode 2026a bot.
// Each champion farms the camp group with the best gems-per-round estimate, coordinating through
// claims in shared memory, banks when its bag justifies the altar trip, shops on recall, and
// fights enemy champions only when the local power balance favours it.
#include "brain/brain.h"
#include "game/sdk_contract.h"
#include "draft/draft.h"
#include "game/turn_order.h"   // our order inside the team for the next round

static bool inited = false;
static int init_stage = 0;

// Spend spare fuel on distance fields for the groups nearest to us.
static void precompute_fields() {
    if (bc::fuel_remaining() < 60000) return;
    int best = -1, bd = 1 << 30;
    for (auto& g : G) {
        if (g.fid >= 0) continue;
        const int d = gdist(g, ME.p);
        if (d < bd) { bd = d; best = g.id; }
    }
    if (best >= 0) ensure_field(G[best], 40000);
}

// ---- The stuck guards, after the turn's own rules. Movement rules each pick one step a turn with no memory; each guard
// watches one way of getting nowhere and gives the job up for a while, so the planner chooses again. Order matters: each
// reads the job the one before it left.
static void drop_job() { JOB = -1; set_claim(-1, false); KNOW_CHANGED = true; }
static void skip_job_until(int round) { G[JOB].skip_until = round; drop_job(); }

// walking to a far job without getting any nearer for many turns (the path runs through a living camp of another group, or
// the step and the detour undo each other)
static void guard_far_job(int r) {
    static int up_job = -1, up_best = 1 << 30, up_n = 0;
    const int d = JOB_TURN && JOB >= 0 && JOB < (int)G.size() ? gdist(G[JOB], ME.p) : -1;
    if (d < 0 || d <= 6 || JOB != up_job) { up_job = d > 6 ? JOB : -1; up_best = d > 6 ? d : 1 << 30; up_n = 0; }
    else if (d < up_best) { up_best = d; up_n = 0; }
    else if (++up_n >= UNJAM_NO_PROGRESS_TURNS) {
        skip_job_until(r + 40);
        up_job = -1; up_best = 1 << 30; up_n = 0;
    }
}

// a job group next to us (within 2) whose health has not moved for JOB_STUCK_TURNS job turns is not being worked, whatever
// the reason (stale memory, a target we cannot hit)
static void guard_job_not_hurt(int r) {
    static int js_g = -1, js_last = -100, js_n = 0;
    static float js_hp = -1.f;
    const bool on = JOB_TURN && JOB >= 0 && JOB < (int)G.size() && G[JOB].st == ST_ALIVE &&
                    !siege_camp(G[JOB]) && gdist(G[JOB], ME.p) <= 2;   // a siege has its own give-up rules
    if (!on) { if (r - js_last > 3) js_g = -1; }
    else if (JOB != js_g || r - js_last > 3 || std::fabs(G[JOB].hp_left - js_hp) > 0.5f) {
        js_g = JOB; js_n = 0; js_hp = G[JOB].hp_left; js_last = r;
    } else {
        js_last = r;
        if (++js_n >= JOB_STUCK_TURNS) {
            skip_job_until(r + JOB_STUCK_SKIP_ROUNDS);
            js_g = -1; js_n = 0;
        }
    }
}

// a capture that has not paid in JOB_CAPTURE_TIMEOUT rounds (an enemy on a far tile of a long group contests it the whole
// time; a normal capture pays in 6). An enemy in sight is a stand-off over the reward, not a deadlock: leaving hands it over.
static void guard_capture_timeout(int r) {
    if (cap_group < 0 || cap_group >= (int)G.size() || r - cap_since <= JOB_CAPTURE_TIMEOUT || group_at[idx(ME.p)] != cap_group) return;
    for (auto& e : ENEMY) if (e.vis && e.alive && cheb(e.p, ME.p) <= 9) return;
    Group& cg = G[cap_group];
    cg.skip_until = r + JOB_STUCK_SKIP_ROUNDS;
    if (JOB == cap_group) JOB = -1;
    cap_group = -1; set_claim(-1, false); KNOW_CHANGED = true;
    if (ME.can_move) step_off_group(cg);
}

// waiting at a group whose respawn timer has run out: a unit on one of its tiles holds the respawn back
static void guard_blocked_respawn(int r) {
    static int rw_g = -1, rw_n = 0, rw_last = -100;
    if (JOB < 0 || JOB >= (int)G.size() || G[JOB].st != ST_RESPAWN || r < G[JOB].ready_at || gdist(G[JOB], ME.p) > 3) return;
    if (JOB != rw_g || r - rw_last > 3) { rw_g = JOB; rw_n = 0; }
    rw_last = r;
    if (++rw_n >= JOB_STUCK_TURNS) {
        skip_job_until(r + JOB_STUCK_SKIP_ROUNDS);
        rw_g = -1; rw_n = 0;
    }
}

// the step we want has been blocked for several turns in a row (allies facing each other in a corridor one tile wide, a
// chain of bodies at a door): one of us backs out
static void guard_blocked_step(int r) {
    static vec2 jam_p{-1, -1};
    static int jam_n = 0;
    if (SF_BLOCKED && !ME.walked && ME.p == jam_p && JOB >= 0 && JOB < (int)G.size()) {
        if (++jam_n >= UNJAM_BLOCKED_TURNS) {
            skip_job_until(r + 20 + 5 * MY_SLOT);
            jam_n = 0;
        }
    } else if (!SF_BLOCKED || ME.p != jam_p) jam_n = 0;
    jam_p = ME.p;
}

// the guard for the whole stuck and ping-pong family. Two movement rules that disagree make a loop of any size (job walk v
// flee, greedy v field, bank v off-spawn, low-health home v escape), and an unreachable goal leaves us standing. Progress is
// measured on the goal itself: if the walking distance to the job (or to an altar while banking) has not reached a new best
// for PROGRESS_GUARD_TURNS rounds and we hit nothing in that time, the goal is blocked. Waits that are the plan are left
// alone: a gather, a respawn, a siege, a capture, a channel.
static void guard_no_progress(int r, const bc::champion_state& st) {
    static int pg_key = -2, pg_best = 1 << 30, pg_best_r = 0;
    int key = -2, d = 1 << 30;
    if (cap_group >= 0 || st.contains(bc::champion_state::recalling) || st.contains(bc::champion_state::submitting_gems) || on_own_spawn()) key = -2;
    else if (JOB >= 0 && JOB < (int)G.size()) {
        const Group& g = G[JOB];
        if (g.st == ST_ALIVE && g.gather_since < 0 && !siege_camp(g) && MR_C != JOB) { key = JOB; d = gdist(g, ME.p); }
    } else if (banking && F_ALTAR.ok()) { key = -1; d = bank_walk_distance(); }
    if (key != pg_key) { pg_key = key; pg_best = d; pg_best_r = r; }
    else if (d < pg_best) { pg_best = d; pg_best_r = r; }
    const bool acting = r - LAST_CAMP_HIT <= PROGRESS_GUARD_TURNS || r - LAST_CHAMP_HIT <= PROGRESS_GUARD_TURNS;
    if (key != -2 && !acting && r - pg_best_r >= PROGRESS_GUARD_TURNS && d > 1) {
        if (key >= 0) skip_job_until(r + 20);
        else hold_banking(r);
        pg_key = -2;
    }
}

// never stand on the spawn tiles of a respawning group unless capturing: it blocks the respawn for everyone. Only when the
// respawn is due: standing there earlier blocks nothing, and a bank walk onto the tile stepped off and back every round.
static void step_off_due_respawn(int r) {
    if (ME.walked || !ME.can_move || cap_group >= 0) return;
    const int gi = group_at[idx(ME.p)];
    if (gi >= 0 && G[gi].st == ST_RESPAWN && G[gi].ready_at - r <= 2 && !banking) step_off_group(G[gi]);
}

static void play_turn() {
    auto& me = bc::self();
    ++turn_no;
    ROUND = (int)bc::round_number();
    if (!inited) {
        if (init_stage == 0) { init_static(); init_stage = 1; }
        if ((long)bc::fuel_remaining() < 85000) { return; }
        if (init_stage == 1) { init_me(); read_plan(); init_build(); init_stage = 2; }
        if ((long)bc::fuel_remaining() < 45000) { return; }
        if (init_stage == 2) { init_allies(); init_stage = 3; }
        if ((long)bc::fuel_remaining() >= 70000) qm::init();   // about 42k: keep it out of the first fight turn
        inited = true;
    } else if (init_stage == 3) {
        init_altar_field(); init_stage = 4;
    } else if (init_stage == 4) {
        if (!PLAN_OK) init_away_field();
        init_stage = 5;
    } else if (!enemies_loaded) {
        init_enemies();
    }
    const int r = rnd();
    // the first-turn set-up (fields, camp groups) can use nearly all of the first turn's fuel on a large or
    // dense map; acting on what is left ran out mid-think and finished the move a turn late on stale state. Wait instead.
    if ((long)bc::fuel_remaining() < 25000) { return; }
    sm_read();
    infer_from_claims();
    if (init_stage >= 4) { try { mr_rec_tick(r, me.is_alive()); } catch (const std::exception&) {} }
    if (!me.is_alive()) {
        report_gems(0);
        set_claim(-1, false);
        JOB = -1; cap_group = -1; banking = false; BANK_DEST = vec2{-1, -1};
        ME.gold = (int)me.gold_count();
        if (RALLY_ON() && beacon_mine_at >= 0) drop_beacon();
        if (init_stage >= 4) tc_dead();
        shop_now();
        return;
    }
    observe();
    if (init_stage >= 4) TC_OBS_R = r;   // the team counter ticks at the end of this turn
    if (init_stage >= 4) {
        gate_sm_tick();
        refresh_altar_field_deferred(r);
        if (banking && !bank_reachable()) banking = false;
    }
    build_hazards();
    report_gems(ME.gems);
    if (init_stage >= 4) gate_tick();
    // observe() already captured this pre-action state; a successful cancel refreshes ME below.
    const auto st = bc::champion_state::from_bits_retain(ME.state);
    static int ch_last_hp = -1, ch_last_r = -10;
    const int ch_dmg = (ch_last_hp >= 0 && r - ch_last_r == 1) ? ch_last_hp - (int)ME.hp : 0;
    ch_last_hp = (int)ME.hp; ch_last_r = r;
    bool ch_cancelled = false;
    float ch_burst = 0.f;
    int rem = 0;   // turns left on the tribute
    if (st.contains(bc::champion_state::submitting_gems) && ME.gems >= TRIBUTE_CANCEL_GEMS) {
        try { rem = (int)me.remaining_channel_time(); } catch (const std::exception&) { rem = 0; }
        ch_burst = tribute_burst(ME.p, rem);
    }
    if (st.contains(bc::champion_state::submitting_gems) && ME.gems >= TRIBUTE_CANCEL_GEMS && (ch_dmg > 0 || ch_burst > 0.f)) {
        // the bag stays on us until the tribute ends; dying first hands it to them
        // the damage rate over every turn left, not only the next two: a 235-gem tribute (24 turns) took 256 a
        // round from a Vesper for 9 rounds and was cancelled at 191 hp, too late to walk away (2.2 = rem 2 + 0.2)
        if ((rem >= 2 && (float)ME.hp <= ch_dmg * (rem + 0.2f)) || (rem >= 3 && ch_burst >= ME.hp * TRIBUTE_CANCEL_BURST)) {
            try { me.cancel_submit_gems(); ch_cancelled = true; }
            catch (const std::exception&) {}
            if (ch_cancelled) FORCE_FLEE_UNTIL = r + 1;
            if (ch_cancelled) { refresh_flags(); note_hidden_fire(r); }
        }
    }
    // the bait recall we started at the end of last turn is cancelled and the turn is played. This runs before
    // a bait is no trip home caught under fire, so it must not set FORCE_FLEE (fk7c fled 20 extra turns a game)
    // a flag, not the round: a turn suspended after FAKE-RC (the spare-fuel work) finishes in round r+1, so the next
    // turn is r+2 and a round check skipped the cancel; the recall then took us home from mid-map (~1 a game)
    static bool bait = false;
    const bool was_bait = bait;
    bait = false;
    if (!ch_cancelled && was_bait && st.contains(bc::champion_state::recalling)) {
        // 2: keep it only where our own rules recall this turn: low health (LOWHP), a shop or heal trip (going_home), or a
        // retreat below 0.6 (DANGER still holds last turn's value here); each of them recalls when far and safe
        const bool home_now = ME.hp < ME.mhp * RETREAT_HEALTH || going_home || (DANGER && ME.hp < ME.mhp * 0.6f);
        if (!(home_now && F_HOME.at(ME.p) > 8 && safe_to_channel())) {
            try { me.cancel_recall(); ch_cancelled = true; } catch (const std::exception&) {}
            if (ch_cancelled) refresh_flags();
        }
    }
    if (!ch_cancelled && st.contains(bc::champion_state::recalling)) {
        const float rb = step_burst(ME.p);
        if (rb >= ME.hp * LETHAL_BURST_SHARE) {
            try { me.cancel_recall(); ch_cancelled = true; }
            catch (const std::exception&) {}
            if (ch_cancelled) { FORCE_FLEE_UNTIL = r + 1; refresh_flags(); }
        }
    }
    if (!ch_cancelled && is_channelling(st)) return;
    if (on_own_spawn() || champ(ME.type).remote_shop) { shop_now(); if (on_own_spawn()) { going_home = false; SHOP_NEXT_G = -1; } }
    // passing next to our spawn with gold: step on and shop
    // (from the next item's price, not 60 gold, and onto the spawn tile on the way to the job)
    if (!on_own_spawn() && !champ(ME.type).remote_shop && build_pos < BUILD.size() && F_HOME.at(ME.p) == 1 &&
        ME.gold >= (int)next_item_price()) {
        if ((shop_tile_step() || step_field(F_HOME, 0)) && on_own_spawn()) { shop_now(); going_home = false; }
    }
    SF_BLOCKED = false;
    JOB_TURN = false;
    jm_observe(rnd());
    SHA_LATE = false;
    think();
    if (SHA_LATE) {
        // the shield ahead a dash champion held for the fight plan: up now if the turn left the cast unused and no
        // channel started (a cast would break it)
        if (!is_channelling(me.state())) shield_late();
    }
    guard_far_job(r);
    guard_job_not_hurt(r);
    guard_capture_timeout(r);
    guard_blocked_respawn(r);
    guard_blocked_step(r);
    guard_no_progress(r, st);
    step_off_due_respawn(r);
    bush_ward();
    // the last action of the turn is a bait recall (a channeller can do nothing else: the wards and steps above
    // went missing behind it) unless a real channel runs (not while capturing: a channeller may not contest)
    if (!on_own_spawn() && !(my_claim & CLAIM_CAPTURING) && !is_channelling(me.state())) {
        try { me.recall(); bait = true; } catch (const std::exception&) {}
    }
    // the turn's actions are done; ask for next round's order before the spare-fuel users below drain the budget
    try { torder::update(); } catch (const std::exception&) {}
    if (init_stage >= 4) { try { mr_tick(r); } catch (const std::exception&) {} }   // mid_plan.h
    if (init_stage >= 4) altar_spare();   // first of the spare-fuel users (banking needs it)
    if (turn_no > 1) precompute_fields();
    if (!bc::is_draft()) jm_spare();   // job model: its copy of the away field, once, from spare fuel (jm.h)
    if (init_stage >= 5 && turn_no > 1) lf::work(JOB >= 0 && JOB < (int)G.size() ? G[JOB].fid : -1);   // last: drains spare fuel to 15k
}

int main() {
    while (true) {
        try {
            if (bc::is_draft()) play_draft();
            else play_turn();
        } catch (const std::exception& e) {
        }
        // a turn that returned early (channelling, dead, an exception) has not asked yet
        if (!bc::is_draft() && torder::DONE_R != rnd()) { try { torder::update(); } catch (const std::exception&) {} }
        // bank engine: after every decision, when the turn has the fuel for it (out of fuel = the next round lost)
        if (!bc::is_draft() && (long)bc::fuel_remaining() >= 4000) { try { be_end_turn(); } catch (const std::exception&) {} }
        // team counter (team_counter.h): after every decision, channelling turns too; fuel spent before the decisions
        // costs (memory fuel-sensitivity), so its flag is read on the next turn
        if (!bc::is_draft() && TC_OBS_R == rnd() && (long)bc::fuel_remaining() >= 3000) { try { tc_tick(rnd()); } catch (const std::exception&) {} }
        bc::end_turn();
    }
}
