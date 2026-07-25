/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DruidTriggers.h"
#include "Player.h"
#include "Playerbots.h"
#include "PvpTactics.h"

namespace
{
    bool IsPvpDruidDotTarget(PlayerbotAI* botAI, Player* bot, Unit* target)
    {
        Player* enemy = target ? target->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;
        return bot && target && target->IsAlive() && target->IsInWorld() &&
               target->GetMapId() == bot->GetMapId() && ai::pvp::IsPvpContext(bot) &&
               enemy && botAI->IsOpposing(enemy) &&
               ai::pvp::CanEngageDuringBattlegroundCapture(botAI, target) &&
               ai::pvp::CanDamageTarget(botAI, target, false);
    }
}

bool MarkOfTheWildOnPartyTrigger::IsActive()
{
    return BuffOnPartyTrigger::IsActive() && !botAI->HasAura("gift of the wild", GetTarget());
}

bool MarkOfTheWildTrigger::IsActive()
{
    return BuffTrigger::IsActive() && !botAI->HasAura("gift of the wild", GetTarget());
}

bool ThornsOnPartyTrigger::IsActive()
{
    return BuffOnPartyTrigger::IsActive() && !botAI->HasAura("thorns", GetTarget());
}

bool EntanglingRootsKiteTrigger::IsActive()
{
    return DebuffTrigger::IsActive() && AI_VALUE(uint8, "attacker count") < 3 && !GetTarget()->GetPower(POWER_MANA);
}

bool ThornsTrigger::IsActive() { return BuffTrigger::IsActive() && !botAI->HasAura("thorns", GetTarget()); }

bool PvpInsectSwarmTrigger::IsActive()
{
    return IsPvpDruidDotTarget(botAI, bot, GetTarget()) && BuffTrigger::IsActive();
}

bool PvpMoonfireTrigger::IsActive()
{
    return IsPvpDruidDotTarget(botAI, bot, GetTarget()) && BuffTrigger::IsActive();
}

bool BearFormTrigger::IsActive() { return !botAI->HasAnyAuraOf(bot, "bear form", "dire bear form", nullptr); }

bool TreeFormTrigger::IsActive() { return !botAI->HasAura(33891, bot); }

bool CatFormTrigger::IsActive() { return !botAI->HasAura("cat form", bot); }

const std::set<uint32> HurricaneChannelCheckTrigger::HURRICANE_SPELL_IDS = {
    16914,  // Hurricane Rank 1
    17401,  // Hurricane Rank 2
    17402,  // Hurricane Rank 3
    27012,  // Hurricane Rank 4
    48467   // Hurricane Rank 5
};

bool HurricaneChannelCheckTrigger::IsActive()
{
    Player* bot = botAI->GetBot();

    // Check if the bot is channeling a spell
    if (Spell* spell = bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
    {
        // Only trigger if the spell being channeled is Hurricane
        if (HURRICANE_SPELL_IDS.count(spell->m_spellInfo->Id))
        {
            uint8 attackerCount = AI_VALUE(uint8, "attacker count");
            return attackerCount < minEnemies;
        }
    }

    // Not channeling Hurricane
    return false;
}
