#pragma once

static bool QM_WARD_ON = false;   // we have placed a ward this game (state feature ward_on)
static bool QM_NOSH = false;      // the model chose not to shield: micro_exec skips its automatic shield

enum MicroMode { MM_BATTLE = 0, MM_FLEE = 1, MM_HOLD = 2, MM_POKE = 3 };

// turns enemy e takes between this turn of ours and the next. Champions alternate one by one, the team
// that starts swaps every round (team 0 starts odd rounds), inside a team higher priority goes first, then draft order.
// Each side's rank is taken for this round (prev_priority) and the next (priority: what a champion asked for in its
// turn). Ours: our own reads and the level we will ask for at the end of this turn; allies while
// visible, out of sight their DANGER bit (level 1).
static int team_pos(int rank, bool first, int n_other) { return rank < n_other ? 2 * rank + (first ? 0 : 1) : 2 * n_other + (rank - n_other); }
static bool ENEMY_PRIO = false;   // an enemy was seen with a priority other than 0: from then on its unread ones are unknown
static void note_prio(Champ& e, int v, int vn) {
    if (v != 0 || vn != 0) ENEMY_PRIO = true;
    e.prio = v; e.prio_n = vn; e.prio_at = ROUND; e.prio_known = true;
}
// our level for the next round without engine reads (torder::level reads state and channel time, ~500 fuel): its
// danger rule from our own view; a channel level (2, 3) is kept from our last request
static bool CTR_UP = false;   // our last request was raised by the counter (ctr_level)
static int own_next_level(int cur) {
    if (cur >= 2 && !CTR_UP) return cur;
    if (DANGER) return 1;
    bool near = false;
    for (const auto& e : ENEMY) if (e.vis && e.alive && cheb(e.p, ME.p) <= 5) { near = true; break; }
    if (near && ME.hp < ME.mhp * 0.5f) return 1;
    return near ? -1 : 0;
}
static int OWN_SET = 0;   // the priority we last asked for (torder::update): ours this round and, kept, the next
static int OWN_AT = -1, OWN_P[2][6], OWN_RA[2];   // our team's priorities this round and the next, our rank in it
static bool OWN_DANGER = false;   // the DANGER that our next level was worked out with
// Allies are read once a round. Our own next level follows DANGER, and a flee sets DANGER partway through the turn.
static void own_prios() {
    const bool new_round = OWN_AT != ROUND;
    if (!new_round && OWN_DANGER == DANGER) return;
    if (new_round) {
        OWN_AT = ROUND;
        for (int k = 0; k < (int)ALLY.size() && k < 6; ++k) {
            // an ally out of sight is not read (the engine throws, ~4k fuel each): its DANGER bit
            int p0 = claim_danger(k) ? 1 : 0, p1 = p0;
            if (k == MY_SLOT) p0 = p1 = OWN_SET;
            else if (ALLY[k].vis) {
                try {
                    const int a = (int)ALLY[k].h.prev_priority(), b = (int)ALLY[k].h.priority(); p0 = a; p1 = b;
                } catch (const std::exception&) {}
            }
            OWN_P[0][k] = p0; OWN_P[1][k] = p1;
        }
    }
    OWN_DANGER = DANGER;
    // Not ctr_level's raise: that is decided from where we end this turn, after our hits, and allies may ask for the same
    // slot. A turn is only counted away when we are sure of it.
    OWN_P[1][MY_SLOT] = own_next_level(OWN_SET);
    OWN_RA[0] = OWN_RA[1] = 0;
    for (int k = 0; k < (int)ALLY.size() && k < 6; ++k) {
        if (k == MY_SLOT) continue;
        for (int t = 0; t < 2; ++t)
            if (OWN_P[t][k] > OWN_P[t][MY_SLOT] || (OWN_P[t][k] == OWN_P[t][MY_SLOT] && k < MY_SLOT)) ++OWN_RA[t];
    }
}
static bool prio_fresh(const Champ& o) { return o.prio_known && (!ENEMY_PRIO || o.prio_at == ROUND); }
// does enemy e still act this round, after this turn of ours (so before the next round's camp phase)? 1 yes, 0 no
// (it acted already), -1 unknown: enemy_acts' first term
static int enemy_later(const Champ& e) {
    const int na = (int)ALLY.size(), nb = (int)ENEMY.size();
    if (na == 0 || nb == 0 || MY_SLOT >= 6) return -1;
    if (!prio_fresh(e)) return -1;
    own_prios();
    static int my_team = -1;
    if (my_team < 0) { try { my_team = (int)bc::detail::sdk_my_team(); } catch (...) { my_team = 0; } }
    const bool we_first = (my_team == 0) == (ROUND % 2 == 1);
    const int me_r = team_pos(OWN_RA[0], we_first, nb);
    auto later = [&](int r0) { return team_pos(r0, !we_first, na) > me_r ? 1 : 0; };
    if (!ENEMY_PRIO) return later(e.slot);
    int rb = 0, unk = 0;
    for (const auto& o : ENEMY) {
        if (&o == &e) continue;
        if (!prio_fresh(o)) { ++unk; continue; }
        if (o.prio > e.prio || (o.prio == e.prio && o.slot < e.slot)) ++rb;
    }
    const int a = later(rb);
    return unk > 0 && later(rb + unk) != a ? -1 : a;
}
// Its turns before our next one. An order we cannot work out reads as 1, with *known set false: it may really be 0 or 2.
static int enemy_acts(const Champ& e, bool* known = nullptr) {
    if (known) *known = false;
    const int na = (int)ALLY.size(), nb = (int)ENEMY.size();
    if (na == 0 || nb == 0 || MY_SLOT >= 6) return 1;
    if (!prio_fresh(e)) return 1;
    own_prios();
    const int* ra = OWN_RA;
    static int my_team = -1;
    if (my_team < 0) { try { my_team = (int)bc::detail::sdk_my_team(); } catch (...) { my_team = 0; } }
    const bool we_first = (my_team == 0) == (ROUND % 2 == 1);
    auto acts_at = [&](int r0, int r1) {
        const int me_r = team_pos(ra[0], we_first, nb), e_r = team_pos(r0, !we_first, na);
        const int me_n = team_pos(ra[1], !we_first, nb), e_n = team_pos(r1, we_first, na);
        return (e_r > me_r ? 1 : 0) + (e_n < me_n ? 1 : 0);
    };
    // no enemy reorders (so far): all keep draft order, the rank is the slot
    if (!ENEMY_PRIO) { if (known) *known = true; return acts_at(e.slot, e.slot); }
    const int ep[2] = {e.prio, e.prio_n};
    int rb[2] = {0, 0}, unk = 0;
    for (const auto& o : ENEMY) {
        if (&o == &e) continue;
        if (!prio_fresh(o)) { ++unk; continue; }
        const int op[2] = {o.prio, o.prio_n};
        for (int t = 0; t < 2; ++t) if (op[t] > ep[t] || (op[t] == ep[t] && o.slot < e.slot)) ++rb[t];
    }
    // the enemy reorders and some were not read this round: any rank from rb to rb + unk is possible in each round
    // acts_at(r0, r1) = f(r0) + g(r1): f rises with r0, g falls with r1 (team_pos rises with the rank), so the box is
    // constant exactly when both far corners equal a0
    const int a0 = acts_at(rb[0], rb[1]);
    if (unk > 0 && (acts_at(rb[0] + unk, rb[1]) != a0 || acts_at(rb[0], rb[1] + unk) != a0)) return 1;
    if (known) *known = true;
    return a0;
}
// Counter to the enemy's order. An enemy within 5 that will act early next round takes its team's first
// slots; we ask for the lowest level from `base` up to 1 that puts our next turn before it: our hurt and channelling
// champions keep their slots ahead (none either when their team starts and it holds their first slot). Its request: read when it already acted this round, else foreseen by
// turn_order.h's level rules from this turn's view (a channel ending 2, under half health with one of us within 5 1).
static int ctr_level(int base) {
    const int na = (int)ALLY.size(), nb = (int)ENEMY.size();
    if (MY_SLOT >= 6 || nb == 0 || nb > 8 || na > 6) return base;
    bool any = false;
    for (auto& e : ENEMY) {
        if (!e.vis || !e.alive) continue;
        if (e.prio_at != ROUND) {
            try {
                const int v = (int)e.h.prev_priority();
                int vn = v;
                try { vn = (int)e.h.priority(); } catch (const std::exception&) {}
                note_prio(e, v, vn);
            } catch (const std::exception&) {}
        }
        if (cheb(e.p, ME.p) <= 5) any = true;
    }
    if (!any || !ENEMY_PRIO) return base;
    own_prios();
    static int team = -1;
    if (team < 0) { try { team = (int)bc::detail::sdk_my_team(); } catch (...) { team = 0; } }
    const bool now_first = (team == 0) == (ROUND % 2 == 1), next_first = !now_first;
    int r0 = 0;   // our rank this round
    for (int k = 0; k < na; ++k)
        if (k != MY_SLOT && (OWN_P[0][k] > OWN_P[0][MY_SLOT] || (OWN_P[0][k] == OWN_P[0][MY_SLOT] && k < MY_SLOT))) ++r0;
    const int me_now = team_pos(r0, now_first, nb);
    int cur[8], nxt[8];
    for (int i = 0; i < nb; ++i) cur[i] = ENEMY[i].vis && ENEMY[i].prio_at == ROUND ? ENEMY[i].prio : 0;
    for (int i = 0; i < nb; ++i) {
        Champ& e = ENEMY[i];
        nxt[i] = 0;
        if (!e.vis || !e.alive || e.prio_at != ROUND) continue;
        int k0 = 0;
        for (int j = 0; j < nb; ++j) if (j != i && (cur[j] > cur[i] || (cur[j] == cur[i] && ENEMY[j].slot < e.slot))) ++k0;
        if (team_pos(k0, !now_first, na) < me_now) { nxt[i] = e.prio_n; continue; }   // it already asked
        try {   // its health after our hits this turn
            bc::detail::champion_info ci{};
            if (bc::detail::sdk_champion(static_cast<uint32_t>(static_cast<bc::handle_champion_t>(e.h)), &ci) == 0 && ci.x >= 0) e.hp = ci.health;
        } catch (...) {}
        int p = std::min(e.prio_n, 1);   // fleeing: it asks for 1 again while it still runs
        if (e.channel && e.chan_rem >= 0 && e.chan_rem <= 1) p = 2;
        else if (e.hp < e.mhp * 0.5f) {
            bool near = cheb(e.p, ME.p) <= 5;
            for (const auto& a : ALLY) if (!near && a.vis && a.alive && cheb(a.p, e.p) <= 5) near = true;
            if (near) p = std::max(p, 1);
        }
        nxt[i] = p;
    }
    int want = base;
    bool spell = false;   // a damaging spell ready by our next turn
    for (int s = 0; s < 2; ++s) {
        const int kind = kit_of(ME.type).s[s].kind;
        if ((kind == SK_DMG || kind == SK_AREA) && ME.cd[s] <= 1) spell = true;
    }
    const float burst = spell ? spell_burst(ME.type, ME.focus) : 0.f;
    for (int i = 0; i < nb; ++i) {
        if (!ENEMY[i].vis || !ENEMY[i].alive || cheb(ENEMY[i].p, ME.p) > 5) continue;
        bool strike = false;   // a fighting enemy and us, each within a step and a hit of the other
        if (nxt[i] <= 0) {
            const int d = cheb(ENEMY[i].p, ME.p);
            strike = d <= (ME.melee ? 2 : 3) && d <= (is_melee(ENEMY[i].type) ? 2 : 3);
            if (!strike) continue;
        }
        if (!strike)
        {   // what our earlier turn does to it: a kill, or a hit that stops its recall
            Champ& e = ENEMY[i];
            const int d = cheb(e.p, ME.p);
            const bool hit = d <= (ME.melee ? 2 : 3);   // a step and a hit, or a ranged hit from here (next turn: not this turn's used-up flag)
            bool rcl = false;
            try {   // health after our hits this turn, and the channel kind
                bc::detail::champion_info ci{};
                if (bc::detail::sdk_champion(static_cast<uint32_t>(static_cast<bc::handle_champion_t>(e.h)), &ci) == 0 && ci.x >= 0) {
                    e.hp = ci.health;
                    rcl = bc::champion_state::from_bits_retain(ci.state).contains(bc::champion_state::recalling);
                }
            } catch (...) {}
            const float dmg = (hit ? my_str() : 0.f) + (d <= 4 ? burst : 0.f);   // HoR passive
            if (!(rcl && hit) && (dmg <= 0.f || dmg < e.hp)) continue;
        }
        int rb = 0;
        for (int j = 0; j < nb; ++j)
            if (j != i && ENEMY[j].alive && (nxt[j] > nxt[i] || (nxt[j] == nxt[i] && ENEMY[j].slot < ENEMY[i].slot))) ++rb;
        const int e_pos = team_pos(rb, !next_first, na);
        for (int L = base; L <= 1; ++L) {
            int r = 0;
            for (int k = 0; k < na; ++k) if (k != MY_SLOT && (OWN_P[1][k] > L || (OWN_P[1][k] == L && k < MY_SLOT))) ++r;
            if (team_pos(r, next_first, nb) < e_pos) {
                want = std::max(want, L); break;
            }
        }
    }
    return want;
}
// Live cooldowns and the stun of a visible champion, once per round (-1: could not be read, treated as ready).
static int ECD_FAILS = 0;   // the engine refusing enemy cooldowns: stop asking after a few failures
static void read_ecd(Champ& e) {
    if (e.ecd_at == ROUND) return;
    e.ecd_at = ROUND;
    e.stunned = e.loses_turn = false;
    try {
        const auto limits = e.h.restrictions();
        e.stunned = limits.contains(bc::buff_restrictions::move) && limits.contains(bc::buff_restrictions::attack) &&
                    limits.contains(bc::buff_restrictions::cast);
        // the flag stays up for the rest of the round after the lost turn. On the champion's own stunned turn the
        // timer always reads 1; a timer of 0 has no turn left to take.
        if (e.stunned)
            for (const auto& buff : e.h.buffs())
                if (const auto* stun = std::get_if<bc::buff_stun>(&buff)) e.loses_turn = stun->duration >= 1;
    } catch (const std::exception&) {}
    if (ECD_FAILS >= 6) e.ecd[0] = e.ecd[1] = -1;
    else {
        const auto ab = e.h.abilities();   // one copy (the handles), the cooldowns as before
        for (int s = 0; s < 2; ++s) {
            if (ECD_FAILS >= 6) { e.ecd[s] = -1; continue; }
            try { e.ecd[s] = (int)ab[s].cooldown(); ECD_FAILS = 0; } catch (const std::exception&) { e.ecd[s] = -1; ++ECD_FAILS; }
        }
    }
    if (e.ally) return;   // allies reorder too: own_prios reads them
    try {
        const int v = (int)e.h.prev_priority();
        int vn = v;
        try { vn = (int)e.h.priority(); } catch (const std::exception&) {}
        note_prio(e, v, vn);
    } catch (const std::exception&) {}
}

// Largest damaging spell whose exact slot is ready. Unknown cooldowns (-1) remain conservatively ready. `age`
// counts the cooldown down by that many of the enemy's turn ends.
static float ready_spell_burst(const Champ& e, int age = 0) {
    const Kit k = kit_of(e.type);
    float best = 0.f;
    for (int s = 0; s < 2; ++s) {
        if (e.ecd[s] - age > 0) continue;
        const SlotK& sk = k.s[s];
        if (sk.kind == SK_DMG || sk.kind == SK_AREA) best = std::max(best, sk.mult * e.focus);
    }
    return best;
}

static Champ* MFOCUS = nullptr;   // the team's focus target this turn

// The kill bonus: its gems drop and its damage leaves the fight for 60 turns.
static float kill_bonus(const Champ& e) { return 300.f + 5.f * e.gems + 2.f * champ_dps(e.type, e.str, e.focus); }
static constexpr float MFOCUS_K = 1.15f;   // value on the team's focus target

// Value of dealing dmg to e: damage scaled up on hurt targets and priority types, a big bonus for the kill,
// extra for breaking a channel.
static float kill_value(const Champ& e, float dmg) {
    if (dmg <= 0 || e.hp <= 0) return 0.f;
    const float dealt = std::min(dmg, e.hp);
    float v = dealt * (1.f + 0.6f * (1.f - e.hp / std::max(1.f, e.mhp))) * type_mult(e.type);
    if (dmg >= e.hp) v += kill_bonus(e);
    if (e.channel) v += 40.f + 2.f * e.gems;
    if (&e == MFOCUS) v *= MFOCUS_K;
    return v;
}
// A stun takes away the target's next turn (and breaks a tribute or recall).
static float stun_value(const Champ& e) {
    if (champ(e.type).stun_immune) return 0.f;
    return 1.5f * (e.str * 1.1f + spell_avg(e.type, e.focus)) + 20.f + (e.channel ? 80.f + 3.f * e.gems : 0.f);
}

// an enemy that stood with us in a fight where nobody hit anybody is left alone while it keeps its distance
static bool standoff_ignored(const Champ& e) { return rnd() < e.so_until && cheb(e.p, ME.p) > STANDOFF_MIN_DISTANCE; }
// What this visible enemy can do before our next turn. turn_ends: look that many of its turns further ahead for cooldowns.
// A cooldown ticks at its owner's turn end (rules 03), so its second turn before our next one finds it one lower. A stun
// costs exactly one turn, whoever acts first (rules 03, Q12).
// One ability slot as the danger table sees it. A Heart of Ruin landing is a dash that also does damage.
struct EnemyCast { int kind; float dmg; int cd; };

// One turn of this enemy from d tiles away with this cast (null: none): what it lands on us and how far away it ends
// (rules 03). A melee champion walks and hits; a ranged one does one or the other. A reset gives one more hit and a
// dash two tiles. A damage spell cannot follow a walk or be followed by one, so a hit goes with it only from where it
// stands. A Vendetta kill reset adds nothing here: the kill takes her hit and one cast a turn still holds.
static float cast_turn(const EnemyPower& p, const EnemyCast* c, int d, int& end_d) {
    const bool melee = p.reach == 2;
    const int basic = attack_reach(melee);
    const float plain = d <= p.reach ? p.hit : 0.f;
    end_d = !melee && d <= basic ? d : std::max(0, d - 1);
    if (!c) return plain;
    switch (c->kind) {
        case SK_RESET:
            if (d <= p.reach) return 2.f * p.hit;
            if (!melee && d == p.reach + 1) return p.hit;   // walk, reset, attack
            return plain;
        case SK_DASH:
            if (d <= p.reach + 2) { end_d = std::max(0, d - (melee ? 3 : 2)); return p.hit + c->dmg; }
            end_d = std::max(0, d - 3);
            return 0.f;
        case SK_DMG:
        case SK_AREA:
            if (d <= (c->kind == SK_AREA ? 5 : 4)) { end_d = d; return c->dmg + (d <= basic ? p.hit : 0.f); }
            return plain;
        default:
            return plain;
    }
}

// Fill the table: the best use of its turns from each distance. One cast a turn; a slot it used stays on cooldown, and a
// slot is ready on a turn only if its cooldown has run out by then. last_age: turn ends counted down by its last turn.
static void fill_danger(EnemyPower& p, const EnemyCast (&cast)[2], int last_age) {
    auto pick = [&](int s) { return s < 0 ? nullptr : &cast[s]; };
    for (int d = 0; d < 9 && p.turns > 0; ++d) {
        float best = 0.f;
        for (int a = -1; a < 2; ++a) {
            const int first_age = p.turns == 2 ? last_age - 1 : last_age;
            if (a >= 0 && cast[a].cd > first_age) continue;
            int end_d = d;
            const float one = cast_turn(p, pick(a), d, end_d);
            if (p.turns == 1) { best = std::max(best, one); continue; }
            for (int b = -1; b < 2; ++b) {
                if (b >= 0 && (b == a || cast[b].cd > last_age)) continue;
                int unused = 0;
                best = std::max(best, one + cast_turn(p, pick(b), end_d, unused));
            }
        }
        p.at[d] = best;
    }
    p.runs = 0;
    int last = 8;
    while (last >= 0 && p.at[last] == 0.f) --last;
    for (int d = 0; d <= last; ++d) {
        if (p.runs > 0 && p.run_v[p.runs - 1] == p.at[d]) p.run_hi[p.runs - 1] = d;
        else { p.run_hi[p.runs] = d; p.run_v[p.runs] = p.at[d]; ++p.runs; }
    }
}

static EnemyPower read_enemy_power(Champ& e, int turn_ends, int min_turns = 0) {
    // every visible enemy first: enemy_acts takes an enemy not read this round as unknown
    for (auto& o : ENEMY) if (o.vis && o.alive && o.hp > 0) read_ecd(o);
    bool known = false;
    const int acts = enemy_acts(e, &known);
    const int age = std::max(0, acts - 1) + turn_ends;
    const float hor = is_hor(e) ? hor_mult(e) : 1.f;   // Heart of Ruin's passive, on all its damage
    EnemyPower p;
    // with the order unknown it may have a live turn after the lost one: no discount
    p.turns = std::max(min_turns, acts - (e.loses_turn && known ? 1 : 0));
    p.hit = e.str * hor;
    p.reach = is_melee(e.type) ? 2 : 3;
    const int ds = dash_slot_of(e.type);
    p.dash_ready = ds >= 0 && e.ecd[ds] <= age;
    p.spell = ready_spell_burst(e, age) * hor;
    const int as = area_slot(e.type);
    p.area = as >= 0 && e.ecd[as] <= age ? area_burst(e.type, e.focus) * hor : 0.f;
    const Kit kit = kit_of(e.type);
    EnemyCast cast[2];
    for (int s = 0; s < 2; ++s) {
        const SlotK& sk = kit.s[s];
        cast[s] = {s == ds ? SK_DASH : sk.kind, sk.mult * e.focus * hor, e.ecd[s]};
    }
    fill_danger(p, cast, age);
    return p;
}
static const EnemyPower& enemy_power(Champ& e) {
    // our next-round rank, and so its turns, follow DANGER
    const int key = ROUND * 2 + (DANGER ? 1 : 0);
    if (e.power_key != key) { e.power_key = key; e.power = read_enemy_power(e, 0); }
    return e.power;
}
// The worst this enemy can land on a champion d tiles away before our next turn.
static float danger_at(const EnemyPower& p, int d) {
    return d <= 8 ? p.at[d] : 0.f;
}
static bool targetable(const Champ& e) { return e.vis && e.alive && e.hp > 0 && !on_enemy_spawn(e.p) && !standoff_ignored(e); }
static bool spell_area(int s) { const Ab& a = ME.ab[s]; return a.aoe || (a.ground && !a.enemies); }

static bool SP_OK[2];
static float SP_DMG[2];
// Read the live amount for every offensive target type, including CC abilities.
// Some CC abilities carry a damage modifier; treating them as zero makes the
// rollout choose the wrong target and can suppress a lethal/stun action. A
// pure CC ability may legitimately report no damage, so an accessor failure
// must preserve its CC action while only disabling damage-only spells.
// my spell's damage_amount() read once a round per slot (the shop runs before think(); nothing in a turn
// changes it before we act); false: the engine threw
static int MYDMG_AT[2] = {-1, -1};
static bool MYDMG_OK[2];
static float MYDMG_V[2];
static bool my_dmg(int s, float& v) {
    if (MYDMG_AT[s] != ROUND) {
        MYDMG_AT[s] = ROUND;
        try { MYDMG_V[s] = (float)bc::self().abilities()[s].damage_amount(); MYDMG_OK[s] = true; }
        catch (const std::exception&) { MYDMG_V[s] = 0.f; MYDMG_OK[s] = false; }
    }
    v = MYDMG_V[s];
    return MYDMG_OK[s];
}
static bool read_spell_damage(int s, const Ab& a) {
    SP_DMG[s] = 0;
    if (!(a.dmg || a.aoe || a.cc)) return true;
    float v;
    if (my_dmg(s, v)) { SP_DMG[s] = v; return true; }
    return !(a.dmg || a.aoe);
}
// Ground centres for area spells (any tile within one of an enemy, so a centre at range 4 hits up to 5)
static vec2 AC_P[2][60];
static float AC_V[2][60];
static int AC_N[2] = {0, 0};

// ---- direct evaluation (used when executing, after earlier actions changed health)
static float basic_value(const Champ& e) {
    float v = kill_value(e, my_str());   // HoR passive
    if (champ(ME.type).splash)   // splash on everything next to the target
        for (auto& o : ENEMY) if (&o != &e && targetable(o) && cheb(o.p, e.p) <= 1) v += 0.8f * kill_value(o, my_str());
    return v;
}
static Champ* basic_target(vec2 from) {
    const int reach = attack_reach(ME.melee);
    Champ* best = nullptr;
    float bv = 0;
    for (auto& e : ENEMY) {
        if (!targetable(e) || cheb(from, e.p) > reach) continue;
        const float v = basic_value(e);
        if (v > bv) { bv = v; best = &e; }
    }
    return best;
}
static float spell_best(int s, vec2 from, Champ** who, vec2* tile) {
    const bool area = spell_area(s), cc = ME.ab[s].cc;
    float best = 0;
    for (auto& c : ENEMY) {
        if (!targetable(c) || cheb(from, c.p) > 4) continue;
        float v = 0;
        if (area) { for (auto& o : ENEMY) if (targetable(o) && cheb(o.p, c.p) <= 1) v += kill_value(o, SP_DMG[s]) + (cc ? stun_value(o) : 0.f); }
        else v = kill_value(c, SP_DMG[s]) + (cc ? stun_value(c) : 0.f);
        if (v > best) { best = v; *who = &c; *tile = c.p; }
    }
    return best;
}
