/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "FuryWarriorStrategy.h"

class FuryWarriorStrategyActionNodeFactory : public NamedObjectFactory<ActionNode>
{
public:
    FuryWarriorStrategyActionNodeFactory()
    {
        creators["charge"] = &charge;
        creators["intercept"] = &intercept;
        creators["piercing howl"] = &piercing_howl;
        creators["pummel"] = &pummel;
        creators["enraged regeneration"] = &enraged_regeneration;
    }

private:
    static ActionNode* charge(PlayerbotAI* /*botAI*/)
    {
        return new ActionNode(
            "charge",
            /*P*/ {},
            /*A*/ { NextAction("intercept" )},
            /*C*/ {}
        );
    }

    static ActionNode* intercept(PlayerbotAI* /*botAI*/)
    {
        return new ActionNode(
            "intercept",
            /*P*/ {},
            /*A*/ { NextAction("reach melee" )},
            /*C*/ {}
        );
    }

    static ActionNode* piercing_howl(PlayerbotAI* /*botAI*/)
    {
        return new ActionNode(
            "piercing howl",
            /*P*/ {},
            /*A*/ { NextAction("hamstring" )},
            /*C*/ {}
        );
    }

    static ActionNode* pummel(PlayerbotAI* /*botAI*/)
    {
        return new ActionNode(
            "pummel",
            /*P*/ {},
            /*A*/ { NextAction("intercept" )},
            /*C*/ {}
        );
    }

    static ActionNode* enraged_regeneration(PlayerbotAI* /*botAI*/)
    {
        return new ActionNode(
            "enraged regeneration",
            /*P*/ {},
            /*A*/ {},
            /*C*/ {}
        );
    }
};

FuryWarriorStrategy::FuryWarriorStrategy(PlayerbotAI* botAI) : GenericWarriorStrategy(botAI)
{
    actionNodeFactories.Add(new FuryWarriorStrategyActionNodeFactory());
}

std::vector<NextAction> FuryWarriorStrategy::getDefaultActions()
{
    // Sunder armor was the filler between bloodthirst and whirlwind, which cost roughly five opening
    // global cooldowns on a debuff that deals no damage. Stacking it is the tank strategy's job; the
    // rage dump belongs here instead.
    return {
        NextAction("bloodthirst", ACTION_DEFAULT + 0.5f),
        NextAction("whirlwind", ACTION_DEFAULT + 0.4f),
        NextAction("heroic strike", ACTION_DEFAULT + 0.3f),
        NextAction("execute", ACTION_DEFAULT + 0.2f),
        NextAction("melee", ACTION_DEFAULT)
    };
}

void FuryWarriorStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    GenericWarriorStrategy::InitTriggers(triggers);

    triggers.push_back(
        new TriggerNode(
            "enemy out of melee",
            {
                NextAction("charge", ACTION_MOVE + 9)
            }
        )
    );
    // Execute only existed as a default action below bloodthirst, whirlwind and sunder armor, so a
    // fury warrior applied an armor debuff to a target at 15% health and reached its finisher only
    // once everything above it was on cooldown. Arms already promotes it the same way.
    triggers.push_back(
        new TriggerNode(
            "target critical health",
            {
                NextAction("execute", ACTION_HIGH + 5)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "berserker stance", {
                NextAction("berserker stance", ACTION_HIGH + 9)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "battle shout",
            {
                NextAction("battle shout", ACTION_HIGH + 8)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "pummel on enemy healer",
            {
                NextAction("pummel on enemy healer", ACTION_INTERRUPT)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "pummel",
            {
                NextAction("pummel", ACTION_INTERRUPT)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "victory rush",
            {
                NextAction("victory rush", ACTION_INTERRUPT)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "bloodthirst",
            {
                NextAction("bloodthirst", ACTION_HIGH + 7)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "whirlwind",
            {
                NextAction("whirlwind", ACTION_HIGH + 6)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "instant slam",
            {
                NextAction("slam", ACTION_HIGH + 5)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "bloodrage",
            {
                NextAction("bloodrage", ACTION_HIGH + 2)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "medium rage available",
            {
                NextAction("heroic strike", ACTION_DEFAULT + 0.1f)
            }
        )
    );

    triggers.push_back(
        new TriggerNode(
            "death wish",
            {
                NextAction("death wish", ACTION_HIGH)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "recklessness",
            {
                NextAction("recklessness", ACTION_HIGH)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "critical health",
            {
                NextAction("enraged regeneration", ACTION_EMERGENCY)
            }
        )
    );

    // Fury had no way to hold a target down: after its one charge or intercept it could neither slow
    // the target nor free itself, and no node named any of the registered intercept triggers, so the
    // healer stun was only ever used by accident when charge failed. Hamstring and the intercept
    // variants below resolve against the snare target and enemy healer values, so they peel the unit
    // the trigger picked rather than whatever the bot happens to be hitting.
    triggers.push_back(
        new TriggerNode(
            "hamstring",
            {
                NextAction("hamstring", ACTION_HIGH + 3)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "intercept on enemy healer",
            {
                NextAction("intercept on enemy healer", ACTION_INTERRUPT + 1)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "intercept on snare target",
            {
                NextAction("intercept on snare target", ACTION_HIGH + 4)
            }
        )
    );
    // Heroic Fury clears the root or snare and resets intercept - the only escape a fury warrior has.
    // Fury had no "pvp " nodes at all: the six that exist live in ArmsWarriorStrategy and
    // TankWarriorStrategy, which fury bots never get.
    triggers.push_back(
        new TriggerNode(
            "pvp physical pressure",
            {
                NextAction("retaliation", ACTION_EMERGENCY + 3)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "pvp physical target",
            {
                NextAction("disarm", ACTION_HIGH + 7)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "pvp burst window",
            {
                NextAction("death wish", ACTION_HIGH + 8),
                NextAction("recklessness", ACTION_HIGH + 7)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "pvp movement controlled",
            {
                NextAction("heroic fury", ACTION_EMERGENCY)
            }
        )
    );
    // Berserker Rage lives here rather than in the shared warrior strategy: it needs Berserker Stance,
    // and a stance change cannot be cast while feared, so for the stance locked specs the prerequisite
    // could never resolve. Arms and protection break fear through the racials strategy instead.
    triggers.push_back(
        new TriggerNode(
            "fear sleep sap",
            {
                NextAction("berserker rage", ACTION_EMERGENCY + 1)
            }
        )
    );
}
