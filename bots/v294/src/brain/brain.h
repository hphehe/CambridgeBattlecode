// the decision layer: safety, fights, banking, shopping, job choice and capture.
#pragma once
#include "../camps/fight.h"
#include "../micro/micro.h"
#include "shop.h"
#include "../draft/plan.h"

static int beacon_mine_at = -1;   // rally beacon: last round we (re)raised it; -1 if it is not ours
static int JOB = -1;            // current camp group
static bool JOB_TURN = false;   // UNJAM_NO_PROGRESS_TURNS: this turn went to fight_group for the job
static int job_since = 0;
static int cap_group = -1;      // group we are standing on to capture
static int cap_start_gems = 0, cap_since = 0;
static int RCOV_G = -1, RCOV_PAY = -1;   // the capture we recalled from, and the round it pays
static bool banking = false;
static bool going_home = false;  // walking/recalling to shop or heal
static int turn_no = 0;
static bool opening_done = false;
static int team_camp_group();   // below: the roaming team's group
static int chase_turns = 0, no_chase_until = 0, battle_until = -1;
static int scr_wait = 0;           // turns in a row a ranged champion waited for a melee screen
static bool call_beacon = false;   // the beacon we hold is a call for help, not a battle

static int our_banked() { return (int)bc::global_submitted_gems(team::ally); }
static int their_banked() { return (int)bc::global_submitted_gems(team::enemy); }
#include "gold_weight.h"

static float my_dps_est() {
    const auto design = champion_design(ME.type);
    const float base = base_dps(ME.type);
    float d;
    if (champ(ME.type).dps_scales_focus)
        d = base * (0.4f + 0.6f * ME.focus / design.f);
    else
        // base_dps's own model at the live stats. Marcella's and HoR's Focus term (bd_f) grows with their
        // Focus builds but was scaled by Strength (Marcella with 984 Focus: 125 for 342); with no Focus term it is the same
        d = ME.str * champ(ME.type).bd_s1 * champ(ME.type).bd_s2 + champ(ME.type).bd_f * ME.focus / 8.f;
    return std::max(10.f, d) * my_mult();   // HoR passive
}
static float ally_dps_est(CT t) { return base_dps(t) * (1.f + std::min(1.5f, rnd() / 600.f)); }

#include "brain_safety.h"  // Channel safety, carrier policy, escape and teleport routing.
#include "brain_economy.h"  // Shopping value and camp staffing feasibility.
#include "squad.h"  // The team opening: every body walks its squad's camp list.
#include "team_counter.h"  // Committed / safe enemy flag from our camp contests.
#include "../models/jm.h"  // Job model: P(caught) for jobs and stay / leave near enemies, measured on ladder games.
#include "../models/wmodel.h"  // state-dependent prices for job choice (gold, carried bag, death, kill)
#include "brain_job_score.h"  // Objective scoring and risk adjustment.
#include "brain_jobs.h"  // Team allocation, leader coordination and job selection.
#include "brain_objectives.h"  // Capture, gem sharing, warding and enclosed objectives.
#include "brain_threat.h"  // Visible and remembered enemy burst estimates for movement.
#include "mid_plan.h"  // Respawn appointment for rich contested camps.
#include "brain_turn.h"  // Turn priorities: safety, combat, delivery, recovery, then objectives.
