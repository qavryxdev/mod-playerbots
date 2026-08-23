/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "AttackersValue.h"

#include "Battleground.h"
#include "BattlegroundAV.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Playerbots.h"
#include "PositionValue.h"
#include "ReputationMgr.h"
#include "ServerFacade.h"

#include <algorithm>
#include <limits>
#include <utility>

static bool AllianceAVUnitIsInIcebloodTowerArea(Unit* unit)
{
    if (!unit)
        return false;

    float const x = unit->GetPositionX();
    float const y = unit->GetPositionY();
    return x <= -520.0f && x >= -630.0f && y <= -245.0f && y >= -345.0f;
}

static bool AllianceAVPositionNear(float x, float y, float targetX, float targetY, float radius)
{
    float const dx = x - targetX;
    float const dy = y - targetY;
    return dx * dx + dy * dy <= radius * radius;
}

static bool AllianceAVPositionIsIcebloodAssaultPerimeter(float x, float y)
{
    if (x < -675.0f || y > -335.0f || y < -475.0f)
        return false;

    return AllianceAVPositionNear(x, y, -617.858f, -400.654f, 96.0f) ||
           AllianceAVPositionNear(x, y, -590.000f, -354.000f, 32.0f) ||
           AllianceAVPositionNear(x, y, -644.000f, -430.000f, 42.0f);
}

static bool AllianceAVBotIsInIcebloodAssaultArea(Player* bot)
{
    if (!bot)
        return false;

    if (AllianceAVPositionIsIcebloodAssaultPerimeter(bot->GetPositionX(), bot->GetPositionY()))
        return true;

    return AllianceAVUnitIsInIcebloodTowerArea(bot);
}

static bool AllianceAVPositionIsIcebloodAssaultObjective(PositionInfo const& pos)
{
    // A rally is somewhere to wait, not an assault objective, and treating it as one would suppress
    // target selection for a bot that is standing around precisely because it has no objective.
    if (!pos.valueSet || pos.isRally())
        return false;

    if (pos.x <= -520.0f && pos.x >= -630.0f && pos.y <= -245.0f && pos.y >= -345.0f)
        return false;

    return pos.x <= -560.0f && pos.x >= -710.0f && pos.y <= -335.0f && pos.y >= -470.0f;
}

static bool AllianceAVBotHasIcebloodAssaultObjective(PlayerbotAI* botAI, Player* bot)
{
    if (!botAI || !bot)
        return false;

    if (!AllianceAVBotIsInIcebloodAssaultArea(bot))
        return false;

    PositionMap& positions = botAI->GetAiObjectContext()->GetValue<PositionMap&>("position")->Get();
    auto const objective = positions.find("bg objective");
    if (objective != positions.end() && AllianceAVPositionIsIcebloodAssaultObjective(objective->second))
        return true;

    return AllianceAVPositionIsIcebloodAssaultPerimeter(bot->GetPositionX(), bot->GetPositionY());
}

static bool AllianceAVShouldRejectIcebloodAssaultTarget(Unit* attacker, Player* bot, PlayerbotAI* botAI)
{
    if (!attacker || !bot || !botAI || bot->GetTeamId() != TEAM_ALLIANCE)
        return false;

    Battleground* bg = bot->GetBattleground();
    if (!bg)
        return false;

    BattlegroundTypeId bgType = bg->GetBgTypeID();
    if (bgType == BATTLEGROUND_RB)
        bgType = bg->GetBgTypeID(true);

    if (bgType != BATTLEGROUND_AV)
        return false;

    BattlegroundAV* av = static_cast<BattlegroundAV*>(bg);
    BG_AV_NodeInfo const& iceblood = av->GetAVNodeInfo(BG_AV_NODES_ICEBLOOD_GRAVE);
    if (iceblood.State != POINT_ASSAULTED || iceblood.OwnerId != TEAM_ALLIANCE)
        return false;

    if (!AllianceAVBotHasIcebloodAssaultObjective(botAI, bot))
        return false;

    if (AllianceAVUnitIsInIcebloodTowerArea(attacker))
        return true;

    return !AllianceAVPositionIsIcebloodAssaultPerimeter(attacker->GetPositionX(), attacker->GetPositionY());
}

GuidVector AttackersValue::Calculate()
{
    std::unordered_set<Unit*> targets;

    GuidVector result;
    if (!botAI->AllowActivity(ALL_ACTIVITY))
        return result;

    AddAttackersOf(bot, targets);

    if (Group* group = bot->GetGroup())
        AddAttackersOf(group, targets);

    RemoveNonThreating(targets);

    // prioritized target
    GuidVector prioritizedTargets = AI_VALUE(GuidVector, "prioritized targets");
    for (ObjectGuid target : prioritizedTargets)
    {
        Unit* unit = botAI->GetUnit(target);
        if (unit && IsValidTarget(unit, bot))
            targets.insert(unit);
    }
    if (Group* group = bot->GetGroup())
    {
        ObjectGuid skullGuid = group->GetTargetIcon(7);
        Unit* skullTarget = botAI->GetUnit(skullGuid);
        if (skullTarget && IsValidTarget(skullTarget, bot))
            targets.insert(skullTarget);
    }

    for (Unit* unit : targets)
        result.push_back(unit->GetGUID());

    if (bot->duel && bot->duel->Opponent)
        result.push_back(bot->duel->Opponent->GetGUID());

    // workaround for bots of same faction not fighting in arena
    if (bot->InArena())
    {
        GuidVector possibleTargets = AI_VALUE(GuidVector, "possible targets");
        for (ObjectGuid const guid : possibleTargets)
        {
            Unit* unit = botAI->GetUnit(guid);
            if (unit && unit->IsPlayer() && IsValidTarget(unit, bot))
                result.push_back(unit->GetGUID());
        }
    }

    // Everything above walks std::unordered_set / std::unordered_map, so the order this list came out
    // in was bucket order - and it is rebuilt from scratch every second, which reshuffles it. That
    // matters because the consumers break ties by "whichever one I saw first": the grind target is
    // literally attackers.front(), and the PvE target scorers keep the incumbent when two candidates
    // score the same. Two equally healthy mobs were therefore picked at random each second, and since
    // every change of target stops the bot dead (AttackAction::Attack clears its movement), the bot
    // stood still flipping between them. Sort by distance, GUID breaking the tie, so identical world
    // state always produces an identical list.
    std::vector<std::pair<float, ObjectGuid>> ordered;
    ordered.reserve(result.size());
    for (ObjectGuid const& guid : result)
    {
        Unit* unit = botAI->GetUnit(guid);
        ordered.emplace_back(unit ? bot->GetExactDist2d(unit) : std::numeric_limits<float>::max(), guid);
    }

    std::sort(ordered.begin(), ordered.end(),
              [](std::pair<float, ObjectGuid> const& left, std::pair<float, ObjectGuid> const& right)
              { return left.first != right.first ? left.first < right.first : left.second < right.second; });

    result.clear();
    for (std::pair<float, ObjectGuid> const& entry : ordered)
        if (result.empty() || result.back() != entry.second)  // the duel opponent can already be listed
            result.push_back(entry.second);

    return result;
}

void AttackersValue::AddAttackersOf(Group* group, std::unordered_set<Unit*>& targets)
{
    Group::MemberSlotList const& groupSlot = group->GetMemberSlots();
    for (Group::member_citerator itr = groupSlot.begin(); itr != groupSlot.end(); itr++)
    {
        Player* member = ObjectAccessor::FindPlayer(itr->guid);
        if (!member || !member->IsAlive() || member == bot || member->GetMapId() != bot->GetMapId() ||
            ServerFacade::instance().GetDistance2d(bot, member) > sPlayerbotAIConfig.sightDistance)
            continue;

        AddAttackersOf(member, targets);
    }
}

struct AddGuardiansHelper
{
    explicit AddGuardiansHelper(std::vector<Unit*>& units) : units(units) {}

    void operator()(Unit* target) const { units.push_back(target); }

    std::vector<Unit*>& units;
};

void AttackersValue::AddAttackersOf(Player* player, std::unordered_set<Unit*>& targets)
{
    if (!player || !player->IsInWorld() || player->IsBeingTeleported())
        return;

    for (auto const& [guid, ref] : player->GetThreatMgr().GetThreatenedByMeList())
    {
        Unit* attacker = ref->GetOwner();
        if (!attacker)
            continue;

        if (player->IsValidAttackTarget(attacker) &&
            player->GetDistance2d(attacker) < sPlayerbotAIConfig.sightDistance)
            targets.insert(attacker);
    }
}

void AttackersValue::RemoveNonThreating(std::unordered_set<Unit*>& targets)
{
    for (std::unordered_set<Unit*>::iterator tIter = targets.begin(); tIter != targets.end();)
    {
        Unit* unit = *tIter;
        if (bot->GetMapId() != unit->GetMapId() || !hasRealThreat(unit) || !IsValidTarget(unit, bot))
        {
            std::unordered_set<Unit*>::iterator tIter2 = tIter;
            ++tIter;
            targets.erase(tIter2);
        }
        else
            ++tIter;
    }
}

bool AttackersValue::hasRealThreat(Unit* attacker)
{
    return attacker && attacker->IsInWorld() && attacker->IsAlive() && !attacker->IsPolymorphed() &&
           // !attacker->isInRoots() &&
           !attacker->IsFriendlyTo(bot);
}

bool AttackersValue::IsPossibleTarget(Unit* attacker, Player* bot, float /*range*/)
{
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return false;

    // Basic check
    if (!attacker)
        return false;

    // bool inCannon = botAI->IsInVehicle(false, true);
    // bool enemy = botAI->GetAiObjectContext()->GetValue<Unit*>("enemy player target")->Get();

    // Validity checks
    if (!attacker->IsVisible() || !attacker->IsInWorld() || attacker->GetMapId() != bot->GetMapId())
        return false;

    if (attacker->isDead() || attacker->HasSpiritOfRedemptionAura())
        return false;

    // Flag checks
    if (attacker->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_NON_ATTACKABLE_2))
        return false;

    if (attacker->HasUnitFlag(UNIT_FLAG_IMMUNE_TO_PC) || attacker->HasUnitFlag(UNIT_FLAG_NOT_SELECTABLE))
        return false;

    // Relationship checks
    if (attacker->IsFriendlyTo(bot))
        return false;

    if (AllianceAVShouldRejectIcebloodAssaultTarget(attacker, bot, botAI))
        return false;

    // Critter exception
    if (attacker->GetCreatureType() == CREATURE_TYPE_CRITTER && !attacker->IsInCombat())
        return false;

    // Visibility check
    if (!bot->CanSeeOrDetect(attacker))
        return false;

    // PvP prohibition checks (skip for duels)
    if ((attacker->GetGUID().IsPlayer() || attacker->GetGUID().IsPet()) &&
        (!bot->duel || bot->duel->Opponent != attacker) &&
        (sPlayerbotAIConfig.IsPvpProhibited(attacker->GetZoneId(), attacker->GetAreaId()) ||
        sPlayerbotAIConfig.IsPvpProhibited(bot->GetZoneId(), bot->GetAreaId())))
    {
        // This will stop aggresive pets from starting an attack.
        // This will stop currently attacking pets from continuing their attack.
        // This will first require the bot to change from a combat strat. It will
        // not be reached if the bot only switches targets, including NPC targets.
        for (Unit::ControlSet::const_iterator itr = bot->m_Controlled.begin();
            itr != bot->m_Controlled.end(); ++itr)
        {
            Creature* creature = dynamic_cast<Creature*>(*itr);
            if (creature && creature->GetVictim() == attacker)
            {
                creature->AttackStop();
                if (CharmInfo* charmInfo = creature->GetCharmInfo())
                    charmInfo->SetIsCommandAttack(false);
            }
        }

        return false;
    }

    // Unflagged player check
    if (attacker->IsPlayer() && !attacker->IsPvP() && !attacker->IsFFAPvP() &&
        (!bot->duel || bot->duel->Opponent != attacker))
        return false;

    // Creature-specific checks
    Creature* c = attacker->ToCreature();
    if (c)
    {
        if (c->IsInEvadeMode())
            return false;

        bool leaderHasThreat = false;
        if (bot->GetGroup() && botAI->GetMaster())
            leaderHasThreat = attacker->GetThreatMgr().GetThreat(botAI->GetMaster());

        bool isMemberBotGroup = false;
        if (bot->GetGroup() && botAI->GetMaster())
        {
            PlayerbotAI* masterBotAI = GET_PLAYERBOT_AI(botAI->GetMaster());
            if (masterBotAI && !masterBotAI->IsRealPlayer())
                isMemberBotGroup = true;
        }

        bool canAttack = (!isMemberBotGroup && botAI->HasStrategy("attack tagged", BOT_STATE_NON_COMBAT)) ||
            leaderHasThreat ||
            (!c->hasLootRecipient() &&
                (!c->GetVictim() ||
                    (c->GetVictim() &&
                        ((!c->GetVictim()->IsPlayer() || bot->IsInSameGroupWith(c->GetVictim()->ToPlayer())) ||
                            (botAI->GetMaster() && c->GetVictim() == botAI->GetMaster()))))) ||
            c->isTappedBy(bot);

        if (!canAttack)
            return false;
    }

    return true;
}

bool AttackersValue::IsValidTarget(Unit* attacker, Player* bot)
{
    return IsPossibleTarget(attacker, bot) && bot->IsWithinLOSInMap(attacker);
}

bool PossibleAddsValue::Calculate()
{
    GuidVector possible = botAI->GetAiObjectContext()->GetValue<GuidVector>("possible targets no los")->Get();
    GuidVector attackers = botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get();

    for (ObjectGuid const guid : possible)
    {
        if (find(attackers.begin(), attackers.end(), guid) != attackers.end())
            continue;
        Unit* add = botAI->GetUnit(guid);
        if (!add || !add->IsInWorld() || add->IsDuringRemoveFromWorld())
            continue;

        if (!add->GetTarget() && !add->GetThreatMgr().GetLastVictim() && add->IsHostileTo(bot))
        {
            for (ObjectGuid const attackerGUID : attackers)
            {
                Unit* attacker = botAI->GetUnit(attackerGUID);
                if (!attacker)
                    continue;

                float dist = ServerFacade::instance().GetDistance2d(attacker, add);
                if (ServerFacade::instance().IsDistanceLessOrEqualThan(dist, sPlayerbotAIConfig.aoeRadius * 1.5f))
                    continue;

                if (ServerFacade::instance().IsDistanceLessOrEqualThan(dist, sPlayerbotAIConfig.aggroDistance))
                    return true;
            }
        }
    }

    return false;
}
