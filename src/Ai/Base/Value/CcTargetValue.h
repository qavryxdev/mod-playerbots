/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_CCTARGETVALUE_H
#define _PLAYERBOT_CCTARGETVALUE_H

#include "NamedObjectContext.h"
#include "TargetValue.h"

class PlayerbotAI;
class Unit;

class CcTargetValue : public TargetValue, public Qualified
{
public:
    CcTargetValue(PlayerbotAI* botAI, std::string const name = "cc target") : TargetValue(botAI, name) {}

    Unit* Calculate() override;
};

namespace ai::cc
{
    bool HasActiveCrowdControl(Unit* target);
    Unit* GetActivePolymorphTarget(PlayerbotAI* botAI);
    Unit* GetActiveFearTarget(PlayerbotAI* botAI);
    bool IsGoodPolymorphTarget(PlayerbotAI* botAI, Unit* target);
    bool IsDiminishingBlocked(PlayerbotAI* botAI, Unit* target, std::string const& spell);
    int32 GetDiminishingPenalty(PlayerbotAI* botAI, Unit* target, std::string const& spell);
    void RecordCrowdControl(PlayerbotAI* botAI, Unit* target, std::string const& spell);
}

#endif
