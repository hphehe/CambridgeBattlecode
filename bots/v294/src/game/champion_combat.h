// Champion combat estimates and direct fighting.
#pragma once

// Lanchester-style balance of a local fight: >1 means we kill them before they kill us.
// champ_dps of a seen champion with Heart of Ruin's passive at its health
static float champ_dps_c(const Champ& c) {
    if (is_hor(c)) return champ_dps(c.type, c.str, c.focus) * hor_mult(c);
    return champ_dps(c.type, c.str, c.focus);
}
// A champion on its own spawn heals to full at its turn end and cannot be targeted; while it stays it hurts us only with what it
// reaches from there. its damage per round in our fight Balance by distance d: within a walk and a basic attack
// (melee 2, ranged 3; +2 with a dash) its whole champ_dps, else within its damage spell's reach (4, 5 for an area spell) the
// spell part only, else nothing. Its health never counts (not ours to wear down).
static float camper_dps(const Champ& e, int d) {
    const float m = is_hor(e) ? hor_mult(e) : 1.f;
    if (d <= (is_melee(e.type) ? 2 : 3) + (has_dash(e.type) ? 2 : 0)) return champ_dps(e.type, e.str, e.focus) * m;
    const int sr = area_slot(e.type) >= 0 ? 5 : spell_burst(e.type, e.focus) > 0.f ? 4 : 0;
    return d <= sr ? spell_avg(e.type, e.focus) * m : 0.f;
}
// my own champ_dps with the Heart of Ruin passive at my health, as champ_dps_c counts a seen HoR
static float my_dps_c() { return champ_dps(ME.type, ME.str, ME.focus) * my_mult(); }
// a self-shield (Varg 2.5 x Focus, Heart of Ruin 1.5 x Focus; 5 turns up, 8-turn cooldown) as hit points on the
// body's side of the Balance, instead of as damage in champ_dps (shield_f, a Varg-era hand fit). Mine counts when the slot is
// ready or the shield is up (cooldown 8..4 after the cast); a seen enemy's when its cooldown is unknown, ready or up (the
// micro reads ecd); an ally's always (cooldowns not read).
static float my_shield_hp() {
    for (int s = 0; s < 2; ++s)
        if (ME.ab[s].shield) return (ME.cd[s] == 0 || ME.cd[s] >= 4) ? shield_mult(ME.type) * ME.focus : 0.f;
    return 0.f;
}
static float shield_hp(const Champ& c) {
    const int s = shield_slot_of(c.type);
    if (s < 0) return 0.f;
    if (!c.ally && c.ecd[s] >= 1 && c.ecd[s] <= 3 && rnd() - c.ecd_at <= 1) return 0.f;
    return shield_mult(c.type) * c.focus;
}
// a seen enemy's ready heal (Willow: 3 x Focus on another ally in range 4, and she can step first) as hit points on her
// side of the Balance, once for each healer, as shields are. It gives back what is missing, so the most hurt body she can
// reach caps it. The cooldown is 8 turns: a rate per round would count it about 8 times over.
static constexpr int HEAL_REACH = 4 + 1;
static float heal_hp(const Champ& c) {
    const int s = heal_slot_of(c.type);
    if (s < 0) return 0.f;
    const int age = rnd() - c.ecd_at;
    if (age <= 1 && c.ecd[s] > age) return 0.f;   // she has had at most `age` turn ends since the read
    float missing = 0.f;
    for (const auto& o : ENEMY)
        if (&o != &c && o.vis && o.alive && o.hp > 0 && cheb(o.p, c.p) <= HEAL_REACH) missing = std::max(missing, o.mhp - o.hp);
    return std::min(heal_mult(c.type) * c.focus, missing);
}
struct Balance {
    float us_hp = 0, us_dps = 0, th_hp = 0, th_dps = 0;
    int us_n = 0, th_n = 0;
    void add_us(float hp, float dps) { us_hp += hp; us_dps += dps; ++us_n; }
    void add_th(float hp, float dps) { th_hp += hp; th_dps += dps; ++th_n; }
    float lanchester() const {
        if (th_n == 0) return 99.f;
        const float t_them = th_hp / std::max(1.f, us_dps), t_us = us_hp / std::max(1.f, th_dps);
        return t_us / std::max(0.1f, t_them);
    }
    float adv() const { return lanchester(); }
};

static bool attack_champ(Champ& e) {
    if (!ME.can_attack || cheb(ME.p, e.p) > attack_reach(ME.melee)) return false;
    try {
        if (!e.h.is_visible()) return false;
        bc::self().attack(e.h);
    } catch (const std::exception&) { ME.can_attack = false; return false; }
    ME.can_attack = false;
    LAST_CHAMP_HIT = rnd();
    return true;
}
