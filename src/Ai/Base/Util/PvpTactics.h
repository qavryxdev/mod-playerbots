/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_PVPTACTICS_H
#define _PLAYERBOT_PVPTACTICS_H

#include "ObjectGuid.h"
#include "PositionValue.h"

#include <string>

class Player;
class PlayerbotAI;
struct SpellInfo;
class Unit;
class WorldLocation;

namespace ai::pvp
{
    struct TargetProfile
    {
        bool valid = false;
        bool healer = false;
        bool melee = false;
        bool rangedPhysical = false;
        bool caster = false;
        bool physicalDamage = false;
        bool magicDamage = false;
        bool usesMana = false;
        bool feralDruid = false;
        bool treeDruid = false;
        uint8 playerClass = 0;
        uint8 specTab = 0;
    };

    struct PressureProfile
    {
        uint32 total = 0;
        uint32 physical = 0;
        uint32 magic = 0;
        uint8 meleeAttackers = 0;
        uint8 casterAttackers = 0;
        uint8 hostileCasts = 0;
    };

    // Strength of an enemy defensive cooldown. Callers pick the threshold that matters to them:
    // switching target needs IMMUNE, holding burst needs HEAVY, LIGHT is only a scoring hint.
    enum MajorDefenseTier : uint8
    {
        DEFENSE_TIER_NONE = 0,
        DEFENSE_TIER_LIGHT = 1,
        DEFENSE_TIER_HEAVY = 2,
        DEFENSE_TIER_IMMUNE = 3,
    };

    enum class CombatPhase : uint8
    {
        None,
        Control,
        Setup,
        Burst,
        Sustain,
        Reset
    };

    // Structured PvP only (battleground / arena / duel). Deliberately does NOT include the PvP flag:
    // a flagged bot questing in the open world must keep its plain PvE behaviour.
    bool IsPvpContext(Player* bot);
    // Structured PvP, or open-world combat where the target really is player controlled.
    bool IsPvpContext(PlayerbotAI* botAI, Unit* target);
    bool IsEnemyPlayerOrOwnedUnit(PlayerbotAI* botAI, Unit* target);
    bool IsCastingHelpfulSpell(Unit* target);
    bool IsInAlteracValley(Player* bot);
    bool GetActiveAVObjective(PlayerbotAI* botAI, Player* bot, PositionInfo& objective);
    bool IsNearObjective(Unit* unit, PositionInfo const& objective, float radius);
    bool IsAttackingFriendlyHealer(PlayerbotAI* botAI, Unit* target);
    // True when the enemy player is holding OUR faction's flag.
    bool IsCarryingOurFlag(PlayerbotAI* botAI, Player* target);
    bool IsObjectiveRelevantEnemy(PlayerbotAI* botAI, Unit* target, bool threatTarget = false,
                                  float botObjectiveRadius = 60.0f, float targetObjectiveRadius = 38.0f);
    bool HasActiveBattlegroundCaptureObjective(PlayerbotAI* botAI);
    bool IsSelfDefenseTarget(Player* bot, Unit* target);
    bool HasSelfDefenseAttacker(Player* bot);
    bool IsCaptureObjectiveThreat(PlayerbotAI* botAI, Unit* target);
    bool HasCaptureObjectiveThreat(PlayerbotAI* botAI);
    bool ShouldPrioritizeBattlegroundCapture(PlayerbotAI* botAI);
    bool CanEngageDuringBattlegroundCapture(PlayerbotAI* botAI, Unit* target);

    bool IsBreakableCrowdControlled(Unit* target);
    bool CanDamageTarget(PlayerbotAI* botAI, Unit* target, bool areaDamage = false);
    bool SpellCanBreakCrowdControl(SpellInfo const* spellInfo);
    bool CurrentAoeIsSafe(PlayerbotAI* botAI);
    bool IsAoeSafe(PlayerbotAI* botAI, WorldLocation const& position, float radius);

    // Spells whose only purpose is to stop a cast. Only these may be suppressed when the cast is
    // not worth interrupting - everything else must stay available as normal rotation filler.
    bool IsDedicatedInterruptSpell(std::string const& spell);
    // Dedicated interrupts plus dual-use spells (earth shock, hammer of justice, intercept, ...).
    bool IsInterruptSpell(std::string const& spell);
    bool CanAttemptInterrupt(PlayerbotAI* botAI, Unit* target, std::string const& spell);
    bool TryReserveInterrupt(PlayerbotAI* botAI, Unit* target, std::string const& spell, uint32 holdMs = 900);
    void ReleaseInterrupt(PlayerbotAI* botAI, Unit* target);
    bool TryReserveCrowdControl(PlayerbotAI* botAI, Unit* target, std::string const& spell,
                                uint32 holdMs = 1500);
    void ReleaseCrowdControl(PlayerbotAI* botAI, Unit* target);

    TargetProfile GetTargetProfile(PlayerbotAI* botAI, Unit* target);
    bool IsPhysicalDamageTarget(PlayerbotAI* botAI, Unit* target);
    bool IsCasterTarget(PlayerbotAI* botAI, Unit* target);
    bool HasIncomingHostileCast(PlayerbotAI* botAI);
    int32 ScoreOffensiveDispelTarget(PlayerbotAI* botAI, Unit* target, uint32 dispelType, bool threatTarget = false);
    int32 ScoreClassMatchup(PlayerbotAI* botAI, Unit* target);
    PressureProfile GetIncomingPressureProfile(PlayerbotAI* botAI);
    uint32 GetIncomingPressure(PlayerbotAI* botAI);
    bool HasPhysicalPressure(PlayerbotAI* botAI);
    bool HasMagicPressure(PlayerbotAI* botAI);
    Unit* GetClosestPvpMeleeAttacker(PlayerbotAI* botAI, float maxDistance);
    CombatPhase GetCombatPhase(PlayerbotAI* botAI, Unit* target);
    uint8 GetMajorDefenseTier(Unit* target);
    bool IsMajorDefenseActive(Unit* target, uint8 minimumTier = DEFENSE_TIER_HEAVY);
    bool ShouldUseDefensiveCooldown(PlayerbotAI* botAI, bool critical = false);
    bool IsDefensiveCooldownSpell(std::string const& spell);
    bool CanUseDefensiveCooldown(PlayerbotAI* botAI, std::string const& spell);
    bool TryReserveDefensiveCooldown(PlayerbotAI* botAI, std::string const& spell, uint32 holdMs = 1800);
    void ReleaseDefensiveCooldown(PlayerbotAI* botAI);
    bool ShouldUseBurstCooldown(PlayerbotAI* botAI, Unit* target);
    bool CanUseOffensiveCooldown(PlayerbotAI* botAI, std::string const& spell);
    bool CanUseUtilitySpell(PlayerbotAI* botAI, Unit* target, std::string const& spell);
    bool ShouldUseDruidPvpDot(PlayerbotAI* botAI, Unit* target, std::string const& spell);
    bool ShouldUseDruidFaerieFire(PlayerbotAI* botAI, Unit* target);
    bool ShouldUseHunterSting(PlayerbotAI* botAI, Unit* target, std::string const& sting);
    bool ShouldUseWarlockCurse(PlayerbotAI* botAI, Unit* target, std::string const& curse);
}

#endif
