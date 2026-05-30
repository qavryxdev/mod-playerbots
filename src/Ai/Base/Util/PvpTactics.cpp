/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "PvpTactics.h"

#include "AiObjectContext.h"
#include "Battleground.h"
#include "BattlegroundAB.h"
#include "BattlegroundAV.h"
#include "BattlegroundEY.h"
#include "BattlegroundIC.h"
#include "BattlegroundWS.h"
#include "GameObject.h"
#include "Group.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "ServerFacade.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "Timer.h"
#include "Unit.h"

#include <algorithm>
#include <iterator>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace
{
    constexpr uint32 InterruptReservationPruneMs = 3000;
    constexpr uint32 CaptureBannerSpellId = 21651;

    struct InterruptReservationKey
    {
        ObjectGuid target;
        uint32 spellId = 0;

        bool operator==(InterruptReservationKey const& other) const
        {
            return target == other.target && spellId == other.spellId;
        }
    };

    struct InterruptReservationKeyHash
    {
        std::size_t operator()(InterruptReservationKey const& key) const
        {
            return std::hash<ObjectGuid>()(key.target) ^ (std::hash<uint32>()(key.spellId) << 1);
        }
    };

    struct InterruptReservation
    {
        ObjectGuid bot;
        uint32 untilMs = 0;
    };

    std::mutex interruptReservationsMutex;
    std::unordered_map<InterruptReservationKey, InterruptReservation, InterruptReservationKeyHash> interruptReservations;

    SpellInfo const* GetCurrentCastingSpell(Unit* target)
    {
        if (!target)
            return nullptr;

        if (Spell* spell = target->GetCurrentSpell(CURRENT_GENERIC_SPELL))
            if (spell->m_spellInfo)
                return spell->m_spellInfo;

        if (Spell* spell = target->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
            if (spell->m_spellInfo)
                return spell->m_spellInfo;

        return nullptr;
    }

    bool IsCastingHelpfulSpell(Unit* target)
    {
        SpellInfo const* spellInfo = GetCurrentCastingSpell(target);
        return spellInfo && (spellInfo->IsPositive() ||
            PlayerbotAI::IsHealingSpell(spellInfo->SpellFamilyName, spellInfo->SpellFamilyFlags));
    }

    bool IsEnemyPlayerOrOwnedUnit(PlayerbotAI* botAI, Unit* target)
    {
        if (!botAI || !target)
            return false;

        Player* owner = target->ToPlayer();
        if (!owner)
            owner = target->GetCharmerOrOwnerPlayerOrPlayerItself();

        return owner && botAI->IsOpposing(owner);
    }

    bool IsImportantDispelAura(SpellInfo const* spellInfo, uint32 dispelType)
    {
        if (!spellInfo || !spellInfo->IsPositive() || spellInfo->Dispel != dispelType)
            return false;

        switch (spellInfo->Id)
        {
            case 642:    // Divine Shield
            case 1020:
            case 1022:   // Blessing/Hand of Protection
            case 5599:
            case 10278:
            case 1044:   // Blessing/Hand of Freedom
            case 6940:   // Blessing/Hand of Sacrifice
            case 2825:   // Bloodlust
            case 32182:  // Heroism
            case 17:     // Power Word: Shield ranks
            case 592:
            case 600:
            case 3747:
            case 6065:
            case 6066:
            case 10898:
            case 10899:
            case 10900:
            case 10901:
            case 25217:
            case 25218:
            case 48065:
            case 48066:
            case 11426:  // Ice Barrier ranks
            case 13031:
            case 13032:
            case 13033:
            case 27134:
            case 33405:
            case 43038:
            case 43039:
            case 974:    // Earth Shield ranks
            case 32593:
            case 32594:
            case 49283:
            case 49284:
                return true;
            default:
                break;
        }

        for (SpellEffectInfo const& effect : spellInfo->Effects)
        {
            if (effect.ApplyAuraName == SPELL_AURA_SCHOOL_IMMUNITY ||
                effect.ApplyAuraName == SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN ||
                effect.ApplyAuraName == SPELL_AURA_MOD_DAMAGE_PERCENT_DONE ||
                effect.ApplyAuraName == SPELL_AURA_MOD_INCREASE_SPEED ||
                effect.ApplyAuraName == SPELL_AURA_MOD_ROOT)
                return true;
        }

        return false;
    }

    uint32 CountUsefulDispelAuras(Unit* target, uint32 dispelType, bool* hasImportantAura)
    {
        if (hasImportantAura)
            *hasImportantAura = false;

        if (!target)
            return 0;

        uint32 count = 0;
        Unit::AuraApplicationMap const& auras = target->GetAppliedAuras();
        for (Unit::AuraApplicationMap::const_iterator itr = auras.begin(); itr != auras.end(); ++itr)
        {
            AuraApplication const* aurApp = itr->second;
            Aura const* aura = aurApp ? aurApp->GetBase() : nullptr;
            SpellInfo const* spellInfo = aura ? aura->GetSpellInfo() : nullptr;
            if (!aura || aura->IsRemoved() || !spellInfo || !spellInfo->IsPositive() || spellInfo->Dispel != dispelType)
                continue;

            ++count;
            if (hasImportantAura && IsImportantDispelAura(spellInfo, dispelType))
                *hasImportantAura = true;
        }

        return count;
    }

    void PruneInterruptReservations(uint32 now)
    {
        for (auto itr = interruptReservations.begin(); itr != interruptReservations.end();)
        {
            if (itr->second.untilMs + InterruptReservationPruneMs < now)
                itr = interruptReservations.erase(itr);
            else
                ++itr;
        }
    }

    bool IsSameMapUnit(Player* bot, Unit* target)
    {
        return bot && target && target->IsInWorld() && target->IsAlive() && target->GetMapId() == bot->GetMapId();
    }

    BattlegroundTypeId GetRealBattlegroundType(Battleground* bg)
    {
        if (!bg)
            return BATTLEGROUND_TYPE_NONE;

        BattlegroundTypeId bgType = bg->GetBgTypeID();
        return bgType == BATTLEGROUND_RB ? bg->GetBgTypeID(true) : bgType;
    }

    bool GetActiveBattlegroundObjective(PlayerbotAI* botAI, Player* bot, PositionInfo& objective)
    {
        if (!botAI || !bot || !bot->InBattleground())
            return false;

        PositionMap& positions = botAI->GetAiObjectContext()->GetValue<PositionMap&>("position")->Get();
        auto const itr = positions.find("bg objective");
        if (itr == positions.end() || !itr->second.valueSet)
            return false;

        objective = itr->second;
        return true;
    }

    bool IsPositionNear2d(PositionInfo const& position, float x, float y, float radius)
    {
        if (!position.valueSet)
            return false;

        float const dx = position.x - x;
        float const dy = position.y - y;
        return dx * dx + dy * dy <= radius * radius;
    }

    bool IsObjectiveNearBgObject(Battleground* bg, PositionInfo const& objective, uint32 objectType, float radius)
    {
        GameObject* go = bg ? bg->GetBGObject(objectType) : nullptr;
        return go && IsPositionNear2d(objective, go->GetPositionX(), go->GetPositionY(), radius);
    }

    bool IsObjectiveNearAnyBgObject(Battleground* bg, PositionInfo const& objective, uint32 const* objectTypes,
                                    size_t objectTypeCount, float radius)
    {
        for (size_t i = 0; i < objectTypeCount; ++i)
            if (IsObjectiveNearBgObject(bg, objective, objectTypes[i], radius))
                return true;

        return false;
    }

    bool IsObjectiveNearArathiNode(PositionInfo const& objective)
    {
        for (uint8 i = 0; i < BG_AB_DYNAMIC_NODES_COUNT; ++i)
            if (IsPositionNear2d(objective, BG_AB_NodePositions[i][0], BG_AB_NodePositions[i][1], 32.0f))
                return true;

        return false;
    }

    bool IsObjectiveNearEyeNode(PositionInfo const& objective)
    {
        for (uint8 i = 0; i < EY_POINTS_MAX; ++i)
            if (IsPositionNear2d(objective, BG_EY_TriggerPositions[i][0], BG_EY_TriggerPositions[i][1], 40.0f))
                return true;

        return false;
    }

    bool HasCaptureBannerCast(Player* bot)
    {
        if (!bot)
            return false;

        for (uint8 type = CURRENT_MELEE_SPELL; type <= CURRENT_CHANNELED_SPELL; ++type)
        {
            if (Spell* spell = bot->GetCurrentSpell(static_cast<CurrentSpellTypes>(type)))
                if (spell->m_spellInfo && spell->m_spellInfo->Id == CaptureBannerSpellId)
                    return true;
        }

        return false;
    }

    GameObject* GetCaptureBannerTarget(Unit* unit)
    {
        if (!unit)
            return nullptr;

        for (uint8 type = CURRENT_MELEE_SPELL; type <= CURRENT_CHANNELED_SPELL; ++type)
        {
            if (Spell* spell = unit->GetCurrentSpell(static_cast<CurrentSpellTypes>(type)))
            {
                if (spell->m_spellInfo && spell->m_spellInfo->Id == CaptureBannerSpellId)
                    return spell->m_targets.GetGOTarget();
            }
        }

        return nullptr;
    }

    bool IsCarryingBattlegroundFlag(Player* bot)
    {
        return bot && (bot->HasAura(BG_WS_SPELL_WARSONG_FLAG) || bot->HasAura(BG_WS_SPELL_SILVERWING_FLAG) ||
                       bot->HasAura(BG_EY_NETHERSTORM_FLAG_SPELL));
    }

    bool IsAlteracCaptureObjective(Battleground* bg, PositionInfo const& objective)
    {
        static uint32 const avCaptureObjects[] =
        {
            BG_AV_OBJECT_FLAG_A_FIRSTAID_STATION,
            BG_AV_OBJECT_FLAG_A_STORMPIKE_GRAVE,
            BG_AV_OBJECT_FLAG_A_STONEHEART_GRAVE,
            BG_AV_OBJECT_FLAG_A_SNOWFALL_GRAVE,
            BG_AV_OBJECT_FLAG_A_ICEBLOOD_GRAVE,
            BG_AV_OBJECT_FLAG_A_FROSTWOLF_GRAVE,
            BG_AV_OBJECT_FLAG_A_FROSTWOLF_HUT,
            BG_AV_OBJECT_FLAG_A_DUNBALDAR_SOUTH,
            BG_AV_OBJECT_FLAG_A_DUNBALDAR_NORTH,
            BG_AV_OBJECT_FLAG_A_ICEWING_BUNKER,
            BG_AV_OBJECT_FLAG_A_STONEHEART_BUNKER,
            BG_AV_OBJECT_FLAG_C_A_FIRSTAID_STATION,
            BG_AV_OBJECT_FLAG_C_A_STORMPIKE_GRAVE,
            BG_AV_OBJECT_FLAG_C_A_STONEHEART_GRAVE,
            BG_AV_OBJECT_FLAG_C_A_SNOWFALL_GRAVE,
            BG_AV_OBJECT_FLAG_C_A_ICEBLOOD_GRAVE,
            BG_AV_OBJECT_FLAG_C_A_FROSTWOLF_GRAVE,
            BG_AV_OBJECT_FLAG_C_A_FROSTWOLF_HUT,
            BG_AV_OBJECT_FLAG_C_A_ICEBLOOD_TOWER,
            BG_AV_OBJECT_FLAG_C_A_TOWER_POINT,
            BG_AV_OBJECT_FLAG_C_A_FROSTWOLF_ETOWER,
            BG_AV_OBJECT_FLAG_C_A_FROSTWOLF_WTOWER,
            BG_AV_OBJECT_FLAG_C_H_FIRSTAID_STATION,
            BG_AV_OBJECT_FLAG_C_H_STORMPIKE_GRAVE,
            BG_AV_OBJECT_FLAG_C_H_STONEHEART_GRAVE,
            BG_AV_OBJECT_FLAG_C_H_SNOWFALL_GRAVE,
            BG_AV_OBJECT_FLAG_C_H_ICEBLOOD_GRAVE,
            BG_AV_OBJECT_FLAG_C_H_FROSTWOLF_GRAVE,
            BG_AV_OBJECT_FLAG_C_H_FROSTWOLF_HUT,
            BG_AV_OBJECT_FLAG_C_H_DUNBALDAR_SOUTH,
            BG_AV_OBJECT_FLAG_C_H_DUNBALDAR_NORTH,
            BG_AV_OBJECT_FLAG_C_H_ICEWING_BUNKER,
            BG_AV_OBJECT_FLAG_C_H_STONEHEART_BUNKER,
            BG_AV_OBJECT_FLAG_H_FIRSTAID_STATION,
            BG_AV_OBJECT_FLAG_H_STORMPIKE_GRAVE,
            BG_AV_OBJECT_FLAG_H_STONEHEART_GRAVE,
            BG_AV_OBJECT_FLAG_H_SNOWFALL_GRAVE,
            BG_AV_OBJECT_FLAG_H_ICEBLOOD_GRAVE,
            BG_AV_OBJECT_FLAG_H_FROSTWOLF_GRAVE,
            BG_AV_OBJECT_FLAG_H_FROSTWOLF_HUT,
            BG_AV_OBJECT_FLAG_H_ICEBLOOD_TOWER,
            BG_AV_OBJECT_FLAG_H_TOWER_POINT,
            BG_AV_OBJECT_FLAG_H_FROSTWOLF_ETOWER,
            BG_AV_OBJECT_FLAG_H_FROSTWOLF_WTOWER,
            BG_AV_OBJECT_FLAG_N_SNOWFALL_GRAVE
        };

        return IsObjectiveNearAnyBgObject(bg, objective, avCaptureObjects, std::size(avCaptureObjects), 45.0f);
    }

    bool IsAlteracContestedGraveyardObjective(Battleground* bg, PositionInfo const& objective)
    {
        static uint32 const avContestedGraveyardObjects[] =
        {
            BG_AV_OBJECT_FLAG_C_A_FIRSTAID_STATION,
            BG_AV_OBJECT_FLAG_C_A_STORMPIKE_GRAVE,
            BG_AV_OBJECT_FLAG_C_A_STONEHEART_GRAVE,
            BG_AV_OBJECT_FLAG_C_A_SNOWFALL_GRAVE,
            BG_AV_OBJECT_FLAG_C_A_ICEBLOOD_GRAVE,
            BG_AV_OBJECT_FLAG_C_A_FROSTWOLF_GRAVE,
            BG_AV_OBJECT_FLAG_C_A_FROSTWOLF_HUT,
            BG_AV_OBJECT_FLAG_C_H_FIRSTAID_STATION,
            BG_AV_OBJECT_FLAG_C_H_STORMPIKE_GRAVE,
            BG_AV_OBJECT_FLAG_C_H_STONEHEART_GRAVE,
            BG_AV_OBJECT_FLAG_C_H_SNOWFALL_GRAVE,
            BG_AV_OBJECT_FLAG_C_H_ICEBLOOD_GRAVE,
            BG_AV_OBJECT_FLAG_C_H_FROSTWOLF_GRAVE,
            BG_AV_OBJECT_FLAG_C_H_FROSTWOLF_HUT
        };

        return IsObjectiveNearAnyBgObject(bg, objective, avContestedGraveyardObjects,
                                          std::size(avContestedGraveyardObjects), 45.0f);
    }

    bool IsIsleCaptureObjective(Battleground* bg, PositionInfo const& objective)
    {
        static uint32 const icCaptureObjects[] =
        {
            BG_IC_GO_ALLIANCE_BANNER,
            BG_IC_GO_HORDE_BANNER,
            BG_IC_GO_WORKSHOP_BANNER,
            BG_IC_GO_DOCKS_BANNER,
            BG_IC_GO_HANGAR_BANNER,
            BG_IC_GO_QUARRY_BANNER,
            BG_IC_GO_REFINERY_BANNER
        };

        return IsObjectiveNearAnyBgObject(bg, objective, icCaptureObjects, std::size(icCaptureObjects), 45.0f);
    }

    bool IsEyeCaptureObjective(Battleground* bg, PositionInfo const& objective)
    {
        static uint32 const eyCaptureObjects[] =
        {
            BG_EY_OBJECT_FLAG_NETHERSTORM,
            BG_EY_OBJECT_FLAG_FEL_REAVER,
            BG_EY_OBJECT_FLAG_BLOOD_ELF,
            BG_EY_OBJECT_FLAG_DRAENEI_RUINS,
            BG_EY_OBJECT_FLAG_MAGE_TOWER,
            BG_EY_OBJECT_TOWER_CAP_FEL_REAVER,
            BG_EY_OBJECT_TOWER_CAP_BLOOD_ELF,
            BG_EY_OBJECT_TOWER_CAP_DRAENEI_RUINS,
            BG_EY_OBJECT_TOWER_CAP_MAGE_TOWER
        };

        return IsObjectiveNearAnyBgObject(bg, objective, eyCaptureObjects, std::size(eyCaptureObjects), 38.0f) ||
               IsObjectiveNearEyeNode(objective);
    }

    bool IsWarsongCaptureObjective(Battleground* bg, PositionInfo const& objective)
    {
        static uint32 const wsCaptureObjects[] =
        {
            BG_WS_OBJECT_A_FLAG,
            BG_WS_OBJECT_H_FLAG
        };

        return IsObjectiveNearAnyBgObject(bg, objective, wsCaptureObjects, std::size(wsCaptureObjects), 45.0f);
    }
}

namespace ai::pvp
{
    bool IsPvpContext(Player* bot)
    {
        return bot && (bot->InBattleground() || bot->InArena() || bot->duel || bot->IsPvP() || bot->IsFFAPvP());
    }

    bool IsInAlteracValley(Player* bot)
    {
        if (!bot || !bot->InBattleground() || !bot->GetBattleground())
            return false;

        BattlegroundTypeId bgType = bot->GetBattleground()->GetBgTypeID();
        if (bgType == BATTLEGROUND_RB)
            bgType = bot->GetBattleground()->GetBgTypeID(true);

        return bgType == BATTLEGROUND_AV;
    }

    bool GetActiveAVObjective(PlayerbotAI* botAI, Player* bot, PositionInfo& objective)
    {
        if (!botAI || !bot || !IsInAlteracValley(bot))
            return false;

        return GetActiveBattlegroundObjective(botAI, bot, objective);
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
        return victim && botAI && !botAI->IsOpposing(victim) && botAI->IsHeal(victim);
    }

    bool HasActiveBattlegroundCaptureObjective(PlayerbotAI* botAI)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        if (!bot || !bot->InBattleground() || bot->InArena())
            return false;

        if (HasCaptureBannerCast(bot) || IsCarryingBattlegroundFlag(bot))
            return true;

        Battleground* bg = bot->GetBattleground();
        BattlegroundTypeId const bgType = GetRealBattlegroundType(bg);
        PositionInfo objective;
        if (!GetActiveBattlegroundObjective(botAI, bot, objective))
            return false;

        switch (bgType)
        {
            case BATTLEGROUND_AV:
                if (IsAlteracContestedGraveyardObjective(bg, objective))
                    return false;

                return IsAlteracCaptureObjective(bg, objective);
            case BATTLEGROUND_AB:
                return IsObjectiveNearArathiNode(objective);
            case BATTLEGROUND_IC:
                return IsIsleCaptureObjective(bg, objective);
            case BATTLEGROUND_EY:
                return IsEyeCaptureObjective(bg, objective);
            case BATTLEGROUND_WS:
                return IsWarsongCaptureObjective(bg, objective);
            default:
                return false;
        }
    }

    bool IsSelfDefenseTarget(Player* bot, Unit* target)
    {
        if (!IsSameMapUnit(bot, target))
            return false;

        if (target->GetVictim() == bot || target->GetTarget() == bot->GetGUID())
            return true;

        ObjectGuid const targetGuid = target->GetGUID();
        Unit::AttackerSet const& attackers = bot->getAttackers();
        for (Unit* attacker : attackers)
        {
            if (!attacker || !attacker->IsAlive())
                continue;

            if (attacker == target || attacker->GetGUID() == targetGuid)
                return true;

            Unit* owner = attacker->GetCharmerOrOwner();
            if (owner && owner->GetGUID() == targetGuid)
                return true;
        }

        return false;
    }

    bool HasSelfDefenseAttacker(Player* bot)
    {
        if (!bot || !bot->IsAlive())
            return false;

        Unit::AttackerSet const& attackers = bot->getAttackers();
        for (Unit* attacker : attackers)
            if (IsSelfDefenseTarget(bot, attacker))
                return true;

        return false;
    }

    bool IsCaptureObjectiveThreat(PlayerbotAI* botAI, Unit* target)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        Player* playerTarget = target ? target->ToPlayer() : nullptr;
        if (!IsSameMapUnit(bot, target) || !playerTarget || !botAI->IsOpposing(playerTarget))
            return false;

        PositionInfo objective;
        if (!GetActiveBattlegroundObjective(botAI, bot, objective))
            return false;

        GameObject* captureTarget = GetCaptureBannerTarget(target);
        if (!captureTarget)
            return false;

        bool const captureTargetMatchesObjective =
            IsPositionNear2d(objective, captureTarget->GetPositionX(), captureTarget->GetPositionY(), 35.0f);
        bool const targetIsOnObjective = IsNearObjective(target, objective, 14.0f);
        bool const botCanInfluenceObjective = IsNearObjective(bot, objective, 70.0f) || bot->IsWithinDist(target, 45.0f);

        return botCanInfluenceObjective && (captureTargetMatchesObjective || targetIsOnObjective);
    }

    bool HasCaptureObjectiveThreat(PlayerbotAI* botAI)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        if (!bot || !HasActiveBattlegroundCaptureObjective(botAI))
            return false;

        std::unordered_set<ObjectGuid> checked;
        auto checkUnit = [&](Unit* unit) -> bool
        {
            if (!unit || !unit->GetGUID() || checked.find(unit->GetGUID()) != checked.end())
                return false;

            checked.insert(unit->GetGUID());
            return IsCaptureObjectiveThreat(botAI, unit);
        };

        if (checkUnit(botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get()))
            return true;

        if (checkUnit(botAI->GetAiObjectContext()->GetValue<Unit*>("enemy player target")->Get()))
            return true;

        GuidVector attackers = botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get();
        for (ObjectGuid const& guid : attackers)
            if (checkUnit(botAI->GetUnit(guid)))
                return true;

        GuidVector players = botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest enemy players")->Get();
        uint8 scanned = 0;
        for (ObjectGuid const& guid : players)
        {
            if (checkUnit(botAI->GetUnit(guid)))
                return true;

            if (++scanned >= 24)
                break;
        }

        return false;
    }

    bool CanEngageDuringBattlegroundCapture(PlayerbotAI* botAI, Unit* target)
    {
        if (!HasActiveBattlegroundCaptureObjective(botAI))
            return true;

        return IsSelfDefenseTarget(botAI ? botAI->GetBot() : nullptr, target) ||
               IsCaptureObjectiveThreat(botAI, target);
    }

    bool IsObjectiveRelevantEnemy(PlayerbotAI* botAI, Unit* target, bool threatTarget, float botObjectiveRadius,
                                  float targetObjectiveRadius)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        if (!IsSameMapUnit(bot, target))
            return false;

        if (!CanEngageDuringBattlegroundCapture(botAI, target))
            return false;

        if (!IsInAlteracValley(bot))
            return true;

        if (threatTarget || target->GetVictim() == bot || IsAttackingFriendlyHealer(botAI, target))
            return true;

        float const distanceToBot = ServerFacade::instance().GetDistance2d(bot, target);
        if (distanceToBot <= 18.0f)
            return true;

        PositionInfo objective;
        if (!GetActiveAVObjective(botAI, bot, objective))
            return true;

        if (IsNearObjective(bot, objective, botObjectiveRadius) && IsNearObjective(target, objective, targetObjectiveRadius))
            return true;

        if (target->IsNonMeleeSpellCast(true) && IsNearObjective(bot, objective, botObjectiveRadius) &&
            IsNearObjective(target, objective, targetObjectiveRadius + 10.0f))
            return true;

        return false;
    }

    bool IsBreakableCrowdControlled(Unit* target)
    {
        return target && (
            target->IsPolymorphed() ||
            target->HasAuraType(SPELL_AURA_MOD_CONFUSE) ||
            target->HasAuraType(SPELL_AURA_MOD_FEAR) ||
            target->HasAuraType(SPELL_AURA_MOD_CHARM) ||
            target->HasAuraType(SPELL_AURA_AOE_CHARM) ||
            target->HasAuraWithMechanic(1 << MECHANIC_SLEEP) ||
            target->HasAuraWithMechanic(1 << MECHANIC_SAPPED));
    }

    bool CanDamageTarget(PlayerbotAI* botAI, Unit* target, bool areaDamage)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        if (!IsPvpContext(bot) || !target || !IsBreakableCrowdControlled(target))
            return true;

        if (target->GetVictim() == bot || IsAttackingFriendlyHealer(botAI, target))
            return true;

        if (areaDamage)
            return false;

        Unit* currentTarget = botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
        return currentTarget == target && target->GetHealthPct() < 20.0f;
    }

    bool SpellCanBreakCrowdControl(SpellInfo const* spellInfo)
    {
        if (!spellInfo || spellInfo->IsPositive())
            return false;

        for (SpellEffectInfo const& effect : spellInfo->Effects)
        {
            switch (effect.Effect)
            {
                case SPELL_EFFECT_SCHOOL_DAMAGE:
                case SPELL_EFFECT_WEAPON_DAMAGE:
                case SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL:
                case SPELL_EFFECT_NORMALIZED_WEAPON_DMG:
                    return true;
                case SPELL_EFFECT_APPLY_AURA:
                case SPELL_EFFECT_APPLY_AREA_AURA_PARTY:
                case SPELL_EFFECT_APPLY_AREA_AURA_RAID:
                case SPELL_EFFECT_APPLY_AREA_AURA_FRIEND:
                case SPELL_EFFECT_APPLY_AREA_AURA_ENEMY:
                    if (effect.ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE ||
                        effect.ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE_PERCENT ||
                        effect.ApplyAuraName == SPELL_AURA_PERIODIC_LEECH ||
                        effect.ApplyAuraName == SPELL_AURA_PROC_TRIGGER_DAMAGE)
                        return true;
                    break;
                default:
                    break;
            }
        }

        return false;
    }

    bool IsAoeSafe(PlayerbotAI* botAI, WorldLocation const& position, float radius)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        if (!IsPvpContext(bot))
            return true;

        std::unordered_set<ObjectGuid> checked;
        auto checkGuid = [&](ObjectGuid const& guid) -> bool
        {
            if (!guid || checked.find(guid) != checked.end())
                return true;

            checked.insert(guid);
            Unit* unit = botAI->GetUnit(guid);
            if (!unit || unit == bot || !unit->IsAlive() || unit->GetMapId() != bot->GetMapId() ||
                !IsEnemyPlayerOrOwnedUnit(botAI, unit))
                return true;

            if (!IsBreakableCrowdControlled(unit) || CanDamageTarget(botAI, unit, true))
                return true;

            float const distance = ServerFacade::instance().GetDistance2d(unit, position.GetPositionX(), position.GetPositionY());
            return distance > radius;
        };

        GuidVector attackers = botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get();
        for (ObjectGuid const& guid : attackers)
            if (!checkGuid(guid))
                return false;

        GuidVector possibleTargets = botAI->GetAiObjectContext()->GetValue<GuidVector>("possible targets")->Get();
        for (ObjectGuid const& guid : possibleTargets)
            if (!checkGuid(guid))
                return false;

        return true;
    }

    bool CurrentAoeIsSafe(PlayerbotAI* botAI)
    {
        if (!botAI || *botAI->GetAiObjectContext()->GetValue<uint8>("aoe count") <= 1)
            return true;

        WorldLocation position = *botAI->GetAiObjectContext()->GetValue<WorldLocation>("aoe position");
        return IsAoeSafe(botAI, position, sPlayerbotAIConfig.aoeRadius + 2.0f);
    }

    bool IsInterruptSpell(std::string const& spell)
    {
        return spell == "counterspell" || spell == "kick" || spell == "pummel" || spell == "shield bash" ||
               spell == "mind freeze" || spell == "strangulate" || spell == "wind shear" || spell == "earth shock" ||
               spell == "silence" || spell == "spell lock" || spell == "silencing shot" ||
               spell == "hammer of justice" || spell == "bash" || spell == "intercept" ||
               spell == "repentance" || spell == "arcane torrent";
    }

    bool TryReserveInterrupt(PlayerbotAI* botAI, Unit* target, std::string const& spell, uint32 holdMs)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        SpellInfo const* castingSpell = GetCurrentCastingSpell(target);
        if (!bot || !target || !IsInterruptSpell(spell) || !castingSpell)
            return true;

        if (!botAI->IsInterruptableSpellCasting(target, spell))
            return false;

        uint32 const now = getMSTime();
        InterruptReservationKey key{ target->GetGUID(), castingSpell->Id };
        std::lock_guard<std::mutex> guard(interruptReservationsMutex);
        PruneInterruptReservations(now);

        auto itr = interruptReservations.find(key);
        if (itr != interruptReservations.end() && itr->second.untilMs >= now && itr->second.bot != bot->GetGUID())
            return false;

        interruptReservations[key] = { bot->GetGUID(), now + holdMs };
        return true;
    }

    int32 ScoreOffensiveDispelTarget(PlayerbotAI* botAI, Unit* target, uint32 dispelType, bool threatTarget)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        if (!IsSameMapUnit(bot, target) || !IsEnemyPlayerOrOwnedUnit(botAI, target) || !botAI->HasAuraToDispel(target, dispelType))
            return 0;

        if (!IsObjectiveRelevantEnemy(botAI, target, threatTarget, 65.0f, 45.0f))
            return 0;

        bool hasImportantAura = false;
        uint32 const dispelAuraCount = CountUsefulDispelAuras(target, dispelType, &hasImportantAura);
        if (!dispelAuraCount)
            return 0;

        int32 score = 160 + static_cast<int32>(dispelAuraCount) * 80;
        if (hasImportantAura)
            score += 650;

        Player* playerTarget = target->ToPlayer();
        if (playerTarget && botAI->IsHeal(playerTarget))
            score += 260;

        if (IsCastingHelpfulSpell(target))
            score += 180;

        if (target->GetVictim() == bot || IsAttackingFriendlyHealer(botAI, target))
            score += 220;

        Unit* currentTarget = botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
        if (target == currentTarget)
            score += 100;

        float const distance = ServerFacade::instance().GetDistance2d(bot, target);
        score -= static_cast<int32>(std::min(distance, 80.0f));
        return score;
    }

    bool ShouldUseBurstCooldown(PlayerbotAI* botAI, Unit* target)
    {
        Player* bot = botAI ? botAI->GetBot() : nullptr;
        if (!bot || !target || !IsPvpContext(bot) || !IsEnemyPlayerOrOwnedUnit(botAI, target))
            return false;

        if (!IsObjectiveRelevantEnemy(botAI, target, false, 70.0f, 50.0f))
            return false;

        if (target->GetHealthPct() < 35.0f)
            return true;

        Player* playerTarget = target->ToPlayer();
        if (playerTarget && botAI->IsHeal(playerTarget) && (IsCastingHelpfulSpell(target) || target->GetHealthPct() < 60.0f))
            return true;

        uint32 attackers = 0;
        Unit::AttackerSet const& attackerSet = target->getAttackers();
        for (Unit* attacker : attackerSet)
        {
            Player* player = attacker ? attacker->ToPlayer() : nullptr;
            if (player && !botAI->IsOpposing(player) && ++attackers >= 2)
                return true;
        }

        return false;
    }
}
