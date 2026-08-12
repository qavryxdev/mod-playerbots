/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "MageActions.h"
#include <cmath>
#include <string>
#include <unordered_set>
#include "CcTargetValue.h"
#include "ObjectGuid.h"
#include "UseItemAction.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "PvpTactics.h"
#include "ServerFacade.h"
#include "SharedDefines.h"
#include "Unit.h"

namespace
{
    constexpr float PointBlankAoeRadius = 10.0f;

    bool CanBeFrozen(Unit* unit)
    {
        if (unit->isFrozen())
            return false;

        Creature* creature = unit->ToCreature();
        return !creature || !creature->HasMechanicTemplateImmunity(1 << (MECHANIC_FREEZE - 1));
    }

    // Frost Nova and Arcane Explosion are self-centred point blank areas of effect. What decides
    // whether they are worth a global cooldown is what stands inside the radius, not the nuke target
    // the mage happens to be facing on the other side of the field. The bot's own attacker list is
    // scanned first so the melee that is actually training the mage always counts.
    Unit* FindPointBlankEnemy(PlayerbotAI* botAI, Player* bot, float radius, bool freezableOnly)
    {
        if (!botAI || !bot)
            return nullptr;

        Unit* found = nullptr;
        std::unordered_set<ObjectGuid> checked;
        auto consider = [&](ObjectGuid const& guid)
        {
            if (found || !guid || !checked.insert(guid).second)
                return;

            Unit* unit = botAI->GetUnit(guid);
            if (!unit || !unit->IsAlive() || !unit->IsInWorld() || unit->GetMapId() != bot->GetMapId())
                return;

            if (bot->IsFriendlyTo(unit) || !bot->IsValidAttackTarget(unit))
                return;

            if (freezableOnly && !CanBeFrozen(unit))
                return;

            if (bot->GetExactDist2d(unit) <= radius)
                found = unit;
        };

        for (ObjectGuid const& guid : botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get())
            consider(guid);

        for (ObjectGuid const& guid : botAI->GetAiObjectContext()->GetValue<GuidVector>("possible targets")->Get())
            consider(guid);

        return found;
    }
}

Value<Unit*>* CastPolymorphAction::GetTargetValue() { return context->GetValue<Unit*>("cc target", getName()); }

Value<Unit*>* CastSpellstealAction::GetTargetValue()
{
    return context->GetValue<Unit*>("offensive dispel target", std::to_string(DISPEL_MAGIC));
}

bool CastPolymorphAction::isPossible()
{
    if (ai::cc::GetActivePolymorphTarget(botAI))
        return false;

    Unit* target = GetTarget();
    return ai::cc::IsGoodPolymorphTarget(botAI, target) && CastCrowdControlSpellAction::isPossible();
}

bool CastPolymorphAction::isUseful()
{
    if (ai::cc::GetActivePolymorphTarget(botAI))
        return false;

    Unit* target = GetTarget();
    return ai::cc::IsGoodPolymorphTarget(botAI, target) && CastCrowdControlSpellAction::isUseful();
}

bool UseManaSapphireAction::isUseful()
{
    Player* bot = botAI->GetBot();
    return AI_VALUE2(bool, "combat", "self target") && bot->GetItemCount(33312, false) > 0;  // Mana Sapphire
}

bool UseManaEmeraldAction::isUseful()
{
    Player* bot = botAI->GetBot();
    return AI_VALUE2(bool, "combat", "self target") && bot->GetItemCount(22044, false) > 0;  // Mana Emerald
}

bool UseManaRubyAction::isUseful()
{
    Player* bot = botAI->GetBot();
    return AI_VALUE2(bool, "combat", "self target") && bot->GetItemCount(8008, false) > 0;  // Mana Ruby
}

bool UseManaCitrineAction::isUseful()
{
    Player* bot = botAI->GetBot();
    return AI_VALUE2(bool, "combat", "self target") && bot->GetItemCount(8007, false) > 0;  // Mana Citrine
}

bool UseManaJadeAction::isUseful()
{
    Player* bot = botAI->GetBot();
    return AI_VALUE2(bool, "combat", "self target") && bot->GetItemCount(5513, false) > 0;  // Mana Jade
}

bool UseManaAgateAction::isUseful()
{
    Player* bot = botAI->GetBot();
    return AI_VALUE2(bool, "combat", "self target") && bot->GetItemCount(5514, false) > 0;  // Mana Agate
}

bool CastFrostNovaAction::isUseful()
{
    // Judging the nova by the current target made it unusable exactly when it matters: under melee
    // pressure the mage's current target is the assist target, not the attacker standing on it.
    if (!FindPointBlankEnemy(botAI, bot, PointBlankAoeRadius, true))
        return false;

    return CastSpellAction::isUseful();
}

bool CastArcaneExplosionAction::isUseful()
{
    if (!FindPointBlankEnemy(botAI, bot, PointBlankAoeRadius, false))
        return false;

    return CastSpellAction::isUseful();
}

bool CastConeOfColdAction::isUseful()
{
    bool facingTarget = AI_VALUE2(bool, "facing", "current target");
    bool targetClose = ServerFacade::instance().IsDistanceLessOrEqualThan(AI_VALUE2(float, "distance", GetTargetName()), 10.f);
    return facingTarget && targetClose && CastSpellAction::isUseful();
}

bool CastDragonsBreathAction::isUseful()
{
    Unit* target = AI_VALUE(Unit*, "current target");
    if (!target)
        return false;
    bool facingTarget = AI_VALUE2(bool, "facing", "current target");
    bool targetClose = bot->IsWithinCombatRange(target, 10.0f);
    return facingTarget && targetClose && CastSpellAction::isUseful();
}

bool CastBlastWaveAction::isUseful()
{
    Unit* target = AI_VALUE(Unit*, "current target");
    if (!target)
        return false;
    bool targetClose = bot->IsWithinCombatRange(target, 10.0f);
    return targetClose && CastSpellAction::isUseful();
}

Unit* CastFocusMagicOnPartyAction::GetTarget()
{
    Group* group = bot->GetGroup();
    if (!group)
        return nullptr;

    Unit* casterDps = nullptr;
    Unit* healer = nullptr;
    Unit* target = nullptr;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || member == bot || !member->IsAlive())
            continue;

        if (member->GetMap() != bot->GetMap() || bot->GetDistance(member) > sPlayerbotAIConfig.spellDistance)
            continue;

        if (member->HasAura(54646))
            continue;

        if (member->getClass() == CLASS_MAGE)
            return member;

        if (!casterDps && botAI->IsCaster(member) && botAI->IsDps(member))
            casterDps = member;

        if (!healer && botAI->IsHeal(member))
            healer = member;

        if (!target)
            target = member;
    }

    if (casterDps)
        return casterDps;

    if (healer)
        return healer;

    return target;
}

bool CastBlinkBackAction::Execute(Event event)
{
    // Blink has to move the mage away from whatever is on it. In PvP that is the melee attacker,
    // which is rarely the same unit as the current (assist) target.
    Unit* target = ai::pvp::GetClosestPvpMeleeAttacker(botAI, PointBlankAoeRadius);
    if (!target)
        target = AI_VALUE(Unit*, "current target");

    if (!target)
        return false;
    // can cast spell check passed in isUseful()
    bot->SetOrientation(bot->GetAngle(target) + M_PI);
    return CastSpellAction::Execute(event);
}
