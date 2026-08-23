/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "InvalidTargetValue.h"

#include "AttackersValue.h"
#include "Playerbots.h"
#include "Unit.h"

bool InvalidTargetValue::Calculate()
{
    Unit* target = AI_VALUE(Unit*, qualifier);
    Unit* enemy = AI_VALUE(Unit*, "enemy player target");
    if (target && enemy && target == enemy && target->IsAlive() && AttackersValue::IsValidTarget(target, bot))
        return false;

    if (target && qualifier == "current target")
    {
        // A feared target is deliberately not listed below. Fear lasts a couple of seconds and the mob
        // is still the bot kill, but calling it invalid hands the bot to DropTargetAction, which stops
        // the attack, clears the selection and pushes the bot back to the non-combat engine - and since
        // the mob is still in combat with it, the very next tick picks the same mob up again. Measured
        // on the live server that ran at two to three acquire/drop cycles per second, with the bot
        // rooted to the spot throughout, because both halves of the cycle wipe its movement. The target
        // scorers already rank breakable crowd control last, so a bot with anything else worth hitting
        // still switches away on its own, and one with nothing else keeps fighting instead of freezing.
        return target->GetMapId() != bot->GetMapId() || target->HasUnitFlag(UNIT_FLAG_NOT_SELECTABLE) ||
               target->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE) || target->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE_2) ||
               !target->IsVisible() || !target->IsAlive() || target->IsPolymorphed() || target->IsCharmed() ||
               target->HasUnitState(UNIT_STATE_ISOLATED) || target->IsFriendlyTo(bot) ||
               !AttackersValue::IsValidTarget(target, bot);
    }

    return !target;
}
