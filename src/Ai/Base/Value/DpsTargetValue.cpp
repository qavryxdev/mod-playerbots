/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DpsTargetValue.h"

#include "Battleground.h"
#include "ObjectGuid.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "PositionValue.h"
#include "PvpTactics.h"
#include "ServerFacade.h"
#include "Spell.h"
#include "SpellAuraDefines.h"

#include <algorithm>
#include <limits>
#include <unordered_set>

namespace
{
    // These helpers used to be copy-pasted here with subtly different rules than the ones in
    // PvpTactics, so fixes only ever landed on one side. They now forward to the single definition.
    using ai::pvp::IsAttackingFriendlyHealer;
    using ai::pvp::IsBreakableCrowdControlled;
    using ai::pvp::IsCastingHelpfulSpell;
    using ai::pvp::IsEnemyPlayerOrOwnedUnit;
    using ai::pvp::IsNearObjective;

    bool IsPvpTargetingContext(Player* bot)
    {
        return ai::pvp::IsPvpContext(bot);
    }

    bool IsAlteracTowerBowman(Unit* target)
    {
        if (!target || !target->ToCreature())
            return false;

        switch (target->GetEntry())
        {
            case 13358: // Stormpike Bowman
            case 13359: // Frostwolf Bowman
                return true;
            default:
                return false;
        }
    }

    bool GetActiveAVObjective(PlayerbotAI* botAI, Player* bot, PositionInfo& objective)
    {
        return ai::pvp::GetActiveAVObjective(botAI, bot, objective);
    }

    bool CanFreeTargetDuringAVObjective(PlayerbotAI* botAI, Unit* target, bool threatTarget)
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

        // While AV pathing owns the bot, only fight free PvP targets that are on the same objective.
        return IsNearObjective(bot, objective, 60.0f) && IsNearObjective(target, objective, 38.0f);
    }

    bool ShouldKeepCurrentAlteracTowerBowman(PlayerbotAI* botAI, Unit* candidate, Unit* currentTarget)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        if (!bot || !candidate || !currentTarget || candidate == currentTarget)
            return false;

        if (!IsAlteracTowerBowman(candidate) || !IsAlteracTowerBowman(currentTarget))
            return false;

        if (!currentTarget->IsAlive() || !currentTarget->IsInWorld() || currentTarget->GetMapId() != bot->GetMapId() ||
            bot->IsFriendlyTo(currentTarget) || !bot->IsValidAttackTarget(currentTarget) ||
            !bot->IsWithinLOSInMap(currentTarget))
            return false;

        bool const currentBowmanIsEngaged =
            currentTarget->GetVictim() == bot || currentTarget->GetTarget() == bot->GetGUID() || bot->GetVictim() == currentTarget;
        if (!currentBowmanIsEngaged)
            return false;

        float const candidateDistance = ServerFacade::instance().GetDistance2d(bot, candidate);
        float const currentDistance = ServerFacade::instance().GetDistance2d(bot, currentTarget);
        float const stickRange =
            botAI->IsRanged(bot) ? sPlayerbotAIConfig.spellDistance + 20.0f : sPlayerbotAIConfig.meleeDistance + 12.0f;

        return currentDistance <= stickRange || currentDistance <= candidateDistance + 20.0f;
    }
}

class PvpFindTargetSmartStrategy : public FindTargetStrategy
{
public:
    PvpFindTargetSmartStrategy(PlayerbotAI* botAI)
        : FindTargetStrategy(botAI),
          bestScore(std::numeric_limits<int32>::min()),
          currentTarget(botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get())
    {
    }

    void CheckAttacker(Unit* attacker, ThreatManager*) override { CheckCandidate(attacker, true); }

    void CheckCandidate(Unit* target, bool allowNpcCombatTarget = false)
    {
        Player* bot = botAI->GetBot();
        if (!target || !target->IsAlive() || target == bot)
            return;

        if (!target->IsInWorld() || target->GetMapId() != bot->GetMapId() || bot->IsFriendlyTo(target) ||
            !bot->IsValidAttackTarget(target) || !bot->IsWithinLOSInMap(target))
            return;

        if (!ai::pvp::CanEngageDuringBattlegroundCapture(botAI, target))
            return;

        if (!CanFreeTargetDuringAVObjective(botAI, target, allowNpcCombatTarget))
            return;

        if (Group* group = bot->GetGroup())
        {
            ObjectGuid guid = group->GetTargetIcon(4);
            if (guid && target->GetGUID() == guid)
                return;
        }

        if (ShouldKeepCurrentAlteracTowerBowman(botAI, target, currentTarget))
            return;

        bool const isHighPriority = IsHighPriority(target);
        bool const isEnemyPlayerUnit = IsEnemyPlayerOrOwnedUnit(botAI, target);
        bool const isNpcCombatTarget =
            allowNpcCombatTarget && !isEnemyPlayerUnit && (target->GetVictim() == bot || IsAttackingFriendlyHealer(botAI, target));
        if (!isEnemyPlayerUnit && !isHighPriority && !isNpcCombatTarget)
            return;

        int32 score = 0;
        Player* playerTarget = target->ToPlayer();
        bool const isEnemyHealer = playerTarget && botAI->IsHeal(playerTarget);
        bool const isCastingHeal = IsCastingHelpfulSpell(target);
        bool const isProtectingHealer = IsAttackingFriendlyHealer(botAI, target);
        float const distance = ServerFacade::instance().GetDistance2d(bot, target);
        float const attackRange =
            botAI->IsRanged(bot) ? sPlayerbotAIConfig.spellDistance : sPlayerbotAIConfig.meleeDistance + 5.0f;
        PositionInfo activeObjective;
        bool const hasActiveObjective = GetActiveAVObjective(botAI, bot, activeObjective);
        bool const targetOnObjective = hasActiveObjective && IsNearObjective(target, activeObjective, 16.0f);

        if (isHighPriority)
            score += 100000;

        if (targetOnObjective)
        {
            score += 720;
            if (target->IsNonMeleeSpellCast(true))
                score += 520;
        }

        if (isEnemyPlayerUnit)
            score += 220;

        if (isEnemyHealer)
            score += 520;

        if (isCastingHeal)
            score += 480;

        if (isProtectingHealer)
            score += 420;

        if (target->GetVictim() == bot)
            score += botAI->IsHeal(bot) ? 260 : 120;

        float const healthPct = target->GetHealthPct();
        if (healthPct < 20.0f)
            score += 360;
        else if (healthPct < 35.0f)
            score += 240;
        else if (healthPct < 50.0f)
            score += 120;

        // A crowd controlled enemy is a bad pick, but excluding it outright used to leave the bot with
        // no target at all whenever the only enemy nearby got feared or sapped - and then the whole
        // rotation stalled. Rank it last instead so it still serves as a fallback.
        if (IsBreakableCrowdControlled(target))
            score -= 4000;

        if (target == currentTarget)
            score += 420;

        if (target == currentTarget && IsAlteracTowerBowman(target))
            score += 2500;

        score += ai::pvp::ScoreClassMatchup(botAI, target);

        if (bot->GetVictim() == target)
            score += 80;

        uint32 tankCount = 0;
        uint32 dpsCount = 0;
        GetPlayerCount(target, &tankCount, &dpsCount);
        uint32 const friendlyPressure = tankCount + dpsCount;
        if (friendlyPressure >= 3)
            score += 320;
        else if (friendlyPressure == 2)
            score += 220;
        else if (friendlyPressure == 1)
            score += 90;

        uint32 const usefulAttackerCap =
            isHighPriority || targetOnObjective ? 6u : (isEnemyHealer ? 4u : (healthPct < 25.0f ? 2u : 3u));
        if (friendlyPressure > usefulAttackerCap && target != currentTarget && !isCastingHeal)
            score -= static_cast<int32>((friendlyPressure - usefulAttackerCap) * 240);

        if (distance <= attackRange)
            score += 140;
        else
            score -= static_cast<int32>(std::min(distance, 80.0f));

        int32 requiredScore = bestScore;
        if (result == currentTarget && target != currentTarget && !isHighPriority && target->GetVictim() != bot)
        {
            int32 switchThreshold = 650;
            if (isProtectingHealer || (isEnemyHealer && isCastingHeal))
                switchThreshold = 260;

            requiredScore += switchThreshold;
        }

        if (!result || score > requiredScore)
        {
            result = target;
            bestScore = score;
        }
    }

private:
    int32 bestScore;
    Unit* currentTarget;
};

class FindMaxThreatGapTargetStrategy : public FindTargetStrategy
{
public:
    FindMaxThreatGapTargetStrategy(PlayerbotAI* botAI) : FindTargetStrategy(botAI), minThreat(0) {}

    void CheckAttacker(Unit* attacker, ThreatManager* threatMgr) override
    {
        if (!attacker->IsAlive())
            return;

        if (foundHighPriority)
            return;

        if (IsHighPriority(attacker))
        {
            result = attacker;
            foundHighPriority = true;
            return;
        }
        if (!result || CalcThreatGap(attacker, threatMgr) > CalcThreatGap(result, &result->GetThreatMgr()))
            result = attacker;
    }
    float CalcThreatGap(Unit* attacker, ThreatManager* threatMgr)
    {
        Unit* victim = attacker->GetVictim();
        return threatMgr->GetThreat(victim) - threatMgr->GetThreat(attacker);
    }

protected:
    float minThreat;
};

// caster
class CasterFindTargetSmartStrategy : public FindTargetStrategy
{
public:
    CasterFindTargetSmartStrategy(PlayerbotAI* botAI, float dps)
        : FindTargetStrategy(botAI), dps_(dps), targetExpectedLifeTime(1000000)
    {
        result = nullptr;
    }

    void CheckAttacker(Unit* attacker, ThreatManager* /*threatMgr*/) override
    {
        if (Group* group = botAI->GetBot()->GetGroup())
        {
            ObjectGuid guid = group->GetTargetIcon(4);
            if (guid && attacker->GetGUID() == guid)
                return;
        }
        if (!attacker->IsAlive())
            return;

        if (foundHighPriority)
            return;

        if (IsHighPriority(attacker))
        {
            result = attacker;
            foundHighPriority = true;
            return;
        }
        float expectedLifeTime = attacker->GetHealth() / dps_;
        // Unit* victim = attacker->GetVictim();
        if (!result || IsBetter(attacker, result))
        {
            targetExpectedLifeTime = expectedLifeTime;
            result = attacker;
        }
    }
    bool IsBetter(Unit* new_unit, Unit* old_unit)
    {
        float new_time = new_unit->GetHealth() / dps_;
        float old_time = old_unit->GetHealth() / dps_;
        // [5-30] > (5-0] > (20-inf)
        int new_level = GetIntervalLevel(new_unit);
        int old_level = GetIntervalLevel(old_unit);
        if (new_level != old_level)
            return new_level > old_level;

        int32_t level = new_level;
        if (level % 10 == 2 || level % 10 == 0)
            return new_time < old_time;
        // dont switch targets when all of them with low health
        Unit* currentTarget = botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
        if (currentTarget == new_unit)
            return true;

        if (currentTarget == old_unit)
            return false;

        return new_time > old_time;
    }
    int32_t GetIntervalLevel(Unit* unit)
    {
        float time = unit->GetHealth() / dps_;
        float dis = unit->GetDistance(botAI->GetBot());
        float attackRange =
            botAI->IsRanged(botAI->GetBot()) ? sPlayerbotAIConfig.spellDistance : sPlayerbotAIConfig.meleeDistance;
        attackRange += 5.0f;
        int level = dis < attackRange ? 10 : 0;
        if (time >= 5 && time <= 30)
            return level + 2;

        if (time > 30)
            return level;

        return level + 1;
    }

protected:
    float dps_;
    float targetExpectedLifeTime;
};

// General
class GeneralFindTargetSmartStrategy : public FindTargetStrategy
{
public:
    GeneralFindTargetSmartStrategy(PlayerbotAI* botAI, float dps)
        : FindTargetStrategy(botAI), dps_(dps), targetExpectedLifeTime(1000000)
    {
    }

    void CheckAttacker(Unit* attacker, ThreatManager*) override
    {
        if (Group* group = botAI->GetBot()->GetGroup())
        {
            ObjectGuid guid = group->GetTargetIcon(4);
            if (guid && attacker->GetGUID() == guid)
                return;
        }
        if (!attacker->IsAlive())
            return;

        if (foundHighPriority)
            return;

        if (IsHighPriority(attacker))
        {
            result = attacker;
            foundHighPriority = true;
            return;
        }
        float expectedLifeTime = attacker->GetHealth() / dps_;
        // Unit* victim = attacker->GetVictim();
        if (!result || IsBetter(attacker, result))
        {
            targetExpectedLifeTime = expectedLifeTime;
            result = attacker;
        }
    }
    bool IsBetter(Unit* new_unit, Unit* old_unit)
    {
        float new_time = new_unit->GetHealth() / dps_;
        float old_time = old_unit->GetHealth() / dps_;
        int new_level = GetIntervalLevel(new_unit);
        int old_level = GetIntervalLevel(old_unit);
        if (new_level != old_level)
        {
            return new_level > old_level;
        }
        // attack enemy in range and with lowest health
        int level = new_level;
        if (level == 10)
            return new_time < old_time;

        // all targets are far away, choose the closest one
        return botAI->GetBot()->GetDistance(new_unit) < botAI->GetBot()->GetDistance(old_unit);
    }
    int32_t GetIntervalLevel(Unit* unit)
    {
        float dis = unit->GetDistance(botAI->GetBot());
        float attackRange =
            botAI->IsRanged(botAI->GetBot()) ? sPlayerbotAIConfig.spellDistance : sPlayerbotAIConfig.meleeDistance;
        attackRange += 5.0f;
        int level = dis < attackRange ? 10 : 0;
        return level;
    }

protected:
    float dps_;
    float targetExpectedLifeTime;
};

// combo
class ComboFindTargetSmartStrategy : public FindTargetStrategy
{
public:
    ComboFindTargetSmartStrategy(PlayerbotAI* botAI, float dps)
        : FindTargetStrategy(botAI), dps_(dps), targetExpectedLifeTime(1000000)
    {
    }

    void CheckAttacker(Unit* attacker, ThreatManager*) override
    {
        if (Group* group = botAI->GetBot()->GetGroup())
        {
            ObjectGuid guid = group->GetTargetIcon(4);
            if (guid && attacker->GetGUID() == guid)
                return;
        }
        if (!attacker->IsAlive())
            return;

        if (foundHighPriority)
            return;

        if (IsHighPriority(attacker))
        {
            result = attacker;
            foundHighPriority = true;
            return;
        }
        float expectedLifeTime = attacker->GetHealth() / dps_;
        // Unit* victim = attacker->GetVictim();
        if (!result || IsBetter(attacker, result))
        {
            targetExpectedLifeTime = expectedLifeTime;
            result = attacker;
        }
    }
    bool IsBetter(Unit* new_unit, Unit* old_unit)
    {
        float new_time = new_unit->GetHealth() / dps_;
        float old_time = old_unit->GetHealth() / dps_;
        // [5-20] > (5-0] > (20-inf)
        int new_level = GetIntervalLevel(new_unit);
        int old_level = GetIntervalLevel(old_unit);
        if (new_level != old_level)
            return new_level > old_level;

        // attack enemy in range and with lowest health
        int level = new_level;
        Player* bot = botAI->GetBot();
        if (level == 10)
        {
            Unit* combo_unit = bot->GetComboTarget();
            if (new_unit == combo_unit)
                return true;

            return new_time < old_time;
        }
        // all targets are far away, choose the closest one
        return bot->GetDistance(new_unit) < bot->GetDistance(old_unit);
    }
    int32_t GetIntervalLevel(Unit* unit)
    {
        float dis = unit->GetDistance(botAI->GetBot());
        float attackRange =
            botAI->IsRanged(botAI->GetBot()) ? sPlayerbotAIConfig.spellDistance : sPlayerbotAIConfig.meleeDistance;
        attackRange += 5.0f;
        int level = dis < attackRange ? 10 : 0;
        return level;
    }

protected:
    float dps_;
    float targetExpectedLifeTime;
};

Unit* DpsTargetValue::Calculate()
{
    Unit* rti = RtiTargetValue::Calculate();
    if (rti)
        return rti;

    if (IsPvpTargetingContext(bot))
    {
        PvpFindTargetSmartStrategy strategy(botAI);
        std::unordered_set<ObjectGuid> checked;

        auto checkGuid = [&](ObjectGuid const& guid, bool threatTarget = false)
        {
            if (!guid || checked.find(guid) != checked.end())
                return;

            checked.insert(guid);
            strategy.CheckCandidate(botAI->GetUnit(guid), threatTarget);
        };

        if (Unit* currentTarget = botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get())
            checkGuid(currentTarget->GetGUID());

        GuidVector attackers = botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get();
        for (ObjectGuid const& guid : attackers)
            checkGuid(guid, true);

        if (Unit* enemyPlayer = botAI->GetAiObjectContext()->GetValue<Unit*>("enemy player target")->Get())
            checkGuid(enemyPlayer->GetGUID());

        GuidVector possibleTargets = botAI->GetAiObjectContext()->GetValue<GuidVector>("possible targets")->Get();
        uint8 scanned = 0;
        for (ObjectGuid const& guid : possibleTargets)
        {
            checkGuid(guid);
            if (++scanned >= 32)
                break;
        }

        if (Unit* target = strategy.GetResult())
            return target;
    }

    float dps = AI_VALUE(float, "estimated group dps");

    if (botAI->GetNearGroupMemberCount() > 3)
    {
        if (botAI->IsCaster(bot))
        {
            // Caster find target strategy avoids casting spells on enemies
            // with too low health to ensure the effectiveness of casting
            CasterFindTargetSmartStrategy strategy(botAI, dps);
            return TargetValue::FindTarget(&strategy);
        }
        else if (botAI->IsCombo(bot))
        {
            ComboFindTargetSmartStrategy strategy(botAI, dps);
            return TargetValue::FindTarget(&strategy);
        }
    }
    GeneralFindTargetSmartStrategy strategy(botAI, dps);
    return TargetValue::FindTarget(&strategy);
}

class FindMaxHpTargetStrategy : public FindTargetStrategy
{
public:
    FindMaxHpTargetStrategy(PlayerbotAI* botAI) : FindTargetStrategy(botAI), maxHealth(0) {}

    void CheckAttacker(Unit* attacker, ThreatManager*) override
    {
        if (Group* group = botAI->GetBot()->GetGroup())
        {
            ObjectGuid guid = group->GetTargetIcon(4);
            if (guid && attacker->GetGUID() == guid)
                return;
        }

        if (!result || result->GetHealth() < attacker->GetHealth())
            result = attacker;
    }

protected:
    float maxHealth;
};

Unit* DpsAoeTargetValue::Calculate()
{
    Unit* rti = RtiTargetValue::Calculate();
    if (rti)
        return rti;

    FindMaxHpTargetStrategy strategy(botAI);
    return TargetValue::FindTarget(&strategy);
}
