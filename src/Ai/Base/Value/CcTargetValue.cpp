/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "CcTargetValue.h"

#include "Action.h"
#include "AiObjectContext.h"
#include "Battleground.h"
#include "Group.h"
#include "ObjectGuid.h"
#include "PlayerbotAI.h"
#include "PositionValue.h"
#include "PvpTactics.h"
#include "ServerFacade.h"
#include "Spell.h"
#include "SpellMgr.h"
#include "SpellAuras.h"
#include "SpellAuraDefines.h"
#include "Timer.h"
#include "UnitDefines.h"

#include <algorithm>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace
{
    constexpr uint32 CcDiminishingResetMs = 18 * IN_MILLISECONDS;
    constexpr size_t CcDiminishingMaxEntries = 4096;

    struct CcDiminishingKey
    {
        ObjectGuid target;
        DiminishingGroup group;

        bool operator==(CcDiminishingKey const& other) const
        {
            return target == other.target && group == other.group;
        }
    };

    struct CcDiminishingKeyHash
    {
        std::size_t operator()(CcDiminishingKey const& key) const
        {
            return std::hash<ObjectGuid>()(key.target) ^ (std::hash<uint32>()(key.group) << 1);
        }
    };

    struct CcDiminishingState
    {
        uint8 hitCount = 0;
        uint32 lastMs = 0;
    };

    std::mutex ccDiminishingMutex;
    std::unordered_map<CcDiminishingKey, CcDiminishingState, CcDiminishingKeyHash> ccDiminishingCache;

    DiminishingGroup GetDiminishingGroup(PlayerbotAI* botAI, std::string const& spell)
    {
        if (!botAI)
            return DIMINISHING_NONE;

        uint32 spellId = botAI->GetAiObjectContext()->GetValue<uint32>("spell id", spell)->Get();
        if (!spellId)
            return DIMINISHING_NONE;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (!spellInfo)
            return DIMINISHING_NONE;

        DiminishingGroup const group = GetDiminishingReturnsGroupForSpell(spellInfo, false);
        return GetDiminishingReturnsGroupType(group) == DRTYPE_NONE ? DIMINISHING_NONE : group;
    }

    bool IsDiminishingExpired(CcDiminishingState const& state, uint32 now)
    {
        return !state.lastMs || getMSTimeDiff(state.lastMs, now) > CcDiminishingResetMs;
    }

    void PruneDiminishingCache(uint32 now)
    {
        if (ccDiminishingCache.size() <= CcDiminishingMaxEntries)
            return;

        for (auto itr = ccDiminishingCache.begin(); itr != ccDiminishingCache.end();)
        {
            if (IsDiminishingExpired(itr->second, now))
                itr = ccDiminishingCache.erase(itr);
            else
                ++itr;
        }
    }

    bool IsCastingHelpfulSpell(Unit* target)
    {
        if (!target)
            return false;

        Spell* spell = target->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (spell && spell->m_spellInfo && (spell->m_spellInfo->IsPositive() ||
            PlayerbotAI::IsHealingSpell(spell->m_spellInfo->SpellFamilyName, spell->m_spellInfo->SpellFamilyFlags)))
            return true;

        spell = target->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
        return spell && spell->m_spellInfo && (spell->m_spellInfo->IsPositive() ||
            PlayerbotAI::IsHealingSpell(spell->m_spellInfo->SpellFamilyName, spell->m_spellInfo->SpellFamilyFlags));
    }

    bool HasPeriodicDamage(Unit* target)
    {
        return target && (
            target->HasAuraType(SPELL_AURA_PERIODIC_DAMAGE) ||
            target->HasAuraType(SPELL_AURA_PERIODIC_DAMAGE_PERCENT) ||
            target->HasAuraType(SPELL_AURA_PERIODIC_LEECH));
    }

    bool IsInActiveAoe(PlayerbotAI* botAI, Unit* target)
    {
        if (!botAI || !target)
            return false;

        if (*botAI->GetAiObjectContext()->GetValue<uint8>("aoe count") <= 2)
            return false;

        WorldLocation aoe = *botAI->GetAiObjectContext()->GetValue<WorldLocation>("aoe position");
        return ServerFacade::instance().IsDistanceLessOrEqualThan(
            ServerFacade::instance().GetDistance2d(target, aoe.GetPositionX(), aoe.GetPositionY()),
            sPlayerbotAIConfig.aoeRadius);
    }

    bool IsFriendlyDamagePressure(PlayerbotAI* botAI, Unit* target)
    {
        if (!botAI || !target)
            return false;

        Player* bot = botAI->GetBot();
        if (!bot)
            return false;

        Unit::AttackerSet const& attackers = target->getAttackers();
        for (Unit* attacker : attackers)
        {
            if (!attacker || !attacker->IsAlive())
                continue;

            Player* playerAttacker = attacker->ToPlayer();
            if (!playerAttacker && attacker->GetOwner())
                playerAttacker = attacker->GetOwner()->ToPlayer();

            if (playerAttacker && playerAttacker != bot && !botAI->IsOpposing(playerAttacker))
                return true;
        }

        return false;
    }

    bool IsTreeHealerDruid(PlayerbotAI* botAI, Unit* target)
    {
        Player* player = target ? target->ToPlayer() : nullptr;
        return botAI && player && player->getClass() == CLASS_DRUID && player->GetShapeshiftForm() == FORM_TREE &&
               botAI->IsHeal(player);
    }

    bool GetActiveAVObjective(PlayerbotAI* botAI, Player* bot, PositionInfo& objective)
    {
        if (!botAI || !bot)
            return false;

        Battleground* bg = bot->GetBattleground();
        if (!bg)
            return false;

        BattlegroundTypeId bgType = bg->GetBgTypeID();
        if (bgType == BATTLEGROUND_RB)
            bgType = bg->GetBgTypeID(true);

        if (bgType != BATTLEGROUND_AV)
            return false;

        PositionMap& positions = botAI->GetAiObjectContext()->GetValue<PositionMap&>("position")->Get();
        auto const itr = positions.find("bg objective");
        if (itr == positions.end() || !itr->second.valueSet)
            return false;

        objective = itr->second;
        return true;
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
        Player* victim = target && target->GetVictim() ? target->GetVictim()->ToPlayer() : nullptr;
        return victim && !botAI->IsOpposing(victim) && botAI->IsHeal(victim);
    }

    bool CanFreeCcDuringAVObjective(PlayerbotAI* botAI, Unit* target, bool threatTarget)
    {
        if (threatTarget)
            return true;

        Player* bot = botAI ? botAI->GetBot() : nullptr;
        PositionInfo objective;
        if (!GetActiveAVObjective(botAI, bot, objective))
            return true;

        if (!target)
            return false;

        if (target->GetVictim() == bot || IsAttackingFriendlyHealer(botAI, target))
            return true;

        float const distanceToBot = ServerFacade::instance().GetDistance2d(bot, target);
        if (distanceToBot <= 18.0f)
            return true;

        return IsNearObjective(bot, objective, 60.0f) && IsNearObjective(target, objective, 38.0f);
    }
}

namespace ai::cc
{
    bool HasActiveCrowdControl(Unit* target)
    {
        return target && (
            target->HasAuraType(SPELL_AURA_MOD_CHARM) ||
            target->HasAuraType(SPELL_AURA_MOD_CONFUSE) ||
            target->HasAuraType(SPELL_AURA_MOD_FEAR) ||
            target->HasAuraType(SPELL_AURA_MOD_PACIFY) ||
            target->HasAuraType(SPELL_AURA_MOD_PACIFY_SILENCE) ||
            target->HasAuraType(SPELL_AURA_MOD_POSSESS) ||
            target->HasAuraType(SPELL_AURA_MOD_POSSESS_PET) ||
            target->HasAuraType(SPELL_AURA_MOD_STUN));
    }

    bool HasPolymorphFromCaster(Unit* target, ObjectGuid const& casterGuid)
    {
        if (!target || !casterGuid)
            return false;

        Unit::AuraApplicationMap const& auras = target->GetAppliedAuras();
        for (Unit::AuraApplicationMap::const_iterator itr = auras.begin(); itr != auras.end(); ++itr)
        {
            AuraApplication const* aurApp = itr->second;
            Aura const* aura = aurApp ? aurApp->GetBase() : nullptr;
            if (!aura || aura->IsRemoved() || aura->GetCasterGUID() != casterGuid)
                continue;

            SpellInfo const* spellInfo = aura->GetSpellInfo();
            if (spellInfo && spellInfo->GetSpellSpecific() == SPELL_SPECIFIC_MAGE_POLYMORPH)
                return true;
        }

        return false;
    }

    bool HasSpellRankFromCaster(Unit* target, ObjectGuid const& casterGuid, SpellInfo const* expectedSpellInfo)
    {
        if (!target || !casterGuid || !expectedSpellInfo)
            return false;

        Unit::AuraApplicationMap const& auras = target->GetAppliedAuras();
        for (Unit::AuraApplicationMap::const_iterator itr = auras.begin(); itr != auras.end(); ++itr)
        {
            AuraApplication const* aurApp = itr->second;
            Aura const* aura = aurApp ? aurApp->GetBase() : nullptr;
            if (!aura || aura->IsRemoved() || aura->GetCasterGUID() != casterGuid)
                continue;

            SpellInfo const* spellInfo = aura->GetSpellInfo();
            if (spellInfo && spellInfo->IsRankOf(expectedSpellInfo))
                return true;
        }

        return false;
    }

    Unit* GetActivePolymorphTarget(PlayerbotAI* botAI)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        if (!bot)
            return nullptr;

        ObjectGuid const casterGuid = bot->GetGUID();
        std::unordered_set<ObjectGuid> checked;

        auto checkGuid = [&](ObjectGuid const& guid) -> Unit*
        {
            if (!guid || checked.find(guid) != checked.end())
                return nullptr;

            checked.insert(guid);
            Unit* unit = botAI->GetUnit(guid);
            if (!unit || !unit->IsInWorld() || !unit->IsAlive() || unit->GetMapId() != bot->GetMapId())
                return nullptr;

            return HasPolymorphFromCaster(unit, casterGuid) ? unit : nullptr;
        };

        if (Unit* currentTarget = botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get())
            if (HasPolymorphFromCaster(currentTarget, casterGuid))
                return currentTarget;

        GuidVector attackers = botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get();
        for (ObjectGuid const& guid : attackers)
            if (Unit* unit = checkGuid(guid))
                return unit;

        GuidVector possibleTargets = botAI->GetAiObjectContext()->GetValue<GuidVector>("possible targets")->Get();
        for (ObjectGuid const& guid : possibleTargets)
            if (Unit* unit = checkGuid(guid))
                return unit;

        return nullptr;
    }

    Unit* GetActiveFearTarget(PlayerbotAI* botAI)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        if (!bot)
            return nullptr;

        uint32 const fearSpellId = botAI->GetAiObjectContext()->GetValue<uint32>("spell id", "fear")->Get();
        SpellInfo const* fearSpellInfo = fearSpellId ? sSpellMgr->GetSpellInfo(fearSpellId) : nullptr;
        if (!fearSpellInfo)
            return nullptr;

        ObjectGuid const casterGuid = bot->GetGUID();
        std::unordered_set<ObjectGuid> checked;

        auto checkGuid = [&](ObjectGuid const& guid) -> Unit*
        {
            if (!guid || checked.find(guid) != checked.end())
                return nullptr;

            checked.insert(guid);
            Unit* unit = botAI->GetUnit(guid);
            if (!unit || !unit->IsInWorld() || !unit->IsAlive() || unit->GetMapId() != bot->GetMapId())
                return nullptr;

            return HasSpellRankFromCaster(unit, casterGuid, fearSpellInfo) ? unit : nullptr;
        };

        if (Unit* currentTarget = botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get())
            if (HasSpellRankFromCaster(currentTarget, casterGuid, fearSpellInfo))
                return currentTarget;

        GuidVector attackers = botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get();
        for (ObjectGuid const& guid : attackers)
            if (Unit* unit = checkGuid(guid))
                return unit;

        GuidVector possibleTargets = botAI->GetAiObjectContext()->GetValue<GuidVector>("possible targets")->Get();
        for (ObjectGuid const& guid : possibleTargets)
            if (Unit* unit = checkGuid(guid))
                return unit;

        return nullptr;
    }

    bool IsGoodPolymorphTarget(PlayerbotAI* botAI, Unit* target)
    {
        if (!botAI || !target || !target->IsAlive())
            return false;

        Player* bot = botAI->GetBot();
        if (!bot || target == bot)
            return false;

        if (Unit* activePolymorph = GetActivePolymorphTarget(botAI))
            if (activePolymorph != target)
                return false;

        Unit* currentTarget = *botAI->GetAiObjectContext()->GetValue<Unit*>("current target");
        if (target == currentTarget || bot->GetVictim() == target)
            return false;

        if (target->GetHealthPct() < 85.0f)
            return false;

        if (HasPeriodicDamage(target) || IsFriendlyDamagePressure(botAI, target) || IsInActiveAoe(botAI, target))
            return false;

        return true;
    }

    bool IsDiminishingBlocked(PlayerbotAI* botAI, Unit* target, std::string const& spell)
    {
        if (!target)
            return true;

        DiminishingGroup const group = GetDiminishingGroup(botAI, spell);
        if (group == DIMINISHING_NONE)
            return false;

        uint32 const now = getMSTime();
        std::lock_guard<std::mutex> guard(ccDiminishingMutex);
        PruneDiminishingCache(now);

        auto itr = ccDiminishingCache.find({ target->GetGUID(), group });
        if (itr == ccDiminishingCache.end() || IsDiminishingExpired(itr->second, now))
            return false;

        return itr->second.hitCount >= 3;
    }

    int32 GetDiminishingPenalty(PlayerbotAI* botAI, Unit* target, std::string const& spell)
    {
        if (!target)
            return 100000;

        DiminishingGroup const group = GetDiminishingGroup(botAI, spell);
        if (group == DIMINISHING_NONE)
            return 0;

        uint32 const now = getMSTime();
        std::lock_guard<std::mutex> guard(ccDiminishingMutex);

        auto itr = ccDiminishingCache.find({ target->GetGUID(), group });
        if (itr == ccDiminishingCache.end() || IsDiminishingExpired(itr->second, now))
            return 0;

        if (itr->second.hitCount >= 3)
            return 100000;

        return itr->second.hitCount == 2 ? 140 : 35;
    }

    void RecordCrowdControl(PlayerbotAI* botAI, Unit* target, std::string const& spell)
    {
        if (!target)
            return;

        DiminishingGroup const group = GetDiminishingGroup(botAI, spell);
        if (group == DIMINISHING_NONE)
            return;

        uint32 const now = getMSTime();
        std::lock_guard<std::mutex> guard(ccDiminishingMutex);
        PruneDiminishingCache(now);

        CcDiminishingState& state = ccDiminishingCache[{ target->GetGUID(), group }];
        state.hitCount = IsDiminishingExpired(state, now) ? 1 : std::min<uint8>(state.hitCount + 1, 4);
        state.lastMs = now;
    }
}

class FindTargetForCcStrategy : public FindTargetStrategy
{
public:
    FindTargetForCcStrategy(PlayerbotAI* botAI, std::string const spell)
        : FindTargetStrategy(botAI), spell(spell), bestScore(std::numeric_limits<int32>::min())
    {
    }

public:
    void CheckAttacker(Unit* creature, ThreatManager* /*threatMgr*/) override
    {
        CheckCandidate(creature, true);
    }

    void CheckCandidate(Unit* creature, bool threatTarget = false)
    {
        Player* bot = botAI->GetBot();
        if (!creature || !creature->IsAlive() || creature == bot)
            return;

        if (!ai::pvp::CanEngageDuringBattlegroundCapture(botAI, creature))
            return;

        if (!CanFreeCcDuringAVObjective(botAI, creature, threatTarget))
            return;

        if (!botAI->CanCastSpell(spell, creature))
            return;

        if (ai::cc::HasActiveCrowdControl(creature) || ai::cc::IsDiminishingBlocked(botAI, creature, spell))
            return;

        bool const isPolymorph = spell == "polymorph";
        if (isPolymorph && !ai::cc::IsGoodPolymorphTarget(botAI, creature))
            return;

        if (*botAI->GetAiObjectContext()->GetValue<Unit*>("rti cc target") == creature)
        {
            result = creature;
            bestScore = std::numeric_limits<int32>::max();
            return;
        }

        Unit* currentTarget = *botAI->GetAiObjectContext()->GetValue<Unit*>("current target");
        Unit* victim = creature->GetVictim();
        bool const isEnemyPlayer = creature->IsPlayer();
        bool const isEnemyHealer = isEnemyPlayer && botAI->IsHeal(creature->ToPlayer());
        bool const isTreeHealerDruid = spell == "banish" && isEnemyPlayer && IsTreeHealerDruid(botAI, creature);
        bool const isCastingHeal = IsCastingHelpfulSpell(creature);
        bool const isThreateningBot = victim == bot || creature->GetTarget() == bot->GetGUID();

        uint8 health = static_cast<uint8>(creature->GetHealthPct());
        if (health < sPlayerbotAIConfig.mediumHealth && !isEnemyHealer && !isCastingHeal && !isThreateningBot)
            return;

        int32 score = 0;
        float const distance = ServerFacade::instance().GetDistance2d(bot, creature);

        if (isEnemyPlayer)
            score += 80;

        if (isEnemyHealer)
            score += 450;

        if (isTreeHealerDruid)
            score += 900;

        if (isCastingHeal)
            score += 500;

        if (isThreateningBot)
            score += 360;

        if (victim && victim->IsPlayer() && botAI->IsHeal(victim->ToPlayer()))
            score += 220;

        if (creature == currentTarget)
            score -= isThreateningBot ? 20 : 130;

        if (spell == "banish")
            score += isTreeHealerDruid ? 260 : 160;

        if (distance < 12.0f)
            score += 60;
        else if (distance > botAI->GetRange("spell") * 0.85f)
            score -= 25;

        Group* group = bot->GetGroup();
        if (*botAI->GetAiObjectContext()->GetValue<uint8>("aoe count") > 2)
        {
            WorldLocation aoe = *botAI->GetAiObjectContext()->GetValue<WorldLocation>("aoe position");
            if (ServerFacade::instance().IsDistanceLessOrEqualThan(
                    ServerFacade::instance().GetDistance2d(creature, aoe.GetPositionX(), aoe.GetPositionY()),
                    sPlayerbotAIConfig.aoeRadius))
            {
                if (isPolymorph)
                    return;

                score -= isEnemyHealer ? 50 : 220;
            }
        }

        if (group)
        {
            uint32 tankCount = 0;
            uint32 dpsCount = 0;
            GetPlayerCount(creature, &tankCount, &dpsCount);
            if (!tankCount || !dpsCount)
                score += 120;

            float minTankDistance = botAI->GetRange("spell");
            Group::MemberSlotList const& groupSlot = group->GetMemberSlots();
            for (Group::member_citerator itr = groupSlot.begin(); itr != groupSlot.end(); itr++)
            {
                Player* member = ObjectAccessor::FindPlayer(itr->guid);
                if (!member || !member->IsAlive() || member == bot)
                    continue;

                if (!botAI->IsTank(member))
                    continue;

                float tankDistance = ServerFacade::instance().GetDistance2d(member, creature);
                if (tankDistance < minTankDistance)
                    minTankDistance = tankDistance;
            }

            score += static_cast<int32>(std::min(minTankDistance, 40.0f));
        }

        score -= ai::cc::GetDiminishingPenalty(botAI, creature, spell);

        if (!result || score > bestScore)
        {
            result = creature;
            bestScore = score;
        }
    }

private:
    std::string const spell;
    int32 bestScore;
};

Unit* CcTargetValue::Calculate()
{
    if (qualifier == "polymorph" && ai::cc::GetActivePolymorphTarget(botAI))
        return nullptr;

    if (qualifier == "fear" && ai::cc::GetActiveFearTarget(botAI))
        return nullptr;

    FindTargetForCcStrategy strategy(botAI, qualifier);
    std::unordered_set<ObjectGuid> checked;

    auto checkGuid = [&](ObjectGuid const& guid, bool threatTarget = false)
    {
        if (!guid || checked.find(guid) != checked.end())
            return;

        checked.insert(guid);
        strategy.CheckCandidate(botAI->GetUnit(guid), threatTarget);
    };

    if (Unit* rtiTarget = *botAI->GetAiObjectContext()->GetValue<Unit*>("rti cc target"))
        checkGuid(rtiTarget->GetGUID(), true);

    if (Unit* healer = botAI->GetAiObjectContext()->GetValue<Unit*>("enemy healer target", qualifier)->Get())
        checkGuid(healer->GetGUID());

    GuidVector attackers = botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get();
    for (ObjectGuid const& guid : attackers)
        checkGuid(guid, true);

    Player* bot = botAI->GetBot();
    if (bot->InBattleground() || bot->InArena() || bot->duel)
    {
        GuidVector possibleTargets = botAI->GetAiObjectContext()->GetValue<GuidVector>("possible targets")->Get();
        uint8 scanned = 0;
        for (ObjectGuid const& guid : possibleTargets)
        {
            checkGuid(guid);
            if (++scanned >= 24)
                break;
        }
    }

    return strategy.GetResult();
}
