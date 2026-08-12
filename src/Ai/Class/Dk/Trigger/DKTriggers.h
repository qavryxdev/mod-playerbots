/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DKTRIGGERS_H
#define _PLAYERBOT_DKTRIGGERS_H

#include "GenericTriggers.h"

class PlayerbotAI;

BUFF_TRIGGER(HornOfWinterTrigger, "horn of winter");
BUFF_TRIGGER(BoneShieldTrigger, "bone shield");
BUFF_TRIGGER(ImprovedIcyTalonsTrigger, "improved icy talons");
// DEBUFF_CHECKISOWNER_TRIGGER(PlagueStrikeDebuffTrigger, "blood plague");
class PlagueStrikeDebuffTrigger : public DebuffTrigger
{
public:
    PlagueStrikeDebuffTrigger(PlayerbotAI* botAI) : DebuffTrigger(botAI, "blood plague", 1, true, .0f) {}
};

class PlagueStrike3sDebuffTrigger : public DebuffTrigger
{
public:
    PlagueStrike3sDebuffTrigger(PlayerbotAI* botAI) : DebuffTrigger(botAI, "blood plague", 1, true, .0f, 3000) {}
};

// DEBUFF_CHECKISOWNER_TRIGGER(IcyTouchDebuffTrigger, "frost fever");
class IcyTouchDebuffTrigger : public DebuffTrigger
{
public:
    IcyTouchDebuffTrigger(PlayerbotAI* botAI) : DebuffTrigger(botAI, "frost fever", 1, true, .0f) {}
};

class IcyTouch3sDebuffTrigger : public DebuffTrigger
{
public:
    IcyTouch3sDebuffTrigger(PlayerbotAI* botAI) : DebuffTrigger(botAI, "frost fever", 1, true, .0f, 3000) {}
};

BUFF_TRIGGER(UnbreakableArmorTrigger, "unbreakable armor");
class PlagueStrikeDebuffOnAttackerTrigger : public DebuffOnMeleeAttackerTrigger
{
public:
    PlagueStrikeDebuffOnAttackerTrigger(PlayerbotAI* botAI)
        : DebuffOnMeleeAttackerTrigger(botAI, "blood plague", true, .0f)
    {
    }
};

class IcyTouchDebuffOnAttackerTrigger : public DebuffOnMeleeAttackerTrigger
{
public:
    IcyTouchDebuffOnAttackerTrigger(PlayerbotAI* botAI) : DebuffOnMeleeAttackerTrigger(botAI, "frost fever", true, .0f)
    {
    }
};

class DKPresenceTrigger : public BuffTrigger
{
public:
    DKPresenceTrigger(PlayerbotAI* botAI) : BuffTrigger(botAI, "blood presence") {}

    bool IsActive() override;
};

class BloodTapTrigger : public BuffTrigger
{
public:
    BloodTapTrigger(PlayerbotAI* botAI) : BuffTrigger(botAI, "blood tap") {}
};

class RaiseDeadTrigger : public BuffTrigger
{
public:
    RaiseDeadTrigger(PlayerbotAI* botAI) : BuffTrigger(botAI, "raise dead") {}
};

class RuneStrikeTrigger : public SpellCanBeCastTrigger
{
public:
    RuneStrikeTrigger(PlayerbotAI* botAI) : SpellCanBeCastTrigger(botAI, "rune strike") {}
};

class DeathCoilTrigger : public SpellCanBeCastTrigger
{
public:
    DeathCoilTrigger(PlayerbotAI* botAI) : SpellCanBeCastTrigger(botAI, "death coil") {}
};

class PestilenceGlyphTrigger : public SpellTrigger
{
public:
    PestilenceGlyphTrigger(PlayerbotAI* botAI) : SpellTrigger(botAI, "pestilence") {}
    virtual bool IsActive() override;
};

class BloodStrikeTrigger : public DebuffTrigger
{
public:
    BloodStrikeTrigger(PlayerbotAI* botAI) : DebuffTrigger(botAI, "blood strike", 1, true) {}
};

class HowlingBlastTrigger : public DebuffTrigger
{
public:
    HowlingBlastTrigger(PlayerbotAI* botAI) : DebuffTrigger(botAI, "howling blast", 1, true) {}
};

class MindFreezeInterruptSpellTrigger : public InterruptSpellTrigger
{
public:
    MindFreezeInterruptSpellTrigger(PlayerbotAI* botAI) : InterruptSpellTrigger(botAI, "mind freeze") {}
};

class StrangulateInterruptSpellTrigger : public InterruptSpellTrigger
{
public:
    StrangulateInterruptSpellTrigger(PlayerbotAI* botAI) : InterruptSpellTrigger(botAI, "strangulate") {}
};

// Killing Machine is a proc, not a cooldown to press: as a BoostTrigger it reported active exactly
// while the proc was missing. React to the aura instead so the guaranteed crit gets spent.
class KillingMachineTrigger : public HasAuraTrigger
{
public:
    KillingMachineTrigger(PlayerbotAI* botAI) : HasAuraTrigger(botAI, "killing machine") {}
};

class MindFreezeOnEnemyHealerTrigger : public InterruptEnemyHealerTrigger
{
public:
    MindFreezeOnEnemyHealerTrigger(PlayerbotAI* botAI) : InterruptEnemyHealerTrigger(botAI, "mind freeze") {}
};

class ChainsOfIceSnareTrigger : public SnareTargetTrigger
{
public:
    ChainsOfIceSnareTrigger(PlayerbotAI* botAI) : SnareTargetTrigger(botAI, "chains of ice") {}
};

// Death Grip forces the target to attack the death knight for three seconds. As a gap closer it may
// therefore only be aimed at an enemy player - gripping a creature would pull it off the tank.
class EnemyPlayerOutOfMeleeTrigger : public OutOfRangeTrigger
{
public:
    EnemyPlayerOutOfMeleeTrigger(PlayerbotAI* botAI)
        : OutOfRangeTrigger(botAI, "enemy player out of melee", sPlayerbotAIConfig.meleeDistance)
    {
    }

    bool IsActive() override;
};

class StrangulateOnEnemyHealerTrigger : public InterruptEnemyHealerTrigger
{
public:
    StrangulateOnEnemyHealerTrigger(PlayerbotAI* botAI) : InterruptEnemyHealerTrigger(botAI, "strangulate") {}
};

class HighBloodRuneTrigger : public Trigger
{
public:
    HighBloodRuneTrigger(PlayerbotAI* botAI) : Trigger(botAI, "high blood rune") {}
    bool IsActive() override;
};

class HighFrostRuneTrigger : public Trigger
{
public:
    HighFrostRuneTrigger(PlayerbotAI* botAI) : Trigger(botAI, "high frost rune") {}
    bool IsActive() override;
};

class HighUnholyRuneTrigger : public Trigger
{
public:
    HighUnholyRuneTrigger(PlayerbotAI* botAI) : Trigger(botAI, "high unholy rune") {}
    bool IsActive() override;
};

class NoRuneTrigger : public Trigger
{
public:
    NoRuneTrigger(PlayerbotAI* botAI) : Trigger(botAI, "no rune") {}
    bool IsActive() override;
};

class FreezingFogTrigger : public HasAuraTrigger
{
public:
    FreezingFogTrigger(PlayerbotAI* botAI) : HasAuraTrigger(botAI, "freezing fog") {}
};

class DesolationTrigger : public BuffTrigger
{
public:
    DesolationTrigger(PlayerbotAI* botAI) : BuffTrigger(botAI, "desolation", 1, false, true, 10000) {}
    bool IsActive() override;
};

class DeathAndDecayCooldownTrigger : public SpellCooldownTrigger
{
public:
    DeathAndDecayCooldownTrigger(PlayerbotAI* botAI) : SpellCooldownTrigger(botAI, "death and decay") {}
    bool IsActive() override;
};

class ArmyOfTheDeadTrigger : public BoostTrigger
{
public:
    ArmyOfTheDeadTrigger(PlayerbotAI* botAI) : BoostTrigger(botAI, "army of the dead") {}
};

class HysteriaNoCooldownTrigger : public SpellNoCooldownTrigger
{
public:
    HysteriaNoCooldownTrigger(PlayerbotAI* botAI) : SpellNoCooldownTrigger(botAI, "hysteria") {}
};

#endif
