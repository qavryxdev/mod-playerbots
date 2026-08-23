/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "TargetChurn.h"

#include "AttackersValue.h"
#include "Creature.h"
#include "MotionMaster.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "Timer.h"
#include "Unit.h"

#include <array>
#include <mutex>
#include <unordered_map>

namespace
{
    // How much churn counts as a problem, and how loudly to say so.
    constexpr uint32 ChurnWindowMs = 5000;    // changes older than this stop counting
    constexpr uint32 ChurnThreshold = 5;      // this many inside the window is not normal retargeting
    constexpr uint32 ReportCooldownMs = 30000;  // one report per bot per half minute, no more
    constexpr size_t HistorySize = 8;

    struct TargetChange
    {
        uint32 timeMs = 0;
        uint32 oldEntry = 0;
        uint32 newEntry = 0;
        ObjectGuid oldGuid;
        ObjectGuid newGuid;
        std::string source;
    };

    struct BotHistory
    {
        std::array<TargetChange, HistorySize> entries;
        size_t next = 0;
        size_t count = 0;
        uint32 lastReportMs = 0;
    };

    // Bot AI runs on a worker pool, so the store is sharded the same way the capture cache is.
    constexpr size_t ShardCount = 16;

    struct Shard
    {
        std::mutex mutex;
        std::unordered_map<ObjectGuid, BotHistory> map;
    };

    std::array<Shard, ShardCount>& GetShards()
    {
        static std::array<Shard, ShardCount> shards;
        return shards;
    }

    Shard& GetShard(ObjectGuid const& guid)
    {
        return GetShards()[guid.GetCounter() % ShardCount];
    }

    std::string DescribeUnit(Unit* unit)
    {
        if (!unit)
            return "none";

        return unit->GetName() + "/" + std::to_string(unit->GetEntry());
    }
}

namespace ai::debug
{
    bool TargetChurnDebugEnabled() { return sPlayerbotAIConfig.targetChurnDebug; }

    char const* InvalidTargetReason(Player* bot, Unit* target)
    {
        if (!bot)
            return "no-bot";

        if (!target)
            return "no-target";

        if (target->GetMapId() != bot->GetMapId())
            return "other-map";

        if (target->HasUnitFlag(UNIT_FLAG_NOT_SELECTABLE))
            return "not-selectable";

        if (target->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE) || target->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE_2))
            return "non-attackable";

        if (!target->IsVisible())
            return "not-visible";

        if (!target->IsAlive())
            return "dead";

        if (target->IsPolymorphed())
            return "polymorphed";

        if (target->IsCharmed())
            return "charmed";

        if (target->HasFearAura())
            return "feared";

        if (target->HasUnitState(UNIT_STATE_ISOLATED))
            return "isolated";

        if (target->IsFriendlyTo(bot))
            return "friendly";

        if (!bot->IsWithinLOSInMap(target))
            return "no-los";

        if (Creature* creature = target->ToCreature())
        {
            if (creature->IsInEvadeMode())
                return "evading";

            // The tap rules are the most likely way a mob the bot is already fighting turns invalid
            // from one tick to the next, so name them separately from the rest of IsPossibleTarget.
            if (creature->hasLootRecipient() && !creature->isTappedBy(bot))
                return "tapped-by-other";
        }

        if (!AttackersValue::IsPossibleTarget(target, bot))
            return "not-possible";

        return "valid";
    }

    void NoteTargetChange(Player* bot, Unit* oldTarget, Unit* newTarget, std::string const& source)
    {
        if (!sPlayerbotAIConfig.targetChurnDebug || !bot)
            return;

        // Setting the same target again is not a change and must not count towards the churn.
        if (oldTarget == newTarget)
            return;

        uint32 const now = getMSTime();
        ObjectGuid const botGuid = bot->GetGUID();

        std::string report;
        {
            Shard& shard = GetShard(botGuid);
            std::lock_guard<std::mutex> guard(shard.mutex);
            BotHistory& history = shard.map[botGuid];

            TargetChange& entry = history.entries[history.next];
            entry.timeMs = now;
            entry.oldEntry = oldTarget ? oldTarget->GetEntry() : 0;
            entry.newEntry = newTarget ? newTarget->GetEntry() : 0;
            entry.oldGuid = oldTarget ? oldTarget->GetGUID() : ObjectGuid::Empty;
            entry.newGuid = newTarget ? newTarget->GetGUID() : ObjectGuid::Empty;
            entry.source = source;

            history.next = (history.next + 1) % HistorySize;
            if (history.count < HistorySize)
                ++history.count;

            uint32 recent = 0;
            for (size_t i = 0; i < history.count; ++i)
                if (getMSTimeDiff(history.entries[i].timeMs, now) <= ChurnWindowMs)
                    ++recent;

            if (recent < ChurnThreshold)
                return;

            if (history.lastReportMs && getMSTimeDiff(history.lastReportMs, now) < ReportCooldownMs)
                return;

            history.lastReportMs = now;

            // Oldest first, so the line reads in the order the changes happened.
            for (size_t i = 0; i < history.count; ++i)
            {
                size_t const index = (history.next + i) % HistorySize;
                TargetChange const& change = history.entries[index];
                if (!change.timeMs)
                    continue;

                report += " [-" + std::to_string(getMSTimeDiff(change.timeMs, now)) + "ms " + change.source + " " +
                          std::to_string(change.oldEntry) + "->" + std::to_string(change.newEntry) + "]";
            }
        }

        Unit* victim = bot->GetVictim();
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        uint32 const motionType =
            bot->GetMotionMaster() ? static_cast<uint32>(bot->GetMotionMaster()->GetCurrentMovementGeneratorType()) : 0;

        LOG_INFO("playerbots",
                 "target-churn bot={} map={} zone={} combat={} engine={} moving={} motion={} victim={} selection={} "
                 "invalidReason={} changes={}",
                 bot->GetName(), bot->GetMapId(), bot->GetZoneId(), bot->IsInCombat() ? 1 : 0,
                 botAI && botAI->GetState() == BOT_STATE_COMBAT ? "combat" : "noncombat", bot->isMoving() ? 1 : 0,
                 motionType, DescribeUnit(victim), DescribeUnit(newTarget),
                 InvalidTargetReason(bot, oldTarget ? oldTarget : victim), report);
    }
}
