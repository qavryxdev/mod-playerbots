/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_ROGUECOMBOACTIONS_H
#define _PLAYERBOT_ROGUECOMBOACTIONS_H

#include "GenericSpellActions.h"

class PlayerbotAI;

// Builders share the overcap guard: at 5 combo points the points a builder would generate are lost,
// so the finisher has to go out first.
class CastComboAction : public CastMeleeSpellAction
{
public:
    CastComboAction(PlayerbotAI* botAI, std::string const name) : CastMeleeSpellAction(botAI, name) {}

    bool isUseful() override;
};

class CastSinisterStrikeAction : public CastComboAction
{
public:
    CastSinisterStrikeAction(PlayerbotAI* botAI) : CastComboAction(botAI, "sinister strike") {}
};

class CastMutilateAction : public CastComboAction
{
public:
    CastMutilateAction(PlayerbotAI* botAI) : CastComboAction(botAI, "mutilate") {}
};

class CastRiposteAction : public CastSpellAction
{
public:
    CastRiposteAction(PlayerbotAI* botAI) : CastSpellAction(botAI, "riposte") {}
};

class CastGougeAction : public CastSpellAction
{
public:
    CastGougeAction(PlayerbotAI* botAI) : CastSpellAction(botAI, "gouge") {}
};

class CastBackstabAction : public CastComboAction
{
public:
    CastBackstabAction(PlayerbotAI* botAI) : CastComboAction(botAI, "backstab") {}
};

#endif
