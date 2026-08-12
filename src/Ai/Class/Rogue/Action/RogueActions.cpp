/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "RogueActions.h"

#include "AiFactory.h"
#include "Event.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "PvpTactics.h"

bool CastStealthAction::isUseful()
{
    Unit* target = AI_VALUE(Unit*, "current target");
    if (target && bot->GetDistance(target) >= sPlayerbotAIConfig.spellDistance)
        return false;
    return true;
}

bool CastStealthAction::isPossible()
{
    // do not use with WSG flag or EYE flag
    return !botAI->HasAura(23333, bot) && !botAI->HasAura(23335, bot) && !botAI->HasAura(34976, bot);
}

bool UnstealthAction::Execute(Event /*event*/)
{
    botAI->RemoveAura("stealth");
    // botAI->ChangeStrategy("+dps,-stealthed", BOT_STATE_COMBAT);

    return true;
}

bool CheckStealthAction::Execute(Event /*event*/)
{
    // Combat rogues run "dps", Assassination and Subtlety run "melee". Toggling the wrong one would
    // leave the bot without a rotation on unstealth, or bolt a second spec's rotation onto its own.
    uint8 const tab = AiFactory::GetPlayerSpecTab(bot);
    std::string const rotation =
        (tab == ROGUE_TAB_ASSASSINATION || tab == ROGUE_TAB_SUBTLETY) ? "melee" : "dps";

    if (botAI->HasAura("stealth", bot))
    {
        botAI->ChangeStrategy("-" + rotation + ",+stealthed", BOT_STATE_COMBAT);
    }
    else
    {
        botAI->ChangeStrategy("+" + rotation + ",-stealthed", BOT_STATE_COMBAT);
    }

    return true;
}

bool CastVanishAction::isUseful()
{
    // do not use with WSG flag or EYE flag
    if (botAI->HasAura(23333, bot) || botAI->HasAura(23335, bot) || botAI->HasAura(34976, bot))
        return false;

    return !ai::pvp::IsPvpContext(bot) || ai::pvp::ShouldUseDefensiveCooldown(botAI, false);
}

bool CastEnvenomAction::isUseful()
{
    // The base check knows whether the bot is in melee range of a valid target; the energy floor is
    // only there to stop the finisher from starving the next builder.
    return CastMeleeSpellAction::isUseful() && AI_VALUE2(uint8, "energy", "self target") >= 35;
}

bool CastTricksOfTheTradeOnMainTankAction::isUseful()
{
    return CastSpellAction::isUseful() && AI_VALUE2(float, "distance", GetTargetName()) < 20.0f;
}

bool UseDeadlyPoisonAction::Execute(Event /*event*/)
{
    std::vector<std::string> poison_suffixs = {" IX", " VIII", " VII", " VI", " V", " IV", " III", " II", ""};
    std::vector<Item*> items;
    std::string poison_name;
    for (std::string& suffix : poison_suffixs)
    {
        poison_name = "Deadly Poison" + suffix;
        items = AI_VALUE2(std::vector<Item*>, "inventory items", poison_name);
        if (!items.empty())
        {
            break;
        }
    }
    if (items.empty())
    {
        return false;
    }
    Item* const itemForSpell = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
    return UseItem(*items.begin(), ObjectGuid::Empty, itemForSpell);
    // return UseItemAuto(*items.begin());
}

bool UseDeadlyPoisonAction::isPossible()
{
    std::vector<std::string> poison_suffixs = {" IX", " VIII", " VII", " VI", " V", " IV", " III", " II", ""};
    std::vector<Item*> items;
    std::string poison_name;
    for (std::string& suffix : poison_suffixs)
    {
        poison_name = "Deadly Poison" + suffix;
        items = AI_VALUE2(std::vector<Item*>, "inventory items", poison_name);
        if (!items.empty())
        {
            break;
        }
    }
    return !items.empty();
}

bool UseInstantPoisonAction::Execute(Event /*event*/)
{
    std::vector<std::string> poison_suffixs = {" IX", " VIII", " VII", " VI", " V", " IV", " III", " II", ""};
    std::vector<Item*> items;
    std::string poison_name;
    for (std::string& suffix : poison_suffixs)
    {
        poison_name = "Instant Poison" + suffix;
        items = AI_VALUE2(std::vector<Item*>, "inventory items", poison_name);
        if (!items.empty())
        {
            break;
        }
    }
    if (items.empty())
    {
        return false;
    }
    Item* const itemForSpell = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
    return UseItem(*items.begin(), ObjectGuid::Empty, itemForSpell);
}

bool UseInstantPoisonAction::isPossible()
{
    std::vector<std::string> poison_suffixs = {" IX", " VIII", " VII", " VI", " V", " IV", " III", " II", ""};
    std::vector<Item*> items;
    std::string poison_name;
    for (std::string& suffix : poison_suffixs)
    {
        poison_name = "Instant Poison" + suffix;
        items = AI_VALUE2(std::vector<Item*>, "inventory items", poison_name);
        if (!items.empty())
        {
            break;
        }
    }
    return !items.empty();
}

bool UseInstantPoisonOffHandAction::Execute(Event /*event*/)
{
    std::vector<std::string> poison_suffixs = {" IX", " VIII", " VII", " VI", " V", " IV", " III", " II", ""};
    std::vector<Item*> items;
    std::string poison_name;
    for (std::string& suffix : poison_suffixs)
    {
        poison_name = "Instant Poison" + suffix;
        items = AI_VALUE2(std::vector<Item*>, "inventory items", poison_name);
        if (!items.empty())
        {
            break;
        }
    }
    if (items.empty())
    {
        return false;
    }
    Item* const itemForSpell = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
    return UseItem(*items.begin(), ObjectGuid::Empty, itemForSpell);
}

bool UseInstantPoisonOffHandAction::isPossible()
{
    std::vector<std::string> poison_suffixs = {" IX", " VIII", " VII", " VI", " V", " IV", " III", " II", ""};
    std::vector<Item*> items;
    std::string poison_name;
    for (std::string& suffix : poison_suffixs)
    {
        poison_name = "Instant Poison" + suffix;
        items = AI_VALUE2(std::vector<Item*>, "inventory items", poison_name);
        if (!items.empty())
        {
            break;
        }
    }
    return !items.empty();
}
