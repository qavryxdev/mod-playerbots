/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DruidActions.h"

#include "CcTargetValue.h"
#include "Event.h"
#include "Playerbots.h"
#include "PvpTactics.h"
#include "ServerFacade.h"
#include "AoeValues.h"
#include "TargetValue.h"
#include "Timer.h"

#include <unordered_set>

namespace
{
    constexpr float StarfallPvpRadius = 30.0f;

    bool PrepareThornsTarget(PlayerbotAI* botAI, Unit* target)
    {
        if (!target)
            return false;

        Aura* existingThorns = botAI->GetAura("thorns", target, true);
        if (!existingThorns)
            return true;

        target->RemoveOwnedAura(existingThorns, AURA_REMOVE_BY_CANCEL);
        return true;
    }

    bool IsPvpDruidDotTarget(PlayerbotAI* botAI, Player* bot, Unit* target)
    {
        Player* enemy = target ? target->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;
        return bot && target && target->IsAlive() && target->IsInWorld() &&
               target->GetMapId() == bot->GetMapId() && ai::pvp::IsPvpContext(bot) &&
               enemy && botAI->IsOpposing(enemy);
    }

    bool IsPending(uint32 now, uint32 untilMs)
    {
        return untilMs && static_cast<int32>(untilMs - now) > 0;
    }

    bool IsPvpBalanceDruid(PlayerbotAI* botAI, Player* bot)
    {
        return bot && ai::pvp::IsPvpContext(bot) &&
               botAI->HasStrategy("caster", BOT_STATE_COMBAT);
    }

    bool CanUseBalancePvpNuke(PlayerbotAI* botAI, Player* bot, Unit* target)
    {
        if (!IsPvpDruidDotTarget(botAI, bot, target))
            return true;

        return ai::pvp::CanEngageDuringBattlegroundCapture(botAI, target) &&
               ai::pvp::CanDamageTarget(botAI, target, false) &&
               !ai::pvp::IsMajorDefenseActive(target, ai::pvp::DEFENSE_TIER_IMMUNE);
    }

    struct StarfallPvpSnapshot
    {
        uint32 enemyPlayers = 0;
        bool breakableCrowdControl = false;
    };

    StarfallPvpSnapshot GetStarfallPvpSnapshot(PlayerbotAI* botAI, Player* bot)
    {
        StarfallPvpSnapshot snapshot;
        std::unordered_set<ObjectGuid> checkedUnits;
        std::unordered_set<ObjectGuid> enemyPlayers;

        auto inspect = [&](ObjectGuid const& guid)
        {
            if (!guid || !checkedUnits.insert(guid).second)
                return;

            Unit* unit = botAI->GetUnit(guid);
            Player* enemy = unit ? unit->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;
            if (!unit || !enemy || !unit->IsAlive() || !unit->IsInWorld() ||
                unit->GetMapId() != bot->GetMapId() || !botAI->IsOpposing(enemy) ||
                bot->GetExactDist2d(unit) > StarfallPvpRadius ||
                !bot->IsWithinLOSInMap(unit))
                return;

            if (ai::pvp::IsBreakableCrowdControlled(unit))
            {
                snapshot.breakableCrowdControl = true;
                return;
            }

            if (!ai::pvp::IsMajorDefenseActive(unit, ai::pvp::DEFENSE_TIER_IMMUNE))
                enemyPlayers.insert(enemy->GetGUID());
        };

        GuidVector const possibleTargets =
            botAI->GetAiObjectContext()->GetValue<GuidVector>("possible targets")->Get();
        for (ObjectGuid const& guid : possibleTargets)
            inspect(guid);

        GuidVector const attackers =
            botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get();
        for (ObjectGuid const& guid : attackers)
            inspect(guid);

        snapshot.enemyPlayers = static_cast<uint32>(enemyPlayers.size());
        return snapshot;
    }
}

std::vector<NextAction> CastAbolishPoisonAction::getAlternatives()
{
    return NextAction::merge({ NextAction("cure poison") },
                             CastSpellAction::getPrerequisites());
}

std::vector<NextAction> CastAbolishPoisonOnPartyAction::getAlternatives()
{
    return NextAction::merge({ NextAction("cure poison on party") },
                             CastSpellAction::getPrerequisites());
}

bool CastLifebloomOnMainTankAction::isUseful()
{
    Unit* target = GetTarget();
    if (!target || !target->IsAlive() || !CastSpellAction::isUseful())
        return false;

    Aura* lifebloom = botAI->GetAura("lifebloom", target, true, true);
    return !lifebloom || lifebloom->GetStackAmount() < 3 || lifebloom->GetDuration() < 2000;
}

bool CastThornsAction::Execute(Event event)
{
    return PrepareThornsTarget(botAI, GetTarget()) && CastBuffSpellAction::Execute(event);
}

bool CastThornsOnPartyAction::Execute(Event event)
{
    return PrepareThornsTarget(botAI, GetTarget()) && BuffOnPartyAction::Execute(event);
}

bool CastThornsOnMainTankAction::Execute(Event event)
{
    return PrepareThornsTarget(botAI, GetTarget()) && BuffOnMainTankAction::Execute(event);
}

bool CastFaerieFireAction::isUseful()
{
    Unit* target = GetTarget();
    if (!IsPvpDruidDotTarget(botAI, bot, target))
        return CastDebuffSpellAction::isUseful();

    return ai::pvp::ShouldUseDruidFaerieFire(botAI, target) &&
           CastAuraSpellAction::isUseful();
}

bool CastRejuvenationAction::isUseful()
{
    if (IsPvpBalanceDruid(botAI, bot) && bot->GetHealthPct() >= 65.0f)
        return false;

    return CastHealingSpellAction::isUseful();
}

bool CastRegrowthAction::isUseful()
{
    if (IsPvpBalanceDruid(botAI, bot))
    {
        if (bot->GetHealthPct() >= 35.0f || ai::pvp::GetIncomingPressure(botAI) >= 60 ||
            ai::pvp::GetClosestPvpMeleeAttacker(botAI, 10.0f))
            return false;
    }

    return CastHealingSpellAction::isUseful();
}

bool CastWrathAction::isUseful()
{
    return CanUseBalancePvpNuke(botAI, bot, GetTarget()) &&
           CastSpellAction::isUseful();
}

bool CastMoonfireAction::isUseful()
{
    Unit* target = GetTarget();
    if (!IsPvpDruidDotTarget(botAI, bot, target))
        return CastDebuffSpellAction::isUseful();

    uint32 const now = getMSTime();
    if (IsPending(now, nextPvpAttemptMs) ||
        (lastPvpAttemptTarget && lastPvpAttemptTarget != target->GetGUID() &&
         IsPending(now, nextPvpNewTargetMs)))
        return false;

    Aura* aura = botAI->GetAura("moonfire", target, true);
    return ai::pvp::ShouldUseDruidPvpDot(botAI, target, "moonfire") &&
           (!aura || aura->GetDuration() < 1500) &&
           CastSpellAction::isUseful();
}

bool CastMoonfireAction::Execute(Event event)
{
    Unit* target = GetTarget();
    bool const pvpTarget = IsPvpDruidDotTarget(botAI, bot, target);
    bool const cast = CastDebuffSpellAction::Execute(event);
    if (pvpTarget)
    {
        uint32 const now = getMSTime();
        lastPvpAttemptTarget = target->GetGUID();
        nextPvpAttemptMs = now + (cast ? 1200 : 1800);
        nextPvpNewTargetMs = now + 4000;
    }

    return cast;
}

bool CastInsectSwarmAction::isUseful()
{
    Unit* target = GetTarget();
    if (!IsPvpDruidDotTarget(botAI, bot, target))
        return CastDebuffSpellAction::isUseful();

    uint32 const now = getMSTime();
    if (IsPending(now, nextPvpAttemptMs) ||
        (lastPvpAttemptTarget && lastPvpAttemptTarget != target->GetGUID() &&
         IsPending(now, nextPvpNewTargetMs)))
        return false;

    Aura* aura = botAI->GetAura("insect swarm", target, true);
    return ai::pvp::ShouldUseDruidPvpDot(botAI, target, "insect swarm") &&
           (!aura || aura->GetDuration() < 1500) &&
           CastSpellAction::isUseful();
}

bool CastInsectSwarmAction::Execute(Event event)
{
    Unit* target = GetTarget();
    bool const pvpTarget = IsPvpDruidDotTarget(botAI, bot, target);
    bool const cast = CastDebuffSpellAction::Execute(event);
    if (pvpTarget)
    {
        uint32 const now = getMSTime();
        lastPvpAttemptTarget = target->GetGUID();
        nextPvpAttemptMs = now + (cast ? 1200 : 1800);
        nextPvpNewTargetMs = now + 4000;
    }

    return cast;
}

Unit* CastTyphoonAction::GetTarget()
{
    if (ai::pvp::IsPvpContext(bot))
        return ai::pvp::GetClosestPvpMeleeAttacker(botAI, 10.0f);

    return Action::GetTarget();
}

bool CastTyphoonAction::isUseful()
{
    Unit* target = GetTarget();
    return target && (!ai::pvp::IsPvpContext(bot) ||
                      ai::pvp::CanEngageDuringBattlegroundCapture(botAI, target)) &&
           CastSpellAction::isUseful();
}

bool CastTyphoonAction::Execute(Event event)
{
    Unit* target = GetTarget();
    if (!target)
        return false;

    if (ai::pvp::IsPvpContext(bot))
        bot->SetFacingToObject(target);

    return CastSpellAction::Execute(event);
}

Value<Unit*>* CastEntanglingRootsCcAction::GetTargetValue()
{
    return context->GetValue<Unit*>("cc target", "entangling roots");
}

bool CastEntanglingRootsCcAction::Execute(Event /*event*/)
{
    Unit* target = GetTarget();
    if (!ai::pvp::TryReserveCrowdControl(botAI, target, "entangling roots"))
        return false;

    bool const cast = botAI->CastSpell("entangling roots", target);
    if (!cast)
        ai::pvp::ReleaseCrowdControl(botAI, target);
    return cast;
}

Value<Unit*>* CastHibernateCcAction::GetTargetValue() { return context->GetValue<Unit*>("cc target", "hibernate"); }

bool CastHibernateCcAction::Execute(Event /*event*/)
{
    Unit* target = GetTarget();
    if (!ai::pvp::TryReserveCrowdControl(botAI, target, "hibernate"))
        return false;

    bool const cast = botAI->CastSpell("hibernate", target);
    if (!cast)
        ai::pvp::ReleaseCrowdControl(botAI, target);
    return cast;
}

bool CastCycloneAction::isPossible()
{
    return !ai::cc::GetActiveCycloneTarget(botAI) && CastCrowdControlSpellAction::isPossible();
}

bool CastCycloneAction::isUseful()
{
    return !ai::cc::GetActiveCycloneTarget(botAI) && CastCrowdControlSpellAction::isUseful();
}

bool CastStarfallAction::isUseful()
{
    if (!CastSpellAction::isUseful())
        return false;

    Unit* target = context->GetValue<Unit*>("current target")->Get();
    if (IsPvpDruidDotTarget(botAI, bot, target))
    {
        if (!ai::pvp::CanEngageDuringBattlegroundCapture(botAI, target) ||
            !ai::pvp::CanDamageTarget(botAI, target, true))
            return false;

        StarfallPvpSnapshot const snapshot = GetStarfallPvpSnapshot(botAI, bot);
        if (snapshot.breakableCrowdControl ||
            ai::pvp::GetCombatPhase(botAI, target) == ai::pvp::CombatPhase::Reset)
            return false;

        if (snapshot.enemyPlayers >= 2)
            return true;

        return snapshot.enemyPlayers == 1 &&
               ai::pvp::ShouldUseBurstCooldown(botAI, target);
    }

    WorldLocation aoePos = *context->GetValue<WorldLocation>("aoe position");
    Unit* ccTarget = context->GetValue<Unit*>("current cc target")->Get();
    if (ccTarget && ccTarget->IsAlive())
    {
        float dist2d = ServerFacade::instance().GetDistance2d(ccTarget, aoePos.GetPositionX(), aoePos.GetPositionY());
        if (ServerFacade::instance().IsDistanceLessOrEqualThan(dist2d, sPlayerbotAIConfig.aoeRadius))
            return false;
    }

    // Avoid single-target usage on initial pull
    uint8 aoeCount = *context->GetValue<uint8>("aoe count");
    if (aoeCount < 2)
    {
        Unit* target = context->GetValue<Unit*>("current target")->Get();
        if (!target || (!botAI->HasAura("moonfire", target) && !botAI->HasAura("insect swarm", target)))
            return false;
    }

    return true;
}

bool CastStarfireAction::isUseful()
{
    Unit* target = GetTarget();
    if (IsPvpDruidDotTarget(botAI, bot, target) &&
        (ai::pvp::GetIncomingPressure(botAI) >= 60 ||
         ai::pvp::GetClosestPvpMeleeAttacker(botAI, 10.0f)))
        return false;

    return CanUseBalancePvpNuke(botAI, bot, target) &&
           CastSpellAction::isUseful();
}

bool CastForceOfNatureAction::isUseful()
{
    Unit* target = GetTarget();
    if (!IsPvpDruidDotTarget(botAI, bot, target))
        return CastSpellAction::isUseful();

    return target->GetHealthPct() > 15.0f &&
           ai::pvp::CanEngageDuringBattlegroundCapture(botAI, target) &&
           ai::pvp::CanDamageTarget(botAI, target, false) &&
           ai::pvp::ShouldUseBurstCooldown(botAI, target) &&
           CastSpellAction::isUseful();
}

std::vector<NextAction> CastReviveAction::getPrerequisites()
{
    return NextAction::merge({ NextAction("caster form") },
                             ResurrectPartyMemberAction::getPrerequisites());
}

std::vector<NextAction> CastRebirthAction::getPrerequisites()
{
    return NextAction::merge({ NextAction("caster form") },
                             ResurrectPartyMemberAction::getPrerequisites());
}

bool CastRebirthAction::isUseful()
{
    return CastSpellAction::isUseful() &&
           AI_VALUE2(float, "distance", GetTargetName()) <= sPlayerbotAIConfig.spellDistance;
}

Unit* CastRejuvenationOnNotFullAction::GetTarget()
{
    Group* group = bot->GetGroup();
    MinValueCalculator calc(100);
    for (GroupReference* gref = group->GetFirstMember(); gref; gref = gref->next())
    {
        Player* player = gref->GetSource();
        if (!player)
            continue;
        if (player->isDead() || player->IsFullHealth())
        {
            continue;
        }
        if (player->GetDistance2d(bot) > sPlayerbotAIConfig.spellDistance)
        {
            continue;
        }
        if (botAI->HasAura("rejuvenation", player))
        {
            continue;
        }
        calc.probe(player->GetHealthPct(), player);
    }
    return (Unit*)calc.param;
}

bool CastRejuvenationOnNotFullAction::isUseful()
{
    return GetTarget();
}
