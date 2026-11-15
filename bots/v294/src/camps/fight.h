// combat against camp monsters and enemy champions.
#pragma once
#include "../game/nav.h"
#include "../brain/shop.h"

#include "camp_actions.h"  // Camp damage, target refresh, spell aiming and attack execution.
#include "camp_siege.h"  // Camp retaliation estimates and siege approach.
#include "camp_monolith.h"  // Monolith positioning, commitment and quorum rules.
#include "camp_combat.h"  // Camp approach and combat orchestration.
#include "../game/champion_combat.h"  // Champion combat estimates and direct fighting.
