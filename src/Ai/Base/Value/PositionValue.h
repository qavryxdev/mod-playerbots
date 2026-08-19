/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_POSITIONVALUE_H
#define _PLAYERBOT_POSITIONVALUE_H

#include "NamedObjectContext.h"
#include "TravelMgr.h"
#include "Value.h"

class PlayerbotAI;

class PositionInfo
{
public:
    PositionInfo() : x(0), y(0), z(0), mapId(0), valueSet(false), rally(false) {}
    PositionInfo(float x, float y, float z, uint32 mapId, bool valueSet = true)
        : x(x), y(y), z(z), mapId(mapId), valueSet(valueSet), rally(false)
    {
    }
    PositionInfo(PositionInfo const& other)
        : x(other.x), y(other.y), z(other.z), mapId(other.mapId), valueSet(other.valueSet), rally(other.rally)
    {
    }
    PositionInfo& operator=(PositionInfo const& other) = default;

    void Set(float newX, float newY, float newZ, uint32 newMapId)
    {
        x = newX;
        y = newY;
        z = newZ;
        mapId = newMapId;
        valueSet = true;
        rally = false;
    }

    // A rally is somewhere to stand while there is no real work, not an objective. Systems that treat
    // "the bot holds an objective" as a reason to change its combat behaviour must ignore these, or a
    // bot parked on a staging point stops defending itself and everything around it.
    void SetRally(float newX, float newY, float newZ, uint32 newMapId)
    {
        Set(newX, newY, newZ, newMapId);
        rally = true;
    }

    void Reset()
    {
        valueSet = false;
        rally = false;
    }

    bool isSet() const { return valueSet; }
    bool isRally() const { return valueSet && rally; }

    float x;
    float y;
    float z;
    uint32 mapId;
    bool valueSet;
    bool rally;
};

typedef std::map<std::string, PositionInfo> PositionMap;

class PositionValue : public ManualSetValue<PositionMap&>
{
public:
    PositionValue(PlayerbotAI* botAI, std::string const name = "position");

    std::string const Save() override;
    bool Load(std::string const value) override;

private:
    PositionMap positions;
};

class CurrentPositionValue : public LogCalculatedValue<WorldPosition>
{
public:
    CurrentPositionValue(PlayerbotAI* botAI, std::string const name = "current position", uint32 checkInterval = 1)
        : LogCalculatedValue<WorldPosition>(botAI, name, checkInterval)
    {
        minChangeInterval = 60;
        logLength = 30;
    };

    bool EqualToLast(WorldPosition value) override
    {
        return value.fDist(lastValue) < sPlayerbotAIConfig.tooCloseDistance;
    }

    WorldPosition Calculate() override;
};

class SinglePositionValue : public CalculatedValue<PositionInfo>, public Qualified
{
public:
    SinglePositionValue(PlayerbotAI* ai, std::string name = "pos") : CalculatedValue(ai, name), Qualified() {};
    virtual PositionInfo Calculate() override;
    virtual void Set(PositionInfo value) override;
    virtual void Reset() override;
};

#endif
