// micro against enemy champions, XSquare style (Battlecode postmortems: Cout for Clout 2024, Om Nom
// 2025). Every plan for this turn — stay, walk, dash, dash then walk; attack before or after moving; the one cast
// spent on a spell, an attack reset or the dash — is scored by the kill value of what it hits now against the
// damage expected on its end tile before our next turn. Champions alternate one by one, so every enemy acts exactly
// once in between: a melee enemy reaches 2 (walk + hit), a ranged one 3, a dash adds 2, spells reach 4.
// Fuel: everything that does not depend on the plan is computed once (per enemy) or once per tile of the 7x7 window
// the plans end in; a plan is then a handful of lookups.
#pragma once
#include "../camps/fight.h"

// Declaration and evaluation order is intentional: see micro_architecture.md.
#include "micro_rules.h"
#include "micro_turn_state.h"
#include "micro_plan.h"
#include "micro_execute.h"
#include "../models/qmodel.h"
#include "micro_search.h"
