/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_STALLWATCH_H
#define _PLAYERBOT_STALLWATCH_H

#include "Define.h"

class PlayerbotAI;

namespace ai::stall
{
    // Per-bot state, held as a plain member of PlayerbotAI. It is touched only by the map thread that
    // owns the bot, so there is no lock and no allocation here - this runs for every bot on every tick.
    struct StallWatchState
    {
        float lastX = 0.0f;
        float lastY = 0.0f;
        // The destination the bot last committed to. Kept separately because step 1 of the ladder wipes
        // the "last movement" value, and the "is it parked on purpose" test depends on knowing it.
        float parkedX = 0.0f;
        float parkedY = 0.0f;
        float parkedZ = 0.0f;
        uint32 parkedMapId = 0;
        uint32 lastMapId = 0;
        uint32 lastSampleMs = 0;
        uint32 giveUpMs = 0;
        uint16 stallSamples = 0;
        uint8 ladderStep = 0;
        bool hasParked = false;
    };

    // One sample per AI tick. Cheap enough to call unconditionally: it returns immediately unless the
    // sample interval has elapsed. Does nothing at all while the feature is configured off.
    void Sample(PlayerbotAI* botAI, bool minimalAi);

    // Forgets everything about a bot. Call on map change - the millisecond clock and the recorded
    // position are meaningless across a teleport.
    void Forget(PlayerbotAI* botAI);

    // Drains the per-minute counters into one log line. Safe from any thread; rate limited internally.
    void ReportStallCounters(uint32 nowMs);
}

#endif
