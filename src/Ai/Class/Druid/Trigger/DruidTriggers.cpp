/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DruidTriggers.h"
#include "Player.h"
#include "Playerbots.h"
#include "PvpTactics.h"

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
    return ai::pvp::ShouldUseDruidPvpDot(botAI, GetTarget(), "insect swarm") &&
           BuffTrigger::IsActive();
}

bool PvpMoonfireTrigger::IsActive()
{
    return ai::pvp::ShouldUseDruidPvpDot(botAI, GetTarget(), "moonfire") &&
           BuffTrigger::IsActive();
}

bool PvpFaerieFireTrigger::IsActive()
{
    return ai::pvp::ShouldUseDruidFaerieFire(botAI, GetTarget()) &&
           BuffTrigger::IsActive();
}

// Matches the 30s lockout spell_dru_eclipse arms when it applies the aura.
static constexpr time_t ECLIPSE_PROC_COOLDOWN_SECONDS = 30;

bool EclipseProcCooldownTrigger::IsActive()
{
    time_t now = time(nullptr);

    if (botAI->HasAura(aura, bot))
    {
        lastSeen = now;
        return true;
    }

    return lastSeen && (now - lastSeen) < ECLIPSE_PROC_COOLDOWN_SECONDS;
}

bool BearFormTrigger::IsActive() { return !botAI->HasAnyAuraOf(bot, "bear form", "dire bear form", nullptr); }

bool TreeFormTrigger::IsActive() { return !botAI->HasAura(33891, bot); }

bool CatFormTrigger::IsActive() { return !botAI->HasAura("cat form", bot); }

bool ProwlTrigger::IsActive()
{
    if (!botAI->HasStrategy("cat", BOT_STATE_COMBAT) || !ai::pvp::IsPvpContext(bot))
        return false;

    if (bot->IsMounted() || bot->IsInCombat() || botAI->HasAura("prowl", bot))
        return false;

    Unit* enemy = AI_VALUE(Unit*, "enemy player target");
    return enemy && bot->GetDistance(enemy) <= 40.0f;
}

bool StealthOpenerTrigger::IsActive()
{
    if (!botAI->HasAura("prowl", bot))
        return false;

    Unit* target = AI_VALUE(Unit*, "current target");
    return target && bot->IsWithinMeleeRange(target);
}

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
