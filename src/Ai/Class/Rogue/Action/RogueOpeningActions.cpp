/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "RogueOpeningActions.h"

#include "Playerbots.h"
#include "PvpTactics.h"

Value<Unit*>* CastSapAction::GetTargetValue() { return context->GetValue<Unit*>("cc target", getName()); }

bool CastSapAction::Execute(Event /*event*/)
{
    Unit* target = GetTarget();
    if (!ai::pvp::TryReserveCrowdControl(botAI, target, "sap"))
        return false;

    bool const cast = botAI->CastSpell("sap", target);
    if (!cast)
        ai::pvp::ReleaseCrowdControl(botAI, target);
    return cast;
}
