/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "NearestUnitsValue.h"

#include "Playerbots.h"

uint32 NearestUnitsValue::GetEffectiveCheckInterval() const
{
    if (checkInterval < 2)
        return checkInterval;

    if (!bot || bot->InBattleground() || bot->InArena() || bot->IsInCombat())
        return checkInterval;

    if (bot->isMoving())
        return checkInterval < 150 ? 150 : checkInterval;

    return checkInterval < 250 ? 250 : checkInterval;
}

GuidVector NearestUnitsValue::Get()
{
    if (checkInterval < 2)
    {
        value = Calculate();
        return value;
    }

    uint32 const now = getMSTime();
    if (!lastCheckTime || getMSTimeDiff(lastCheckTime, now) >= GetEffectiveCheckInterval())
    {
        lastCheckTime = now;
        value = Calculate();
    }

    return value;
}

GuidVector& NearestUnitsValue::RefGet()
{
    if (checkInterval < 2)
    {
        value = Calculate();
        return value;
    }

    uint32 const now = getMSTime();
    if (!lastCheckTime || getMSTimeDiff(lastCheckTime, now) >= GetEffectiveCheckInterval())
    {
        lastCheckTime = now;
        value = Calculate();
    }

    return value;
}

GuidVector NearestUnitsValue::Calculate()
{
    std::list<Unit*> targets;
    FindUnits(targets);

    GuidVector results;
    for (Unit* unit : targets)
    {
        if (AcceptUnit(unit) && (ignoreLos || bot->IsWithinLOSInMap(unit)))
            results.push_back(unit->GetGUID());
    }

    return results;
}
