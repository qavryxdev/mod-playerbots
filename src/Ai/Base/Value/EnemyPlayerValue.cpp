/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "EnemyPlayerValue.h"

#include "AttackersValue.h"
#include "CombatManager.h"
#include "BattlegroundEY.h"
#include "BattlegroundWS.h"
#include "Playerbots.h"
#include "PvpTactics.h"
#include "ServerFacade.h"
#include "Vehicle.h"

bool NearestEnemyPlayersValue::AcceptUnit(Unit* unit)
{
    // Apply parent's filtering first (includes level difference checks)
    if (!PossibleTargetsValue::AcceptUnit(unit))
        return false;

    bool inCannon = botAI->IsInVehicle(false, true);
    Player* enemy = dynamic_cast<Player*>(unit);
    if (enemy && botAI->IsOpposing(enemy) && enemy->IsPvP() &&
        !sPlayerbotAIConfig.IsPvpProhibited(enemy->GetZoneId(), enemy->GetAreaId()) &&
        !enemy->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_NON_ATTACKABLE_2) &&
        ((inCannon || !enemy->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE))) &&
        /*!enemy->HasStealthAura() && !enemy->HasInvisibilityAura()*/ enemy->CanSeeOrDetect(bot) &&
        !(enemy->HasSpiritOfRedemptionAura()))
    {
        // If with master, only attack if master is PvP flagged
        Player* master = botAI->GetMaster();
        if (master && !master->IsPvP() && !master->IsFFAPvP())
            return false;

        return true;
    }

    return false;
}

Unit* EnemyPlayerValue::Calculate()
{
    bool controllingCannon = false;
    bool controllingVehicle = false;
    if (Vehicle* vehicle = bot->GetVehicle())
    {
        VehicleSeatEntry const* seat = vehicle->GetSeatForPassenger(bot);
        if (!seat || !seat->CanControl())  // not in control of vehicle so cant attack anyone
            return nullptr;
        VehicleEntry const* vi = vehicle->GetVehicleInfo();
        if (vi && vi->m_flags & VEHICLE_FLAG_FIXED_POSITION)
            controllingCannon = true;
        else
            controllingVehicle = true;
    }

    Unit* pVictim = bot->GetVictim();
    float const maxAggroDistance = GetMaxAttackDistance();
    GuidVector players = AI_VALUE(GuidVector, "nearest enemy players");

    auto isEnemyFlagCarrier = [&](Player const* pTarget)
    {
        // Eye of the Storm has a single neutral flag, so either faction carrying it is a target.
        if (pTarget->HasAura(BG_EY_NETHERSTORM_FLAG_SPELL))
            return true;

        if (bot->GetTeamId() == TEAM_HORDE)
            return pTarget->HasAura(BG_WS_SPELL_WARSONG_FLAG);

        return pTarget->HasAura(BG_WS_SPELL_SILVERWING_FLAG);
    };

    auto isUsableEnemyPlayer = [&](Player* pTarget, float range)
    {
        if (!pTarget || pTarget == bot || !pTarget->CanSeeOrDetect(bot) || !bot->IsWithinDist(pTarget, range))
            return false;

        if (!AttackersValue::IsPossibleTarget(pTarget, bot))
            return false;

        if (!ai::pvp::CanEngageDuringBattlegroundCapture(botAI, pTarget))
            return false;

        if (!bot->IsWithinLOSInMap(pTarget))
            return false;

        return controllingCannon || (fabs(bot->GetPositionZ() - pTarget->GetPositionZ()) < 30.0f);
    };

    for (auto const& gTarget : players)
    {
        Unit* pUnit = botAI->GetUnit(gTarget);
        Player* pTarget = pUnit ? dynamic_cast<Player*>(pUnit) : nullptr;
        if (pTarget && isEnemyFlagCarrier(pTarget) && isUsableEnemyPlayer(pTarget, maxAggroDistance * 2.0f))
            return pTarget;
    }

    if (Player* victimPlayer = pVictim ? pVictim->ToPlayer() : nullptr)
    {
        if (isUsableEnemyPlayer(victimPlayer, maxAggroDistance + 10.0f) &&
            ai::pvp::IsObjectiveRelevantEnemy(botAI, victimPlayer, true))
            return victimPlayer;
    }

    // 1. Check units we are currently in PvP combat with.
    std::vector<Unit*> targets;
    for (auto const& [guid, combatRef] : bot->GetCombatManager().GetPvPCombatRefs())
    {
        Unit* pTarget = combatRef->GetOther(bot);
        if (!pTarget || pTarget == pVictim || !pTarget->IsPlayer() || !pTarget->CanSeeOrDetect(bot) ||
            !bot->IsWithinDist(pTarget, VISIBILITY_DISTANCE_NORMAL) ||
            !AttackersValue::IsPossibleTarget(pTarget, bot))
            continue;

        if (!ai::pvp::CanEngageDuringBattlegroundCapture(botAI, pTarget))
            continue;

        if ((bot->GetTeamId() == TEAM_HORDE && pTarget->HasAura(23333)) ||
            (bot->GetTeamId() == TEAM_ALLIANCE && pTarget->HasAura(23335)))
            return pTarget;

        targets.push_back(pTarget);
    }

    if (!targets.empty())
    {
        std::sort(targets.begin(), targets.end(),
                  [&](Unit const* pUnit1, Unit const* pUnit2)
                  { return bot->GetDistance(pUnit1) < bot->GetDistance(pUnit2); });

        return *targets.begin();
    }

    // 2. Find enemy player in range.

    for (auto const& gTarget : players)
    {
        Unit* pUnit = botAI->GetUnit(gTarget);
        if (!pUnit)
            continue;

        Player* pTarget = dynamic_cast<Player*>(pUnit);
        if (!pTarget)
            continue;

        if (pTarget == pVictim)
            continue;

        if (!AttackersValue::IsPossibleTarget(pTarget, bot))
            continue;

        if (!ai::pvp::CanEngageDuringBattlegroundCapture(botAI, pTarget))
            continue;

        if (bot->GetTeamId() == TEAM_HORDE)
        {
            if (pTarget->HasAura(23333))
                return pTarget;
        }
        else
        {
            if (pTarget->HasAura(23335))
                return pTarget;
        }

        // Aggro weak enemies from further away.
        // If controlling mobile vehicle only agro close enemies (otherwise will never reach objective)
        uint32 const aggroDistance = controllingVehicle                                               ? 5.0f
                                     : (controllingCannon || bot->GetHealth() > pTarget->GetHealth()) ? maxAggroDistance
                                                                                                      : 20.0f;
        if (!bot->IsWithinDist(pTarget, aggroDistance))
            continue;

        if (bot->IsWithinLOSInMap(pTarget) &&
            (controllingCannon || (fabs(bot->GetPositionZ() - pTarget->GetPositionZ()) < 30.0f)) &&
            ai::pvp::IsObjectiveRelevantEnemy(botAI, pTarget, false, 65.0f, 45.0f))
            return pTarget;
    }

    // 3. Check party attackers.

    if (Group* pGroup = bot->GetGroup())
    {
        for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
        {
            if (Unit* pMember = itr->GetSource())
            {
                if (pMember == bot)
                    continue;

                if (ServerFacade::instance().GetDistance2d(bot, pMember) > 30.0f)
                    continue;

                if (Unit* pAttacker = pMember->getAttackerForHelper())
                    if (pAttacker->IsPlayer() && bot->IsWithinDist(pAttacker, maxAggroDistance * 2.0f) &&
                        bot->IsWithinLOSInMap(pAttacker) && pAttacker != pVictim && pAttacker->CanSeeOrDetect(bot) &&
                        AttackersValue::IsPossibleTarget(pAttacker, bot) &&
                        ai::pvp::CanEngageDuringBattlegroundCapture(botAI, pAttacker) &&
                        ai::pvp::IsObjectiveRelevantEnemy(botAI, pAttacker, true))
                        return pAttacker;
            }
        }
    }

    return nullptr;
}

float EnemyPlayerValue::GetMaxAttackDistance()
{
    if (!bot->GetBattleground())
        return 60.0f;

    Battleground* bg = bot->GetBattleground();
    if (!bg)
        return 40.0f;

    BattlegroundTypeId bgType = bg->GetBgTypeID();
    if (bgType == BATTLEGROUND_RB)
        bgType = bg->GetBgTypeID(true);

    if (bgType == BATTLEGROUND_IC)
    {
        if (botAI->IsInVehicle(false, true))
            return 120.0f;
    }

    return 40.0f;
}
