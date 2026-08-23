/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_TARGETCHURN_H
#define _PLAYERBOT_TARGETCHURN_H

#include <string>

class Player;
class Unit;

// Diagnostics for bots that keep changing their mind about which NPC to fight and end up rooted to
// the spot. Every write to "current target" funnels through NoteTargetChange; when one bot produces
// too many changes in a short window the whole burst is dumped in a single line, naming the code
// path behind each one. Off unless AiPlayerbot.TargetChurnDebug is set, and it allocates nothing
// while off.
namespace ai::debug
{
    bool TargetChurnDebugEnabled();

    // Why InvalidTargetValue would call this target invalid - the answer the "drop target" path
    // never told anyone. Returns "valid" when nothing is wrong with it.
    char const* InvalidTargetReason(Player* bot, Unit* target);

    void NoteTargetChange(Player* bot, Unit* oldTarget, Unit* newTarget, std::string const& source);
}

#endif
