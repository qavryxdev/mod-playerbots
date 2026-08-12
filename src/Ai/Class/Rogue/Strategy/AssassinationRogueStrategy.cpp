
#include "AssassinationRogueStrategy.h"

#include "Playerbots.h"

class AssassinationRogueStrategyActionNodeFactory : public NamedObjectFactory<ActionNode>
{
public:
    AssassinationRogueStrategyActionNodeFactory()
    {
        creators["mutilate"] = &mutilate;
        creators["envenom"] = &envenom;
        creators["backstab"] = &backstab;
        creators["rupture"] = &rupture;
        creators["blind"] = &blind;
        creators["kick"] = &kick;
    }

private:
    static ActionNode* mutilate([[maybe_unused]] PlayerbotAI* botAI)
    {
        return new ActionNode(
            "mutilate",
            /*P*/ {},
            /*A*/ { NextAction("backstab") },
            /*C*/ {}
        );
    }
    static ActionNode* envenom([[maybe_unused]] PlayerbotAI* botAI)
    {
        return new ActionNode(
            "envenom",
            /*P*/ {},
            /*A*/ { NextAction("eviscerate") },
            /*C*/ {}
        );
    }
    static ActionNode* backstab([[maybe_unused]] PlayerbotAI* botAI)
    {
        return new ActionNode(
            "backstab",
            /*P*/ {},
            /*A*/ { NextAction("sinister strike") },
            /*C*/ {}
        );
    }
    static ActionNode* rupture([[maybe_unused]] PlayerbotAI* botAI)
    {
        return new ActionNode(
            "rupture",
            /*P*/ {},
            /*A*/ { NextAction("eviscerate") },
            /*C*/ {}
        );
    }
    static ActionNode* blind([[maybe_unused]] PlayerbotAI* botAI)
    {
        return new ActionNode(
            "blind",
            /*P*/ {},
            /*A*/ {},
            /*C*/ {}
        );
    }
    // Kick is on a 10 second cooldown, so the interrupt has to fall through to the combo point stun
    // and finally to gouge - without this chain these specs had no interrupt at all besides kick.
    static ActionNode* kick([[maybe_unused]] PlayerbotAI* botAI)
    {
        return new ActionNode(
            "kick",
            /*P*/ {},
            /*A*/ {
                NextAction("kidney shot"),
                NextAction("gouge") },
            /*C*/ {}
        );
    }
};

AssassinationRogueStrategy::AssassinationRogueStrategy(PlayerbotAI* ai) : MeleeCombatStrategy(ai)
{
    actionNodeFactories.Add(new AssassinationRogueStrategyActionNodeFactory());
}

std::vector<NextAction> AssassinationRogueStrategy::getDefaultActions()
{
    return {
        NextAction("melee", ACTION_DEFAULT)
    };
}

void AssassinationRogueStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    MeleeCombatStrategy::InitTriggers(triggers);

    triggers.push_back(
        new TriggerNode(
            "high energy available",
            {
                NextAction("garrote", ACTION_HIGH + 7),
                NextAction("ambush", ACTION_HIGH + 6)
            }
        )
    );

    triggers.push_back(
        new TriggerNode(
            "high energy available",
            {
                NextAction("mutilate", ACTION_NORMAL + 3)
            }
        )
    );

    triggers.push_back(
        new TriggerNode(
            "hunger for blood",
            {
                NextAction("hunger for blood", ACTION_HIGH + 6),
            }
        )
    );

    triggers.push_back(
        new TriggerNode(
            "slice and dice",
            {
                NextAction("slice and dice", ACTION_HIGH + 5),
            }
        )
    );

    triggers.push_back(
        new TriggerNode(
            "combo points 4 available",
            {
                // Rupture reports useful only while the bot's own rupture is missing from the target,
                // so it takes the finisher slot once per application and leaves the rest to envenom.
                NextAction("rupture", ACTION_HIGH + 7),
                NextAction("cold blood", ACTION_HIGH + 6),
                NextAction("envenom", ACTION_HIGH + 5)
            }
        )
    );

    triggers.push_back(
        new TriggerNode(
            "target with combo points almost dead",
            {
                NextAction("envenom", ACTION_HIGH + 4)
            }
        )
    );

    triggers.push_back(
        new TriggerNode(
            "expose armor",
            {
                NextAction("expose armor", ACTION_HIGH + 3),
            }
        )
    );

    triggers.push_back(
        new TriggerNode(
            "medium threat",
            {
                NextAction("vanish", ACTION_HIGH),
            }
        )
    );

    triggers.push_back(
        new TriggerNode(
            "low health",
            {
                NextAction("evasion", ACTION_HIGH + 9),
                NextAction("feint", ACTION_HIGH + 8)
            }
        )
    );

    triggers.push_back(
        new TriggerNode(
            "critical health",
            {
                NextAction("cloak of shadows", ACTION_HIGH + 7)
            }
        )
    );

    triggers.push_back(
        new TriggerNode(
            "kick",
            {
                NextAction("kick", ACTION_INTERRUPT + 2),
            }
        )
    );

    triggers.push_back(
        new TriggerNode(
            "kick on enemy healer",
            {
                NextAction("kick on enemy healer", ACTION_INTERRUPT + 1),
            }
        )
    );

    triggers.push_back(
        new TriggerNode(
            "blind",
            {
                NextAction("blind", ACTION_INTERRUPT + 1),
            }
        )
    );

    triggers.push_back(
        new TriggerNode(
            "medium aoe",
            {
                NextAction("fan of knives", ACTION_NORMAL + 5),
            }
        )
    );

    triggers.push_back(
        new TriggerNode(
            "low tank threat",
            {
                NextAction("tricks of the trade on main tank", ACTION_HIGH + 7),
            }
        )
    );

    triggers.push_back(
        new TriggerNode(
            "enemy out of melee",
            {
                NextAction("stealth", ACTION_HIGH + 3),
                NextAction("sprint", ACTION_HIGH + 2),
                NextAction("reach melee", ACTION_HIGH + 1),
            }
        )
    );

    // Assassination and Subtlety are the specs that see the most PvP, but only the combat rotation
    // carried these reactions, so they had no disarm, no reactive defensives and no control opener.
    triggers.push_back(new TriggerNode("pvp physical target",
        { NextAction("dismantle", ACTION_HIGH + 8) }));
    triggers.push_back(new TriggerNode("pvp physical pressure",
        { NextAction("evasion", ACTION_EMERGENCY + 5) }));
    triggers.push_back(new TriggerNode("pvp magic pressure",
        { NextAction("cloak of shadows", ACTION_EMERGENCY + 4) }));
    triggers.push_back(new TriggerNode("pvp control window",
        { NextAction("blind", ACTION_INTERRUPT + 3),
          NextAction("kidney shot", ACTION_INTERRUPT + 2) }));
    // Vanish and the out of melee stealth both leave the bot stealthed while this rotation keeps
    // running; this hands the engine over to the stealthed strategy so the opener is actually used.
    triggers.push_back(new TriggerNode("in stealth",
        { NextAction("check stealth", ACTION_EMERGENCY) }));
}
