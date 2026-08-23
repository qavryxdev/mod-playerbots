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

#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace
{
    // Burst reporting: one detailed line for a bot that is visibly thrashing.
    constexpr uint32 ChurnWindowMs = 5000;
    constexpr uint32 ChurnThreshold = 5;
    constexpr uint32 ReportCooldownMs = 30000;
    constexpr size_t HistorySize = 8;

    // Unbiased side of the instrument. The burst report answers "what does a bad case look like",
    // and it cannot answer "how often does this happen" - a bot flipping four times every five
    // seconds never trips the threshold, and the per-bot cooldown throws most of the rest away.
    // These counters have no threshold and no cooldown, so the rate can actually be compared
    // between builds.
    constexpr uint32 SummaryIntervalMs = 60000;
    constexpr size_t SummaryTopBots = 6;

    struct TargetChange
    {
        uint32 timeMs = 0;
        uint32 oldEntry = 0;
        uint32 newEntry = 0;
        std::string source;
    };

    struct BotHistory
    {
        std::array<TargetChange, HistorySize> entries;
        size_t next = 0;
        size_t count = 0;
        uint32 lastReportMs = 0;

        std::string name;
        ObjectGuid lastSelection;
        bool selectionSeen = false;
        uint32 targetChanges = 0;     // writes to "current target"
        uint32 selectionChanges = 0;  // what the client actually shows as the bot's target
        uint32 samples = 0;
    };

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

    Shard& GetShard(ObjectGuid const& guid) { return GetShards()[guid.GetCounter() % ShardCount]; }

    std::mutex& SummaryMutex()
    {
        static std::mutex mutex;
        return mutex;
    }

    std::atomic<uint32> g_lastSummaryMs{0};

    std::string DescribeUnit(Unit* unit)
    {
        if (!unit)
            return "none";

        return unit->GetName() + "/" + std::to_string(unit->GetEntry());
    }

    // Walks every shard once a minute and prints the rate plus the worst offenders, then starts a
    // fresh window. Called from the per-tick sampler, so the time check has to be the cheap path.
    void MaybeEmitSummary(uint32 now)
    {
        uint32 const last = g_lastSummaryMs.load(std::memory_order_relaxed);
        if (last && getMSTimeDiff(last, now) < SummaryIntervalMs)
            return;

        std::lock_guard<std::mutex> summaryGuard(SummaryMutex());
        uint32 const recheck = g_lastSummaryMs.load(std::memory_order_relaxed);
        if (recheck == last && last && getMSTimeDiff(last, now) < SummaryIntervalMs)
            return;

        if (recheck != last)
            return;

        g_lastSummaryMs.store(now, std::memory_order_relaxed);
        if (!last)
            return;  // first call only starts the window

        struct Offender
        {
            std::string name;
            uint32 targetChanges;
            uint32 selectionChanges;
            uint32 samples;
            std::string recent;
        };

        uint64 totalTargetChanges = 0;
        uint64 totalSelectionChanges = 0;
        uint64 totalSamples = 0;
        uint32 activeBots = 0;
        std::vector<Offender> offenders;

        for (Shard& shard : GetShards())
        {
            std::lock_guard<std::mutex> guard(shard.mutex);
            for (auto& [guid, history] : shard.map)
            {
                totalTargetChanges += history.targetChanges;
                totalSelectionChanges += history.selectionChanges;
                totalSamples += history.samples;
                if (history.targetChanges || history.selectionChanges)
                    ++activeBots;

                if (history.targetChanges + history.selectionChanges >= 8)
                {
                    std::string recent;
                    for (size_t i = 0; i < history.count; ++i)
                    {
                        size_t const index = (history.next + i) % HistorySize;
                        TargetChange const& change = history.entries[index];
                        if (!change.timeMs)
                            continue;

                        recent += " " + change.source + "(" + std::to_string(change.oldEntry) + "->" +
                                  std::to_string(change.newEntry) + ")";
                    }

                    offenders.push_back({history.name, history.targetChanges, history.selectionChanges,
                                         history.samples, recent});
                }

                history.targetChanges = 0;
                history.selectionChanges = 0;
                history.samples = 0;
            }
        }

        std::sort(offenders.begin(), offenders.end(),
                  [](Offender const& left, Offender const& right)
                  {
                      return left.targetChanges + left.selectionChanges >
                             right.targetChanges + right.selectionChanges;
                  });

        LOG_INFO("playerbots",
                 "target-churn-summary window=60s bots={} ticks={} targetChanges={} selectionChanges={} "
                 "changesPerBotPerMin={:.2f}",
                 activeBots, totalSamples, totalTargetChanges, totalSelectionChanges,
                 activeBots ? static_cast<double>(totalTargetChanges + totalSelectionChanges) / activeBots : 0.0);

        size_t printed = 0;
        for (Offender const& offender : offenders)
        {
            if (printed++ >= SummaryTopBots)
                break;

            LOG_INFO("playerbots", "target-churn-top bot={} targetChanges={} selectionChanges={} ticks={} recent:{}",
                     offender.name, offender.targetChanges, offender.selectionChanges, offender.samples,
                     offender.recent);
        }
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

    void NoteSelectionSample(Player* bot)
    {
        if (!sPlayerbotAIConfig.targetChurnDebug || !bot)
            return;

        uint32 const now = getMSTime();
        ObjectGuid const selection = bot->GetTarget();

        {
            Shard& shard = GetShard(bot->GetGUID());
            std::lock_guard<std::mutex> guard(shard.mutex);
            BotHistory& history = shard.map[bot->GetGUID()];
            if (history.name.empty())
                history.name = bot->GetName();

            ++history.samples;
            if (!history.selectionSeen)
            {
                history.selectionSeen = true;
                history.lastSelection = selection;
            }
            else if (history.lastSelection != selection)
            {
                ++history.selectionChanges;
                history.lastSelection = selection;
            }
        }

        MaybeEmitSummary(now);
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
            if (history.name.empty())
                history.name = bot->GetName();

            ++history.targetChanges;

            TargetChange& entry = history.entries[history.next];
            entry.timeMs = now;
            entry.oldEntry = oldTarget ? oldTarget->GetEntry() : 0;
            entry.newEntry = newTarget ? newTarget->GetEntry() : 0;
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
