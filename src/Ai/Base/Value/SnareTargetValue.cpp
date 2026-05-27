/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "SnareTargetValue.h"

#include "AiObjectContext.h"
#include "PlayerbotAI.h"
#include "ServerFacade.h"

#include <limits>

namespace
{
    bool IsFriendlyHealerVictim(PlayerbotAI* botAI, Unit* unit)
    {
        Player* victim = unit && unit->GetVictim() ? unit->GetVictim()->ToPlayer() : nullptr;
        return victim && !botAI->IsOpposing(victim) && botAI->IsHeal(victim);
    }
}

Unit* SnareTargetValue::Calculate()
{
    std::string const spell = qualifier;

    GuidVector attackers = botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get();
    Unit* result = nullptr;
    int32 bestScore = std::numeric_limits<int32>::min();

    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        if (bot->GetDistance(unit) > botAI->GetRange("spell"))
            continue;

        if (botAI->HasAura(spell, unit, false, true))
            continue;

        Unit* chaseTarget;
        int32 score = 0;
        switch (unit->GetMotionMaster()->GetCurrentMovementGeneratorType())
        {
            case FLEEING_MOTION_TYPE:
                score += 300;
                break;
            case CHASE_MOTION_TYPE:
            {
                chaseTarget = ServerFacade::instance().GetChaseTarget(unit);
                if (!chaseTarget)
                    continue;
                Player* chaseTargetPlayer = ObjectAccessor::FindPlayer(chaseTarget->GetGUID());
                // check if need to snare
                bool shouldSnare = true;

                // do not slow down if bot is melee and mob/bot attack each other
                if (chaseTargetPlayer && !botAI->IsRanged(bot) && chaseTargetPlayer == bot)
                    shouldSnare = false;

                if (!unit->isMoving())
                    shouldSnare = false;

                if (unit->HasAuraType(SPELL_AURA_MOD_ROOT))
                    shouldSnare = false;

                if (!chaseTargetPlayer || !shouldSnare || botAI->IsTank(chaseTargetPlayer))
                    continue;

                score += 180;
                if (botAI->IsHeal(chaseTargetPlayer))
                    score += 500;
                if (chaseTargetPlayer == bot)
                    score += botAI->IsHeal(bot) ? 280 : 80;
                break;
            }
            default:
                continue;
        }

        if (unit->IsPlayer())
            score += 80;

        if (IsFriendlyHealerVictim(botAI, unit))
            score += 420;

        if (unit == botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get())
            score += 50;

        score -= static_cast<int32>(bot->GetDistance(unit));

        if (!result || score > bestScore)
        {
            result = unit;
            bestScore = score;
        }
    }

    return result;
}
