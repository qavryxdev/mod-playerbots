/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "OffensiveDispelTargetValue.h"

#include "ObjectGuid.h"
#include "Playerbots.h"
#include "PvpTactics.h"

#include <cstdlib>
#include <limits>
#include <unordered_set>

Unit* OffensiveDispelTargetValue::Calculate()
{
    uint32 const dispelType = static_cast<uint32>(std::strtoul(qualifier.c_str(), nullptr, 10));
    if (!dispelType)
        return nullptr;

    Unit* result = nullptr;
    int32 bestScore = std::numeric_limits<int32>::min();
    std::unordered_set<ObjectGuid> checked;

    auto checkUnit = [&](Unit* target, bool threatTarget)
    {
        if (!target || !target->GetGUID() || checked.find(target->GetGUID()) != checked.end())
            return;

        checked.insert(target->GetGUID());
        int32 const score = ai::pvp::ScoreOffensiveDispelTarget(botAI, target, dispelType, threatTarget);
        if (score > 0 && (!result || score > bestScore))
        {
            result = target;
            bestScore = score;
        }
    };

    checkUnit(botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get(), true);

    GuidVector attackers = botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get();
    for (ObjectGuid const& guid : attackers)
        checkUnit(botAI->GetUnit(guid), true);

    GuidVector possibleTargets = botAI->GetAiObjectContext()->GetValue<GuidVector>("possible targets")->Get();
    uint8 scanned = 0;
    for (ObjectGuid const& guid : possibleTargets)
    {
        checkUnit(botAI->GetUnit(guid), false);
        if (++scanned >= 32)
            break;
    }

    return result;
}
