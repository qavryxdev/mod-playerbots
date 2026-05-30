/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_OFFENSIVEDISPELTARGETVALUE_H
#define _PLAYERBOT_OFFENSIVEDISPELTARGETVALUE_H

#include "NamedObjectContext.h"
#include "TargetValue.h"

class OffensiveDispelTargetValue : public UnitCalculatedValue, public Qualified
{
public:
    OffensiveDispelTargetValue(PlayerbotAI* botAI, std::string const name = "offensive dispel target")
        : UnitCalculatedValue(botAI, name, 500), Qualified()
    {
    }

    Unit* Calculate() override;
};

#endif
