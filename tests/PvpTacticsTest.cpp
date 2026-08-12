/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include <gtest/gtest.h>

#include "PvpTactics.h"

// These cover the parts of the PvP tactic layer that are pure functions of a spell name or of a
// tier value, so they need no world, no map and no bot. That is deliberate: every one of them sits
// on the path that decides whether a cast happens at all, and a regression there does not produce a
// worse rotation, it produces a bot standing still. The stateful parts (pressure, combat phase,
// target scoring) need a live Unit and belong in a harness with the core's TestMap mocks.

namespace
{
    using namespace ai::pvp;
}

TEST(PvpInterrupts, DedicatedInterruptsAreRecognised)
{
    for (std::string const& spell : {"counterspell", "kick", "pummel", "shield bash", "mind freeze",
                                     "wind shear", "spell lock", "silencing shot", "silence", "strangulate"})
    {
        EXPECT_TRUE(IsDedicatedInterruptSpell(spell)) << spell;
        EXPECT_TRUE(IsInterruptSpell(spell)) << spell;
    }
}

TEST(PvpInterrupts, DualUseSpellsAreNeverSuppressedAsInterrupts)
{
    // These can interrupt, but they are also plain damage, a gap closer or a stun. Classifying them
    // as dedicated interrupts removed them from the rotation whenever the target was casting
    // something the bot decided was not worth interrupting.
    for (std::string const& spell : {"earth shock", "hammer of justice", "intercept", "bash",
                                     "repentance", "arcane torrent"})
    {
        EXPECT_FALSE(IsDedicatedInterruptSpell(spell)) << spell;
        EXPECT_TRUE(IsInterruptSpell(spell)) << spell;
    }
}

TEST(PvpInterrupts, OrdinaryRotationSpellsAreNotInterrupts)
{
    for (std::string const& spell : {"shadow bolt", "mortal strike", "steady shot", "frostbolt", "shoot"})
        EXPECT_FALSE(IsInterruptSpell(spell)) << spell;
}

TEST(PvpDefensives, EmergencyButtonsAreClassedAsDefensiveCooldowns)
{
    for (std::string const& spell : {"ice block", "divine shield", "shield wall", "dispersion",
                                     "cloak of shadows", "evasion", "deterrence", "icebound fortitude",
                                     "anti magic shell", "barkskin", "survival instincts", "retaliation"})
        EXPECT_TRUE(IsDefensiveCooldownSpell(spell)) << spell;
}

TEST(PvpDefensives, UpkeepCastsAreNotDefensiveCooldowns)
{
    // Gating these behind combat pressure meant a mage never pre-shielded and an enhancement shaman
    // never regenerated mana.
    for (std::string const& spell : {"mana shield", "shamanistic rage", "stoneclaw totem"})
        EXPECT_FALSE(IsDefensiveCooldownSpell(spell)) << spell;
}

TEST(PvpDefenseTiers, TiersAreOrdered)
{
    EXPECT_LT(DEFENSE_TIER_NONE, DEFENSE_TIER_LIGHT);
    EXPECT_LT(DEFENSE_TIER_LIGHT, DEFENSE_TIER_HEAVY);
    EXPECT_LT(DEFENSE_TIER_HEAVY, DEFENSE_TIER_IMMUNE);
}

TEST(PvpDefenseTiers, NullTargetHasNoDefense)
{
    EXPECT_EQ(GetMajorDefenseTier(nullptr), DEFENSE_TIER_NONE);
    EXPECT_FALSE(IsMajorDefenseActive(nullptr));
    EXPECT_FALSE(IsMajorDefenseActive(nullptr, DEFENSE_TIER_IMMUNE));
}

TEST(PvpGuards, NullArgumentsNeverCrashAndNeverBlockTheRotation)
{
    // Every one of these is on the cast path. Returning "blocked" for a null argument would stop a
    // rotation outright, so the safe direction is always "allowed".
    EXPECT_FALSE(IsPvpContext(static_cast<Player*>(nullptr)));
    EXPECT_FALSE(IsPvpContext(nullptr, nullptr));
    EXPECT_FALSE(IsEnemyPlayerOrOwnedUnit(nullptr, nullptr));
    EXPECT_FALSE(IsCastingHelpfulSpell(nullptr));
    EXPECT_FALSE(IsBreakableCrowdControlled(nullptr));
    EXPECT_TRUE(CanDamageTarget(nullptr, nullptr, false));
    EXPECT_TRUE(CanDamageTarget(nullptr, nullptr, true));
    EXPECT_FALSE(SpellCanBreakCrowdControl(nullptr));
    EXPECT_TRUE(CurrentAoeIsSafe(nullptr));
    EXPECT_TRUE(CanUseDefensiveCooldown(nullptr, "ice block"));
    EXPECT_TRUE(CanUseOffensiveCooldown(nullptr, "icy veins"));
    EXPECT_TRUE(CanUseUtilitySpell(nullptr, nullptr, "disarm"));
    EXPECT_FALSE(ShouldUseBurstCooldown(nullptr, nullptr));
    EXPECT_FALSE(HasIncomingHostileCast(nullptr));
    EXPECT_EQ(ScoreClassMatchup(nullptr, nullptr), 0);
    EXPECT_EQ(GetIncomingPressure(nullptr), 0u);
    EXPECT_FALSE(HasPhysicalPressure(nullptr));
    EXPECT_FALSE(HasMagicPressure(nullptr));
    EXPECT_EQ(GetClosestPvpMeleeAttacker(nullptr, 10.0f), nullptr);
    EXPECT_EQ(GetCombatPhase(nullptr, nullptr), CombatPhase::None);
    EXPECT_FALSE(ShouldPrioritizeBattlegroundCapture(nullptr));
    EXPECT_TRUE(CanEngageDuringBattlegroundCapture(nullptr, nullptr));
    EXPECT_FALSE(IsCarryingOurFlag(nullptr, nullptr));
}

TEST(PvpGuards, TargetProfileOfNothingIsInvalid)
{
    TargetProfile const profile = GetTargetProfile(nullptr, nullptr);
    EXPECT_FALSE(profile.valid);
    EXPECT_FALSE(profile.healer);
    EXPECT_FALSE(profile.melee);
    EXPECT_FALSE(profile.caster);
}
