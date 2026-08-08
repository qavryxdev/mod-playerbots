/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "PvpTactics.h"

#include "AiObjectContext.h"
#include "AiFactory.h"
#include "Battleground.h"
#include "BattlegroundAB.h"
#include "BattlegroundAV.h"
#include "BattlegroundEY.h"
#include "BattlegroundIC.h"
#include "BattlegroundWS.h"
#include "GameObject.h"
#include "Group.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "ServerFacade.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellDefines.h"
#include "SpellInfo.h"
#include "Timer.h"
#include "Unit.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <iterator>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace
{
    constexpr uint32 InterruptReservationPruneMs = 3000;
    constexpr uint32 CrowdControlReservationPruneMs = 4000;
    constexpr uint32 CombatPlanLifetimeMs = 2600;
    constexpr uint32 CaptureBannerSpellId = 21651;
    constexpr uint32 CaptureCacheLifetimeMs = 300;
    constexpr uint32 MaintenanceIntervalMs = 60000;
    constexpr uint32 StaleEntryMs = 300000;

    // Bot AI runs concurrently on several map update threads. One global mutex per table would
    // serialise every bot on the realm, so each table is split into independent shards.
    template <typename Key, typename Value, typename Hash = std::hash<Key>>
    class ShardedMap
    {
    public:
        static constexpr size_t ShardCount = 16;

        struct Shard
        {
            std::mutex mutex;
            std::unordered_map<Key, Value, Hash> map;
        };

        Shard& GetShard(Key const& key) { return shards[Hash{}(key) % ShardCount]; }
        std::array<Shard, ShardCount>& GetShards() { return shards; }

    private:
        std::array<Shard, ShardCount> shards;
    };

    template <typename Map, typename Predicate>
    void PruneShardedMap(Map& shardedMap, Predicate expired)
    {
        for (auto& shard : shardedMap.GetShards())
        {
            std::lock_guard<std::mutex> guard(shard.mutex);
            for (auto itr = shard.map.begin(); itr != shard.map.end();)
                itr = expired(itr->second) ? shard.map.erase(itr) : std::next(itr);
        }
    }

    bool IsOlderThan(uint32 stampMs, uint32 now, uint32 ageMs)
    {
        return stampMs && getMSTimeDiff(stampMs, now) > ageMs;
    }

    // Rotation gate telemetry. Every PvP rule that can suppress a cast bumps a counter, so a bot
    // standing around doing nothing can be attributed to a specific rule instead of guessed at.
    // Dumped once a minute at INFO level, which the playerbots logger already captures.
    struct GateCounters
    {
        std::atomic<uint64> evaluations{0};
        std::atomic<uint64> crowdControlProtected{0};
        std::atomic<uint64> interruptNotWorthwhile{0};
        std::atomic<uint64> interruptTakenByOther{0};
        std::atomic<uint64> defensiveReserved{0};
        std::atomic<uint64> defensiveNotWarranted{0};
        std::atomic<uint64> burstWindowClosed{0};
        std::atomic<uint64> utilityRejected{0};
        std::atomic<uint64> aoeUnsafe{0};
        std::atomic<uint64> captureObjectiveLock{0};
    };

    GateCounters gateCounters;

    void CountGate(std::atomic<uint64>& counter)
    {
        counter.fetch_add(1, std::memory_order_relaxed);
    }

    void ReportGateCounters()
    {
        uint64 const evaluations = gateCounters.evaluations.exchange(0, std::memory_order_relaxed);
        uint64 const crowdControl = gateCounters.crowdControlProtected.exchange(0, std::memory_order_relaxed);
        uint64 const interruptSkipped = gateCounters.interruptNotWorthwhile.exchange(0, std::memory_order_relaxed);
        uint64 const interruptTaken = gateCounters.interruptTakenByOther.exchange(0, std::memory_order_relaxed);
        uint64 const defensiveReserved = gateCounters.defensiveReserved.exchange(0, std::memory_order_relaxed);
        uint64 const defensiveSkipped = gateCounters.defensiveNotWarranted.exchange(0, std::memory_order_relaxed);
        uint64 const burstClosed = gateCounters.burstWindowClosed.exchange(0, std::memory_order_relaxed);
        uint64 const utility = gateCounters.utilityRejected.exchange(0, std::memory_order_relaxed);
        uint64 const aoe = gateCounters.aoeUnsafe.exchange(0, std::memory_order_relaxed);
        uint64 const captureLock = gateCounters.captureObjectiveLock.exchange(0, std::memory_order_relaxed);

        uint64 const blocked = crowdControl + interruptSkipped + interruptTaken + defensiveReserved +
                               defensiveSkipped + burstClosed + utility + aoe + captureLock;
        if (!evaluations && !blocked)
            return;

        LOG_INFO("playerbots",
                 "PvP gates/min: eval={} blocked={} cc={} interruptSkip={} interruptTaken={} "
                 "defReserved={} defNotWarranted={} burstClosed={} utility={} aoe={} captureLock={}",
                 evaluations, blocked, crowdControl, interruptSkipped, interruptTaken, defensiveReserved,
                 defensiveSkipped, burstClosed, utility, aoe, captureLock);
    }

    struct InterruptReservationKey
    {
        ObjectGuid target;
        uint32 spellId = 0;

        bool operator==(InterruptReservationKey const& other) const
        {
            return target == other.target && spellId == other.spellId;
        }
    };

    struct InterruptReservationKeyHash
    {
        std::size_t operator()(InterruptReservationKey const& key) const
        {
            return std::hash<ObjectGuid>()(key.target) ^ (std::hash<uint32>()(key.spellId) << 1);
        }
    };

    struct InterruptReservation
    {
        ObjectGuid bot;
        uint32 untilMs = 0;
    };

    ShardedMap<InterruptReservationKey, InterruptReservation, InterruptReservationKeyHash> interruptReservations;

    struct CrowdControlReservation
    {
        ObjectGuid bot;
        std::string spell;
        uint32 untilMs = 0;
    };

    struct DefensiveReservation
    {
        std::string spell;
        uint32 untilMs = 0;
    };

    struct CombatPlanState
    {
        ObjectGuid target;
        ai::pvp::CombatPhase phase = ai::pvp::CombatPhase::None;
        uint32 untilMs = 0;
    };

    struct HealthSample
    {
        float healthPct = 100.0f;
        float recentLossPerSecond = 0.0f;
        uint32 sampledMs = 0;
    };

    struct PressureCacheEntry
    {
        ai::pvp::PressureProfile profile;
        uint32 untilMs = 0;
    };

    struct DefenseObservation
    {
        uint32 activeMask = 0;
        uint32 lastSeenMs = 0;
        uint32 unavailableUntilMs = 0;
    };

    struct ObservedDefenseState
    {
        bool active = false;
        bool recentlySpent = false;
    };

    struct OwnedDebuffFamily
    {
        std::string spell;
        int32 remainingMs = 0;
    };

    struct CaptureCacheEntry
    {
        bool hasObjective = false;
        bool prioritizeCapture = false;
        uint32 untilMs = 0;
    };

    ShardedMap<ObjectGuid, CrowdControlReservation> crowdControlReservations;
    ShardedMap<ObjectGuid, DefensiveReservation> defensiveReservations;
    ShardedMap<ObjectGuid, CombatPlanState> combatPlans;
    ShardedMap<ObjectGuid, HealthSample> healthSamples;
    ShardedMap<ObjectGuid, PressureCacheEntry> pressureCache;
    ShardedMap<ObjectGuid, DefenseObservation> defenseObservations;
    ShardedMap<ObjectGuid, CaptureCacheEntry> captureCache;

    std::atomic<uint32> lastMaintenanceMs{0};

    // Every one of these tables used to grow for the lifetime of the process; only the interrupt and
    // crowd control tables were ever pruned. Sweep the rest on a slow timer.
    void RunPeriodicMaintenance(uint32 now)
    {
        uint32 last = lastMaintenanceMs.load(std::memory_order_relaxed);
        if (last && getMSTimeDiff(last, now) < MaintenanceIntervalMs)
            return;

        if (!lastMaintenanceMs.compare_exchange_strong(last, now, std::memory_order_relaxed))
            return;

        // One line per minute, emitted by whichever thread won the CAS above.
        if (last)
            ReportGateCounters();

        PruneShardedMap(defensiveReservations,
            [now](DefensiveReservation const& entry) { return IsOlderThan(entry.untilMs, now, StaleEntryMs); });
        PruneShardedMap(combatPlans,
            [now](CombatPlanState const& entry) { return IsOlderThan(entry.untilMs, now, StaleEntryMs); });
        PruneShardedMap(healthSamples,
            [now](HealthSample const& entry) { return IsOlderThan(entry.sampledMs, now, StaleEntryMs); });
        PruneShardedMap(pressureCache,
            [now](PressureCacheEntry const& entry) { return IsOlderThan(entry.untilMs, now, StaleEntryMs); });
        PruneShardedMap(captureCache,
            [now](CaptureCacheEntry const& entry) { return IsOlderThan(entry.untilMs, now, StaleEntryMs); });
        PruneShardedMap(defenseObservations, [now](DefenseObservation const& entry)
        {
            return !entry.activeMask && IsOlderThan(entry.lastSeenMs, now, StaleEntryMs) &&
                   IsOlderThan(entry.unavailableUntilMs, now, StaleEntryMs);
        });
    }

    SpellInfo const* GetCurrentCastingSpell(Unit* target)
    {
        if (!target)
            return nullptr;

        if (Spell* spell = target->GetCurrentSpell(CURRENT_GENERIC_SPELL))
            if (spell->m_spellInfo)
                return spell->m_spellInfo;

        if (Spell* spell = target->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
            if (spell->m_spellInfo)
                return spell->m_spellInfo;

        return nullptr;
    }

    Player* GetControllingPlayer(Unit* target)
    {
        if (!target)
            return nullptr;

        if (Player* player = target->ToPlayer())
            return player;

        return target->GetCharmerOrOwnerPlayerOrPlayerItself();
    }

    std::string NormalizeSpellName(std::string name)
    {
        std::transform(name.begin(), name.end(), name.begin(),
            [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        return name;
    }

    OwnedDebuffFamily GetOwnedDebuffFamily(Player* bot, Unit* target, SpellSpecificType family)
    {
        OwnedDebuffFamily result;
        if (!bot || !target)
            return result;

        Unit::AuraApplicationMap const& auras = target->GetAppliedAuras();
        for (auto const& [_, aurApp] : auras)
        {
            Aura const* aura = aurApp ? aurApp->GetBase() : nullptr;
            SpellInfo const* spellInfo = aura ? aura->GetSpellInfo() : nullptr;
            if (!aura || aura->IsRemoved() || !spellInfo || aura->GetCasterGUID() != bot->GetGUID() ||
                spellInfo->GetSpellSpecific() != family)
                continue;

            result.spell = NormalizeSpellName(spellInfo->SpellName[0]);
            result.remainingMs = aura->GetDuration();
            return result;
        }

        return result;
    }

    bool KnowsNamedSpell(PlayerbotAI* botAI, std::string const& spell)
    {
        return botAI && botAI->GetAiObjectContext()->GetValue<uint32>("spell id", spell)->Get();
    }

    float GetManaPct(Unit* target)
    {
        if (!target)
            return 0.0f;

        uint32 const maximum = target->GetMaxPower(POWER_MANA);
        return maximum ? 100.0f * static_cast<float>(target->GetPower(POWER_MANA)) /
                             static_cast<float>(maximum) : 0.0f;
    }

    uint32 GetMajorDefenseMask(Unit* target)
    {
        if (!target)
            return 0;

        uint32 mask = 0;
        if (target->HasAura(642) || target->HasAura(1020))       // Divine Shield
            mask |= 1u << 0;
        if (target->HasAura(1022) || target->HasAura(5599) || target->HasAura(10278)) // Hand of Protection
            mask |= 1u << 1;
        if (target->HasAura(45438))                              // Ice Block
            mask |= 1u << 2;
        if (target->HasAura(31224))                              // Cloak of Shadows
            mask |= 1u << 3;
        if (target->HasAura(5277) || target->HasAura(26669))    // Evasion
            mask |= 1u << 4;
        if (target->HasAura(47585))                              // Dispersion
            mask |= 1u << 5;
        if (target->HasAura(33206) || target->HasAura(47788))   // Pain Suppression / Guardian Spirit
            mask |= 1u << 6;
        if (target->HasAura(22812) || target->HasAura(61336))   // Barkskin / Survival Instincts
            mask |= 1u << 7;
        if (target->HasAura(871))                                // Shield Wall
            mask |= 1u << 8;
        if (target->HasAura(19263))                              // Deterrence
            mask |= 1u << 9;
        if (target->HasAura(48707) || target->HasAura(48792))   // Anti-Magic Shell / Icebound Fortitude
            mask |= 1u << 10;
        if (target->HasAura(30823))                              // Shamanistic Rage
            mask |= 1u << 11;

        return mask;
    }

    uint32 GetObservedDefenseCooldownMs(uint32 mask)
    {
        if (mask & ((1u << 0) | (1u << 2) | (1u << 8)))
            return 120 * IN_MILLISECONDS;
        if (mask & ((1u << 3) | (1u << 5) | (1u << 9)))
            return 75 * IN_MILLISECONDS;
        return 45 * IN_MILLISECONDS;
    }

    ObservedDefenseState ObserveMajorDefense(Unit* target)
    {
        ObservedDefenseState result;
        if (!target || !target->GetGUID())
            return result;

        uint32 const now = getMSTime();
        uint32 const currentMask = GetMajorDefenseMask(target);
        auto& shard = defenseObservations.GetShard(target->GetGUID());
        std::lock_guard<std::mutex> guard(shard.mutex);
        auto itr = shard.map.find(target->GetGUID());

        if (currentMask)
        {
            if (itr == shard.map.end())
                itr = shard.map.emplace(target->GetGUID(), DefenseObservation{}).first;

            DefenseObservation& observation = itr->second;
            observation.activeMask = currentMask;
            observation.lastSeenMs = now;
            result.active = true;
            return result;
        }

        if (itr == shard.map.end())
            return result;

        DefenseObservation& observation = itr->second;
        if (observation.activeMask)
        {
            if (observation.lastSeenMs && getMSTimeDiff(observation.lastSeenMs, now) <= 15000)
                observation.unavailableUntilMs = now + GetObservedDefenseCooldownMs(observation.activeMask);

            observation.activeMask = 0;
        }

        result.recentlySpent = observation.unavailableUntilMs &&
            static_cast<int32>(observation.unavailableUntilMs - now) > 0;
        if (!result.recentlySpent)
            shard.map.erase(itr);

        return result;
    }

    // Emergency buttons worth coordinating. Deliberately excludes mana shield, shamanistic rage and
    // stoneclaw totem: those are routine upkeep/regen casts, and gating them behind combat pressure
    // meant a mage never pre-shielded and an enhancement shaman never regenerated mana.
    bool IsPersonalDefensiveSpell(std::string const& spell)
    {
        static std::unordered_set<std::string> const spells =
        {
            "anti magic shell", "barkskin", "cloak of shadows", "deterrence", "dispersion", "divine shield",
            "evasion", "ice block", "icebound fortitude", "retaliation", "shield wall", "survival instincts"
        };
        return spells.find(spell) != spells.end();
    }

    // Higher tier may pre-empt a lower tier reservation - a mage must be able to follow barkskin-class
    // filler with ice block inside the same reservation window.
    uint8 GetDefensiveTier(std::string const& spell)
    {
        if (spell == "divine shield" || spell == "ice block" || spell == "shield wall" || spell == "dispersion")
            return 2;
        return IsPersonalDefensiveSpell(spell) ? 1 : 0;
    }

    // Burst cooldowns that must be spent on an opening, not hoarded.
    bool IsOffensiveCooldownSpell(std::string const& spell)
    {
        static std::unordered_set<std::string> const spells =
        {
            "adrenaline rush", "arcane power", "avenging wrath", "berserk", "bestial wrath", "bladestorm",
            "cold blood", "combustion", "dancing rune weapon", "death wish", "demonic empowerment",
            "elemental mastery", "icy veins", "killing spree",
            "rapid fire", "shadow dance"
        };
        // starfall / feral spirit / force of nature / summon gargoyle are used on cooldown as plain
        // damage, so they are intentionally absent - gating them wasted most of their uptime.
        // metamorphosis is absent because it is also a defensive cooldown; MetamorphosisTrigger
        // decides when to spend it.
        return spells.find(spell) != spells.end();
    }

    // Control that opens a burst window even at full target health.
    bool IsControlledForBurst(Unit* target)
    {
        return target && (
            target->HasAuraType(SPELL_AURA_MOD_STUN) ||
            target->HasAuraType(SPELL_AURA_MOD_ROOT) ||
            target->HasAuraType(SPELL_AURA_MOD_SILENCE) ||
            target->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED) ||
            target->isFrozen());
    }

    uint32 CountFriendlyPlayerAttackers(PlayerbotAI* botAI, Unit* target)
    {
        if (!botAI || !target)
            return 0;

        uint32 count = 0;
        for (Unit* attacker : target->getAttackers())
        {
            Player* player = GetControllingPlayer(attacker);
            if (player && !botAI->IsOpposing(player))
                ++count;
        }

        return count;
    }

    bool IsImportantDispelAura(SpellInfo const* spellInfo, uint32 dispelType)
    {
        if (!spellInfo || !spellInfo->IsPositive() || spellInfo->Dispel != dispelType)
            return false;

        switch (spellInfo->Id)
        {
            case 642:    // Divine Shield
            case 1020:
            case 1022:   // Blessing/Hand of Protection
            case 5599:
            case 10278:
            case 1044:   // Blessing/Hand of Freedom
            case 6940:   // Blessing/Hand of Sacrifice
            case 2825:   // Bloodlust
            case 32182:  // Heroism
            case 31884:  // Avenging Wrath
            case 12042:  // Arcane Power
            case 12472:  // Icy Veins
            case 10060:  // Power Infusion
            case 17116:  // Nature's Swiftness
            case 16188:
            case 16166:  // Elemental Mastery
            case 49016:  // Hysteria
            case 17:     // Power Word: Shield ranks
            case 592:
            case 600:
            case 3747:
            case 6065:
            case 6066:
            case 10898:
            case 10899:
            case 10900:
            case 10901:
            case 25217:
            case 25218:
            case 48065:
            case 48066:
            case 11426:  // Ice Barrier ranks
            case 13031:
            case 13032:
            case 13033:
            case 27134:
            case 33405:
            case 43038:
            case 43039:
            case 974:    // Earth Shield ranks
            case 32593:
            case 32594:
            case 49283:
            case 49284:
                return true;
            default:
                break;
        }

        for (SpellEffectInfo const& effect : spellInfo->Effects)
        {
            if (effect.ApplyAuraName == SPELL_AURA_SCHOOL_IMMUNITY ||
                effect.ApplyAuraName == SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN ||
                effect.ApplyAuraName == SPELL_AURA_MOD_DAMAGE_PERCENT_DONE ||
                effect.ApplyAuraName == SPELL_AURA_MOD_INCREASE_SPEED ||
                effect.ApplyAuraName == SPELL_AURA_MOD_ROOT)
                return true;
        }

        return false;
    }

    uint32 CountUsefulDispelAuras(Unit* target, uint32 dispelType, bool* hasImportantAura)
    {
        if (hasImportantAura)
            *hasImportantAura = false;

        if (!target)
            return 0;

        uint32 count = 0;
        Unit::AuraApplicationMap const& auras = target->GetAppliedAuras();
        for (Unit::AuraApplicationMap::const_iterator itr = auras.begin(); itr != auras.end(); ++itr)
        {
            AuraApplication const* aurApp = itr->second;
            Aura const* aura = aurApp ? aurApp->GetBase() : nullptr;
            SpellInfo const* spellInfo = aura ? aura->GetSpellInfo() : nullptr;
            if (!aura || aura->IsRemoved() || !spellInfo || !spellInfo->IsPositive() || spellInfo->Dispel != dispelType)
                continue;

            ++count;
            if (hasImportantAura && IsImportantDispelAura(spellInfo, dispelType))
                *hasImportantAura = true;
        }

        return count;
    }

    template <typename Shard>
    void PruneInterruptReservations(Shard& shard, uint32 now)
    {
        for (auto itr = shard.map.begin(); itr != shard.map.end();)
        {
            if (itr->second.untilMs + InterruptReservationPruneMs < now)
                itr = shard.map.erase(itr);
            else
                ++itr;
        }
    }

    template <typename Shard>
    void PruneCrowdControlReservations(Shard& shard, uint32 now)
    {
        for (auto itr = shard.map.begin(); itr != shard.map.end();)
        {
            if (itr->second.untilMs + CrowdControlReservationPruneMs < now)
                itr = shard.map.erase(itr);
            else
                ++itr;
        }
    }

    bool IsSameMapUnit(Player* bot, Unit* target)
    {
        return bot && target && target->IsInWorld() && target->IsAlive() && target->GetMapId() == bot->GetMapId();
    }

    BattlegroundTypeId GetRealBattlegroundType(Battleground* bg)
    {
        if (!bg)
            return BATTLEGROUND_TYPE_NONE;

        BattlegroundTypeId bgType = bg->GetBgTypeID();
        return bgType == BATTLEGROUND_RB ? bg->GetBgTypeID(true) : bgType;
    }

    bool GetActiveBattlegroundObjective(PlayerbotAI* botAI, Player* bot, PositionInfo& objective)
    {
        if (!botAI || !bot || !bot->InBattleground())
            return false;

        PositionMap& positions = botAI->GetAiObjectContext()->GetValue<PositionMap&>("position")->Get();
        auto const itr = positions.find("bg objective");
        if (itr == positions.end() || !itr->second.valueSet)
            return false;

        objective = itr->second;
        return true;
    }

    bool IsPositionNear2d(PositionInfo const& position, float x, float y, float radius)
    {
        if (!position.valueSet)
            return false;

        float const dx = position.x - x;
        float const dy = position.y - y;
        return dx * dx + dy * dy <= radius * radius;
    }

    bool IsObjectiveNearBgObject(Battleground* bg, PositionInfo const& objective, uint32 objectType, float radius)
    {
        GameObject* go = bg ? bg->GetBGObject(objectType) : nullptr;
        return go && IsPositionNear2d(objective, go->GetPositionX(), go->GetPositionY(), radius);
    }

    bool IsObjectiveNearAnyBgObject(Battleground* bg, PositionInfo const& objective, uint32 const* objectTypes,
                                    size_t objectTypeCount, float radius)
    {
        for (size_t i = 0; i < objectTypeCount; ++i)
            if (IsObjectiveNearBgObject(bg, objective, objectTypes[i], radius))
                return true;

        return false;
    }

    bool IsObjectiveNearArathiNode(PositionInfo const& objective)
    {
        for (uint8 i = 0; i < BG_AB_DYNAMIC_NODES_COUNT; ++i)
            if (IsPositionNear2d(objective, BG_AB_NodePositions[i][0], BG_AB_NodePositions[i][1], 32.0f))
                return true;

        return false;
    }

    bool IsObjectiveNearEyeNode(PositionInfo const& objective)
    {
        for (uint8 i = 0; i < EY_POINTS_MAX; ++i)
            if (IsPositionNear2d(objective, BG_EY_TriggerPositions[i][0], BG_EY_TriggerPositions[i][1], 40.0f))
                return true;

        return false;
    }

    bool HasCaptureBannerCast(Player* bot)
    {
        if (!bot)
            return false;

        for (uint8 type = CURRENT_MELEE_SPELL; type <= CURRENT_CHANNELED_SPELL; ++type)
        {
            if (Spell* spell = bot->GetCurrentSpell(static_cast<CurrentSpellTypes>(type)))
                if (spell->m_spellInfo && spell->m_spellInfo->Id == CaptureBannerSpellId)
                    return true;
        }

        return false;
    }

    GameObject* GetCaptureBannerTarget(Unit* unit)
    {
        if (!unit)
            return nullptr;

        for (uint8 type = CURRENT_MELEE_SPELL; type <= CURRENT_CHANNELED_SPELL; ++type)
        {
            if (Spell* spell = unit->GetCurrentSpell(static_cast<CurrentSpellTypes>(type)))
            {
                if (spell->m_spellInfo && spell->m_spellInfo->Id == CaptureBannerSpellId)
                    return spell->m_targets.GetGOTarget();
            }
        }

        return nullptr;
    }

    bool IsCarryingBattlegroundFlag(Player* bot)
    {
        return bot && (bot->HasAura(BG_WS_SPELL_WARSONG_FLAG) || bot->HasAura(BG_WS_SPELL_SILVERWING_FLAG) ||
                       bot->HasAura(BG_EY_NETHERSTORM_FLAG_SPELL));
    }

    uint32 const* GetAlteracCaptureObjectIds(size_t& objectCount)
    {
        static uint32 const avCaptureObjects[] =
        {
            BG_AV_OBJECT_FLAG_A_FIRSTAID_STATION,
            BG_AV_OBJECT_FLAG_A_STORMPIKE_GRAVE,
            BG_AV_OBJECT_FLAG_A_STONEHEART_GRAVE,
            BG_AV_OBJECT_FLAG_A_SNOWFALL_GRAVE,
            BG_AV_OBJECT_FLAG_A_ICEBLOOD_GRAVE,
            BG_AV_OBJECT_FLAG_A_FROSTWOLF_GRAVE,
            BG_AV_OBJECT_FLAG_A_FROSTWOLF_HUT,
            BG_AV_OBJECT_FLAG_A_DUNBALDAR_SOUTH,
            BG_AV_OBJECT_FLAG_A_DUNBALDAR_NORTH,
            BG_AV_OBJECT_FLAG_A_ICEWING_BUNKER,
            BG_AV_OBJECT_FLAG_A_STONEHEART_BUNKER,
            BG_AV_OBJECT_FLAG_C_A_FIRSTAID_STATION,
            BG_AV_OBJECT_FLAG_C_A_STORMPIKE_GRAVE,
            BG_AV_OBJECT_FLAG_C_A_STONEHEART_GRAVE,
            BG_AV_OBJECT_FLAG_C_A_SNOWFALL_GRAVE,
            BG_AV_OBJECT_FLAG_C_A_ICEBLOOD_GRAVE,
            BG_AV_OBJECT_FLAG_C_A_FROSTWOLF_GRAVE,
            BG_AV_OBJECT_FLAG_C_A_FROSTWOLF_HUT,
            BG_AV_OBJECT_FLAG_C_A_ICEBLOOD_TOWER,
            BG_AV_OBJECT_FLAG_C_A_TOWER_POINT,
            BG_AV_OBJECT_FLAG_C_A_FROSTWOLF_ETOWER,
            BG_AV_OBJECT_FLAG_C_A_FROSTWOLF_WTOWER,
            BG_AV_OBJECT_FLAG_C_H_FIRSTAID_STATION,
            BG_AV_OBJECT_FLAG_C_H_STORMPIKE_GRAVE,
            BG_AV_OBJECT_FLAG_C_H_STONEHEART_GRAVE,
            BG_AV_OBJECT_FLAG_C_H_SNOWFALL_GRAVE,
            BG_AV_OBJECT_FLAG_C_H_ICEBLOOD_GRAVE,
            BG_AV_OBJECT_FLAG_C_H_FROSTWOLF_GRAVE,
            BG_AV_OBJECT_FLAG_C_H_FROSTWOLF_HUT,
            BG_AV_OBJECT_FLAG_C_H_DUNBALDAR_SOUTH,
            BG_AV_OBJECT_FLAG_C_H_DUNBALDAR_NORTH,
            BG_AV_OBJECT_FLAG_C_H_ICEWING_BUNKER,
            BG_AV_OBJECT_FLAG_C_H_STONEHEART_BUNKER,
            BG_AV_OBJECT_FLAG_H_FIRSTAID_STATION,
            BG_AV_OBJECT_FLAG_H_STORMPIKE_GRAVE,
            BG_AV_OBJECT_FLAG_H_STONEHEART_GRAVE,
            BG_AV_OBJECT_FLAG_H_SNOWFALL_GRAVE,
            BG_AV_OBJECT_FLAG_H_ICEBLOOD_GRAVE,
            BG_AV_OBJECT_FLAG_H_FROSTWOLF_GRAVE,
            BG_AV_OBJECT_FLAG_H_FROSTWOLF_HUT,
            BG_AV_OBJECT_FLAG_H_ICEBLOOD_TOWER,
            BG_AV_OBJECT_FLAG_H_TOWER_POINT,
            BG_AV_OBJECT_FLAG_H_FROSTWOLF_ETOWER,
            BG_AV_OBJECT_FLAG_H_FROSTWOLF_WTOWER,
            BG_AV_OBJECT_FLAG_N_SNOWFALL_GRAVE
        };

        objectCount = std::size(avCaptureObjects);
        return avCaptureObjects;
    }

    bool IsAlteracCaptureObjective(Battleground* bg, PositionInfo const& objective)
    {
        size_t objectCount = 0;
        uint32 const* avCaptureObjects = GetAlteracCaptureObjectIds(objectCount);
        return IsObjectiveNearAnyBgObject(bg, objective, avCaptureObjects, objectCount, 45.0f);
    }

    bool GetAlteracObjectNodeId(uint32 objectType, uint8& nodeId)
    {
        switch (objectType)
        {
            case BG_AV_OBJECT_FLAG_A_FIRSTAID_STATION:
            case BG_AV_OBJECT_FLAG_H_FIRSTAID_STATION:
            case BG_AV_OBJECT_FLAG_C_A_FIRSTAID_STATION:
            case BG_AV_OBJECT_FLAG_C_H_FIRSTAID_STATION:
                nodeId = BG_AV_NODES_FIRSTAID_STATION;
                return true;
            case BG_AV_OBJECT_FLAG_A_STORMPIKE_GRAVE:
            case BG_AV_OBJECT_FLAG_H_STORMPIKE_GRAVE:
            case BG_AV_OBJECT_FLAG_C_A_STORMPIKE_GRAVE:
            case BG_AV_OBJECT_FLAG_C_H_STORMPIKE_GRAVE:
                nodeId = BG_AV_NODES_STORMPIKE_GRAVE;
                return true;
            case BG_AV_OBJECT_FLAG_A_STONEHEART_GRAVE:
            case BG_AV_OBJECT_FLAG_H_STONEHEART_GRAVE:
            case BG_AV_OBJECT_FLAG_C_A_STONEHEART_GRAVE:
            case BG_AV_OBJECT_FLAG_C_H_STONEHEART_GRAVE:
                nodeId = BG_AV_NODES_STONEHEART_GRAVE;
                return true;
            case BG_AV_OBJECT_FLAG_A_SNOWFALL_GRAVE:
            case BG_AV_OBJECT_FLAG_H_SNOWFALL_GRAVE:
            case BG_AV_OBJECT_FLAG_C_A_SNOWFALL_GRAVE:
            case BG_AV_OBJECT_FLAG_C_H_SNOWFALL_GRAVE:
            case BG_AV_OBJECT_FLAG_N_SNOWFALL_GRAVE:
                nodeId = BG_AV_NODES_SNOWFALL_GRAVE;
                return true;
            case BG_AV_OBJECT_FLAG_A_ICEBLOOD_GRAVE:
            case BG_AV_OBJECT_FLAG_H_ICEBLOOD_GRAVE:
            case BG_AV_OBJECT_FLAG_C_A_ICEBLOOD_GRAVE:
            case BG_AV_OBJECT_FLAG_C_H_ICEBLOOD_GRAVE:
                nodeId = BG_AV_NODES_ICEBLOOD_GRAVE;
                return true;
            case BG_AV_OBJECT_FLAG_A_FROSTWOLF_GRAVE:
            case BG_AV_OBJECT_FLAG_H_FROSTWOLF_GRAVE:
            case BG_AV_OBJECT_FLAG_C_A_FROSTWOLF_GRAVE:
            case BG_AV_OBJECT_FLAG_C_H_FROSTWOLF_GRAVE:
                nodeId = BG_AV_NODES_FROSTWOLF_GRAVE;
                return true;
            case BG_AV_OBJECT_FLAG_A_FROSTWOLF_HUT:
            case BG_AV_OBJECT_FLAG_H_FROSTWOLF_HUT:
            case BG_AV_OBJECT_FLAG_C_A_FROSTWOLF_HUT:
            case BG_AV_OBJECT_FLAG_C_H_FROSTWOLF_HUT:
                nodeId = BG_AV_NODES_FROSTWOLF_HUT;
                return true;
            case BG_AV_OBJECT_FLAG_A_DUNBALDAR_SOUTH:
            case BG_AV_OBJECT_FLAG_C_H_DUNBALDAR_SOUTH:
                nodeId = BG_AV_NODES_DUNBALDAR_SOUTH;
                return true;
            case BG_AV_OBJECT_FLAG_A_DUNBALDAR_NORTH:
            case BG_AV_OBJECT_FLAG_C_H_DUNBALDAR_NORTH:
                nodeId = BG_AV_NODES_DUNBALDAR_NORTH;
                return true;
            case BG_AV_OBJECT_FLAG_A_ICEWING_BUNKER:
            case BG_AV_OBJECT_FLAG_C_H_ICEWING_BUNKER:
                nodeId = BG_AV_NODES_ICEWING_BUNKER;
                return true;
            case BG_AV_OBJECT_FLAG_A_STONEHEART_BUNKER:
            case BG_AV_OBJECT_FLAG_C_H_STONEHEART_BUNKER:
                nodeId = BG_AV_NODES_STONEHEART_BUNKER;
                return true;
            case BG_AV_OBJECT_FLAG_H_ICEBLOOD_TOWER:
            case BG_AV_OBJECT_FLAG_C_A_ICEBLOOD_TOWER:
                nodeId = BG_AV_NODES_ICEBLOOD_TOWER;
                return true;
            case BG_AV_OBJECT_FLAG_H_TOWER_POINT:
            case BG_AV_OBJECT_FLAG_C_A_TOWER_POINT:
                nodeId = BG_AV_NODES_TOWER_POINT;
                return true;
            case BG_AV_OBJECT_FLAG_H_FROSTWOLF_ETOWER:
            case BG_AV_OBJECT_FLAG_C_A_FROSTWOLF_ETOWER:
                nodeId = BG_AV_NODES_FROSTWOLF_ETOWER;
                return true;
            case BG_AV_OBJECT_FLAG_H_FROSTWOLF_WTOWER:
            case BG_AV_OBJECT_FLAG_C_A_FROSTWOLF_WTOWER:
                nodeId = BG_AV_NODES_FROSTWOLF_WTOWER;
                return true;
            default:
                return false;
        }
    }

    bool AlteracNodeNeedsCaptureByTeam(BattlegroundAV* av, uint8 nodeId, TeamId teamId)
    {
        if (!av || nodeId >= BG_AV_NODES_MAX)
            return false;

        BG_AV_NodeInfo const& node = av->GetAVNodeInfo(nodeId);
        if (node.State == POINT_DESTROYED)
            return false;

        if (node.State == POINT_ASSAULTED)
            return node.OwnerId != teamId && node.PrevOwnerId == teamId;

        return node.OwnerId != teamId && node.TotalOwnerId != teamId;
    }

    bool IsAlteracUsableCaptureObjective(Player* bot, Battleground* bg, PositionInfo const& objective)
    {
        if (!bot || !bg || !objective.valueSet)
            return false;

        BattlegroundAV* av = static_cast<BattlegroundAV*>(bg);
        size_t objectCount = 0;
        uint32 const* avCaptureObjects = GetAlteracCaptureObjectIds(objectCount);
        for (size_t i = 0; i < objectCount; ++i)
        {
            uint32 const objectType = avCaptureObjects[i];
            GameObject* go = bg->GetBGObject(objectType);
            if (!go || !go->isSpawned() || go->GetGoState() != GO_STATE_READY ||
                !IsPositionNear2d(objective, go->GetPositionX(), go->GetPositionY(), 45.0f))
                continue;

            uint8 nodeId = 0;
            if (!GetAlteracObjectNodeId(objectType, nodeId) ||
                !AlteracNodeNeedsCaptureByTeam(av, nodeId, bot->GetTeamId()))
                continue;

            if (bot->CanUseBattlegroundObject(go))
                return true;
        }

        return false;
    }

    bool IsIsleCaptureObjective(Battleground* bg, PositionInfo const& objective)
    {
        static uint32 const icCaptureObjects[] =
        {
            BG_IC_GO_ALLIANCE_BANNER,
            BG_IC_GO_HORDE_BANNER,
            BG_IC_GO_WORKSHOP_BANNER,
            BG_IC_GO_DOCKS_BANNER,
            BG_IC_GO_HANGAR_BANNER,
            BG_IC_GO_QUARRY_BANNER,
            BG_IC_GO_REFINERY_BANNER
        };

        return IsObjectiveNearAnyBgObject(bg, objective, icCaptureObjects, std::size(icCaptureObjects), 45.0f);
    }

    bool IsEyeCaptureObjective(Battleground* bg, PositionInfo const& objective)
    {
        static uint32 const eyCaptureObjects[] =
        {
            BG_EY_OBJECT_FLAG_NETHERSTORM,
            BG_EY_OBJECT_FLAG_FEL_REAVER,
            BG_EY_OBJECT_FLAG_BLOOD_ELF,
            BG_EY_OBJECT_FLAG_DRAENEI_RUINS,
            BG_EY_OBJECT_FLAG_MAGE_TOWER,
            BG_EY_OBJECT_TOWER_CAP_FEL_REAVER,
            BG_EY_OBJECT_TOWER_CAP_BLOOD_ELF,
            BG_EY_OBJECT_TOWER_CAP_DRAENEI_RUINS,
            BG_EY_OBJECT_TOWER_CAP_MAGE_TOWER
        };

        return IsObjectiveNearAnyBgObject(bg, objective, eyCaptureObjects, std::size(eyCaptureObjects), 38.0f) ||
               IsObjectiveNearEyeNode(objective);
    }

    bool IsWarsongCaptureObjective(Battleground* bg, PositionInfo const& objective)
    {
        static uint32 const wsCaptureObjects[] =
        {
            BG_WS_OBJECT_A_FLAG,
            BG_WS_OBJECT_H_FLAG
        };

        return IsObjectiveNearAnyBgObject(bg, objective, wsCaptureObjects, std::size(wsCaptureObjects), 45.0f);
    }
}

namespace ai::pvp
{
    bool IsPvpContext(Player* bot)
    {
        // The PvP flag used to be part of this test, which meant a flagged bot questing in the open
        // world ran the whole PvP tactic layer against ordinary creatures: it refused to damage
        // feared mobs, hoarded its offensive cooldowns and never pre-cast its own upkeep buffs.
        return bot && (bot->InBattleground() || bot->InArena() || bot->duel);
    }

    bool IsEnemyPlayerOrOwnedUnit(PlayerbotAI* botAI, Unit* target)
    {
        if (!botAI || !target)
            return false;

        Player* owner = target->ToPlayer();
        if (!owner)
            owner = target->GetCharmerOrOwnerPlayerOrPlayerItself();

        return owner && botAI->IsOpposing(owner);
    }

    bool IsCastingHelpfulSpell(Unit* target)
    {
        SpellInfo const* spellInfo = GetCurrentCastingSpell(target);
        return spellInfo && (spellInfo->IsPositive() ||
            PlayerbotAI::IsHealingSpell(spellInfo->SpellFamilyName, spellInfo->SpellFamilyFlags));
    }

    bool IsPvpContext(PlayerbotAI* botAI, Unit* target)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        return bot && (IsPvpContext(bot) || IsEnemyPlayerOrOwnedUnit(botAI, target));
    }

    TargetProfile GetTargetProfile(PlayerbotAI* botAI, Unit* target)
    {
        TargetProfile profile;
        Player* player = GetControllingPlayer(target);
        if (!botAI || !player)
            return profile;

        profile.valid = true;
        profile.playerClass = player->getClass();
        profile.specTab = AiFactory::GetPlayerSpecTab(player);
        profile.healer = botAI->IsHeal(player);
        profile.usesMana = player->GetMaxPower(POWER_MANA) > 0;

        switch (profile.playerClass)
        {
            case CLASS_WARRIOR:
            case CLASS_ROGUE:
                profile.melee = true;
                profile.physicalDamage = true;
                break;
            case CLASS_HUNTER:
                profile.rangedPhysical = true;
                profile.physicalDamage = true;
                break;
            case CLASS_DEATH_KNIGHT:
                profile.melee = true;
                profile.physicalDamage = true;
                profile.magicDamage = true;
                break;
            case CLASS_PALADIN:
                if (profile.specTab == PALADIN_TAB_HOLY || profile.healer)
                {
                    profile.caster = true;
                    profile.magicDamage = true;
                }
                else
                {
                    profile.melee = true;
                    profile.physicalDamage = true;
                    profile.magicDamage = true;
                }
                break;
            case CLASS_PRIEST:
            case CLASS_MAGE:
            case CLASS_WARLOCK:
                profile.caster = true;
                profile.magicDamage = true;
                break;
            case CLASS_SHAMAN:
                if (profile.specTab == SHAMAN_TAB_ENHANCEMENT)
                {
                    profile.melee = true;
                    profile.physicalDamage = true;
                    profile.magicDamage = true;
                }
                else
                {
                    profile.caster = true;
                    profile.magicDamage = true;
                }
                break;
            case CLASS_DRUID:
            {
                ShapeshiftForm const form = player->GetShapeshiftForm();
                profile.feralDruid = profile.specTab == DRUID_TAB_FERAL || form == FORM_CAT ||
                                     form == FORM_BEAR || form == FORM_DIREBEAR;
                profile.treeDruid = form == FORM_TREE;
                if (profile.feralDruid && !profile.healer)
                {
                    profile.melee = true;
                    profile.physicalDamage = true;
                }
                else
                {
                    profile.caster = true;
                    profile.magicDamage = true;
                }
                break;
            }
            default:
                break;
        }

        return profile;
    }

    bool IsPhysicalDamageTarget(PlayerbotAI* botAI, Unit* target)
    {
        return GetTargetProfile(botAI, target).physicalDamage;
    }

    bool IsCasterTarget(PlayerbotAI* botAI, Unit* target)
    {
        TargetProfile const profile = GetTargetProfile(botAI, target);
        return profile.caster || profile.healer;
    }

    bool HasIncomingHostileCast(PlayerbotAI* botAI)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        if (!bot || !IsPvpContext(bot))
            return false;

        std::unordered_set<ObjectGuid> checked;
        auto isThreateningCast = [&](Unit* caster) -> bool
        {
            if (!caster || !caster->IsAlive() || !caster->IsInWorld() || caster->GetMapId() != bot->GetMapId() ||
                !checked.insert(caster->GetGUID()).second || !IsEnemyPlayerOrOwnedUnit(botAI, caster) ||
                ServerFacade::instance().GetDistance2d(bot, caster) > 45.0f)
                return false;

            SpellInfo const* spellInfo = GetCurrentCastingSpell(caster);
            if (!spellInfo || spellInfo->IsPositive())
                return false;

            ObjectGuid const intendedTarget = caster->GetTarget();
            if (intendedTarget == bot->GetGUID())
                return true;

            Unit* protectedTarget = intendedTarget ? botAI->GetUnit(intendedTarget) : nullptr;
            Player* protectedPlayer = protectedTarget ? protectedTarget->ToPlayer() : nullptr;
            if (protectedPlayer && !botAI->IsOpposing(protectedPlayer) && botAI->IsHeal(protectedPlayer) &&
                ServerFacade::instance().GetDistance2d(bot, protectedPlayer) <= 30.0f)
                return true;

            return !intendedTarget && ServerFacade::instance().GetDistance2d(bot, caster) <= 18.0f;
        };

        if (isThreateningCast(botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get()))
            return true;

        GuidVector attackers = botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get();
        for (ObjectGuid const& guid : attackers)
            if (isThreateningCast(botAI->GetUnit(guid)))
                return true;

        GuidVector possibleTargets = botAI->GetAiObjectContext()->GetValue<GuidVector>("possible targets")->Get();
        uint8 scanned = 0;
        for (ObjectGuid const& guid : possibleTargets)
        {
            if (isThreateningCast(botAI->GetUnit(guid)))
                return true;
            if (++scanned >= 24)
                break;
        }

        return false;
    }

    bool IsInAlteracValley(Player* bot)
    {
        if (!bot || !bot->InBattleground() || !bot->GetBattleground())
            return false;

        BattlegroundTypeId bgType = bot->GetBattleground()->GetBgTypeID();
        if (bgType == BATTLEGROUND_RB)
            bgType = bot->GetBattleground()->GetBgTypeID(true);

        return bgType == BATTLEGROUND_AV;
    }

    bool GetActiveAVObjective(PlayerbotAI* botAI, Player* bot, PositionInfo& objective)
    {
        if (!botAI || !bot || !IsInAlteracValley(bot))
            return false;

        return GetActiveBattlegroundObjective(botAI, bot, objective);
    }

    bool IsNearObjective(Unit* unit, PositionInfo const& objective, float radius)
    {
        if (!unit || !objective.valueSet)
            return false;

        float const dx = unit->GetPositionX() - objective.x;
        float const dy = unit->GetPositionY() - objective.y;
        return dx * dx + dy * dy <= radius * radius;
    }

    bool IsAttackingFriendlyHealer(PlayerbotAI* botAI, Unit* target)
    {
        if (!botAI || !target)
            return false;

        Player* victim = target->GetVictim() ? target->GetVictim()->ToPlayer() : nullptr;
        if (!victim && target->GetTarget())
        {
            Unit* selectedTarget = botAI->GetUnit(target->GetTarget());
            victim = selectedTarget ? selectedTarget->ToPlayer() : nullptr;
        }

        return victim && !botAI->IsOpposing(victim) && botAI->IsHeal(victim);
    }

    // Scans up to 24 nearby enemies; kept separate from HasCaptureObjectiveThreat so the cached
    // capture state can call it without recursing back into the cache.
    static bool ScanCaptureObjectiveThreat(PlayerbotAI* botAI);

    static bool ComputeHasActiveBattlegroundCaptureObjective(PlayerbotAI* botAI, Player* bot)
    {
        if (HasCaptureBannerCast(bot) || IsCarryingBattlegroundFlag(bot))
            return true;

        Battleground* bg = bot->GetBattleground();
        BattlegroundTypeId const bgType = GetRealBattlegroundType(bg);
        PositionInfo objective;
        if (!GetActiveBattlegroundObjective(botAI, bot, objective))
            return false;

        switch (bgType)
        {
            case BATTLEGROUND_AV:
                if (!IsAlteracCaptureObjective(bg, objective))
                    return false;

                return IsAlteracUsableCaptureObjective(bot, bg, objective);
            case BATTLEGROUND_AB:
                return IsObjectiveNearArathiNode(objective);
            case BATTLEGROUND_IC:
                return IsIsleCaptureObjective(bg, objective);
            case BATTLEGROUND_EY:
                return IsEyeCaptureObjective(bg, objective);
            case BATTLEGROUND_WS:
                return IsWarsongCaptureObjective(bg, objective);
            default:
                return false;
        }
    }

    static bool ComputeShouldPrioritizeBattlegroundCapture(PlayerbotAI* botAI, Player* bot, bool hasObjective)
    {
        if (!hasObjective || HasSelfDefenseAttacker(bot))
            return false;

        if (HasCaptureBannerCast(bot) || IsCarryingBattlegroundFlag(bot))
            return true;

        PositionInfo objective;
        if (!GetActiveBattlegroundObjective(botAI, bot, objective))
            return false;

        float const commitRadius = IsInAlteracValley(bot) ? 75.0f : 55.0f;
        if (!IsNearObjective(bot, objective, commitRadius))
            return false;

        return !ScanCaptureObjectiveThreat(botAI);
    }

    // This chain walks every battleground capture object and scans nearby enemies, and it used to run
    // once per candidate inside target selection and once per action inside CastSpellAction::isUseful.
    // A short per-bot cache keeps the behaviour while collapsing that to one evaluation per tick.
    static CaptureCacheEntry GetCaptureState(PlayerbotAI* botAI, Player* bot)
    {
        uint32 const now = getMSTime();
        auto& shard = captureCache.GetShard(bot->GetGUID());
        {
            std::lock_guard<std::mutex> guard(shard.mutex);
            auto const itr = shard.map.find(bot->GetGUID());
            if (itr != shard.map.end() && itr->second.untilMs >= now)
                return itr->second;
        }

        CaptureCacheEntry state;
        state.hasObjective = ComputeHasActiveBattlegroundCaptureObjective(botAI, bot);
        state.prioritizeCapture = ComputeShouldPrioritizeBattlegroundCapture(botAI, bot, state.hasObjective);
        state.untilMs = now + CaptureCacheLifetimeMs;

        {
            std::lock_guard<std::mutex> guard(shard.mutex);
            shard.map[bot->GetGUID()] = state;
        }

        return state;
    }

    bool HasActiveBattlegroundCaptureObjective(PlayerbotAI* botAI)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        if (!bot || !bot->InBattleground() || bot->InArena())
            return false;

        return GetCaptureState(botAI, bot).hasObjective;
    }

    bool IsSelfDefenseTarget(Player* bot, Unit* target)
    {
        if (!IsSameMapUnit(bot, target))
            return false;

        if (target->GetVictim() == bot || target->GetTarget() == bot->GetGUID())
            return true;

        ObjectGuid const targetGuid = target->GetGUID();
        Unit::AttackerSet const& attackers = bot->getAttackers();
        for (Unit* attacker : attackers)
        {
            if (!attacker || !attacker->IsAlive())
                continue;

            if (attacker == target || attacker->GetGUID() == targetGuid)
                return true;

            Unit* owner = attacker->GetCharmerOrOwner();
            if (owner && owner->GetGUID() == targetGuid)
                return true;
        }

        return false;
    }

    bool HasSelfDefenseAttacker(Player* bot)
    {
        if (!bot || !bot->IsAlive())
            return false;

        Unit::AttackerSet const& attackers = bot->getAttackers();
        for (Unit* attacker : attackers)
            if (IsSelfDefenseTarget(bot, attacker))
                return true;

        return false;
    }

    bool IsCaptureObjectiveThreat(PlayerbotAI* botAI, Unit* target)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        Player* playerTarget = target ? target->ToPlayer() : nullptr;
        if (!IsSameMapUnit(bot, target) || !playerTarget || !botAI->IsOpposing(playerTarget))
            return false;

        PositionInfo objective;
        if (!GetActiveBattlegroundObjective(botAI, bot, objective))
            return false;

        GameObject* captureTarget = GetCaptureBannerTarget(target);
        if (!captureTarget)
            return false;

        bool const captureTargetMatchesObjective =
            IsPositionNear2d(objective, captureTarget->GetPositionX(), captureTarget->GetPositionY(), 35.0f);
        bool const targetIsOnObjective = IsNearObjective(target, objective, 14.0f);
        bool const botCanInfluenceObjective = IsNearObjective(bot, objective, 70.0f) || bot->IsWithinDist(target, 45.0f);

        return botCanInfluenceObjective && (captureTargetMatchesObjective || targetIsOnObjective);
    }

    static bool ScanCaptureObjectiveThreat(PlayerbotAI* botAI)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        if (!bot)
            return false;

        std::unordered_set<ObjectGuid> checked;
        auto checkUnit = [&](Unit* unit) -> bool
        {
            if (!unit || !unit->GetGUID() || checked.find(unit->GetGUID()) != checked.end())
                return false;

            checked.insert(unit->GetGUID());
            return IsCaptureObjectiveThreat(botAI, unit);
        };

        if (checkUnit(botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get()))
            return true;

        if (checkUnit(botAI->GetAiObjectContext()->GetValue<Unit*>("enemy player target")->Get()))
            return true;

        GuidVector attackers = botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get();
        for (ObjectGuid const& guid : attackers)
            if (checkUnit(botAI->GetUnit(guid)))
                return true;

        GuidVector players = botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest enemy players")->Get();
        uint8 scanned = 0;
        for (ObjectGuid const& guid : players)
        {
            if (checkUnit(botAI->GetUnit(guid)))
                return true;

            if (++scanned >= 24)
                break;
        }

        return false;
    }

    bool HasCaptureObjectiveThreat(PlayerbotAI* botAI)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        if (!bot || !HasActiveBattlegroundCaptureObjective(botAI))
            return false;

        return ScanCaptureObjectiveThreat(botAI);
    }

    bool ShouldPrioritizeBattlegroundCapture(PlayerbotAI* botAI)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        if (!bot || !bot->InBattleground() || bot->InArena())
            return false;

        return GetCaptureState(botAI, bot).prioritizeCapture;
    }

    bool CanEngageDuringBattlegroundCapture(PlayerbotAI* botAI, Unit* target)
    {
        if (!ShouldPrioritizeBattlegroundCapture(botAI))
            return true;

        Player* bot = botAI ? botAI->GetBot() : nullptr;
        bool const engage = IsSelfDefenseTarget(bot, target) || IsCaptureObjectiveThreat(botAI, target);
        if (!engage)
            CountGate(gateCounters.captureObjectiveLock);

        return engage;
    }

    bool IsObjectiveRelevantEnemy(PlayerbotAI* botAI, Unit* target, bool threatTarget, float botObjectiveRadius,
                                  float targetObjectiveRadius)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        if (!IsSameMapUnit(bot, target))
            return false;

        if (!CanEngageDuringBattlegroundCapture(botAI, target))
            return false;

        if (!IsInAlteracValley(bot))
            return true;

        if (threatTarget || target->GetVictim() == bot || IsAttackingFriendlyHealer(botAI, target))
            return true;

        float const distanceToBot = ServerFacade::instance().GetDistance2d(bot, target);
        if (distanceToBot <= 18.0f)
            return true;

        PositionInfo objective;
        if (!GetActiveAVObjective(botAI, bot, objective))
            return true;

        if (IsNearObjective(bot, objective, botObjectiveRadius) && IsNearObjective(target, objective, targetObjectiveRadius))
            return true;

        if (target->IsNonMeleeSpellCast(true) && IsNearObjective(bot, objective, botObjectiveRadius) &&
            IsNearObjective(target, objective, targetObjectiveRadius + 10.0f))
            return true;

        return false;
    }

    // Only auras the core itself will strip on damage count as "do not touch". Testing aura *types*
    // instead lumped in effects that damage never breaks - most importantly warlock Death Coil, whose
    // horror is a MOD_FEAR aura. Every bot in range used to stop attacking a death coiled target for
    // the full duration, and the target was dropped from target selection entirely.
    bool IsBreakableCrowdControlled(Unit* target)
    {
        if (!target)
            return false;

        constexpr uint32 damageBreaksAura = AURA_INTERRUPT_FLAG_TAKE_DAMAGE | AURA_INTERRUPT_FLAG_DIRECT_DAMAGE;

        Unit::AuraApplicationMap const& auras = target->GetAppliedAuras();
        for (auto const& [_, aurApp] : auras)
        {
            Aura const* aura = aurApp ? aurApp->GetBase() : nullptr;
            SpellInfo const* spellInfo = aura ? aura->GetSpellInfo() : nullptr;
            if (!aura || aura->IsRemoved() || !spellInfo || spellInfo->IsPositive())
                continue;

            if (!(spellInfo->AuraInterruptFlags & damageBreaksAura))
                continue;

            for (SpellEffectInfo const& effect : spellInfo->Effects)
            {
                if (effect.Effect != SPELL_EFFECT_APPLY_AURA)
                    continue;

                switch (effect.ApplyAuraName)
                {
                    case SPELL_AURA_MOD_CONFUSE:
                    case SPELL_AURA_MOD_FEAR:
                    case SPELL_AURA_MOD_CHARM:
                    case SPELL_AURA_AOE_CHARM:
                    case SPELL_AURA_MOD_POSSESS:
                    case SPELL_AURA_MOD_PACIFY:
                    case SPELL_AURA_MOD_PACIFY_SILENCE:
                    case SPELL_AURA_MOD_STUN:
                        return true;
                    default:
                        break;
                }
            }
        }

        return false;
    }

    bool CanDamageTarget(PlayerbotAI* botAI, Unit* target, bool areaDamage)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        if (!bot || !target || !IsPvpContext(botAI, target))
            return true;

        // Counted only past the PvP check, so bots questing in the open world never touch the
        // shared counters at all.
        CountGate(gateCounters.evaluations);

        if (!IsBreakableCrowdControlled(target))
            return true;

        if (areaDamage)
        {
            CountGate(gateCounters.crowdControlProtected);
            return false;
        }

        Unit* currentTarget = botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
        bool const allowed = currentTarget == target && target->GetHealthPct() < 12.0f &&
                             GetCombatPhase(botAI, target) == CombatPhase::Burst;
        if (!allowed)
            CountGate(gateCounters.crowdControlProtected);

        return allowed;
    }

    bool SpellCanBreakCrowdControl(SpellInfo const* spellInfo)
    {
        if (!spellInfo || spellInfo->IsPositive())
            return false;

        for (SpellEffectInfo const& effect : spellInfo->Effects)
        {
            switch (effect.Effect)
            {
                case SPELL_EFFECT_SCHOOL_DAMAGE:
                case SPELL_EFFECT_WEAPON_DAMAGE:
                case SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL:
                case SPELL_EFFECT_NORMALIZED_WEAPON_DMG:
                    return true;
                case SPELL_EFFECT_APPLY_AURA:
                case SPELL_EFFECT_APPLY_AREA_AURA_PARTY:
                case SPELL_EFFECT_APPLY_AREA_AURA_RAID:
                case SPELL_EFFECT_APPLY_AREA_AURA_FRIEND:
                case SPELL_EFFECT_APPLY_AREA_AURA_ENEMY:
                    if (effect.ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE ||
                        effect.ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE_PERCENT ||
                        effect.ApplyAuraName == SPELL_AURA_PERIODIC_LEECH ||
                        effect.ApplyAuraName == SPELL_AURA_PROC_TRIGGER_DAMAGE)
                        return true;
                    break;
                default:
                    break;
            }
        }

        return false;
    }

    bool IsAoeSafe(PlayerbotAI* botAI, WorldLocation const& position, float radius)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        if (!IsPvpContext(bot))
            return true;

        std::unordered_set<ObjectGuid> checked;
        auto checkGuid = [&](ObjectGuid const& guid) -> bool
        {
            if (!guid || checked.find(guid) != checked.end())
                return true;

            checked.insert(guid);
            Unit* unit = botAI->GetUnit(guid);
            if (!unit || unit == bot || !unit->IsAlive() || unit->GetMapId() != bot->GetMapId() ||
                !IsEnemyPlayerOrOwnedUnit(botAI, unit))
                return true;

            if (!IsBreakableCrowdControlled(unit) || CanDamageTarget(botAI, unit, true))
                return true;

            float const distance = ServerFacade::instance().GetDistance2d(unit, position.GetPositionX(), position.GetPositionY());
            return distance > radius;
        };

        GuidVector attackers = botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get();
        for (ObjectGuid const& guid : attackers)
            if (!checkGuid(guid))
                return false;

        GuidVector possibleTargets = botAI->GetAiObjectContext()->GetValue<GuidVector>("possible targets")->Get();
        for (ObjectGuid const& guid : possibleTargets)
            if (!checkGuid(guid))
                return false;

        return true;
    }

    bool CurrentAoeIsSafe(PlayerbotAI* botAI)
    {
        if (!botAI || *botAI->GetAiObjectContext()->GetValue<uint8>("aoe count") <= 1)
            return true;

        WorldLocation position = *botAI->GetAiObjectContext()->GetValue<WorldLocation>("aoe position");
        bool const safe = IsAoeSafe(botAI, position, sPlayerbotAIConfig.aoeRadius + 2.0f);
        if (!safe)
            CountGate(gateCounters.aoeUnsafe);

        return safe;
    }

    bool IsDedicatedInterruptSpell(std::string const& spell)
    {
        return spell == "counterspell" || spell == "kick" || spell == "pummel" || spell == "shield bash" ||
               spell == "mind freeze" || spell == "strangulate" || spell == "wind shear" ||
               spell == "silence" || spell == "spell lock" || spell == "silencing shot";
    }

    bool IsInterruptSpell(std::string const& spell)
    {
        // Dual-use spells: they can interrupt, but they are also plain rotation damage, a gap closer
        // or a stun. Suppressing them whenever the target happens to be casting something not worth
        // interrupting removed them from the rotation entirely, so they are never gated - only
        // the dedicated interrupts above are.
        return IsDedicatedInterruptSpell(spell) || spell == "earth shock" || spell == "hammer of justice" ||
               spell == "bash" || spell == "intercept" || spell == "repentance" || spell == "arcane torrent";
    }

    bool CanAttemptInterrupt(PlayerbotAI* botAI, Unit* target, std::string const& spell)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        SpellInfo const* castingSpell = GetCurrentCastingSpell(target);
        if (!bot || !target)
            return false;
        if (!IsDedicatedInterruptSpell(spell) || !castingSpell)
            return true;

        if (!botAI->IsInterruptableSpellCasting(target, spell))
        {
            CountGate(gateCounters.interruptNotWorthwhile);
            return false;
        }

        uint32 const now = getMSTime();
        InterruptReservationKey key{ target->GetGUID(), castingSpell->Id };
        auto& shard = interruptReservations.GetShard(key);
        std::lock_guard<std::mutex> guard(shard.mutex);
        PruneInterruptReservations(shard, now);

        auto itr = shard.map.find(key);
        bool const free = itr == shard.map.end() || itr->second.untilMs < now || itr->second.bot == bot->GetGUID();
        if (!free)
            CountGate(gateCounters.interruptTakenByOther);

        return free;
    }

    bool TryReserveInterrupt(PlayerbotAI* botAI, Unit* target, std::string const& spell, uint32 holdMs)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        SpellInfo const* castingSpell = GetCurrentCastingSpell(target);
        if (!bot || !target)
            return false;
        if (!IsDedicatedInterruptSpell(spell) || !castingSpell)
            return true;
        if (!botAI->IsInterruptableSpellCasting(target, spell))
            return false;

        uint32 const now = getMSTime();
        InterruptReservationKey key{ target->GetGUID(), castingSpell->Id };
        auto& shard = interruptReservations.GetShard(key);
        std::lock_guard<std::mutex> guard(shard.mutex);
        PruneInterruptReservations(shard, now);
        auto itr = shard.map.find(key);
        if (itr != shard.map.end() && itr->second.untilMs >= now && itr->second.bot != bot->GetGUID())
            return false;

        shard.map[key] = { bot->GetGUID(), now + holdMs };
        return true;
    }

    void ReleaseInterrupt(PlayerbotAI* botAI, Unit* target)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        if (!bot || !target)
            return;

        for (auto& shard : interruptReservations.GetShards())
        {
            std::lock_guard<std::mutex> guard(shard.mutex);
            for (auto itr = shard.map.begin(); itr != shard.map.end();)
            {
                if (itr->first.target == target->GetGUID() && itr->second.bot == bot->GetGUID())
                    itr = shard.map.erase(itr);
                else
                    ++itr;
            }
        }
    }

    bool TryReserveCrowdControl(PlayerbotAI* botAI, Unit* target, std::string const& spell, uint32 holdMs)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        if (!bot || !target)
            return false;

        if (!IsPvpContext(bot))
            return true;

        if (!CanEngageDuringBattlegroundCapture(botAI, target))
            return false;

        uint32 const now = getMSTime();
        auto& shard = crowdControlReservations.GetShard(target->GetGUID());
        std::lock_guard<std::mutex> guard(shard.mutex);
        PruneCrowdControlReservations(shard, now);
        auto itr = shard.map.find(target->GetGUID());
        if (itr != shard.map.end() && itr->second.untilMs >= now && itr->second.bot != bot->GetGUID())
            return false;

        shard.map[target->GetGUID()] = { bot->GetGUID(), spell, now + holdMs };
        return true;
    }

    void ReleaseCrowdControl(PlayerbotAI* botAI, Unit* target)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        if (!bot || !target)
            return;

        auto& shard = crowdControlReservations.GetShard(target->GetGUID());
        std::lock_guard<std::mutex> guard(shard.mutex);
        auto itr = shard.map.find(target->GetGUID());
        if (itr != shard.map.end() && itr->second.bot == bot->GetGUID())
            shard.map.erase(itr);
    }

    int32 ScoreOffensiveDispelTarget(PlayerbotAI* botAI, Unit* target, uint32 dispelType, bool threatTarget)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        if (!IsSameMapUnit(bot, target) || !IsEnemyPlayerOrOwnedUnit(botAI, target) || !botAI->HasAuraToDispel(target, dispelType))
            return 0;

        if (!IsObjectiveRelevantEnemy(botAI, target, threatTarget, 65.0f, 45.0f))
            return 0;

        bool hasImportantAura = false;
        uint32 const dispelAuraCount = CountUsefulDispelAuras(target, dispelType, &hasImportantAura);
        if (!dispelAuraCount)
            return 0;

        int32 score = 160 + static_cast<int32>(dispelAuraCount) * 80;
        if (hasImportantAura)
            score += 650;

        Player* playerTarget = target->ToPlayer();
        if (playerTarget && botAI->IsHeal(playerTarget))
            score += 260;

        if (IsCastingHelpfulSpell(target))
            score += 180;

        if (target->GetVictim() == bot || IsAttackingFriendlyHealer(botAI, target))
            score += 220;

        Unit* currentTarget = botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
        if (target == currentTarget)
            score += 100;

        float const distance = ServerFacade::instance().GetDistance2d(bot, target);
        score -= static_cast<int32>(std::min(distance, 80.0f));
        return score;
    }

    int32 ScoreClassMatchup(PlayerbotAI* botAI, Unit* target)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        Player* playerTarget = GetControllingPlayer(target);
        if (!bot || !playerTarget || !IsPvpContext(bot) || !botAI->IsOpposing(playerTarget))
            return 0;

        ObservedDefenseState const defense = ObserveMajorDefense(target);
        if (defense.active)
            return -900;

        int32 score = defense.recentlySpent ? 90 : 0;
        uint8 const botClass = bot->getClass();
        uint8 const targetClass = playerTarget->getClass();
        TargetProfile const botProfile = GetTargetProfile(botAI, bot);
        TargetProfile const targetProfile = GetTargetProfile(botAI, target);

        if (targetProfile.healer)
            score += 70;

        if (botProfile.physicalDamage)
        {
            if (targetProfile.caster)
                score += 75;
            if (target->HasAura(5277) || target->HasAura(26669) || target->HasAura(19263) ||
                target->HasAura(1022) || target->HasAura(5599) || target->HasAura(10278))
                score -= botProfile.magicDamage ? 280 : 500;
        }

        if (botProfile.magicDamage)
        {
            if (targetProfile.melee)
                score += 45;
            if (target->HasAura(31224) || target->HasAura(48707))
                score -= botProfile.physicalDamage ? 280 : 500;
        }

        if (targetClass == CLASS_HUNTER && botProfile.melee &&
            ServerFacade::instance().GetDistance2d(bot, target) <= 7.0f)
            score += 110;

        if ((targetClass == CLASS_WARRIOR || targetClass == CLASS_DEATH_KNIGHT) &&
            (botProfile.caster || botProfile.rangedPhysical))
            score += 70;

        if (targetClass == CLASS_DRUID && botClass == CLASS_WARLOCK && target->HasAura(33891))
            score += 280;

        if (targetClass == CLASS_ROGUE &&
            (target->HasAuraType(SPELL_AURA_PERIODIC_DAMAGE) || target->HasAura(770) || target->HasAura(16857)))
            score += 80;

        return score;
    }

    PressureProfile GetIncomingPressureProfile(PlayerbotAI* botAI)
    {
        PressureProfile profile;
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        RunPeriodicMaintenance(getMSTime());
        if (!bot || !IsPvpContext(bot) || !bot->IsAlive())
            return profile;

        uint32 const now = getMSTime();
        {
            auto& shard = pressureCache.GetShard(bot->GetGUID());
            std::lock_guard<std::mutex> guard(shard.mutex);
            auto const itr = shard.map.find(bot->GetGUID());
            if (itr != shard.map.end() && itr->second.untilMs >= now)
                return itr->second.profile;
        }

        GuidVector attackers = botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get();
        float const healthPct = bot->GetHealthPct();
        uint8 consideredAttackers = 0;

        for (ObjectGuid const& guid : attackers)
        {
            Unit* attacker = botAI->GetUnit(guid);
            if (!attacker || !attacker->IsAlive() || !attacker->IsInWorld() ||
                attacker->GetMapId() != bot->GetMapId())
                continue;

            if (++consideredAttackers > 4)
                break;

            profile.total += 20;
            TargetProfile const attackerProfile = GetTargetProfile(botAI, attacker);
            if (!attackerProfile.valid)
            {
                profile.physical += 20;
                ++profile.meleeAttackers;
            }
            else
            {
                if (attackerProfile.physicalDamage && attackerProfile.magicDamage)
                {
                    profile.physical += 12;
                    profile.magic += 12;
                }
                else if (attackerProfile.physicalDamage)
                    profile.physical += 20;
                else if (attackerProfile.magicDamage)
                    profile.magic += 20;

                if (attackerProfile.melee)
                    ++profile.meleeAttackers;
                if (attackerProfile.caster)
                    ++profile.casterAttackers;
            }

            if (attacker->GetTarget() == bot->GetGUID() && attacker->IsNonMeleeSpellCast(true))
            {
                profile.total += 8;
                profile.magic += 8;
                ++profile.hostileCasts;
            }
        }

        if (healthPct < 70.0f)
            profile.total += 10;
        if (healthPct < 45.0f)
            profile.total += 20;
        if (healthPct < 25.0f)
            profile.total += 25;
        if (botAI->IsHeal(bot) && !attackers.empty())
            profile.total += 10;

        {
            auto& shard = healthSamples.GetShard(bot->GetGUID());
            std::lock_guard<std::mutex> guard(shard.mutex);
            HealthSample& sample = shard.map[bot->GetGUID()];
            if (!sample.sampledMs)
            {
                sample.healthPct = healthPct;
                sample.sampledMs = now;
            }
            else
            {
                uint32 const elapsed = getMSTimeDiff(sample.sampledMs, now);
                if (elapsed >= 250)
                {
                    float const loss = std::max(0.0f, sample.healthPct - healthPct);
                    sample.recentLossPerSecond = loss * 1000.0f / static_cast<float>(elapsed);
                    sample.healthPct = healthPct;
                    sample.sampledMs = now;
                }
            }

            if (sample.recentLossPerSecond >= 15.0f)
                profile.total += 30;
            else if (sample.recentLossPerSecond >= 7.0f)
                profile.total += 15;
        }

        profile.total = std::min<uint32>(profile.total, 150);
        profile.physical = std::min<uint32>(profile.physical, 100);
        profile.magic = std::min<uint32>(profile.magic, 100);
        {
            auto& shard = pressureCache.GetShard(bot->GetGUID());
            std::lock_guard<std::mutex> guard(shard.mutex);
            shard.map[bot->GetGUID()] = { profile, now + 100 };
        }
        return profile;
    }

    uint32 GetIncomingPressure(PlayerbotAI* botAI)
    {
        return GetIncomingPressureProfile(botAI).total;
    }

    bool HasPhysicalPressure(PlayerbotAI* botAI)
    {
        PressureProfile const profile = GetIncomingPressureProfile(botAI);
        return profile.total >= 60 && profile.physical > 0 &&
               (profile.physical >= profile.magic || profile.meleeAttackers >= 2);
    }

    bool HasMagicPressure(PlayerbotAI* botAI)
    {
        PressureProfile const profile = GetIncomingPressureProfile(botAI);
        return profile.total >= 60 && profile.magic > 0 &&
               (profile.magic > profile.physical || profile.casterAttackers >= 2 || profile.hostileCasts >= 2);
    }

    Unit* GetClosestPvpMeleeAttacker(PlayerbotAI* botAI, float maxDistance)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        if (!bot || !IsPvpContext(bot))
            return nullptr;

        Unit* closest = nullptr;
        float closestDistance = maxDistance;
        GuidVector attackers = botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get();
        for (ObjectGuid const& guid : attackers)
        {
            Unit* attacker = botAI->GetUnit(guid);
            Player* player = GetControllingPlayer(attacker);
            if (!attacker || !player || !botAI->IsOpposing(player) || !attacker->IsAlive() ||
                !attacker->IsInWorld() || attacker->GetMapId() != bot->GetMapId() ||
                !bot->IsWithinLOSInMap(attacker) || !CanDamageTarget(botAI, attacker, false))
                continue;

            bool const meleeThreat = attacker->ToPlayer() ? PlayerbotAI::IsMelee(player, true) : true;
            if (!meleeThreat)
                continue;

            float const distance = bot->GetExactDist2d(attacker);
            if (distance <= closestDistance)
            {
                closest = attacker;
                closestDistance = distance;
            }
        }

        return closest;
    }

    CombatPhase GetCombatPhase(PlayerbotAI* botAI, Unit* target)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        if (!bot || !target || !IsPvpContext(bot) || !IsEnemyPlayerOrOwnedUnit(botAI, target) ||
            !IsObjectiveRelevantEnemy(botAI, target, false, 70.0f, 50.0f))
            return CombatPhase::None;

        uint32 const pressure = GetIncomingPressure(botAI);
        bool const defended = IsMajorDefenseActive(target);
        uint32 const friendlyAttackers = CountFriendlyPlayerAttackers(botAI, target);

        CombatPhase desired = CombatPhase::Sustain;
        if (pressure >= 90)
            desired = CombatPhase::Reset;
        else if (defended)
            desired = CombatPhase::Sustain;
        else if (IsCastingHelpfulSpell(target) && GetControllingPlayer(target) &&
                 botAI->IsHeal(GetControllingPlayer(target)))
            desired = CombatPhase::Control;
        else if (target->GetHealthPct() < 55.0f || IsControlledForBurst(target) ||
                 (GetControllingPlayer(target) && botAI->IsHeal(GetControllingPlayer(target)) &&
                  friendlyAttackers >= 2))
            desired = CombatPhase::Burst;
        else if (target->GetHealthPct() > 70.0f && friendlyAttackers < 2)
            desired = CombatPhase::Setup;

        uint32 const now = getMSTime();
        auto& shard = combatPlans.GetShard(bot->GetGUID());
        std::lock_guard<std::mutex> guard(shard.mutex);
        CombatPlanState& plan = shard.map[bot->GetGUID()];
        bool const sameTarget = plan.target == target->GetGUID();
        bool const active = sameTarget && plan.untilMs >= now;

        if (active && desired != CombatPhase::Reset && !defended)
        {
            bool const urgentTransition = desired == CombatPhase::Control || desired == CombatPhase::Burst ||
                (plan.phase == CombatPhase::Reset && pressure < 70);
            if (urgentTransition)
            {
                plan.phase = desired;
                plan.untilMs = now + CombatPlanLifetimeMs;
            }
            return plan.phase;
        }

        plan.target = target->GetGUID();
        plan.phase = desired;
        plan.untilMs = now + CombatPlanLifetimeMs;
        return desired;
    }

    bool IsMajorDefenseActive(Unit* target)
    {
        return ObserveMajorDefense(target).active;
    }

    bool ShouldUseDefensiveCooldown(PlayerbotAI* botAI, bool critical)
    {
        uint32 const pressure = GetIncomingPressure(botAI);
        return pressure >= (critical ? 85u : 60u);
    }

    bool IsDefensiveCooldownSpell(std::string const& spell)
    {
        return IsPersonalDefensiveSpell(spell);
    }

    bool CanUseDefensiveCooldown(PlayerbotAI* botAI, std::string const& spell)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        if (!bot || !IsPvpContext(bot) || !IsPersonalDefensiveSpell(spell))
            return true;

        PressureProfile const pressure = GetIncomingPressureProfile(botAI);
        bool const emergencyHealth = bot->GetHealthPct() < 25.0f;
        if (pressure.total < 60 && !emergencyHealth)
        {
            CountGate(gateCounters.defensiveNotWarranted);
            return false;
        }

        bool const critical = pressure.total >= 95 || emergencyHealth;
        if (GetMajorDefenseMask(bot) && !critical)
        {
            CountGate(gateCounters.defensiveNotWarranted);
            return false;
        }

        if (spell == "evasion" || spell == "retaliation")
            return critical || HasPhysicalPressure(botAI);
        if (spell == "cloak of shadows" || spell == "anti magic shell")
            return critical || HasMagicPressure(botAI);
        if (spell == "icebound fortitude")
            return critical || HasPhysicalPressure(botAI) || bot->HasAuraType(SPELL_AURA_MOD_STUN);

        return true;
    }

    bool TryReserveDefensiveCooldown(PlayerbotAI* botAI, std::string const& spell, uint32 holdMs)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        if (!bot || !IsPvpContext(bot) || !IsPersonalDefensiveSpell(spell))
            return true;

        uint32 const now = getMSTime();
        auto& shard = defensiveReservations.GetShard(bot->GetGUID());
        std::lock_guard<std::mutex> guard(shard.mutex);
        auto itr = shard.map.find(bot->GetGUID());
        // A held reservation must never lock the bot out of a stronger button - it only stops it from
        // double-popping two cooldowns of the same weight.
        if (itr != shard.map.end() && itr->second.untilMs >= now && itr->second.spell != spell &&
            GetDefensiveTier(spell) <= GetDefensiveTier(itr->second.spell))
        {
            CountGate(gateCounters.defensiveReserved);
            return false;
        }

        shard.map[bot->GetGUID()] = { spell, now + holdMs };
        return true;
    }

    void ReleaseDefensiveCooldown(PlayerbotAI* botAI)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        if (!bot)
            return;

        auto& shard = defensiveReservations.GetShard(bot->GetGUID());
        std::lock_guard<std::mutex> guard(shard.mutex);
        shard.map.erase(bot->GetGUID());
    }

    bool ShouldUseBurstCooldown(PlayerbotAI* botAI, Unit* target)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        if (!bot || !target || !IsPvpContext(bot) || !IsEnemyPlayerOrOwnedUnit(botAI, target))
            return false;

        if (!IsObjectiveRelevantEnemy(botAI, target, false, 70.0f, 50.0f))
            return false;

        if (IsMajorDefenseActive(target))
            return false;

        // Never burst while about to die - survival first.
        if (GetIncomingPressure(botAI) >= 90)
            return false;

        // A burst window in PvP is an opening: the target is controlled, already damaged, is the
        // enemy healer, or the team is committed to it. Waiting for the old "below 45% health"
        // condition meant the cooldowns were still unused when the target got healed or ran away.
        if (IsControlledForBurst(target) || target->GetHealthPct() <= 60.0f)
            return true;

        Player* playerTarget = GetControllingPlayer(target);
        if (playerTarget && botAI->IsHeal(playerTarget))
            return true;

        if (CountFriendlyPlayerAttackers(botAI, target) >= 2)
            return true;

        return GetCombatPhase(botAI, target) == CombatPhase::Burst;
    }

    bool CanUseOffensiveCooldown(PlayerbotAI* botAI, std::string const& spell)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        if (!bot || !IsPvpContext(bot) || !IsOffensiveCooldownSpell(spell))
            return true;

        if (botAI->IsHeal(bot))
            return true;

        Unit* target = botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
        if (!target || !IsEnemyPlayerOrOwnedUnit(botAI, target))
            return true;

        if (*botAI->GetAiObjectContext()->GetValue<uint8>("aoe count") >= 3 && CurrentAoeIsSafe(botAI))
            return true;

        bool const burst = ShouldUseBurstCooldown(botAI, target);
        if (!burst)
            CountGate(gateCounters.burstWindowClosed);

        return burst;
    }

    bool CanUseUtilitySpell(PlayerbotAI* botAI, Unit* target, std::string const& spell)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        if (!bot || !target || !IsPvpContext(bot))
            return true;

        if (spell != "disarm" && spell != "dismantle")
            return true;

        Player* playerTarget = GetControllingPlayer(target);
        TargetProfile const profile = GetTargetProfile(botAI, target);
        if (!playerTarget || !profile.physicalDamage || profile.feralDruid)
        {
            CountGate(gateCounters.utilityRejected);
            return false;
        }

        bool const armed = playerTarget->HasWeaponForAttack(BASE_ATTACK) ||
                           playerTarget->HasWeaponForAttack(OFF_ATTACK) ||
                           playerTarget->HasWeaponForAttack(RANGED_ATTACK);
        if (!armed)
            CountGate(gateCounters.utilityRejected);

        return armed;
    }

    bool ShouldUseDruidPvpDot(PlayerbotAI* botAI, Unit* target, std::string const& spell)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        if (!bot || !target || !target->IsAlive() || !target->IsInWorld() ||
            target->GetMapId() != bot->GetMapId() || !IsPvpContext(bot) ||
            !IsEnemyPlayerOrOwnedUnit(botAI, target) ||
            !CanEngageDuringBattlegroundCapture(botAI, target) ||
            !CanDamageTarget(botAI, target, false) || IsMajorDefenseActive(target))
            return false;

        CombatPhase const phase = GetCombatPhase(botAI, target);
        if (phase != CombatPhase::Setup && phase != CombatPhase::Sustain)
            return false;

        float minimumHealthPct = 0.0f;
        uint32 const friendlyAttackers = CountFriendlyPlayerAttackers(botAI, target);
        if (spell == "insect swarm")
        {
            minimumHealthPct = 25.0f;
            if (friendlyAttackers >= 4 && target->GetHealthPct() < 70.0f)
                return false;
        }
        else if (spell == "moonfire")
        {
            minimumHealthPct = 18.0f;
            if (friendlyAttackers >= 5 && target->GetHealthPct() < 60.0f)
                return false;
        }
        else
            return false;

        return target->GetHealthPct() > minimumHealthPct;
    }

    bool ShouldUseDruidFaerieFire(PlayerbotAI* botAI, Unit* target)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        Player* enemy = GetControllingPlayer(target);
        if (!bot || !target || !enemy || !target->IsAlive() || !target->IsInWorld() ||
            target->GetMapId() != bot->GetMapId() || !IsPvpContext(bot) ||
            !botAI->IsOpposing(enemy) || !CanEngageDuringBattlegroundCapture(botAI, target) ||
            !CanDamageTarget(botAI, target, false) || IsMajorDefenseActive(target))
            return false;

        ShapeshiftForm const form = enemy->GetShapeshiftForm();
        bool const stealthThreat = enemy->getClass() == CLASS_ROGUE ||
            (enemy->getClass() == CLASS_DRUID &&
             (form == FORM_CAT || form == FORM_BEAR || form == FORM_DIREBEAR));
        if (!stealthThreat || target->GetHealthPct() <= 20.0f)
            return false;

        CombatPhase const phase = GetCombatPhase(botAI, target);
        return phase == CombatPhase::Setup || phase == CombatPhase::Sustain ||
               phase == CombatPhase::Control;
    }

    bool ShouldUseHunterSting(PlayerbotAI* botAI, Unit* target, std::string const& sting)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        if (!bot || !target)
            return false;

        if (!IsPvpContext(bot))
            return true;

        Player* playerTarget = GetControllingPlayer(target);
        TargetProfile const targetProfile = GetTargetProfile(botAI, target);
        float const manaPct = GetManaPct(playerTarget ? static_cast<Unit*>(playerTarget) : target);
        bool const enemyHunter = playerTarget && playerTarget->getClass() == CLASS_HUNTER;
        bool const feralDruid = targetProfile.feralDruid;
        bool const manaPriority = targetProfile.usesMana && !feralDruid &&
            (enemyHunter || targetProfile.healer || targetProfile.caster);
        uint32 const pressure = GetIncomingPressure(botAI);
        bool const defensivePhysical =
            targetProfile.melee && !enemyHunter &&
            IsSelfDefenseTarget(bot, target) && pressure >= 70;

        std::string desired;
        if (enemyHunter && manaPct >= 12.0f && KnowsNamedSpell(botAI, "viper sting"))
            desired = "viper sting";
        else if (target->GetHealthPct() < 30.0f && KnowsNamedSpell(botAI, "serpent sting"))
            desired = "serpent sting";
        else if (manaPriority && manaPct >= 20.0f && KnowsNamedSpell(botAI, "viper sting"))
            desired = "viper sting";
        else if (defensivePhysical && KnowsNamedSpell(botAI, "scorpid sting"))
            desired = "scorpid sting";
        else if (KnowsNamedSpell(botAI, "serpent sting"))
            desired = "serpent sting";
        else if (manaPct > 0.0f && KnowsNamedSpell(botAI, "viper sting"))
            desired = "viper sting";
        else if (KnowsNamedSpell(botAI, "scorpid sting"))
            desired = "scorpid sting";

        if (desired.empty() || sting != desired)
            return false;

        OwnedDebuffFamily const active = GetOwnedDebuffFamily(bot, target, SPELL_SPECIFIC_STING);
        if (active.spell.empty() || active.spell == desired)
            return true;

        if (active.spell == "wyvern sting")
            return false;

        bool const nearExpiration = active.remainingMs >= 0 && active.remainingMs <= 2500;
        bool const clearlyWrongProfile =
            desired == "viper sting" || active.spell == "viper sting" ||
            (enemyHunter && active.spell == "scorpid sting");
        bool const emergencyDefense = desired == "scorpid sting" && pressure >= 90;
        return nearExpiration || clearlyWrongProfile || emergencyDefense;
    }

    bool ShouldUseWarlockCurse(PlayerbotAI* botAI, Unit* target, std::string const& curse)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        if (!bot || !target)
            return false;

        if (!IsPvpContext(bot))
            return true;

        if (botAI->HasAura("banish", target, false, true))
            return false;

        Player* playerTarget = GetControllingPlayer(target);
        TargetProfile const targetProfile = GetTargetProfile(botAI, target);
        bool const enemyHunter = playerTarget && playerTarget->getClass() == CLASS_HUNTER;
        bool const feralDruid = targetProfile.feralDruid;
        bool const manaCaster = targetProfile.usesMana && !enemyHunter && !feralDruid &&
            (targetProfile.healer || targetProfile.caster);
        uint32 const pressure = GetIncomingPressure(botAI);
        bool const needsKiting =
            targetProfile.melee && !enemyHunter && !feralDruid &&
            IsSelfDefenseTarget(bot, target) && pressure >= 65;

        std::string desired;
        if ((enemyHunter || feralDruid) && KnowsNamedSpell(botAI, "curse of agony"))
            desired = "curse of agony";
        else if (manaCaster && KnowsNamedSpell(botAI, "curse of tongues"))
            desired = "curse of tongues";
        else if (needsKiting && KnowsNamedSpell(botAI, "curse of exhaustion"))
            desired = "curse of exhaustion";
        else if (KnowsNamedSpell(botAI, "curse of agony"))
            desired = "curse of agony";
        else if (KnowsNamedSpell(botAI, "curse of weakness"))
            desired = "curse of weakness";

        if (desired.empty() || curse != desired)
            return false;

        OwnedDebuffFamily const active = GetOwnedDebuffFamily(bot, target, SPELL_SPECIFIC_CURSE);
        if (active.spell.empty() || active.spell == desired)
            return true;

        bool const nearExpiration = active.remainingMs >= 0 && active.remainingMs <= 2500;
        bool const agonyExhaustionTransition =
            (active.spell == "curse of agony" && desired == "curse of exhaustion") ||
            (active.spell == "curse of exhaustion" && desired == "curse of agony");
        bool const emergencyKite = desired == "curse of exhaustion" && pressure >= 90;

        if (agonyExhaustionTransition && !nearExpiration && !emergencyKite)
            return false;

        return true;
    }
}
