/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "EnemyHealerTargetValue.h"

#include "ObjectGuid.h"
#include "Playerbots.h"
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
}

Unit* EnemyHealerTargetValue::Calculate()
{
    std::string const spell = qualifier;

    Unit* result = nullptr;
    int32 bestScore = std::numeric_limits<int32>::min();
    std::unordered_set<ObjectGuid> checked;

    auto checkGuid = [&](ObjectGuid const& guid)
    {
        if (!guid || checked.find(guid) != checked.end())
            return;

        checked.insert(guid);
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !unit->IsAlive())
            return;

        if (ServerFacade::instance().GetDistance2d(bot, unit) > botAI->GetRange("spell"))
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
        checkGuid(guid);

    if (bot->InBattleground() || bot->InArena() || bot->duel)
    {
        GuidVector possibleTargets = botAI->GetAiObjectContext()->GetValue<GuidVector>("possible targets")->Get();
        uint8 scanned = 0;
        for (ObjectGuid const guid : possibleTargets)
        {
            checkGuid(guid);
            if (++scanned >= 32)
                break;
        }
    }

    return result;
}
