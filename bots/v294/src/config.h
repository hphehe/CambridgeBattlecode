#pragma once
// Every tuned value of the bot, by topic.

// ---- Job choice
static constexpr int JOB_SCAN_FUEL_FLOOR = 45000;  // after the first 8 camps, stop scoring camps below this much fuel
static constexpr int JOB_SCAN_FUEL_HARD_FLOOR = 25000;  // no camp is scored below this much fuel, whatever the count
static constexpr float JOB_TRAVEL_WEIGHT = 1.4f;  // weight of the walk to a job in its score
static constexpr int JOB_DROP_WAIT = 30;     // a held job whose camp comes back later than this is planned again
static constexpr int JOB_PRICE_WAIT = 40;    // a camp that comes back later than this is not priced
static constexpr int SIEGE_PRICE_WAIT = 60;  // the same for a siege or a Monolith by our spawn
static constexpr int RESPAWN_SHOP_GOLD = 60;     // dead with this much gold: wait for the respawn at home and shop
static constexpr float RESPAWN_HEAL_HEALTH = 0.7f;  // and the same below this share of health
static constexpr int FIGHT_BAG_GEMS = 30;    // a bag this big raises the fight and flee bars
static constexpr int GEO_RERANK_COUNT = 5;  // best jobs re-ranked by what can follow them nearby
static constexpr float GEO_FOLLOWUP_WEIGHT = 1.0f;  // weight of the follow-up job in the two-step rate
static constexpr float GEO_WALK_WEIGHT = 1.0f;  // weight of the walk inside the two-step rate
static constexpr float GEO_FOLLOWUP_FLOOR = 0.5f;  // a follow-up never counts below this times the median positive score
static constexpr float GEO_FOLLOWUP_ROUNDS = 30.f;  // rounds that floor is counted over, also used when no neighbour is worth it
static constexpr int SIDE_JOB_SLACK = 6;  // rounds a side job may run past the wait it fills
static constexpr int SIDE_JOB_MIN_WAIT = 8;  // a best job's respawn wait above this many rounds is worth filling with a side job
static constexpr int OPENING_JOB_UNTIL_ROUND = 80;  // the draft plan's opening job lasts until this round
static constexpr int REPLAN_NEWS_ROUNDS = 2;  // rounds since the last plan before new information calls another
static constexpr int REPLAN_EVERY_ROUNDS = 6;  // rounds since the last plan before one is due anyway
static constexpr int SWAP_BACK_ROUNDS = 8;  // going back to the job just left needs a clear gain for this many rounds
static constexpr float SWAP_BACK_MARGIN = 1.6f;  // that clear gain, as a multiple of the job we hold
static constexpr float JOB_MODEL_LENGTH = 30.f;  // job length the job model was fitted on, in rounds
static constexpr float CAMP_EXIT_DEATH_RATE = 0.17f;  // deaths per walk out of a camp's reach after a camp turn
static constexpr float GEMS_PER_GOLD = 0.213f;  // value of one gold in gems, for monolith sieges and the draft opening
static constexpr float GOLD_WEIGHT_START = 1.04f;  // gold weight at round 0 in gold ROI
static constexpr float GOLD_WEIGHT_DECAY = 368.f;  // rounds over which that weight decays exponentially
static constexpr int GOLD_ROLE_FADE_ROUND = 400;  // the role weight on gold fades to 1 by this round
static constexpr float GOLD_ROLE_NON_TAKER_WEIGHT = 0.4f;  // early weight on gold for a champion that is not a taker
static constexpr int TAKE_SAFE_ROUNDS = 20;  // a camp is not safe to take this many rounds after an enemy was seen there
static constexpr int TAKER_MAX_STEPS = 4;  // the carry takes priority when at most this many steps from the group
static constexpr int BIG_REWARD_GEMS = 60;  // a capture paying this many gems is a big reward

// ---- Getting unstuck
static constexpr int JOB_STUCK_TURNS = 25;  // job turns without progress before the job counts as stuck
static constexpr int JOB_CAPTURE_TIMEOUT = 40;  // a capture not paid after this many rounds is given up
static constexpr int JOB_STUCK_SKIP_ROUNDS = 100;  // rounds this champion skips a group it got stuck on
static constexpr int UNJAM_BLOCKED_TURNS = 6;  // turns a wanted step stays blocked by allies before dropping the job
static constexpr int UNJAM_NO_PROGRESS_TURNS = 12;  // job turns further than 6 tiles without a new best distance before dropping it
static constexpr int BLIND_GIVE_UP_TURNS = 12;  // blind turns next to a group before skipping it for a while
static constexpr int REPLAN_GUARD_FUEL = 60000;  // below this much fuel a healthy job is not re-planned every turn
static constexpr int REPLAN_GUARD_ROUNDS = 3;  // rounds a re-plan may be held back by that guard
static constexpr int PROGRESS_GUARD_TURNS = 12;  // turns with no new best distance to the goal and no hit before the guard acts
static constexpr int JACKPOT_STILL_ROUNDS = 20;  // rounds without progress at a jackpot before skipping it
static constexpr int JACKPOT_SKIP_ROUNDS = 150;  // rounds a jackpot is skipped after that

// ---- Team camps
static constexpr int TEAM_CAMP_BODIES = 3;  // bodies the team waits for at a camp, capped by what the camp needs
static constexpr int TEAM_CAMP_MIN_PARTY = 4;  // team camps only if the plan's party size is at least this
static constexpr int TEAM_CAMP_MIN_BODIES = 5;  // team camps only with this many bodies: with four, parallel farming was better
static constexpr int TEAM_CAMP_MIN_NEED = 2;  // the roaming team only takes groups needing at least this many bodies
static constexpr float TEAM_CAMP_RATE_SHARE = 0.75f;  // share of the gem rate in multi-body camps that makes a map a team-camp map
static constexpr int TEAM_CAMP_UNTIL_ROUND = 150;  // team camps only before this round
static constexpr int TEAM_HEAVY_BODIES = 4;  // bodies a camp needs to count as team-heavy
static constexpr float TEAM_HEAVY_SHARE = 0.5f;  // share of our side's gem rate in such camps
static constexpr int TEAM_HEAVY_MIN_ALTARS = 2;  // only with this many altars: one altar cannot take every team-camp bag
static constexpr int TEAM_HEAVY_UNTIL_ROUND = 100;  // team-heavy camps only before this round, then live staffing decides
static constexpr float CAMP_SOAK_SHARE = 0.55f;  // share of its health each champion may lose to a camp when sizing the team
static constexpr float CAMP_SOAK_SHARE_JUNGLE = 0.75f;  // the same with the Jungle emblem, on groups with no enemy seen lately
static constexpr int CAMP_SOAK_QUIET_ROUNDS = 60;  // rounds without an enemy seen that count as lately
static constexpr int CALC_REREAD_ROUNDS = 10;  // rounds between reads of a body's items and cooldowns
static constexpr int CALC_KEEP_ROUNDS = 6;  // rounds a camp calculation is reused while nothing relevant changed
static constexpr int CALC_KEEP_ROUNDS_LOW_FUEL = 20;  // rounds it is reused when fuel is low
static constexpr int JACKPOT_SIM_DPS_PERCENT = 100;  // percent of base damage the jackpot fight sim credits
static constexpr int JACKPOT_WAIT_ROUNDS = 10;  // rounds of waiting at a jackpot before starting with the bodies here
static constexpr int ALLIN_FROM_ROUND = 200;  // all-in on the jackpot only from this round, after gold and a first shop
static constexpr int ALLIN_RECHECK_ROUNDS = 25;  // rounds between re-runs of the jackpot fight sim

// ---- Sharing a capture
static constexpr int SHARE_MAX_STEPS = 3;  // join an ally's capture if a free spawn tile is at most this many steps away
static constexpr int SHARE_SHARD_GAP = 3;  // and we own this many shards fewer than the capturer, gold / 30 included
static constexpr int HANDOFF_SHARD_GAP = (SHARE_SHARD_GAP + 2);  // the capturer leaves once it owns this many shards more than the poorer ally

// ---- Monoliths
static constexpr float MONOLITH_ONLY_SHARE = 0.999f;  // a map counts as monolith-only when this share of its gems sit in monoliths
static constexpr int MONOLITH_QUORUM = 4;  // bodies a healthy monolith needs before committing
static constexpr int MONOLITH_LOW_HEALTH = 2500;  // a monolith at or below this health no longer needs the quorum
static constexpr int MONOLITH_HOLD_MAX = 30;  // longest hold in rounds at a monolith
static constexpr float MONOLITH_MAX_ORDINARY_RATE = 1.5f;  // ordinary gem rate up to which a mixed map still plays for monoliths
static constexpr int MONOLITH_HELPER_STALE = 10;  // rounds a helper's remembered position counts

// ---- Banking and tributes
static constexpr int BANK_BASE_GEMS = 54;  // bank once carrying this many gems
static constexpr float BANK_GEMS_PER_STEP = 1.03f;  // plus this many per step to the nearest altar
static constexpr int BANK_MAX_GEMS = 100;  // but never wait for more than this
static constexpr int BANK_NO_PROGRESS_TURNS = 8;  // a bank walk with no new best altar distance for this many turns is dropped
static constexpr int BANK_RETRY_ROUNDS = 40;  // rounds before banking may start again after that
static constexpr int ENEMY_NEAR_WIN_GEMS = 850;  // the enemy has banked this many: every bag worth a trip goes now
static constexpr int ENEMY_NEAR_WIN_BAG = 10;  // a bag worth that trip
static constexpr int CLOCK_MARGIN_ROUNDS = 8;  // rounds spare when the walk and tribute would just finish before the game ends
static constexpr int BANK_FINISH_CAMP_HP = 300;  // a camp this close to dead is finished before the bank walk
static constexpr int BANK_FINISH_CAMP_STEPS = 3;  // if it is this near
static constexpr int BANK_FINISH_MAX_BAG = 150;  // and the bag is not bigger than this
static constexpr int BANK_FINISH_REWARD_STEPS = 2;  // a reward this near is taken first
static constexpr int HIDDEN_FIRE_HOLD_ROUNDS = 20;  // rounds a carrier stays off the altar after a hidden enemy broke its tribute
static constexpr int ALTAR_REBUILD_ROUNDS = 2;  // rounds a changed altar set may wait before the field is rebuilt
static constexpr int TRIBUTE_PAYS_BAG = 600;  // a bag this large is worth the tribute
static constexpr int TRIBUTE_PAYS_TOTAL = 1000;  // as is a tribute that takes our banked gems to this total
static constexpr int CONTESTED_BANK_GEMS = 30;  // contested altar maps: gems that are enough to bank
static constexpr int CONTESTED_BANK_STEPS = 12;  // only while the altar is at most this many steps away
static constexpr float CONTESTED_ALTAR_RATIO = 0.85f;  // an altar is contested when our path is at least this fraction of theirs
static constexpr int LOCK_MIN_TRIBUTE_GEMS = 36;  // lock an altar only on enemy tributes holding at least this many gems
static constexpr int LOCK_OWN_TRIBUTE_GEMS = 44;  // lock our own altar when starting a tribute with this many gems
static constexpr int TRIBUTE_CANCEL_GEMS = 20;  // a tribute holding this many gems is cancelled if visible enemies could kill us
static constexpr float TRIBUTE_CANCEL_BURST = 0.9f;  // also cancelled with 3+ turns left if their burst reaches this share of our health
static constexpr int CARRIER_GEMS = 60;  // carrying this many gems we avoid optional fights
static constexpr int ENCLAVE_BANK_GEMS = 48;  // a dash champion in an enclave leaves to bank at this many gems
static constexpr int ENCLAVE_WAIT_ROUNDS = 14;  // it waits for a respawn this many rounds away
static constexpr int ENCLAVE_MAX_STEPS = 150;  // it goes there from at most this many steps away

// ---- Shopping and gold
static constexpr float SHOP_TRIP_MARGIN = 1.11f;  // the gain from shopping must exceed the trip cost by this factor
static constexpr int SHOP_NEAR_SPAWN_STEPS = 3;  // this close to our spawn, shop once the next item is affordable
static constexpr float SHOP_GAIN_CASTER = 2.0f;  // shopping gain multiplier for casters and Ren
static constexpr float GOLD_PRIO_STELLAR = 3.0f;  // Stellar counts as this many times poorer than she is
static constexpr float GOLD_PRIO_REN = 2.0f;  // the same for Ren
static constexpr float GOLD_PRIO_CASTER = 1.5f;  // the same for Prism, Tavi and Cassi
static constexpr float HEALTH_BALANCE_RATIO = 2.0f;  // buy health when its gain beats damage by this ratio
static constexpr int BUSH_WARD_KEEP_ROUNDS = 30;  // rounds before a bush ward may be moved
static constexpr int BUSH_WARD_MIN_GAP = 4;  // a new bush ward must be more than this many tiles from the current one

// ---- Fighting
static constexpr float RETREAT_HEALTH = 0.34f;  // retreat and recall below this share of max health
static constexpr float STOP_FIGHT_HEALTH = 0.30f;  // stop fighting below this share of health
static constexpr float FIGHT_ADV_SMALL = 0.0f;  // start a fight at this advantage with fewer than 3 of us
static constexpr float FIGHT_ADV_TEAM = -0.10f;  // the same with 3 or more of us
static constexpr float FLEE_ADV_SMALL = 0.50f;  // flee below this advantage with fewer than 3 of us
static constexpr float FLEE_ADV_THREE = 0.50f;  // the same with 3 of us
static constexpr float FLEE_ADV_FOUR = 0.50f;  // the same with 4 or more of us
static constexpr int FIGHT_ALLY_RADIUS = 5;  // allies within this many tiles count in the fight
static constexpr int FIGHT_MEMORY_ROUNDS = 2;  // allies seen this recently count, and a flight holds this long
static constexpr float LETHAL_BURST_SHARE = 0.9f;  // flee when their possible burst reaches this share of our health
static constexpr float LETHAL_BURST_SHARE_CARRIER = 0.5f;  // the same for a carrier
static constexpr int STANDOFF_TURNS = 12;  // standoff: this many rounds with no hit, no damage taken and no move
static constexpr int STANDOFF_IGNORE_ROUNDS = 20;  // then enemies within 7 are left out of the fight logic for this long
static constexpr int STANDOFF_MIN_DISTANCE = 4;  // while they stay more than this many tiles away
static constexpr int JOIN_RADIUS = 7;  // melee join: an ally's fight with an enemy this close to us
static constexpr float JOIN_MIN_HEALTH = 0.5f;  // only with at least this share of our health
static constexpr int SCREEN_WAIT_TURNS = 3;  // a ranged champion waits at most this many turns for a melee screen
static constexpr int CALL_NEAR_RADIUS = 5;  // call for help: an ally with 40% health within this radius counts as near
static constexpr int CALL_ANSWER_RADIUS = 12;  // healthy free allies answer the call from this far
static constexpr int TEAM_KILL_RADIUS = 8;  // allies within this radius of the target count for a team kill
static constexpr float TEAM_KILL_HEALTH_FACTOR = 1.0f;  // damage needed for a team kill, as a multiple of the target's health
static constexpr float TEAM_KILL_PULL = 150.f;  // micro pull to the target per tile
static constexpr int TEAM_KILL_CAMP_HEALTH = 600;  // no team kill while our camp job within 3 is at or below this health
static constexpr float FOCUS_HURT_BONUS = 200.f;  // focus bonus, in health, for an enemy that lost health since our last read
static constexpr int SAVE_CAST_RADIUS = 8;  // spell saves look this many tiles away, the vision range
static constexpr float FINISH_BAG_DOUBLES = 100.f;  // gems that double the value of a kill or of our own death
static constexpr float FINISH_MIN_GAIN = 0.10f;  // gain the finish model needs to commit to the fight
static constexpr float SOLO_KILL_BONUS = 2000.f;  // micro score bonus for the end tile of a solo kill
static constexpr float MICRO_DAMAGE_WEIGHT = 1.0f;  // micro: weight of damage taken against the value of what we hit
static constexpr float POKE_DAMAGE_WEIGHT = 1.0f;  // the same in poke micro
static constexpr float WAKE_COST_ROUNDS = 3.f;  // rounds of a woken camp's Strength charged to a tile, at the micro's damage weight
static constexpr int HOR_LANDING_CANDIDATES = 3;  // Heart of Ruin landings scored before the walks
static constexpr int DASH_CANDIDATES = 3;  // dash candidates scored before the walks
static constexpr int ENEMY_SPAWN_RADIUS = 7;  // tiles around the enemy spawn, at most a third of the spawn gap, treated as theirs

// ---- Learned models
static constexpr float FIGHT_MODEL_MARGIN = 0.02f;  // delta the fight model needs to overrule the hand rule
static constexpr float FIGHT_MODEL_MARGIN_CARRY = 0.02f;  // extra margin to fight while carrying 30 or more gems
static constexpr int QMODEL_START_FUEL = 45000;  // fuel needed to start the fight choice model, else the hand-written micro runs
static constexpr long QMODEL_TILE_FUEL_GATE = 45000L;  // fuel needed for one enemy, plus 2.5k per extra enemy
static constexpr int QMODEL_FUEL_MIN = 24000;  // stop adding options below this much fuel
static constexpr int QMODEL_DASH_FUEL_MIN = 30000;  // skip the remaining dash tiles below this much fuel
static constexpr float QMODEL_SUPPORT = 0.10f;  // options in the support have at least this times the best probability
static constexpr float QMODEL_TRUST_GAIN = 0.10f;  // take the model's plan only when its value is this much higher

// ---- Mid appointments
static constexpr int MID_RICH_GEMS = 40;  // gems a group pays to count as rich
static constexpr int MID_SQUAD_SIZE = 3;  // squad size
static constexpr float MID_MIN_HEALTH = 0.6f;  // below this share of max health a body heals at home first
static constexpr float MID_WIN_INTERCEPT = -0.3397f;  // P(next capture is ours) = 1 / (1 + exp(-(A + B * edge))), edge = our bodies minus theirs
static constexpr float MID_WIN_SLOPE = 0.759f;  // B in that formula
static constexpr float MID_ENEMY_PRIOR = 1.24f;  // their bodies at a retake's respawn when we saw none lately
static constexpr int MID_MAX_WALK = 30;  // farthest a body joins from, in rounds of walking
static constexpr int MID_LEADER_LEAD = 30;  // the leader claims this many rounds before the respawn
static constexpr int MID_MAX_WAIT = 45;  // longest wait for a respawn
static constexpr int MID_ABORT_EDGE = 2;  // leave when the enemy has this many more bodies at the group
static constexpr int MID_TIMEOUT = 90;  // an appointment is released after this many rounds

// ---- Draft simulation
static constexpr int PRESIM_ROUNDS = 200;  // rounds simulated; there are no champion fights in the sim, so later rounds drift
static constexpr long PRESIM_FUEL_FLOOR = 600000L;  // the sim stops below this much fuel
static constexpr float PRESIM_SHOP_BASE = 30.f;  // a simulated body shops at gold >= base + factor * trip cost
static constexpr float PRESIM_SHOP_TRIP_FACTOR = 3.f;  // the factor in that rule
static constexpr int DRAFT_ALTAR_BYTES = 560;  // offset in draft memory of the sim's per-altar data
static constexpr int ALTAR_ENEMY_VISITED = 10;  // enemy bodies near an altar per sim round, x100, that make it enemy-visited
static constexpr int LONE_ALLY_RADIUS = 8;  // a sim body is not alone with an ally's group within this many tiles
static constexpr int LONE_SIDE_RATIO = 9;  // off our side means d_home * 10 >= d_away * this
static constexpr float LONE_PENALTY = 0.75f;  // score multiplier for a lone body off our side
static constexpr int LONE_UNTIL_ROUND = 100000;  // that penalty applies before this round

// ---- Champion values
static constexpr float REN_STUN_VALUE = 1.1f;  // value of Ren's stun
static constexpr float TAVI_STUN_VALUE = 1.6f;  // value of Tavi's stun
static constexpr float HOR_LANDING_VALUE = 1.5f;  // value of a Heart of Ruin landing
