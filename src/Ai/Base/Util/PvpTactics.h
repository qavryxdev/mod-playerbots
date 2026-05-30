/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_PVPTACTICS_H
#define _PLAYERBOT_PVPTACTICS_H

#include "ObjectGuid.h"
#include "PositionValue.h"

#include <string>

class Player;
class PlayerbotAI;
struct SpellInfo;
class Unit;
class WorldLocation;

namespace ai::pvp
{
    bool IsPvpContext(Player* bot);
    bool IsInAlteracValley(Player* bot);
    bool GetActiveAVObjective(PlayerbotAI* botAI, Player* bot, PositionInfo& objective);
    bool IsNearObjective(Unit* unit, PositionInfo const& objective, float radius);
    bool IsAttackingFriendlyHealer(PlayerbotAI* botAI, Unit* target);
    bool IsObjectiveRelevantEnemy(PlayerbotAI* botAI, Unit* target, bool threatTarget = false,
                                  float botObjectiveRadius = 60.0f, float targetObjectiveRadius = 38.0f);

    bool IsBreakableCrowdControlled(Unit* target);
    bool CanDamageTarget(PlayerbotAI* botAI, Unit* target, bool areaDamage = false);
    bool SpellCanBreakCrowdControl(SpellInfo const* spellInfo);
    bool CurrentAoeIsSafe(PlayerbotAI* botAI);
    bool IsAoeSafe(PlayerbotAI* botAI, WorldLocation const& position, float radius);

    bool IsInterruptSpell(std::string const& spell);
    bool TryReserveInterrupt(PlayerbotAI* botAI, Unit* target, std::string const& spell, uint32 holdMs = 900);

    int32 ScoreOffensiveDispelTarget(PlayerbotAI* botAI, Unit* target, uint32 dispelType, bool threatTarget = false);
    bool ShouldUseBurstCooldown(PlayerbotAI* botAI, Unit* target);
}

#endif
