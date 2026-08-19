/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "StallWatch.h"

#include "Battleground.h"
#include "LastMovementValue.h"
#include "MotionMaster.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "PvpTriggers.h"
#include "Random.h"
#include "Timer.h"

#include <atomic>

namespace ai::stall
{
namespace
{
    // What the ladder is allowed to do, in order. Everything below the configured cap is skipped, so
    // StallWatchMaxStep = 0 makes this observe-only: counters move, behaviour does not.
    enum LadderStep : uint8
    {
        STEP_OBSERVE = 0,
        STEP_UNGATE = 1,
        STEP_REPLAN = 2,
        STEP_LOCAL_UNBLOCK = 3,
        STEP_RELOCATE = 4,
        STEP_GIVE_UP = 5,
    };

    // Squared, so the sample never takes a square root. 3 yards, matching the threshold the existing
    // Alterac Valley stall check already uses.
    constexpr float MovedThresholdSq = 9.0f;
    // A bot standing this close to the destination it chose is where it wants to be.
    constexpr float ParkedRadiusSq = 64.0f;
    constexpr uint32 GiveUpCooldownMs = 60000;
    constexpr uint32 ReportIntervalMs = 60000;

    struct StallCounters
    {
        std::atomic<uint64> samples{0};
        std::atomic<uint64> detected{0};
        std::atomic<uint64> ungate{0};
        std::atomic<uint64> replan{0};
        std::atomic<uint64> localUnblock{0};
        std::atomic<uint64> relocate{0};
        std::atomic<uint64> unrecovered{0};
        // Rejections are the safety instrument: if these dwarf "detected" the mask is doing its job,
        // and if "detected" dwarfs them the thresholds are wrong. Read them before raising MaxStep.
        std::atomic<uint64> rejectCannotMove{0};
        std::atomic<uint64> rejectCasting{0};
        std::atomic<uint64> rejectDead{0};
        std::atomic<uint64> rejectPreStart{0};
        std::atomic<uint64> rejectCapturing{0};
        std::atomic<uint64> rejectCombat{0};
        std::atomic<uint64> rejectStay{0};
        std::atomic<uint64> rejectFollowing{0};
        std::atomic<uint64> rejectMoved{0};
        std::atomic<uint64> rejectCommanded{0};
        std::atomic<uint64> rejectParked{0};
    };

    StallCounters counters;
    std::atomic<uint32> lastReportMs{0};

    void Count(std::atomic<uint64>& counter) { counter.fetch_add(1, std::memory_order_relaxed); }

    // All four slots, so auto-shot and wand count too - a hunter standing still and shooting is working.
    bool IsCastingAnything(Player* bot)
    {
        for (uint8 type = CURRENT_MELEE_SPELL; type < CURRENT_MAX_SPELL; ++type)
            if (bot->GetCurrentSpell(static_cast<CurrentSpellTypes>(type)))
                return true;

        return false;
    }

    // A bot engaged with something it can act on is standing still on purpose, at melee range or at
    // caster range. The distance bound is what stops a stale victim pointer from masking a real stall.
    bool IsFightingInPlace(Player* bot)
    {
        Unit* victim = bot->GetVictim();
        if (!victim || !victim->IsAlive())
            return false;

        return bot->IsWithinMeleeRange(victim) || bot->GetDistance(victim) <= 40.0f;
    }

    // Everything here means "not moving is correct right now". A hit resets the counter rather than
    // merely suppressing escalation, because the bot is not stalled, it is busy.
    bool IsLegitimatelyStationary(PlayerbotAI* botAI, Player* bot)
    {
        if (!botAI->CanMove())
        {
            Count(counters.rejectCannotMove);
            return true;
        }

        if (bot->isDead())
        {
            Count(counters.rejectDead);
            return true;
        }

        // A commanded stay is compliance, not a stall. Tested by strategy rather than by the "stay"
        // position, because that position is cleared after every combat while the strategy stays on.
        if (botAI->HasStrategy("stay", BOT_STATE_NON_COMBAT) || botAI->HasStrategy("stay", BOT_STATE_COMBAT) ||
            botAI->HasStrategy("sit", BOT_STATE_NON_COMBAT))
        {
            Count(counters.rejectStay);
            return true;
        }

        // Following a leader who is standing still. MovementAction::Follow drives the bot with
        // MoveFollow and returns early once it is inside follow distance, without stamping
        // "last movement", so neither the commanded test nor the parked test would catch this.
        if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE)
        {
            Count(counters.rejectFollowing);
            return true;
        }

        if (IsCastingAnything(bot))
        {
            Count(counters.rejectCasting);
            return true;
        }

        if (Battleground* bg = bot->GetBattleground())
        {
            // Waiting in the starting pen before the gates open is the single largest source of
            // stationary bots on the server, and none of them are stuck.
            if (bg->GetStatus() != STATUS_IN_PROGRESS)
            {
                Count(counters.rejectPreStart);
                return true;
            }

            if (PlayerHasFlag::IsCapturingFlag(bot))
            {
                Count(counters.rejectCapturing);
                return true;
            }
        }

        if (IsFightingInPlace(bot))
        {
            Count(counters.rejectCombat);
            return true;
        }

        return false;
    }

    void Restart(StallWatchState& state, Player* bot, uint32 now)
    {
        state.lastX = bot->GetPositionX();
        state.lastY = bot->GetPositionY();
        state.lastMapId = bot->GetMapId();
        state.lastSampleMs = now;
        state.stallSamples = 0;
        state.ladderStep = STEP_OBSERVE;
    }

    void LogStep(Player* bot, char const* step, uint16 samples)
    {
        LOG_DEBUG("playerbots", "StallWatch bot={} step={} samples={} pos=({:.1f},{:.1f},{:.1f}) map={}",
                  bot->GetName(), step, static_cast<uint32>(samples), bot->GetPositionX(), bot->GetPositionY(),
                  bot->GetPositionZ(), bot->GetMapId());
    }

    // Full LastMovement::clear(), the same reset AttackAction already uses. It opens both of the gates
    // in MovementAction that silently swallow a re-issued identical destination, and it also drops
    // lastMoveTo* - hence the copy-out first, so the parked test keeps working - plus the follow target
    // and the flee cooldown, neither of which is load bearing on a bot that has not moved for 12s.
    void StepUngate(PlayerbotAI* botAI, StallWatchState& state, Player* bot)
    {
        LastMovement& lastMove = botAI->GetAiObjectContext()->GetValue<LastMovement&>("last movement")->Get();
        if (lastMove.lastMoveToMapId == bot->GetMapId() &&
            (lastMove.lastMoveToX != 0.0f || lastMove.lastMoveToY != 0.0f))
        {
            state.parkedX = lastMove.lastMoveToX;
            state.parkedY = lastMove.lastMoveToY;
            state.parkedZ = lastMove.lastMoveToZ;
            state.parkedMapId = lastMove.lastMoveToMapId;
            state.hasParked = true;
        }

        lastMove.clear();
        Count(counters.ungate);
        LogStep(bot, "ungate", state.stallSamples);
    }

    // Ask the subsystem that owns the bot to decide again. Deliberately does not decide anything itself:
    // inventing a destination here is what turned the previous attempt at this fix into a worse bug.
    void StepReplan(PlayerbotAI* botAI, Player* bot, uint16 samples)
    {
        Battleground* bg = bot->GetBattleground();
        if (!bg || bg->isArena())
            return;

        if (PlayerHasFlag::IsCapturingFlag(bot))
            return;

        botAI->DoSpecificAction("bg reset objective force", Event(), true);
        Count(counters.replan);
        LogStep(bot, "replan", samples);
    }

    // A short hop to break out of geometry the pathfinder will not leave on its own. It exists only in
    // the movement generator - nothing is written to the position map, so no capture logic can see it.
    void StepLocalUnblock(Player* bot, uint16 samples)
    {
        if (bot->isMoving())
            return;

        if (bot->IsSitState())
            bot->SetStandState(UNIT_STAND_STATE_STAND);

        float x, y, z;
        bot->GetRandomPoint(*bot, 10.0f, x, y, z);

        bot->GetMotionMaster()->Clear();
        bot->GetMotionMaster()->MovePoint(0, x, y, z);
        Count(counters.localUnblock);
        LogStep(bot, "local_unblock", samples);
    }

    void StepRelocate(StallWatchState& state, Player* bot, uint16 samples)
    {
        // Teleporting inside instanced PvP is an exploit surface and unfair to the humans in the match.
        if (bot->InBattleground() || bot->InArena())
            return;

        if (!sPlayerbotAIConfig.stallWatchTeleport)
            return;

        // Only ever to the destination the bot itself committed to. Picking a new one here would make
        // the watchdog a planner, which is exactly the boundary it must not cross.
        if (!state.hasParked || state.parkedMapId != bot->GetMapId())
            return;

        bot->GetMotionMaster()->Clear();
        bot->TeleportTo(state.parkedMapId, state.parkedX, state.parkedY, state.parkedZ, bot->GetOrientation());
        Count(counters.relocate);
        LogStep(bot, "relocate", samples);
    }

    void StepGiveUp(StallWatchState& state, Player* bot, uint32 now)
    {
        if (state.giveUpMs && GetMSTimeDiffToNow(state.giveUpMs) < GiveUpCooldownMs)
            return;

        state.giveUpMs = now ? now : 1;
        Count(counters.unrecovered);
        LOG_WARN("playerbots",
                 "StallWatch bot={} did not recover: pos=({:.1f},{:.1f},{:.1f}) map={} bg={} combat={}",
                 bot->GetName(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId(),
                 bot->InBattleground() ? 1 : 0, bot->IsInCombat() ? 1 : 0);
    }
}

void Forget(PlayerbotAI* botAI)
{
    if (!botAI)
        return;

    botAI->stallWatch = StallWatchState();
}

void Sample(PlayerbotAI* botAI, bool minimalAi)
{
    if (!botAI || !sPlayerbotAIConfig.stallWatchEnabled)
        return;

    Player* bot = botAI->GetBot();
    if (!bot || !bot->IsInWorld())
        return;

    // A bot running reduced AI is not being driven anywhere, so it is not stalled either.
    if (minimalAi)
        return;

    StallWatchState& state = botAI->stallWatch;
    uint32 const now = getMSTime();
    uint32 const interval = sPlayerbotAIConfig.stallWatchSampleMs;

    if (!state.lastSampleMs)
    {
        // Spread the first sample so that a whole team, which enters the same world state on the same
        // tick, does not cross the escalation boundary together.
        state.lastX = bot->GetPositionX();
        state.lastY = bot->GetPositionY();
        state.lastMapId = bot->GetMapId();
        state.lastSampleMs = now - urand(0, interval);
        if (!state.lastSampleMs)
            state.lastSampleMs = 1;
        return;
    }

    // A teleport invalidates both the recorded position and any destination the bot was walking to.
    if (state.lastMapId != bot->GetMapId())
    {
        state.hasParked = false;
        state.parkedMapId = 0;
        Restart(state, bot, now);
        return;
    }

    if (GetMSTimeDiffToNow(state.lastSampleMs) < interval)
        return;

    Count(counters.samples);
    // Cheap: one relaxed load unless the minute has elapsed. Keeping it here means the counters are
    // reported wherever bots tick, with no dependency on any other subsystem's maintenance timer.
    ReportStallCounters(now);

    if (IsLegitimatelyStationary(botAI, bot))
    {
        Restart(state, bot, now);
        return;
    }

    float const dx = bot->GetPositionX() - state.lastX;
    float const dy = bot->GetPositionY() - state.lastY;
    if (dx * dx + dy * dy >= MovedThresholdSq)
    {
        Count(counters.rejectMoved);
        Restart(state, bot, now);
        return;
    }

    LastMovement& lastMove = botAI->GetAiObjectContext()->GetValue<LastMovement&>("last movement")->Get();

    // A movement command was accepted inside the window, so something is driving this bot and it is
    // blocked by geometry rather than by a decision that never happened. Different problem, not ours.
    if (lastMove.msTime && GetMSTimeDiffToNow(lastMove.msTime) < interval)
    {
        Count(counters.rejectCommanded);
        Restart(state, bot, now);
        return;
    }

    // Standing on the destination it picked for itself. This is the test that tells a node guard apart
    // from a frozen bot without the watchdog needing to know what an objective is.
    float parkedX = lastMove.lastMoveToX;
    float parkedY = lastMove.lastMoveToY;
    uint32 parkedMapId = lastMove.lastMoveToMapId;
    if (state.hasParked)
    {
        parkedX = state.parkedX;
        parkedY = state.parkedY;
        parkedMapId = state.parkedMapId;
    }

    if (parkedMapId == bot->GetMapId() && (parkedX != 0.0f || parkedY != 0.0f))
    {
        float const px = bot->GetPositionX() - parkedX;
        float const py = bot->GetPositionY() - parkedY;
        if (px * px + py * py < ParkedRadiusSq)
        {
            Count(counters.rejectParked);
            Restart(state, bot, now);
            return;
        }
    }

    state.lastX = bot->GetPositionX();
    state.lastY = bot->GetPositionY();
    state.lastSampleMs = now;

    ++state.stallSamples;
    if (static_cast<uint32>(state.stallSamples) < sPlayerbotAIConfig.stallWatchSamples)
        return;

    if (state.ladderStep == STEP_OBSERVE)
        Count(counters.detected);

    uint8 const step = state.ladderStep < STEP_GIVE_UP ? static_cast<uint8>(state.ladderStep + 1)
                                                       : static_cast<uint8>(STEP_GIVE_UP);
    state.ladderStep = step;

    if (static_cast<uint32>(step) > sPlayerbotAIConfig.stallWatchMaxStep)
        return;

    switch (step)
    {
        case STEP_UNGATE:
            StepUngate(botAI, state, bot);
            break;
        case STEP_REPLAN:
            StepReplan(botAI, bot, state.stallSamples);
            break;
        case STEP_LOCAL_UNBLOCK:
            StepLocalUnblock(bot, state.stallSamples);
            break;
        case STEP_RELOCATE:
            StepRelocate(state, bot, state.stallSamples);
            break;
        default:
            StepGiveUp(state, bot, now);
            break;
    }
}

void ReportStallCounters(uint32 nowMs)
{
    uint32 last = lastReportMs.load(std::memory_order_relaxed);
    if (last && getMSTimeDiff(last, nowMs) < ReportIntervalMs)
        return;

    if (!lastReportMs.compare_exchange_strong(last, nowMs ? nowMs : 1, std::memory_order_relaxed))
        return;

    // First call only starts the clock - reporting a partial window says nothing.
    if (!last)
        return;

    uint64 const samples = counters.samples.exchange(0, std::memory_order_relaxed);
    uint64 const detected = counters.detected.exchange(0, std::memory_order_relaxed);
    uint64 const ungate = counters.ungate.exchange(0, std::memory_order_relaxed);
    uint64 const replan = counters.replan.exchange(0, std::memory_order_relaxed);
    uint64 const localUnblock = counters.localUnblock.exchange(0, std::memory_order_relaxed);
    uint64 const relocate = counters.relocate.exchange(0, std::memory_order_relaxed);
    uint64 const unrecovered = counters.unrecovered.exchange(0, std::memory_order_relaxed);
    uint64 const cannotMove = counters.rejectCannotMove.exchange(0, std::memory_order_relaxed);
    uint64 const casting = counters.rejectCasting.exchange(0, std::memory_order_relaxed);
    uint64 const dead = counters.rejectDead.exchange(0, std::memory_order_relaxed);
    uint64 const preStart = counters.rejectPreStart.exchange(0, std::memory_order_relaxed);
    uint64 const capturing = counters.rejectCapturing.exchange(0, std::memory_order_relaxed);
    uint64 const combat = counters.rejectCombat.exchange(0, std::memory_order_relaxed);
    uint64 const stay = counters.rejectStay.exchange(0, std::memory_order_relaxed);
    uint64 const following = counters.rejectFollowing.exchange(0, std::memory_order_relaxed);
    uint64 const moved = counters.rejectMoved.exchange(0, std::memory_order_relaxed);
    uint64 const commanded = counters.rejectCommanded.exchange(0, std::memory_order_relaxed);
    uint64 const parked = counters.rejectParked.exchange(0, std::memory_order_relaxed);

    if (!samples && !detected)
        return;

    LOG_INFO("playerbots",
             "Stall watch/min: samples={} detected={} s1_ungate={} s2_replan={} s3_unblock={} s4_relocate={} "
             "s5_unrecovered={} | reject cc={} cast={} dead={} prestart={} capture={} combat={} stay={} "
             "follow={} moved={} commanded={} parked={}",
             samples, detected, ungate, replan, localUnblock, relocate, unrecovered, cannotMove, casting, dead,
             preStart, capturing, combat, stay, following, moved, commanded, parked);
}
}
