/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "PriestActions.h"

#include "PvpTactics.h"

#include "Event.h"
#include "Playerbots.h"

bool CastRemoveShadowformAction::Execute(Event /*event*/)
{
    botAI->RemoveAura("shadowform");
    return true;
}

bool CastRemoveShadowformAction::isUseful() { return botAI->HasAura("shadowform", AI_VALUE(Unit*, "self target")); }

Unit* CastPowerWordShieldOnAlmostFullHealthBelowAction::GetTarget()
{
    Group* group = bot->GetGroup();
    for (GroupReference* gref = group->GetFirstMember(); gref; gref = gref->next())
    {
        Player* player = gref->GetSource();
        if (!player)
            continue;
        if (player->isDead())
        {
            continue;
        }
        if (player->GetHealthPct() > sPlayerbotAIConfig.almostFullHealth)
        {
            continue;
        }
        if (player->GetDistance2d(bot) > sPlayerbotAIConfig.spellDistance)
        {
            continue;
        }
        if (botAI->HasAnyAuraOf(player, "weakened soul", "power word: shield", nullptr))
        {
            continue;
        }
        return player;
    }
    return nullptr;
}

bool CastPowerWordShieldOnAlmostFullHealthBelowAction::isUseful()
{
    Group* group = bot->GetGroup();
    for (GroupReference* gref = group->GetFirstMember(); gref; gref = gref->next())
    {
        Player* player = gref->GetSource();
        if (!player)
            continue;
        if (player->isDead())
        {
            continue;
        }
        if (player->GetHealthPct() > sPlayerbotAIConfig.almostFullHealth)
        {
            continue;
        }
        if (player->GetDistance2d(bot) > sPlayerbotAIConfig.spellDistance)
        {
            continue;
        }
        if (botAI->HasAnyAuraOf(player, "weakened soul", "power word: shield", nullptr))
        {
            continue;
        }
        // Chain to the base so the shared pvp gate is not bypassed by this bespoke scan.
        return HealPartyMemberAction::isUseful();
    }
    return false;
}

Unit* CastPowerWordShieldOnNotFullAction::GetTarget()
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
        if (botAI->HasAnyAuraOf(player, "weakened soul", "power word: shield", nullptr))
        {
            continue;
        }
        calc.probe(player->GetHealthPct(), player);
    }
    return (Unit*)calc.param;
}

bool CastPowerWordShieldOnNotFullAction::isUseful()
{
    // Chain to the base so the shared pvp gate is not bypassed by this bespoke scan.
    return GetTarget() && HealPartyMemberAction::isUseful();
}

bool CastManaBurnAction::isUseful()
{
    Unit* target = GetTarget();
    if (!target || AI_VALUE2(uint8, "mana", "current target") < 20)
        return false;

    if (ai::pvp::IsPvpContext(botAI, target) && !ai::pvp::GetTargetProfile(botAI, target).healer)
        return false;

    return CastSpellAction::isUseful();
}
