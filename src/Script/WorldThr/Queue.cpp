/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Queue.h"
#include "AiObjectContext.h"
#include "Log.h"
#include "PlayerbotAIConfig.h"

void Queue::Push(ActionBasket* action)
{
    if (!action)
    {
        return;
    }

    ActionNode* actionNode = action->getAction();
    if (!actionNode)
    {
        delete action;
        return;
    }

    std::string const& actionName = actionNode->getName();

    for (ActionBasket* basket : actions)
    {
        if (basket && basket->getAction() && actionName == basket->getAction()->getName())
        {
            updateExistingBasket(basket, action);
            return;
        }
    }

    actions.push_back(action);
}

bool Queue::Update(std::string const& actionName, float relevance)
{
    uint32 const now = getMSTime();

    for (ActionBasket* basket : actions)
    {
        if (!basket || !basket->getAction() || basket->getAction()->getName() != actionName)
            continue;

        if (basket->getRelevance() < relevance)
            basket->setRelevance(relevance);

        basket->Refresh(now);
        return true;
    }

    return false;
}

ActionNode* Queue::Pop()
{
    if (actions.empty())
        return nullptr;

    float maxRelevance = -1.0f;
    std::list<ActionBasket*>::iterator selection = actions.end();

    for (std::list<ActionBasket*>::iterator itr = actions.begin(); itr != actions.end(); ++itr)
    {
        ActionBasket* basket = *itr;
        if (!basket)
            continue;

        if (basket->getRelevance() > maxRelevance)
        {
            maxRelevance = basket->getRelevance();
            selection = itr;
        }
    }

    if (selection == actions.end())
        return nullptr;

    ActionBasket* basket = *selection;
    ActionNode* action = basket->getAction();
    actions.erase(selection);
    delete basket;
    return action;
}

ActionBasket* Queue::Peek()
{
    return findHighestRelevanceBasket();
}

uint32 Queue::Size()
{
    return actions.size();
}

void Queue::RemoveExpired()
{
    if (!sPlayerbotAIConfig.expireActionTime)
        return;

    uint32 const now = getMSTime();
    uint32 const expiryTime = sPlayerbotAIConfig.expireActionTime;

    for (std::list<ActionBasket*>::iterator itr = actions.begin(); itr != actions.end();)
    {
        ActionBasket* basket = *itr;
        if (!basket || basket->isExpired(expiryTime, now))
        {
            itr = actions.erase(itr);

            if (basket)
            {
                if (ActionNode* action = basket->getAction())
                    delete action;

                delete basket;
            }

            continue;
        }

        ++itr;
    }
}

// Private helper methods
void Queue::updateExistingBasket(ActionBasket* existing, ActionBasket* newBasket)
{
    uint32 const now = getMSTime();

    if (existing->getRelevance() < newBasket->getRelevance())
    {
        existing->setRelevance(newBasket->getRelevance());
    }
    existing->Refresh(now);

    if (ActionNode* actionNode = newBasket->getAction())
    {
        delete actionNode;
    }

    delete newBasket;
}

ActionBasket* Queue::findHighestRelevanceBasket() const
{
    if (actions.empty())
    {
        return nullptr;
    }

    float maxRelevance = -1.0f;
    ActionBasket* selection = nullptr;

    for (ActionBasket* basket : actions)
    {
        if (!basket)
        {
            continue;
        }

        if (basket->getRelevance() > maxRelevance)
        {
            maxRelevance = basket->getRelevance();
            selection = basket;
        }
    }

    return selection;
}
