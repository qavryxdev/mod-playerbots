/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "EnemyHealerTargetValue.h"

#include "Battleground.h"
#include "ObjectGuid.h"
#include "Playerbots.h"
#include "PositionValue.h"
#include "PvpTactics.h"
#include "ServerFacade.h"
#include "Spell.h"

#include <limits>
#include <unordered_set>

namespace
{
    SpellInfo const* GetHelpfulCastingSpell(Unit* unit)
    {
        if (!unit)
            return nullptr;

        Spell* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (spell && spell->m_spellInfo && (spell->m_spellInfo->IsPositive() ||
            PlayerbotAI::IsHealingSpell(spell->m_spellInfo->SpellFamilyName, spell->m_spellInfo->SpellFamilyFlags)))
            return spell->m_spellInfo;

        spell = unit->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
        if (spell && spell->m_spellInfo && (spell->m_spellInfo->IsPositive() ||
            PlayerbotAI::IsHealingSpell(spell->m_spellInfo->SpellFamilyName, spell->m_spellInfo->SpellFamilyFlags)))
            return spell->m_spellInfo;

        return nullptr;
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

    bool CanScanPvpHealerDuringAVObjective(PlayerbotAI* botAI, Unit* target, bool threatTarget)
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

Unit* EnemyHealerTargetValue::Calculate()
{
    std::string const spell = qualifier;

    Unit* result = nullptr;
    int32 bestScore = std::numeric_limits<int32>::min();
    std::unordered_set<ObjectGuid> checked;

    auto checkGuid = [&](ObjectGuid const& guid, bool threatTarget)
    {
        if (!guid || checked.find(guid) != checked.end())
            return;

        checked.insert(guid);
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !unit->IsAlive())
            return;

        if (ServerFacade::instance().GetDistance2d(bot, unit) > botAI->GetRange("spell"))
            return;

        if (!ai::pvp::CanEngageDuringBattlegroundCapture(botAI, unit))
            return;

        if (!CanScanPvpHealerDuringAVObjective(botAI, unit, threatTarget))
            return;

        if (!botAI->IsInterruptableSpellCasting(unit, spell))
            return;

        SpellInfo const* spellInfo = GetHelpfulCastingSpell(unit);
        if (!spellInfo)
            return;

        int32 score = 0;
        if (unit->IsPlayer())
        {
            score += 250;
            if (botAI->IsHeal(unit->ToPlayer()))
                score += 350;
        }

        if (unit == botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get())
            score += 80;

        score += spellInfo->IsChanneled() ? 120 : 60;
        score -= static_cast<int32>(ServerFacade::instance().GetDistance2d(bot, unit));

        if (!result || score > bestScore)
        {
            result = unit;
            bestScore = score;
        }
    };

    GuidVector attackers = botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get();
    for (ObjectGuid const guid : attackers)
        checkGuid(guid, true);

    if (bot->InBattleground() || bot->InArena() || bot->duel)
    {
        GuidVector possibleTargets = botAI->GetAiObjectContext()->GetValue<GuidVector>("possible targets")->Get();
        uint8 scanned = 0;
        for (ObjectGuid const guid : possibleTargets)
        {
            checkGuid(guid, false);
            if (++scanned >= 32)
                break;
        }
    }

    return result;
}
