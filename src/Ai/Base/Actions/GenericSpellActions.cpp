/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "GenericSpellActions.h"

#include <ctime>

#include "Event.h"
#include "ItemTemplate.h"
#include "ObjectDefines.h"
#include "Opcodes.h"
#include "Player.h"
#include "Playerbots.h"
#include "ServerFacade.h"
#include "WorldPacket.h"
#include "Group.h"
#include "Chat.h"
#include "Ai/Base/Util/GenericBuffUtils.h"
#include "CcTargetValue.h"
#include "PlayerbotAI.h"
#include "PvpTactics.h"

using ai::buff::MakeAuraQualifierForBuff;
using ai::spell::HasSpellOrCategoryCooldown;

namespace
{
    constexpr uint32 SPELL_PVP_TRINKET = 42292;

    bool HasHardCrowdControl(Player* bot)
    {
        return bot && (
            bot->HasAuraType(SPELL_AURA_MOD_STUN) ||
            bot->HasAuraType(SPELL_AURA_MOD_FEAR) ||
            bot->HasAuraType(SPELL_AURA_MOD_CONFUSE) ||
            bot->HasAuraType(SPELL_AURA_MOD_CHARM) ||
            bot->HasAuraType(SPELL_AURA_AOE_CHARM) ||
            bot->HasAuraWithMechanic(1 << MECHANIC_SLEEP) ||
            bot->HasAuraWithMechanic(1 << MECHANIC_SAPPED));
    }

    bool HasMovementControl(Player* bot)
    {
        return bot && (bot->HasAuraType(SPELL_AURA_MOD_ROOT) || bot->isFrozen());
    }

    bool ShouldUsePvpCcBreakTrinket(PlayerbotAI* botAI, Player* bot)
    {
        if (!botAI || !bot)
            return false;

        if (!ai::pvp::IsPvpContext(bot))
            return false;

        GuidVector attackers = botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get();
        bool const pressured = !attackers.empty() || bot->GetHealthPct() < 60.0f;

        if (HasHardCrowdControl(bot))
            return pressured || botAI->IsHeal(bot) || bot->GetHealthPct() < 85.0f;

        if (HasMovementControl(bot))
            return pressured && (botAI->IsHeal(bot) || bot->GetHealthPct() < 55.0f || attackers.size() >= 2);

        return false;
    }

    bool IsValidResurrectTarget(Unit* target)
    {
        Player* player = target ? target->ToPlayer() : nullptr;
        return player && player->IsInWorld() && !player->IsAlive() && !player->isResurrectRequested() &&
               player->getDeathState() == DeathState::Corpse && !player->HasPlayerFlag(PLAYER_FLAGS_GHOST);
    }
}

CastSpellAction::CastSpellAction(PlayerbotAI* botAI, std::string const spell)
    : Action(botAI, spell), spell(spell), range(botAI->GetRange("spell"))
{
}

bool CastSpellAction::Execute(Event /*event*/)
{
    if (spell == "conjure food" || spell == "conjure water")
    {
        // uint32 id = AI_VALUE2(uint32, "spell id", spell);
        // if (!id)
        //     return false;

        uint32 castId = 0;

        for (PlayerSpellMap::iterator itr = bot->GetSpellMap().begin(); itr != bot->GetSpellMap().end(); ++itr)
        {
            uint32 spellId = itr->first;

            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
            if (!spellInfo)
                continue;

            std::string const namepart = spellInfo->SpellName[0];
            std::wstring wnamepart;
            if (!Utf8toWStr(namepart, wnamepart))
                return false;

            wstrToLower(wnamepart);

            if (!Utf8FitTo(spell, wnamepart))
                continue;

            if (spellInfo->Effects[0].Effect != SPELL_EFFECT_CREATE_ITEM)
                continue;

            uint32 itemId = spellInfo->Effects[0].ItemType;
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
            if (!proto)
                continue;

            if (bot->CanUseItem(proto) != EQUIP_ERR_OK)
                continue;

            if (spellInfo->Id > castId)
                castId = spellInfo->Id;
        }

        return botAI->CastSpell(castId, bot);
    }

    Unit* target = GetTarget();
    // Only dedicated interrupts are coordinated between bots. Dual-use spells such as earth shock or
    // intercept must stay castable even while the target happens to be casting something.
    bool const interrupt = ai::pvp::IsDedicatedInterruptSpell(spell);
    if (interrupt && !ai::pvp::TryReserveInterrupt(botAI, target, spell))
        return false;

    bool const defensive = ai::pvp::IsDefensiveCooldownSpell(spell);
    if (defensive && !ai::pvp::TryReserveDefensiveCooldown(botAI, spell))
    {
        if (interrupt)
            ai::pvp::ReleaseInterrupt(botAI, target);
        return false;
    }

    bool const cast = botAI->CastSpell(spell, target);
    if (interrupt && !cast)
        ai::pvp::ReleaseInterrupt(botAI, target);
    if (defensive && !cast)
        ai::pvp::ReleaseDefensiveCooldown(botAI);

    return cast;
}

bool CastSpellAction::isUseful()
{
    if (botAI->IsInVehicle() && !botAI->IsInVehicle(false, false, true))
        return false;

    if (spell == "mount" && !bot->IsMounted() && !bot->IsInCombat())
        return true;

    if (spell == "mount" && bot->IsInCombat())
    {
        bot->Dismount();
        return false;
    }

    Unit* spellTarget = GetTarget();
    if (!spellTarget)
        return false;

    if (!spellTarget->IsInWorld() || spellTarget->GetMapId() != bot->GetMapId())
        return false;

    if (!ai::pvp::CanUseDefensiveCooldown(botAI, spell) ||
        !ai::pvp::CanUseOffensiveCooldown(botAI, spell) ||
        !ai::pvp::CanUseUtilitySpell(botAI, spellTarget, spell))
        return false;

    if (getThreatType() == ActionThreatType::Aoe && !ai::pvp::CurrentAoeIsSafe(botAI))
        return false;

    uint32 const spellId = AI_VALUE2(uint32, "spell id", spell);
    SpellInfo const* spellInfo = spellId ? sSpellMgr->GetSpellInfo(spellId) : nullptr;
    if (ai::pvp::SpellCanBreakCrowdControl(spellInfo) &&
        !ai::pvp::CanDamageTarget(botAI, spellTarget, getThreatType() == ActionThreatType::Aoe))
        return false;

    // float combatReach = bot->GetCombatReach() + target->GetCombatReach();
    // if (!botAI->IsRanged(bot))
    //     combatReach += 4.0f / 3.0f;

    return AI_VALUE2(bool, "spell cast useful", spell);
           // && ServerFacade::instance().GetDistance2d(bot, target) <= (range + combatReach);
}

bool CastSpellAction::isPossible()
{
    if (botAI->IsInVehicle() && !botAI->IsInVehicle(false, false, true))
    {
        if (!sPlayerbotAIConfig.logInGroupOnly || (bot->GetGroup() && botAI->HasRealPlayerMaster()))
        {
            LOG_DEBUG("playerbots", "Can cast spell failed. Vehicle. - bot name: {}", bot->GetName());
        }
        return false;
    }

    if (spell == "mount" && !bot->IsMounted() && !bot->IsInCombat())
        return true;

    if (spell == "mount" && bot->IsInCombat())
    {
        if (!sPlayerbotAIConfig.logInGroupOnly || (bot->GetGroup() && botAI->HasRealPlayerMaster()))
        {
            LOG_DEBUG("playerbots", "Can cast spell failed. Mount. - bot name: {}", bot->GetName());
        }
        bot->Dismount();
        return false;
    }

    Unit* target = GetTarget();
    if (ai::pvp::IsDedicatedInterruptSpell(spell) && !ai::pvp::CanAttemptInterrupt(botAI, target, spell))
        return false;

    // Spell* currentSpell = bot->GetCurrentSpell(CURRENT_GENERIC_SPELL); //not used, line marked for removal.
    return botAI->CanCastSpell(spell, target);
}

CastMeleeSpellAction::CastMeleeSpellAction(PlayerbotAI* botAI, std::string const spell) : CastSpellAction(botAI, spell)
{
    range = ATTACK_DISTANCE;
}

bool CastMeleeSpellAction::isUseful()
{
    Unit* target = GetTarget();
    if (!target)
        return false;

    if (!bot->IsWithinMeleeRange(target))
        return false;

    return CastSpellAction::isUseful();
}

CastMeleeDebuffSpellAction::CastMeleeDebuffSpellAction(
    PlayerbotAI* botAI, std::string const spell, bool isOwner, float needLifeTime) :
    CastDebuffSpellAction(botAI, spell, isOwner, needLifeTime)
{
    range = ATTACK_DISTANCE;
}

bool CastMeleeDebuffSpellAction::isUseful()
{
    Unit* target = GetTarget();
    if (!target)
        return false;

    if (!bot->IsWithinMeleeRange(target))
        return false;

    return CastDebuffSpellAction::isUseful();
}

bool CastAuraSpellAction::isUseful()
{
    if (!GetTarget() || !CastSpellAction::isUseful())
        return false;
    Aura* aura = botAI->GetAura(spell, GetTarget(), isOwner, checkDuration);
    if (!aura)
        return true;
    int32 const duration = aura->GetDuration();
    if (beforeDuration && duration >= 0 && static_cast<uint32>(duration) < beforeDuration)
        return true;
    return false;
}

CastEnchantItemAction::CastEnchantItemAction(PlayerbotAI* botAI, std::string const spell)
    : CastSpellAction(botAI, spell)
{
    range = botAI->GetRange("spell");
}

bool CastEnchantItemAction::isPossible()
{
    // if (!CastSpellAction::isPossible())
    // {
    //     botAI->TellMasterNoFacing("Impossible: " + spell);
    //     return false;
    // }

    uint32 spellId = AI_VALUE2(uint32, "spell id", spell);

    // bool ok = AI_VALUE2(Item*, "item for spell", spellId);
    // Item* item = AI_VALUE2(Item*, "item for spell", spellId);
    // botAI->TellMasterNoFacing("spell: " + spell + ", spell id: " + std::to_string(spellId) + " item for spell: " +
    // std::to_string(ok));
    return spellId && AI_VALUE2(Item*, "item for spell", spellId);
}

CastEnchantItemMainHandAction::CastEnchantItemMainHandAction(PlayerbotAI* botAI, std::string const spell)
    : CastEnchantItemAction(botAI, spell) {}

bool CastEnchantItemMainHandAction::isPossible()
{
    if (!CastEnchantItemAction::isPossible())
        return false;

    Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
    return item && !item->GetEnchantmentId(TEMP_ENCHANTMENT_SLOT) &&
           item->GetTemplate()->Class == ITEM_CLASS_WEAPON;
}

CastEnchantItemOffHandAction::CastEnchantItemOffHandAction(PlayerbotAI* botAI, std::string const spell)
    : CastEnchantItemAction(botAI, spell) {}

bool CastEnchantItemOffHandAction::isPossible()
{
    if (!CastEnchantItemAction::isPossible())
        return false;

    Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
    if (!item || item->GetEnchantmentId(TEMP_ENCHANTMENT_SLOT))
        return false;

    uint32 invType = item->GetTemplate()->InventoryType;
    return invType == INVTYPE_WEAPON || invType == INVTYPE_WEAPONOFFHAND;
}

CastHealingSpellAction::CastHealingSpellAction(PlayerbotAI* botAI, std::string const spell, uint8 estAmount,
                                               HealingManaEfficiency manaEfficiency, bool isOwner)
    : CastAuraSpellAction(botAI, spell, isOwner), manaEfficiency(manaEfficiency), estAmount(estAmount)
{
    range = botAI->GetRange("heal");
}

bool CastHealingSpellAction::isUseful() { return CastAuraSpellAction::isUseful(); }

bool CastAoeHealSpellAction::isUseful() { return CastSpellAction::isUseful(); }

CastCureSpellAction::CastCureSpellAction(PlayerbotAI* botAI, std::string const spell) : CastSpellAction(botAI, spell)
{
    range = botAI->GetRange("heal");
}

Value<Unit*>* CurePartyMemberAction::GetTargetValue()
{
    return context->GetValue<Unit*>("party member to dispel", dispelType);
}

// Make Bots Paladin, druid, mage use the greater buff rank spell
// TODO Priest doen't verify il he have components
Value<Unit*>* BuffOnPartyAction::GetTargetValue()
{
    return context->GetValue<Unit*>("party member without aura", MakeAuraQualifierForBuff(spell));
}

bool BuffOnPartyAction::Execute(Event /*event*/)
{
    std::string castName = spell; // default = mono

    auto SendGroupRP = ai::chat::MakeGroupAnnouncer(bot);
    castName = ai::buff::UpgradeToGroupIfAppropriate(bot, botAI, castName, /*announceOnMissing=*/true, SendGroupRP);

    return botAI->CastSpell(castName, GetTarget());
}
// End greater buff fix

CastShootAction::CastShootAction(PlayerbotAI* botAI) : CastSpellAction(botAI, "shoot"), shootSpellId(0)
{
    if (Item* const pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED))
    {
        spell = "shoot";

        switch (pItem->GetTemplate()->SubClass)
        {
            case ITEM_SUBCLASS_WEAPON_GUN:
                spell += " gun";
                shootSpellId = 3018;
                break;
            case ITEM_SUBCLASS_WEAPON_BOW:
                spell += " bow";
                shootSpellId = 3018;
                break;
            case ITEM_SUBCLASS_WEAPON_CROSSBOW:
                spell += " crossbow";
                shootSpellId = 3018;
                break;
            case ITEM_SUBCLASS_WEAPON_THROWN:
                spell = "throw";
                shootSpellId = 2764;
                break;
        }
    }
}

bool CastShootAction::isPossible()
{
    if (shootSpellId)
        return botAI->CanCastSpell(shootSpellId, GetTarget(), false);

    return CastSpellAction::isPossible();
}

bool CastShootAction::Execute(Event /*event*/)
{
    if (shootSpellId)
        return botAI->CastSpell(shootSpellId, GetTarget());

    return botAI->CastSpell(spell, GetTarget());
}

Value<Unit*>* CastDebuffSpellOnAttackerAction::GetTargetValue()
{
    return context->GetValue<Unit*>("attacker without aura", spell);
}

Value<Unit*>* CastDebuffSpellOnMeleeAttackerAction::GetTargetValue()
{
    return context->GetValue<Unit*>("melee attacker without aura", spell);
}

CastBuffSpellAction::CastBuffSpellAction(PlayerbotAI* botAI, std::string const spell, bool checkIsOwner, uint32 beforeDuration)
    : CastAuraSpellAction(botAI, spell, checkIsOwner, false, beforeDuration)
{
    range = botAI->GetRange("spell");
}

Value<Unit*>* CastSpellOnEnemyHealerAction::GetTargetValue()
{
    return context->GetValue<Unit*>("enemy healer target", spell);
}

bool ResurrectPartyMemberAction::isPossible()
{
    return IsValidResurrectTarget(GetTarget()) && CastSpellAction::isPossible();
}

bool ResurrectPartyMemberAction::isUseful()
{
    return IsValidResurrectTarget(GetTarget()) && CastSpellAction::isUseful();
}

Value<Unit*>* CastSnareSpellAction::GetTargetValue() { return context->GetValue<Unit*>("snare target", spell); }

Value<Unit*>* CastCrowdControlSpellAction::GetTargetValue() { return context->GetValue<Unit*>("cc target", getName()); }

bool CastCrowdControlSpellAction::Execute(Event /*event*/)
{
    Unit* target = GetTarget();
    if (!ai::pvp::TryReserveCrowdControl(botAI, target, getName()))
        return false;

    bool const cast = botAI->CastSpell(getName(), target);
    if (!cast)
        ai::pvp::ReleaseCrowdControl(botAI, target);

    return cast;
}

bool CastCrowdControlSpellAction::isPossible()
{
    Unit* target = GetTarget();
    return target && ai::cc::CanApplyCrowdControl(botAI, target, getName()) &&
           botAI->CanCastSpell(getName(), target);
}

bool CastCrowdControlSpellAction::isUseful()
{
    Unit* target = GetTarget();
    return target && !botAI->HasAura(getName(), target) &&
           ai::cc::CanApplyCrowdControl(botAI, target, getName());
}

std::string const CastProtectSpellAction::GetTargetName() { return "party member to protect"; }

bool CastProtectSpellAction::isUseful() { return GetTarget() && !botAI->HasAura(spell, GetTarget()); }

bool CastVehicleSpellAction::isPossible()
{
    uint32 spellId = AI_VALUE2(uint32, "vehicle spell id", spell);
    return botAI->CanCastVehicleSpell(spellId, GetTarget());
}

bool CastVehicleSpellAction::isUseful() { return botAI->IsInVehicle(false, true); }

bool CastVehicleSpellAction::Execute(Event /*event*/)
{
    uint32 spellId = AI_VALUE2(uint32, "vehicle spell id", spell);
    return botAI->CastVehicleSpell(spellId, GetTarget());
}

bool CastEveryManForHimselfAction::isPossible()
{
    uint32 spellId = AI_VALUE2(uint32, "spell id", spell);
    if (!spellId)
        return false;

    if (!bot->HasSpell(spellId))
        return false;

    if (HasSpellOrCategoryCooldown(bot, spellId))
        return false;

    return true;
}

bool CastEveryManForHimselfAction::isUseful()
{
    return (bot->HasAuraType(SPELL_AURA_MOD_STUN) ||
           bot->HasAuraType(SPELL_AURA_MOD_FEAR) ||
           bot->HasAuraType(SPELL_AURA_MOD_ROOT) ||
           bot->HasAuraType(SPELL_AURA_MOD_CONFUSE) ||
           bot->HasAuraType(SPELL_AURA_MOD_CHARM))
        && CastSpellAction::isUseful();
}

bool CastWillOfTheForsakenAction::isPossible()
{
    uint32 spellId = AI_VALUE2(uint32, "spell id", spell);
    if (!spellId)
        return false;

    if (!bot->HasSpell(spellId))
        return false;

    if (HasSpellOrCategoryCooldown(bot, spellId))
        return false;

    return true;
}

bool CastWillOfTheForsakenAction::isUseful()
{
    return (bot->HasAuraType(SPELL_AURA_MOD_FEAR) ||
           bot->HasAuraType(SPELL_AURA_MOD_CHARM) ||
           bot->HasAuraType(SPELL_AURA_AOE_CHARM) ||
           bot->HasAuraWithMechanic(1 << MECHANIC_SLEEP))
        && CastSpellAction::isUseful();
}

bool UseTrinketAction::Execute(Event /*event*/)
{
    Item* trinket1 = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_TRINKET1);

    if (trinket1 && UseTrinket(trinket1))
        return true;

    Item* trinket2 = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_TRINKET2);
    if (trinket2 && UseTrinket(trinket2))
        return true;

    return false;
}

bool UseTrinketAction::UseTrinket(Item* item)
{
    if (bot->CanUseItem(item) != EQUIP_ERR_OK)
        return false;

    if (bot->IsNonMeleeSpellCast(true))
        return false;

    uint8 bagIndex = item->GetBagSlot();
    uint8 slot = item->GetSlot();
    // uint8 spell_index = 0; //not used, line marked for removal.
    uint8 cast_count = 1;
    ObjectGuid item_guid = item->GetGUID();
    uint32 glyphIndex = 0;
    uint8 castFlags = 0;
    uint32 targetFlag = TARGET_FLAG_NONE;
    uint32 spellId = 0;
    for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
    {
        if (item->GetTemplate()->Spells[i].SpellId > 0 && item->GetTemplate()->Spells[i].SpellTrigger == ITEM_SPELLTRIGGER_ON_USE)
        {
            spellId = item->GetTemplate()->Spells[i].SpellId;
            const SpellInfo* spellInfo = sSpellMgr->GetSpellInfo(spellId);

            if (!spellInfo)
                return false;

            bool const isPvpCcBreak = spellId == SPELL_PVP_TRINKET;
            if (isPvpCcBreak)
            {
                if (!ShouldUsePvpCcBreakTrinket(botAI, bot))
                    return false;

                break;
            }

            if (!spellInfo->IsPositive())
                return false;

            bool applyAura = false;
            for (int i = 0; i < MAX_SPELL_EFFECTS; i++)
            {
                const SpellEffectInfo& effectInfo = spellInfo->Effects[i];
                if (effectInfo.Effect == SPELL_EFFECT_APPLY_AURA)
                {
                    applyAura = true;
                    break;
                }
            }

            if (!applyAura)
                return false;

            uint32 spellProcFlag = spellInfo->ProcFlags;

            // Handle items with procflag "if you kill a target that grants honor or experience"
            // Bots will "learn" the trinket proc, so CanCastSpell() will be true
            // e.g. on Item https://www.wowhead.com/wotlk/item=44074/oracle-talisman-of-ablution leading to
            // constant casting of the proc spell onto themselfes https://www.wowhead.com/wotlk/spell=59787/oracle-ablutions
            // This will lead to multiple hundreds of entries in m_appliedAuras -> Once killing an enemy -> Big diff time spikes
            if (spellProcFlag != 0) return false;

            if (!botAI->CanCastSpell(spellId, bot, false))
            {
                return false;
            }
            break;
        }
    }
    if (!spellId)
        return false;
    WorldPacket packet(CMSG_USE_ITEM);
    packet << bagIndex << slot << cast_count << spellId << item_guid << glyphIndex << castFlags;

    targetFlag = TARGET_FLAG_NONE;
    packet << targetFlag << bot->GetPackGUID();
    bot->GetSession()->HandleUseItemOpcode(packet);
    return true;
}

Value<Unit*>* BuffOnMainTankAction::GetTargetValue() { return context->GetValue<Unit*>("main tank", spell); }

bool CastDebuffSpellAction::isUseful()
{
    Unit* target = GetTarget();
    if (!target || !target->IsAlive() || !target->IsInWorld())
    {
        return false;
    }

    if (!ai::pvp::CanDamageTarget(botAI, target, false))
        return false;

    if (!CastAuraSpellAction::isUseful())
        return false;

    // "estimated group dps" sums every nearby group member, which in a battleground raid runs into
    // six figures. Against a player sized health pool the expected lifetime is then a fraction of a
    // second, so this check rejected every damage over time effect in PvP - shadow priests never
    // applied Vampiric Touch, warlocks never kept their DoTs up. Judge the target itself instead.
    if (ai::pvp::IsPvpContext(botAI, target))
        return target->GetHealthPct() > 20.0f;

    return (target->GetHealth() / AI_VALUE(float, "estimated group dps")) >= needLifeTime;
}
