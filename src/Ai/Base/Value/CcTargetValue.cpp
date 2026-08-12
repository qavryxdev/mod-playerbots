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
#include <unordered_set>

namespace
{
    constexpr uint32 CcChainWindowMs = 900;

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

    bool IsCrowdControlAura(SpellInfo const* spellInfo)
    {
        if (!spellInfo)
            return false;

        for (SpellEffectInfo const& effect : spellInfo->Effects)
        {
            if (effect.Effect != SPELL_EFFECT_APPLY_AURA)
                continue;

            switch (effect.ApplyAuraName)
            {
                case SPELL_AURA_MOD_CHARM:
                case SPELL_AURA_MOD_CONFUSE:
                case SPELL_AURA_MOD_FEAR:
                case SPELL_AURA_MOD_PACIFY:
                case SPELL_AURA_MOD_PACIFY_SILENCE:
                case SPELL_AURA_MOD_POSSESS:
                case SPELL_AURA_MOD_POSSESS_PET:
                case SPELL_AURA_MOD_STUN:
                    return true;
                default:
                    break;
            }
        }

        return false;
    }

    using ai::pvp::IsCastingHelpfulSpell;

    bool HasPeriodicDamage(Unit* target)
    {
        return target && (
            target->HasAuraType(SPELL_AURA_PERIODIC_DAMAGE) ||
            target->HasAuraType(SPELL_AURA_PERIODIC_DAMAGE_PERCENT) ||
            target->HasAuraType(SPELL_AURA_PERIODIC_LEECH));
    }

    bool IsDamageBreakableSpell(std::string const& spell)
    {
        static std::unordered_set<std::string> const spells =
        {
            "blind", "fear", "freezing trap", "hex", "hibernate", "polymorph", "repentance", "sap",
            "scare beast", "shackle undead", "wyvern sting"
        };
        return spells.find(spell) != spells.end();
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

    // Single definition lives in PvpTactics; local copies had drifted apart from it.
    using ai::pvp::GetActiveAVObjective;
    using ai::pvp::IsAttackingFriendlyHealer;
    using ai::pvp::IsNearObjective;

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

    uint32 GetCrowdControlRemainingMs(Unit* target)
    {
        if (!target)
            return 0;

        uint32 remainingMs = 0;
        Unit::AuraApplicationMap const& auras = target->GetAppliedAuras();
        for (auto const& [_, aurApp] : auras)
        {
            Aura const* aura = aurApp ? aurApp->GetBase() : nullptr;
            if (!aura || aura->IsRemoved() || !IsCrowdControlAura(aura->GetSpellInfo()))
                continue;

            int32 const duration = aura->GetDuration();
            if (duration < 0)
                return std::numeric_limits<uint32>::max();

            remainingMs = std::max<uint32>(remainingMs, static_cast<uint32>(duration));
        }

        return remainingMs;
    }

    bool CanApplyCrowdControl(PlayerbotAI* botAI, Unit* target, std::string const& spell)
    {
        if (!target || IsDiminishingBlocked(botAI, target, spell))
            return false;

        if (!HasActiveCrowdControl(target))
            return true;

        return GetCrowdControlRemainingMs(target) <= CcChainWindowMs;
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

    Unit* GetActiveCycloneTarget(PlayerbotAI* botAI)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        if (!bot)
            return nullptr;

        uint32 const cycloneSpellId = botAI->GetAiObjectContext()->GetValue<uint32>("spell id", "cyclone")->Get();
        SpellInfo const* cycloneSpellInfo = cycloneSpellId ? sSpellMgr->GetSpellInfo(cycloneSpellId) : nullptr;
        if (!cycloneSpellInfo)
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

            return HasSpellRankFromCaster(unit, casterGuid, cycloneSpellInfo) ? unit : nullptr;
        };

        if (Unit* currentTarget = botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get())
            if (HasSpellRankFromCaster(currentTarget, casterGuid, cycloneSpellInfo))
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

        // GetActivePolymorphTarget walks every nearby unit and all of its auras. CcTargetValue
        // already establishes that no polymorph of ours is up before it starts scoring candidates,
        // so repeating the scan per candidate was pure cost.

        Unit* currentTarget = *botAI->GetAiObjectContext()->GetValue<Unit*>("current target");
        if (target == currentTarget || bot->GetVictim() == target)
            return false;

        if (target->GetHealthPct() < (ai::pvp::IsPvpContext(botAI->GetBot()) ? 20.0f : 85.0f))
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

        return target->GetDiminishing(group) >= DIMINISHING_LEVEL_IMMUNE;
    }

    int32 GetDiminishingPenalty(PlayerbotAI* botAI, Unit* target, std::string const& spell)
    {
        if (!target)
            return 100000;

        DiminishingGroup const group = GetDiminishingGroup(botAI, spell);
        if (group == DIMINISHING_NONE)
            return 0;

        // The old 45/180 penalties were small next to the priority bonuses in the scorer (a healer is
        // worth 520, a casting healer 500 more), so a target already at the last diminishing step
        // still outranked a fresh one and the bot spent its crowd control on a quarter duration.
        switch (target->GetDiminishing(group))
        {
            case DIMINISHING_LEVEL_1:
                return 0;
            case DIMINISHING_LEVEL_2:
                return 250;
            case DIMINISHING_LEVEL_3:
                return 900;
            default:
                return 100000;
        }
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

        // Without a range reject the scorer happily picks a target 100 yards away out of the
        // possible-targets list, and the cast then fails every tick while a reachable one is
        // ignored. EnemyHealerTargetValue applies the same guard.
        if (ServerFacade::instance().GetDistance2d(bot, creature) > botAI->GetRange("spell"))
            return;

        if (!CanFreeCcDuringAVObjective(botAI, creature, threatTarget))
            return;

        if (!botAI->CanCastSpell(spell, creature))
            return;

        if (!ai::cc::CanApplyCrowdControl(botAI, creature, spell))
            return;

        if (IsDamageBreakableSpell(spell) &&
            (HasPeriodicDamage(creature) || IsFriendlyDamagePressure(botAI, creature) || IsInActiveAoe(botAI, creature)))
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
            // Nobody engaged yet is the bonus; with || this was true for every target that had no
            // tank on it, which in a battleground is almost all of them.
            uint32 const engaged = tankCount + dpsCount;
            if (!engaged)
                score += 120;
            else
                score -= static_cast<int32>(std::min<uint32>(engaged, 4u) * 90);

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

    if (qualifier == "cyclone" && ai::cc::GetActiveCycloneTarget(botAI))
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
