/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "SnareTargetValue.h"

#include "AiObjectContext.h"
#include "ObjectGuid.h"
#include "PlayerbotAI.h"
#include "ServerFacade.h"

#include <limits>
#include <unordered_set>

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

    Unit* result = nullptr;
    int32 bestScore = std::numeric_limits<int32>::min();
    std::unordered_set<ObjectGuid> checked;

    auto checkGuid = [&](ObjectGuid const guid)
    {
        if (!guid || checked.find(guid) != checked.end())
            return;

        checked.insert(guid);
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            return;

        if (bot->GetDistance(unit) > botAI->GetRange("spell"))
            return;

        if (botAI->HasAura(spell, unit, false, true))
            return;

        Unit* chaseTarget;
        int32 score = 0;
        bool const peelsFriendlyHealer = IsFriendlyHealerVictim(botAI, unit);
        bool const peelsSelf = unit->GetVictim() == bot || unit->GetTarget() == bot->GetGUID();
        switch (unit->GetMotionMaster()->GetCurrentMovementGeneratorType())
        {
            case FLEEING_MOTION_TYPE:
                score += 300;
                break;
            case CHASE_MOTION_TYPE:
            {
                chaseTarget = ServerFacade::instance().GetChaseTarget(unit);
                if (!chaseTarget)
                    return;
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
                    return;

                score += 180;
                if (botAI->IsHeal(chaseTargetPlayer))
                    score += 500;
                if (chaseTargetPlayer == bot)
                    score += botAI->IsHeal(bot) ? 280 : 80;
                break;
            }
            default:
                if (!unit->isMoving() || unit->HasAuraType(SPELL_AURA_MOD_ROOT) || (!peelsFriendlyHealer && !peelsSelf))
                    return;

                score += peelsFriendlyHealer ? 360 : 120;
                break;
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
    };

    GuidVector attackers = botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get();
    for (ObjectGuid const guid : attackers)
        checkGuid(guid);

    if (bot->InBattleground() || bot->InArena() || bot->duel)
    {
        GuidVector possibleTargets = botAI->GetAiObjectContext()->GetValue<GuidVector>("possible targets")->Get();
        uint8 scanned = 0;
        for (ObjectGuid const guid : possibleTargets)
        {
            Unit* unit = botAI->GetUnit(guid);
            if (unit && (unit->GetVictim() == bot || IsFriendlyHealerVictim(botAI, unit)))
                checkGuid(guid);

            if (++scanned >= 32)
                break;
        }
    }

    return result;
}
