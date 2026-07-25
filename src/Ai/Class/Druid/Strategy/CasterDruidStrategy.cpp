/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "CasterDruidStrategy.h"

#include "AiObjectContext.h"
#include "FeralDruidStrategy.h"

class CasterDruidStrategyActionNodeFactory : public NamedObjectFactory<ActionNode>
{
public:
    CasterDruidStrategyActionNodeFactory()
    {
        creators["faerie fire"] = &faerie_fire;
        creators["hibernate"] = &hibernate;
        creators["entangling roots"] = &entangling_roots;
        creators["entangling roots on cc"] = &entangling_roots_on_cc;
        creators["wrath"] = &wrath;
        creators["starfall"] = &starfall;
        creators["insect swarm"] = &insect_swarm;
        creators["moonfire"] = &moonfire;
        creators["starfire"] = &starfire;
        creators["moonkin form"] = &moonkin_form;
    }

private:
    static ActionNode* faerie_fire([[maybe_unused]] PlayerbotAI* botAI)
    {
        return new ActionNode(
            "faerie fire",
            /*P*/ { NextAction("moonkin form") },
            /*A*/ {},
            /*C*/ {}
        );
    }

    static ActionNode* hibernate([[maybe_unused]] PlayerbotAI* botAI)
    {
        return new ActionNode(
            "hibernate",
            /*P*/ { NextAction("moonkin form") },
            /*A*/ { NextAction("entangling roots") },
            /*C*/ {}
        );
    }

    static ActionNode* entangling_roots([[maybe_unused]] PlayerbotAI* botAI)
    {
        return new ActionNode(
            "entangling roots",
            /*P*/ { NextAction("moonkin form") },
            /*A*/ {},
            /*C*/ {}
        );
    }

    static ActionNode* entangling_roots_on_cc([[maybe_unused]] PlayerbotAI* botAI)
    {
        return new ActionNode(
            "entangling roots on cc",
            /*P*/ { NextAction("moonkin form") },
            /*A*/ {},
            /*C*/ {}
        );
    }

    static ActionNode* wrath([[maybe_unused]] PlayerbotAI* botAI)
    {
        return new ActionNode(
            "wrath",
            /*P*/ { NextAction("moonkin form") },
            /*A*/ {},
            /*C*/ {}
        );
    }

    static ActionNode* starfall([[maybe_unused]] PlayerbotAI* botAI)
    {
        return new ActionNode(
            "starfall",
            /*P*/ { NextAction("moonkin form") },
            /*A*/ {},
            /*C*/ {}
        );
    }

    static ActionNode* insect_swarm([[maybe_unused]] PlayerbotAI* botAI)
    {
        return new ActionNode(
            "insect swarm",
            /*P*/ { NextAction("moonkin form") },
            /*A*/ {},
            /*C*/ {}
        );
    }

    static ActionNode* moonfire([[maybe_unused]] PlayerbotAI* botAI)
    {
        return new ActionNode(
            "moonfire",
            /*P*/ { NextAction("moonkin form") },
            /*A*/ {},
            /*C*/ {}
        );
    }

    static ActionNode* starfire([[maybe_unused]] PlayerbotAI* botAI)
    {
        return new ActionNode(
            "starfire",
            /*P*/ { NextAction("moonkin form") },
            /*A*/ {},
            /*C*/ {}
        );
    }

    static ActionNode* moonkin_form([[maybe_unused]] PlayerbotAI* botAI)
    {
        return new ActionNode(
            "moonkin form",
            /*P*/ { NextAction("caster form") },
            /*A*/ {},
            /*C*/ {}
        );
    }
};

CasterDruidStrategy::CasterDruidStrategy(PlayerbotAI* botAI) : GenericDruidStrategy(botAI)
{
    actionNodeFactories.Add(new CasterDruidStrategyActionNodeFactory());
    actionNodeFactories.Add(new ShapeshiftDruidStrategyActionNodeFactory());
}

std::vector<NextAction> CasterDruidStrategy::getDefaultActions()
{
    return {
        NextAction("starfall", ACTION_HIGH + 1.0f),
        NextAction("force of nature", ACTION_DEFAULT + 1.0f),
        NextAction("wrath", ACTION_DEFAULT + 0.1f),
    };
}

void CasterDruidStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    GenericDruidStrategy::InitTriggers(triggers);

    triggers.push_back(
        new TriggerNode(
            "pvp melee attacker close",
            {
                NextAction("typhoon", ACTION_EMERGENCY + 5),
                NextAction("nature's grasp", ACTION_EMERGENCY + 4),
                NextAction("moonfire", ACTION_MOVE + 11),
                NextAction("insect swarm", ACTION_MOVE + 10)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "pvp high pressure",
            {
                NextAction("rejuvenation", ACTION_EMERGENCY + 3)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "low health",
            {
                NextAction("regrowth", ACTION_HIGH + 8)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "pvp burst window",
            {
                NextAction("starfall", ACTION_HIGH + 8),
                NextAction("force of nature", ACTION_HIGH + 7)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "pvp faerie fire",
            {
                NextAction("faerie fire", ACTION_HIGH + 2)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "pvp physical target",
            {
                NextAction("insect swarm", ACTION_HIGH + 1)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "pvp caster target",
            {
                NextAction("moonfire", ACTION_HIGH + 1)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "pvp insect swarm",
            {
                NextAction("insect swarm", ACTION_HIGH)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "pvp moonfire",
            {
                NextAction("moonfire", ACTION_HIGH - 1)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "eclipse (lunar) cooldown",
            {
                NextAction("starfire", ACTION_DEFAULT + 0.2f)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "eclipse (solar) cooldown",
            {
                NextAction("wrath", ACTION_DEFAULT + 0.2f)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "insect swarm",
            {
                NextAction("insect swarm", ACTION_NORMAL + 5)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "moonfire",
            {
                NextAction("moonfire", ACTION_NORMAL + 4)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "eclipse (solar)",
            {
                NextAction("wrath", ACTION_HIGH + 4)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "eclipse (lunar)",
            {
                NextAction("starfire", ACTION_HIGH + 4)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "medium mana",
            {
                NextAction("innervate", ACTION_HIGH + 9)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "enemy too close for spell",
            {
                NextAction("flee", ACTION_MOVE + 9)
            }
        )
    );
}

void CasterDruidAoeStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(
        new TriggerNode(
            "hurricane channel check",
            {
                NextAction("cancel channel", ACTION_HIGH + 2)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "medium aoe",
            {
                NextAction("hurricane", ACTION_HIGH + 1)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "light aoe",
            {
                NextAction("insect swarm on attacker", ACTION_NORMAL + 3),
                NextAction("moonfire on attacker", ACTION_NORMAL + 3)
            }
        )
    );
}

void CasterDruidDebuffStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(
        new TriggerNode(
            "faerie fire",
            {
                NextAction("faerie fire", ACTION_HIGH)
            }
        )
    );
}
