/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "BattleGroundTactics.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cmath>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "ArenaTeam.h"
#include "ArenaTeamMgr.h"
#include "BattleGroundJoinAction.h"
#include "Battleground.h"
#include "BattlegroundAB.h"
#include "BattlegroundAV.h"
#include "BattlegroundBE.h"
#include "BattlegroundDS.h"
#include "BattlegroundEY.h"
#include "BattlegroundIC.h"
#include "BattlegroundMgr.h"
#include "BattlegroundNA.h"
#include "BattlegroundRL.h"
#include "BattlegroundRV.h"
#include "BattlegroundSA.h"
#include "BattlegroundWS.h"
#include "Event.h"
#include "GameObject.h"
#include "IVMapMgr.h"
#include "PathGenerator.h"
#include "Playerbots.h"
#include "PositionValue.h"
#include "PvpTriggers.h"
#include "PvpTactics.h"
#include "ServerFacade.h"
#include "Vehicle.h"

// common bg positions
Position const WS_WAITING_POS_HORDE_1 = {944.981f, 1423.478f, 345.434f, 6.18f};
Position const WS_WAITING_POS_HORDE_2 = {948.488f, 1459.834f, 343.066f, 6.27f};
Position const WS_WAITING_POS_HORDE_3 = {933.484f, 1433.726f, 345.535f, 0.08f};
Position const WS_WAITING_POS_ALLIANCE_1 = {1510.502f, 1493.385f, 351.995f, 3.1f};
Position const WS_WAITING_POS_ALLIANCE_2 = {1496.578f, 1457.900f, 344.442f, 3.1f};
Position const WS_WAITING_POS_ALLIANCE_3 = {1521.235f, 1480.951f, 352.007f, 3.2f};
Position const WS_FLAG_POS_HORDE = {915.958f, 1433.925f, 346.193f, 0.0f};
Position const WS_FLAG_POS_ALLIANCE = {1539.219f, 1481.747f, 352.458f, 0.0f};
Position const WS_FLAG_HIDE_HORDE_1 = {926.142f, 1460.641f, 346.116f, 4.84f};
Position const WS_FLAG_HIDE_HORDE_2 = {925.166f, 1458.084f, 355.966f, 0.00f};
Position const WS_FLAG_HIDE_HORDE_3 = {924.922f, 1423.672f, 345.524f, 0.82f};
Position const WS_FLAG_HIDE_ALLIANCE_1 = {1529.249f, 1456.470f, 353.04f, 1.25f};
Position const WS_FLAG_HIDE_ALLIANCE_2 = {1540.286f, 1476.026f, 352.692f, 2.91f};
Position const WS_FLAG_HIDE_ALLIANCE_3 = {1495.807f, 1466.774f, 352.350f, 1.50f};
Position const WS_ROAM_POS = {1227.446f, 1476.235f, 307.484f, 1.50f};
Position const WS_GY_CAMPING_HORDE = {1039.819, 1388.759f, 340.703f, 0.0f};
Position const WS_GY_CAMPING_ALLIANCE = {1422.320f, 1551.978f, 342.834f, 0.0f};
std::vector<Position> const WS_FLAG_HIDE_HORDE = {WS_FLAG_HIDE_HORDE_1, WS_FLAG_HIDE_HORDE_2, WS_FLAG_HIDE_HORDE_3};
std::vector<Position> const WS_FLAG_HIDE_ALLIANCE = {WS_FLAG_HIDE_ALLIANCE_1, WS_FLAG_HIDE_ALLIANCE_2,
                                                     WS_FLAG_HIDE_ALLIANCE_3};

Position const AB_WAITING_POS_HORDE = {702.884f, 703.045f, -16.115f, 0.77f};
Position const AB_WAITING_POS_ALLIANCE = {1286.054f, 1282.500f, -15.697f, 3.95f};
Position const AB_GY_CAMPING_HORDE = {723.513f, 725.924f, -28.265f, 3.99f};
Position const AB_GY_CAMPING_ALLIANCE = {1262.627f, 1256.341f, -27.289f, 0.64f};

// the captains aren't the actual creatures but invisible trigger creatures - they still have correct death state and
// location (unless they move)
uint32 const AV_CREATURE_A_CAPTAIN = AV_CPLACE_TRIGGER16;
uint32 const AV_CREATURE_A_BOSS = AV_CPLACE_A_BOSS;
uint32 const AV_CREATURE_H_CAPTAIN = AV_CPLACE_TRIGGER18;
uint32 const AV_CREATURE_H_BOSS = AV_CPLACE_H_BOSS;
uint32 const AV_NPC_DREKTHAR_ENTRY = 11946;

Position const AV_CAVE_SPAWN_ALLIANCE = {872.460f, -491.571f, 96.546f, 0.0f};
Position const AV_CAVE_SPAWN_HORDE = {-1437.127f, -608.382f, 51.185f, 0.0f};
Position const AV_SNOWFALL_RESPAWN_ALLIANCE = {-157.400f, 31.200f, 77.100f, 0.0f};
Position const AV_WAITING_POS_ALLIANCE = {793.627f, -493.814f, 99.689f, 3.09f};
Position const AV_WAITING_POS_HORDE = {-1381.865f, -544.872f, 54.773f, 0.76f};
Position const AV_ICEBLOOD_GARRISON_WAITING_ALLIANCE = {-523.105f, -182.178f, 57.956f, 0.0f};
Position const AV_ICEBLOOD_GARRISON_ATTACKING_ALLIANCE = {-545.288f, -167.932f, 57.012f, 0.0f};
Position const AV_STONEHEARTH_WAITING_HORDE = {-36.399f, -306.403f, 15.565f, 0.0f};
Position const AV_STONEHEARTH_ATTACKING_HORDE = {-55.210f, -288.546f, 15.578f, 0.0f};
Position const AV_BOSS_WAIT_H = {689.210f, -20.173f, 50.620f, 0.0f};
Position const AV_BOSS_WAIT_A = {-1361.773f, -248.977f, 99.369f, 0.0f};
Position const AV_MINE_SOUTH_1 = {-860.159f, -12.780f, 70.817f, 0.0f};
Position const AV_MINE_SOUTH_2 = {-827.639f, -147.314f, 62.565f, 0.0f};
Position const AV_MINE_SOUTH_3 = {-920.228f, -134.594f, 61.478f, 0.0f};
Position const AV_MINE_NORTH_1 = {822.477f, -456.782f, 48.569f, 0.0f};
Position const AV_MINE_NORTH_2 = {966.362f, -446.570f, 56.641f, 0.0f};
Position const AV_MINE_NORTH_3 = {952.081f, -335.073f, 63.579f, 0.0f};
Position const AV_ALLIANCE_ANTIRUSH_STONEHEARTH_ROAD = {117.990f, -380.243f, 43.571f, 0.0f};
Position const AV_ALLIANCE_ANTIRUSH_ICEWING_ROAD = {195.369f, -407.750f, 42.876f, 0.0f};
Position const AV_ALLIANCE_ANTIRUSH_STORMPIKE_ROAD = {444.974f, -429.032f, 27.276f, 0.0f};
Position const AV_ALLIANCE_ANTIRUSH_DUNBALDAR_ROAD = {635.941f, -272.053f, 30.130f, 0.0f};
Position const AV_ALLIANCE_IBGY_FLAG_HOLD = {-617.858f, -400.654f, 59.692f, 0.0f};
Position const AV_ALLIANCE_IBGY_GALVANGAR_INTERCEPT = {-556.000f, -315.000f, 63.000f, 0.0f};
Position const AV_ALLIANCE_IBGY_TOWER_POINT_INTERCEPT = {-690.000f, -374.000f, 72.000f, 0.0f};
Position const AV_ALLIANCE_IBGY_SOUTH_ROAD_INTERCEPT = {-650.000f, -450.000f, 59.000f, 0.0f};
Position const AV_ALLIANCE_IBGY_NORTH_LOCKDOWN = {-590.000f, -354.000f, 60.000f, 0.0f};
Position const AV_ALLIANCE_IBGY_SOUTH_LOCKDOWN = {-644.000f, -430.000f, 59.000f, 0.0f};
Position const EY_WAITING_POS_HORDE = {1809.102f, 1540.854f, 1267.142f, 0.0f};
Position const EY_WAITING_POS_ALLIANCE = {2523.827f, 1596.915f, 1270.204f, 0.0f};
Position const EY_FLAG_RETURN_POS_RETREAT_HORDE = {1885.529f, 1532.157f, 1200.635f, 0.0f};
Position const EY_FLAG_RETURN_POS_RETREAT_ALLIANCE = {2452.253f, 1602.356f, 1203.617f, 0.0f};
Position const EY_GY_CAMPING_HORDE = {1874.854f, 1530.405f, 1207.432f, 0.0f};
Position const EY_GY_CAMPING_ALLIANCE = {2456.887f, 1599.025f, 1206.280f, 0.0f};

Position const IC_WAITING_POS_HORDE = {1166.322f, -762.402f, 48.628f};
Position const IC_WEST_WAITING_POS_HORDE = {1217.666f, -685.449f, 48.915f};
Position const IC_EAST_WAITING_POS_HORDE = {1219.068f, -838.791f, 48.916f};

Position const IC_WAITING_POS_ALLIANCE = {387.893f, -833.384f, 48.714f};
Position const IC_WEST_WAITING_POS_ALLIANCE = {352.129f, -788.029f, 48.916f};
Position const IC_EAST_WAITING_POS_ALLIANCE = {351.517f, -882.477f, 48.916f};

Position const IC_CANNON_POS_HORDE1 = {1140.938f, -838.685f, 88.124f, 2.30f};
Position const IC_CANNON_POS_HORDE2 = {1139.695f, -686.574f, 88.173f, 3.95f};
Position const IC_CANNON_POS_ALLIANCE1 = {424.860f, -855.795f, 87.96f, 0.44f};
Position const IC_CANNON_POS_ALLIANCE2 = {425.525f, -779.538f, 87.717f, 5.88f};

Position const IC_GATE_ATTACK_POS_HORDE = {506.782f, -828.594f, 24.313f, 0.0f};
Position const IC_GATE_ATTACK_POS_ALLIANCE = {1091.273f, -763.619f, 42.352f, 0.0f};

enum BattleBotWsgWaitSpot
{
    BB_WSG_WAIT_SPOT_SPAWN,
    BB_WSG_WAIT_SPOT_LEFT,
    BB_WSG_WAIT_SPOT_RIGHT
};

std::unordered_map<uint32, BGStrategyData> bgStrategies;
static std::unordered_map<uint32, uint32> avAllianceDebugLogTimers;
static std::unordered_map<uint64, uint32> avAllianceMoveDebugLogTimers;

struct AllianceAVModeState
{
    uint8 mode = 0;
    uint32 sinceMs = 0;
};

static std::unordered_map<uint32, AllianceAVModeState> avAllianceModeStates;

struct AllianceAVIcebloodBeachheadState
{
    bool allianceOwned = false;
    uint32 sinceMs = 0;
    uint32 controlledSinceMs = 0;
};

static std::unordered_map<uint32, AllianceAVIcebloodBeachheadState> avAllianceIcebloodBeachheadStates;

struct AllianceAVObjectiveStallState
{
    float botX = 0.0f;
    float botY = 0.0f;
    float objectiveX = 0.0f;
    float objectiveY = 0.0f;
    uint32 sinceMs = 0;
};

static std::unordered_map<uint64, AllianceAVObjectiveStallState> avAllianceObjectiveStallStates;
static std::mutex avAllianceObjectiveStallStatesMutex;

static bool IsInvalidBattleGroundZ(float z)
{
    return z == VMAP_INVALID_HEIGHT_VALUE || z <= -99999.0f;
}

static void ResolveBattleGroundGroundZ(Player* bot, float x, float y, float& z)
{
    if (!bot)
        return;

    if (IsInvalidBattleGroundZ(z))
        z = bot->GetPositionZ();

    if (Map* map = bot->GetMap())
    {
        float const groundZ = map->GetHeight(x, y, z);
        if (!IsInvalidBattleGroundZ(groundZ))
            z = groundZ;
    }

    if (IsInvalidBattleGroundZ(z))
        z = bot->GetPositionZ();
}

std::vector<uint32> const vFlagsAV = {
    BG_AV_OBJECTID_BANNER_H_B,      BG_AV_OBJECTID_BANNER_H,      BG_AV_OBJECTID_BANNER_A_B,
    BG_AV_OBJECTID_BANNER_A,        BG_AV_OBJECTID_BANNER_CONT_A, BG_AV_OBJECTID_BANNER_CONT_A_B,
    BG_AV_OBJECTID_BANNER_CONT_H_B, BG_AV_OBJECTID_BANNER_CONT_H, BG_AV_OBJECTID_BANNER_SNOWFALL_N};

std::vector<uint32> const vFlagsAB = {
    BG_AB_OBJECTID_BANNER_A,
    BG_AB_OBJECTID_BANNER_CONT_A,
    BG_AB_OBJECTID_BANNER_H,
    BG_AB_OBJECTID_BANNER_CONT_H,
    BG_AB_OBJECTID_NODE_BANNER_0,
    BG_AB_OBJECTID_NODE_BANNER_1,
    BG_AB_OBJECTID_NODE_BANNER_2,
    BG_AB_OBJECTID_NODE_BANNER_3,
    BG_AB_OBJECTID_NODE_BANNER_4
};

std::vector<uint32> const vFlagsWS = {BG_OBJECT_A_FLAG_WS_ENTRY, BG_OBJECT_H_FLAG_WS_ENTRY,
                                      BG_OBJECT_A_FLAG_GROUND_WS_ENTRY, BG_OBJECT_H_FLAG_GROUND_WS_ENTRY};

std::vector<uint32> const vFlagsEY = {BG_OBJECT_FLAG2_EY_ENTRY, BG_OBJECT_FLAG3_EY_ENTRY};

std::vector<uint32> const vFlagsIC = {GO_HORDE_BANNER,
                                      GO_ALLIANCE_BANNER,
                                      GO_WORKSHOP_BANNER,
                                      GO_DOCKS_BANNER,
                                      GO_HANGAR_BANNER,
                                      GO_QUARRY_BANNER,
                                      GO_REFINERY_BANNER,
                                      GO_ALLIANCE_BANNER_DOCK,
                                      GO_ALLIANCE_BANNER_DOCK_CONT,
                                      GO_HORDE_BANNER_DOCK,
                                      GO_HORDE_BANNER_DOCK_CONT,
                                      GO_HORDE_BANNER_HANGAR,
                                      GO_HORDE_BANNER_HANGAR_CONT,
                                      GO_ALLIANCE_BANNER_HANGAR,
                                      GO_ALLIANCE_BANNER_HANGAR_CONT,
                                      GO_ALLIANCE_BANNER_QUARRY,
                                      GO_ALLIANCE_BANNER_QUARRY_CONT,
                                      GO_HORDE_BANNER_QUARRY,
                                      GO_HORDE_BANNER_QUARRY_CONT,
                                      GO_ALLIANCE_BANNER_REFINERY,
                                      GO_ALLIANCE_BANNER_REFINERY_CONT,
                                      GO_HORDE_BANNER_REFINERY,
                                      GO_HORDE_BANNER_REFINERY_CONT,
                                      GO_ALLIANCE_BANNER_WORKSHOP,
                                      GO_ALLIANCE_BANNER_WORKSHOP_CONT,
                                      GO_HORDE_BANNER_WORKSHOP,
                                      GO_HORDE_BANNER_WORKSHOP_CONT,
                                      GO_ALLIANCE_BANNER_GRAVEYARD_A,
                                      GO_ALLIANCE_BANNER_GRAVEYARD_A_CONT,
                                      GO_HORDE_BANNER_GRAVEYARD_A,
                                      GO_HORDE_BANNER_GRAVEYARD_A_CONT,
                                      GO_ALLIANCE_BANNER_GRAVEYARD_H,
                                      GO_ALLIANCE_BANNER_GRAVEYARD_H_CONT,
                                      GO_HORDE_BANNER_GRAVEYARD_H,
                                      GO_HORDE_BANNER_GRAVEYARD_H_CONT};

// BG Waypoints (vmangos)

// Horde Flag Room to Horde Graveyard
BattleBotPath vPath_WSG_HordeFlagRoom_to_HordeGraveyard = {
    {933.331f, 1433.72f, 345.536f, nullptr}, {944.859f, 1423.05f, 345.437f, nullptr},
    {966.691f, 1422.53f, 345.223f, nullptr}, {979.588f, 1422.84f, 345.46f, nullptr},
    {997.806f, 1422.52f, 344.623f, nullptr}, {1008.53f, 1417.02f, 343.206f, nullptr},
    {1016.42f, 1402.33f, 341.352f, nullptr}, {1029.14f, 1387.49f, 340.836f, nullptr},
};

// Horde Graveyard to Horde Tunnel
BattleBotPath vPath_WSG_HordeGraveyard_to_HordeTunnel = {
    {1029.14f, 1387.49f, 340.836f, nullptr}, {1034.95f, 1392.62f, 340.856f, nullptr},
    {1038.21f, 1406.43f, 341.562f, nullptr}, {1043.87f, 1426.9f, 339.197f, nullptr},
    {1054.53f, 1441.47f, 339.725f, nullptr}, {1056.33f, 1456.03f, 341.463f, nullptr},
    {1057.39f, 1469.98f, 342.148f, nullptr}, {1057.67f, 1487.55f, 342.537f, nullptr},
    {1048.7f, 1505.37f, 341.117f, nullptr},  {1042.19f, 1521.69f, 338.003f, nullptr},
    {1050.01f, 1538.22f, 332.43f, nullptr},  {1068.15f, 1548.1f, 321.446f, nullptr},
    {1088.14f, 1538.45f, 316.398f, nullptr}, {1101.26f, 1522.79f, 314.918f, nullptr},
    {1114.67f, 1503.18f, 312.947f, nullptr}, {1126.45f, 1487.4f, 314.136f, nullptr},
    {1124.37f, 1462.28f, 315.853f, nullptr},
};

// Horde Tunnel to Horde Flag Room
BattleBotPath vPath_WSG_HordeTunnel_to_HordeFlagRoom = {
    {1124.37f, 1462.28f, 315.853f, nullptr}, {1106.87f, 1462.13f, 316.558f, nullptr},
    {1089.44f, 1461.04f, 316.332f, nullptr}, {1072.07f, 1459.46f, 317.449f, nullptr},
    {1051.09f, 1459.89f, 323.126f, nullptr}, {1030.1f, 1459.58f, 330.204f, nullptr},
    {1010.76f, 1457.49f, 334.896f, nullptr}, {1005.47f, 1448.19f, 335.864f, nullptr},
    {999.974f, 1458.49f, 335.632f, nullptr}, {982.632f, 1459.18f, 336.127f, nullptr},
    {965.049f, 1459.15f, 338.076f, nullptr}, {944.526f, 1459.0f, 344.207f, nullptr},
    {937.479f, 1451.12f, 345.553f, nullptr}, {933.331f, 1433.72f, 345.536f, nullptr},
};

// Alliance Flag Room to Alliance Graveyard
BattleBotPath vPath_WSG_AllianceFlagRoom_to_AllianceGraveyard = {
    {1519.53f, 1481.87f, 352.024f, nullptr}, {1508.27f, 1493.17f, 352.005f, nullptr},
    {1490.78f, 1493.51f, 352.141f, nullptr}, {1469.79f, 1494.13f, 351.774f, nullptr},
    {1453.65f, 1494.39f, 350.614f, nullptr}, {1443.51f, 1501.75f, 348.317f, nullptr},
    {1443.33f, 1517.78f, 345.534f, nullptr}, {1443.55f, 1533.4f, 343.148f, nullptr},
    {1441.47f, 1548.12f, 342.752f, nullptr}, {1433.79f, 1552.67f, 342.763f, nullptr},
    {1422.88f, 1552.37f, 342.751f, nullptr}, {1415.33f, 1554.79f, 343.156f, nullptr},
};

// Alliance Graveyard to Alliance Tunnel
BattleBotPath vPath_WSG_AllianceGraveyard_to_AllianceTunnel = {
    {1415.33f, 1554.79f, 343.156f, nullptr}, {1428.29f, 1551.79f, 342.751f, nullptr},
    {1441.51f, 1545.79f, 342.757f, nullptr}, {1441.15f, 1530.35f, 343.712f, nullptr},
    {1435.53f, 1517.29f, 346.698f, nullptr}, {1424.81f, 1499.24f, 349.486f, nullptr},
    {1416.31f, 1483.94f, 348.536f, nullptr}, {1408.83f, 1468.4f, 347.648f, nullptr},
    {1404.64f, 1449.79f, 347.279f, nullptr}, {1405.34f, 1432.33f, 345.792f, nullptr},
    {1406.38f, 1416.18f, 344.755f, nullptr}, {1400.22f, 1401.87f, 340.496f, nullptr},
    {1385.96f, 1394.15f, 333.829f, nullptr}, {1372.38f, 1390.75f, 328.722f, nullptr},
    {1362.93f, 1390.02f, 327.034f, nullptr}, {1357.91f, 1398.07f, 325.674f, nullptr},
    {1354.17f, 1411.56f, 324.327f, nullptr}, {1351.44f, 1430.38f, 323.506f, nullptr},
    {1350.36f, 1444.43f, 323.388f, nullptr}, {1348.02f, 1461.06f, 323.167f, nullptr},
};

// Alliance Tunnel to Alliance Flag Room
BattleBotPath vPath_WSG_AllianceTunnel_to_AllianceFlagRoom = {
    {1348.02f, 1461.06f, 323.167f, nullptr}, {1359.8f, 1461.49f, 324.527f, nullptr},
    {1372.47f, 1461.61f, 324.354f, nullptr}, {1389.08f, 1461.12f, 325.913f, nullptr},
    {1406.57f, 1460.48f, 330.615f, nullptr}, {1424.04f, 1459.57f, 336.029f, nullptr},
    {1442.5f, 1459.7f, 342.024f, nullptr},   {1449.59f, 1469.14f, 342.65f, nullptr},
    {1458.03f, 1458.43f, 342.746f, nullptr}, {1469.4f, 1458.14f, 342.794f, nullptr},
    {1489.06f, 1457.86f, 342.794f, nullptr}, {1502.27f, 1457.52f, 347.589f, nullptr},
    {1512.87f, 1457.81f, 352.039f, nullptr}, {1517.53f, 1468.79f, 352.033f, nullptr},
    {1519.53f, 1481.87f, 352.024f, nullptr},
};

// Horde Tunnel to Horde Base Roof
BattleBotPath vPath_WSG_HordeTunnel_to_HordeBaseRoof = {
    {1124.37f, 1462.28f, 315.853f, nullptr}, {1106.87f, 1462.13f, 316.558f, nullptr},
    {1089.44f, 1461.04f, 316.332f, nullptr}, {1072.07f, 1459.46f, 317.449f, nullptr},
    {1051.09f, 1459.89f, 323.126f, nullptr}, {1030.1f, 1459.58f, 330.204f, nullptr},
    {1010.76f, 1457.49f, 334.896f, nullptr}, {981.948f, 1459.07f, 336.154f, nullptr},
    {981.768f, 1480.46f, 335.976f, nullptr}, {974.664f, 1495.9f, 340.837f, nullptr},
    {964.661f, 1510.21f, 347.509f, nullptr}, {951.188f, 1520.99f, 356.377f, nullptr},
    {937.37f, 1513.27f, 362.589f, nullptr},  {935.947f, 1499.58f, 364.199f, nullptr},
    {935.9f, 1482.08f, 366.396f, nullptr},   {937.564f, 1462.81f, 367.287f, nullptr},
    {945.871f, 1458.65f, 367.287f, nullptr}, {956.972f, 1459.48f, 367.291f, nullptr},
    {968.317f, 1459.71f, 367.291f, nullptr}, {979.934f, 1454.58f, 367.078f, nullptr},
    {979.99f, 1442.87f, 367.093f, nullptr},  {978.632f, 1430.71f, 367.125f, nullptr},
    {970.395f, 1422.32f, 367.289f, nullptr}, {956.338f, 1425.09f, 367.293f, nullptr},
    {952.778f, 1433.0f, 367.604f, nullptr},  {952.708f, 1445.01f, 367.604f, nullptr},
};

// Alliance Tunnel to Alliance Base Roof
BattleBotPath vPath_WSG_AllianceTunnel_to_AllianceBaseRoof = {
    {1348.02f, 1461.06f, 323.167f, nullptr}, {1359.8f, 1461.49f, 324.527f, nullptr},
    {1372.47f, 1461.61f, 324.354f, nullptr}, {1389.08f, 1461.12f, 325.913f, nullptr},
    {1406.57f, 1460.48f, 330.615f, nullptr}, {1424.04f, 1459.57f, 336.029f, nullptr},
    {1442.5f, 1459.7f, 342.024f, nullptr},   {1471.86f, 1456.65f, 342.794f, nullptr},
    {1470.93f, 1440.5f, 342.794f, nullptr},  {1472.24f, 1427.49f, 342.06f, nullptr},
    {1476.86f, 1412.46f, 341.426f, nullptr}, {1484.42f, 1396.69f, 346.117f, nullptr},
    {1490.7f, 1387.59f, 351.861f, nullptr},  {1500.79f, 1382.98f, 357.784f, nullptr},
    {1511.08f, 1391.29f, 364.444f, nullptr}, {1517.85f, 1403.18f, 370.336f, nullptr},
    {1517.99f, 1417.59f, 371.636f, nullptr}, {1517.07f, 1431.56f, 372.106f, nullptr},
    {1516.66f, 1445.55f, 372.939f, nullptr}, {1514.23f, 1457.37f, 373.689f, nullptr},
    {1503.73f, 1457.67f, 373.684f, nullptr}, {1486.24f, 1457.8f, 373.718f, nullptr},
    {1476.78f, 1460.35f, 373.711f, nullptr}, {1477.37f, 1470.83f, 373.709f, nullptr},
    {1477.5f, 1484.83f, 373.715f, nullptr},  {1480.53f, 1495.26f, 373.721f, nullptr},
    {1492.61f, 1494.72f, 373.721f, nullptr}, {1499.37f, 1489.02f, 373.718f, nullptr},
    {1500.63f, 1472.89f, 373.707f, nullptr},
};

// Alliance Tunnel to Horde Tunnel
BattleBotPath vPath_WSG_AllianceTunnel_to_HordeTunnel = {
    {1129.30f, 1461.03f, 315.206f, nullptr},
    {1149.55f, 1466.57f, 311.031f, nullptr},
    {1196.39f, 1477.25f, 305.697f, nullptr},
    {1227.73f, 1478.38f, 307.418f, nullptr},
    {1267.06f, 1463.40f, 312.227f, nullptr},
    {1303.42f, 1460.18f, 317.257f, nullptr},
};

// Alliance Graveyard (lower) to Horde Flag room
BattleBotPath vPath_WSG_AllianceGraveyardLower_to_HordeFlagRoom = {
    //{1370.71f, 1543.55f, 321.585f, nullptr},
    //{1339.41f, 1533.42f, 313.336f, nullptr},
    {1316.07f, 1533.53f, 315.700f, nullptr},
    {1276.17f, 1533.72f, 311.722f, nullptr},
    {1246.25f, 1533.86f, 307.072f, nullptr},
    {1206.84f, 1528.22f, 307.677f, nullptr},
    {1172.28f, 1523.28f, 301.958f, nullptr},
    {1135.93f, 1505.27f, 308.085f, nullptr},
    {1103.54f, 1521.89f, 314.583f, nullptr},
    {1073.49f, 1551.19f, 319.418f, nullptr},
    {1042.92f, 1530.49f, 336.667f, nullptr},
    {1052.11f, 1493.52f, 342.176f, nullptr},
    {1057.42f, 1452.75f, 341.131f, nullptr},
    {1037.96f, 1422.27f, 339.919f, nullptr},
    {966.01f, 1422.84f, 345.223f, nullptr},
    {942.74f, 1423.10f, 345.467f, nullptr},
    {929.39f, 1434.75f, 345.535f, nullptr},
};

// Horde Graveyard (lower) to Alliance Flag room
BattleBotPath vPath_WSG_HordeGraveyardLower_to_AllianceFlagRoom = {
    //{1096.59f, 1395.07f, 317.016f, nullptr},
    //{1134.38f, 1370.13f, 312.741f, nullptr},
    {1164.96f, 1356.91f, 313.884f, nullptr},
    {1208.32f, 1346.27f, 313.816f, nullptr},
    {1245.06f, 1346.12f, 312.112f, nullptr},
    {1291.43f, 1394.60f, 314.359f, nullptr},
    {1329.33f, 1411.13f, 318.399f, nullptr},
    {1361.57f, 1391.21f, 326.756f, nullptr},
    {1382.19f, 1381.03f, 332.314f, nullptr},
    {1408.06f, 1412.93f, 344.565f, nullptr},
    {1407.88f, 1458.76f, 347.346f, nullptr},
    {1430.40f, 1489.34f, 348.658f, nullptr},
    {1466.64f, 1493.50f, 351.869f, nullptr},
    {1511.51f, 1493.75f, 352.009f, nullptr},
    {1531.44f, 1481.79f, 351.959f, nullptr},
};

// Alliance Graveyard Jump
BattleBotPath vPath_WSG_AllianceGraveyardJump = {
    {1420.08f, 1552.84f, 342.878f, nullptr},
    {1404.06f, 1551.37f, 342.850f, nullptr},
    {1386.24f, 1543.37f, 321.944f, nullptr},
};

// Horde Graveyard Jump
BattleBotPath vPath_WSG_HordeGraveyardJump = {
    {1045.70f, 1389.15f, 340.638f, nullptr},
    {1057.43f, 1390.12f, 339.869f, nullptr},
    {1074.59f, 1400.67f, 323.811f, nullptr},
    {1092.85f, 1399.38f, 317.429f, nullptr},
};

// Alliance Base to Stables
BattleBotPath vPath_AB_AllianceBase_to_Stables = {
    {1285.67f, 1282.14f, -15.8466f, nullptr}, {1272.52f, 1267.83f, -21.7811f, nullptr},
    {1250.44f, 1248.09f, -33.3028f, nullptr}, {1232.56f, 1233.05f, -41.5241f, nullptr},
    {1213.25f, 1224.93f, -47.5513f, nullptr}, {1189.29f, 1219.49f, -53.119f, nullptr},
    {1177.17f, 1210.21f, -56.4593f, nullptr}, {1167.98f, 1202.9f, -56.4743f, nullptr},
};

// Blacksmith to Lumber Mill
BattleBotPath vPath_AB_Blacksmith_to_LumberMill = {
    {967.04f, 1039.03f, -45.091f, nullptr},  {933.67f, 1016.49f, -50.5154f, nullptr},
    {904.02f, 996.63f, -62.3461f, nullptr},  {841.74f, 985.23f, -58.8920f, nullptr},
    {796.25f, 1009.93f, -44.3286f, nullptr}, {781.29f, 1034.49f, -32.887f, nullptr},
    {793.17f, 1107.21f, 5.5663f, nullptr},   {848.98f, 1155.9f, 11.3453f, nullptr},
};

// Blacksmith to GoldMine
BattleBotPath vPath_AB_Blacksmith_to_GoldMine = {
    {1035.98f, 1015.66f, -46.0278f, nullptr}, {1096.86f, 1002.05f, -60.8013f, nullptr},
    {1159.93f, 1003.69f, -63.8378f, nullptr}, {1198.03f, 1064.09f, -65.8385f, nullptr},
    {1218.58f, 1016.96f, -76.9848f, nullptr}, {1192.83f, 956.25f, -93.6974f, nullptr},
    {1162.93f, 908.92f, -108.6703f, nullptr}, {1144.94f, 860.09f, -111.2100f, nullptr},
};

// Farm to Stables
BattleBotPath vPath_AB_Farm_to_Stable = {
    {749.88f, 878.23f, -55.1523f, nullptr},   {819.77f, 931.13f, -57.5882f, nullptr},
    {842.34f, 984.76f, -59.0333f, nullptr},   {863.03f, 1051.47f, -58.0495f, nullptr},
    {899.28f, 1098.27f, -57.4149f, nullptr},  {949.22f, 1153.27f, -54.4464f, nullptr},
    {999.07f, 1189.47f, -49.9125f, nullptr},  {1063.11f, 1211.55f, -53.4164f, nullptr},
    {1098.45f, 1225.47f, -53.1301f, nullptr}, {1146.02f, 1226.34f, -53.8979f, nullptr},
    {1167.10f, 1204.31f, -56.55f, nullptr},
};

// Alliance Base to Gold Mine
BattleBotPath vPath_AB_AllianceBase_to_GoldMine = {
    {1285.67f, 1282.14f, -15.8466f, nullptr}, {1276.41f, 1267.11f, -20.775f, nullptr},
    {1261.34f, 1241.52f, -31.2971f, nullptr}, {1244.91f, 1219.03f, -41.9658f, nullptr},
    {1232.25f, 1184.41f, -50.3348f, nullptr}, {1226.89f, 1150.82f, -55.7935f, nullptr},
    {1224.09f, 1120.38f, -57.0633f, nullptr}, {1220.03f, 1092.72f, -59.1744f, nullptr},
    {1216.05f, 1060.86f, -67.2771f, nullptr}, {1213.77f, 1027.96f, -74.429f, nullptr},
    {1208.56f, 998.394f, -81.9493f, nullptr}, {1197.42f, 969.73f, -89.9385f, nullptr},
    {1185.23f, 944.531f, -97.2433f, nullptr}, {1166.29f, 913.945f, -107.214f, nullptr},
    {1153.17f, 887.863f, -112.34f, nullptr},  {1148.89f, 871.391f, -111.96f, nullptr},
    {1145.24f, 850.82f, -110.514f, nullptr},
};

// Alliance Base to Lumber Mill
BattleBotPath vPath_AB_AllianceBase_to_LumberMill = {
    {1285.67f, 1282.14f, -15.8466f, nullptr}, {1269.13f, 1267.89f, -22.7764f, nullptr},
    {1247.79f, 1249.77f, -33.2518f, nullptr}, {1226.29f, 1232.02f, -43.9193f, nullptr},
    {1196.68f, 1230.15f, -50.4644f, nullptr}, {1168.72f, 1228.98f, -53.9329f, nullptr},
    {1140.82f, 1226.7f, -53.6318f, nullptr},  {1126.85f, 1225.77f, -47.98f, nullptr},
    {1096.5f, 1226.57f, -53.1769f, nullptr},  {1054.52f, 1226.14f, -49.2011f, nullptr},
    {1033.52f, 1226.08f, -45.5968f, nullptr}, {1005.52f, 1226.08f, -43.2912f, nullptr},
    {977.53f, 1226.68f, -40.16f, nullptr},    {957.242f, 1227.94f, -34.1487f, nullptr},
    {930.689f, 1221.57f, -18.9588f, nullptr}, {918.202f, 1211.98f, -12.2494f, nullptr},
    {880.329f, 1192.63f, 7.61168f, nullptr},  {869.965f, 1178.52f, 10.9678f, nullptr},
    {864.74f, 1163.78f, 12.385f, nullptr},    {859.165f, 1148.84f, 11.5289f, nullptr},
};

// Stables to Blacksmith
BattleBotPath vPath_AB_Stables_to_Blacksmith = {
    {1169.52f, 1198.71f, -56.2742f, nullptr}, {1166.93f, 1185.2f, -56.3634f, nullptr},
    {1173.84f, 1170.6f, -56.4094f, nullptr},  {1186.7f, 1163.92f, -56.3961f, nullptr},
    {1189.7f, 1150.68f, -55.8664f, nullptr},  {1185.18f, 1129.31f, -58.1044f, nullptr},
    {1181.7f, 1108.6f, -62.1797f, nullptr},   {1177.92f, 1087.95f, -63.5768f, nullptr},
    {1174.52f, 1067.23f, -64.402f, nullptr},  {1171.27f, 1051.09f, -65.0833f, nullptr},
    {1163.22f, 1031.7f, -64.954f, nullptr},   {1154.25f, 1010.25f, -63.5299f, nullptr},
    {1141.07f, 999.479f, -63.3713f, nullptr}, {1127.12f, 1000.37f, -60.628f, nullptr},
    {1106.17f, 1001.66f, -61.7457f, nullptr}, {1085.64f, 1005.62f, -58.5932f, nullptr},
    {1064.88f, 1008.65f, -52.3547f, nullptr}, {1044.16f, 1011.96f, -47.2647f, nullptr},
    {1029.72f, 1014.88f, -45.3546f, nullptr}, {1013.94f, 1028.7f, -43.9786f, nullptr},
    {990.89f, 1039.3f, -42.7659f, nullptr},   {978.269f, 1043.84f, -44.4588f, nullptr},
};

// Horde Base to Farm
BattleBotPath vPath_AB_HordeBase_to_Farm = {
    {707.259f, 707.839f, -17.5318f, nullptr}, {712.063f, 712.928f, -20.1802f, nullptr},
    {725.941f, 728.682f, -29.7536f, nullptr}, {734.715f, 739.591f, -35.2144f, nullptr},
    {747.607f, 756.161f, -40.899f, nullptr},  {753.994f, 766.668f, -43.3049f, nullptr},
    {758.715f, 787.106f, -46.7014f, nullptr}, {762.077f, 807.831f, -48.4721f, nullptr},
    {764.132f, 821.68f, -49.656f, nullptr},   {767.947f, 839.274f, -50.8574f, nullptr},
    {773.745f, 852.013f, -52.6226f, nullptr}, {785.123f, 869.103f, -54.2089f, nullptr},
    {804.429f, 874.961f, -55.2691f, nullptr},
};

// Horde Base to Gold Mine
BattleBotPath vPath_AB_HordeBase_to_GoldMine = {
    {707.259f, 707.839f, -17.5318f, nullptr}, {717.935f, 716.874f, -23.3941f, nullptr},
    {739.195f, 732.483f, -34.5791f, nullptr}, {757.087f, 742.008f, -38.1123f, nullptr},
    {776.946f, 748.775f, -42.7346f, nullptr}, {797.138f, 754.539f, -46.3237f, nullptr},
    {817.37f, 760.167f, -48.9235f, nullptr},  {837.638f, 765.664f, -49.7374f, nullptr},
    {865.092f, 774.738f, -51.9831f, nullptr}, {878.86f, 777.149f, -47.2361f, nullptr},
    {903.911f, 780.212f, -53.1424f, nullptr}, {923.454f, 787.888f, -54.7937f, nullptr},
    {946.218f, 798.93f, -59.0904f, nullptr},  {978.1f, 813.321f, -66.7268f, nullptr},
    {1002.94f, 817.895f, -77.3119f, nullptr}, {1030.77f, 820.92f, -88.7717f, nullptr},
    {1058.61f, 823.889f, -94.1623f, nullptr}, {1081.6f, 828.32f, -99.4137f, nullptr},
    {1104.64f, 844.773f, -106.387f, nullptr}, {1117.56f, 853.686f, -110.716f, nullptr},
    {1144.9f, 850.049f, -110.522f, nullptr},
};

// Horde Base to Lumber Mill
BattleBotPath vPath_AB_HordeBase_to_LumberMill = {
    {707.259f, 707.839f, -17.5318f, nullptr}, {721.611f, 726.507f, -27.9646f, nullptr},
    {733.846f, 743.573f, -35.8633f, nullptr}, {746.201f, 760.547f, -40.838f, nullptr},
    {758.937f, 787.565f, -46.741f, nullptr},  {761.289f, 801.357f, -48.0037f, nullptr},
    {764.341f, 822.128f, -49.6908f, nullptr}, {769.766f, 842.244f, -51.1239f, nullptr},
    {775.322f, 855.093f, -53.1161f, nullptr}, {783.995f, 874.216f, -55.0822f, nullptr},
    {789.917f, 886.902f, -56.2935f, nullptr}, {798.03f, 906.259f, -57.1162f, nullptr},
    {803.183f, 919.266f, -57.6692f, nullptr}, {813.248f, 937.688f, -57.7106f, nullptr},
    {820.412f, 958.712f, -56.1492f, nullptr}, {814.247f, 973.692f, -50.4602f, nullptr},
    {807.697f, 985.502f, -47.2383f, nullptr}, {795.672f, 1002.69f, -44.9382f, nullptr},
    {784.653f, 1020.77f, -38.6278f, nullptr}, {784.826f, 1037.34f, -31.5719f, nullptr},
    {786.083f, 1051.28f, -24.0793f, nullptr}, {787.314f, 1065.23f, -16.8918f, nullptr},
    {788.892f, 1086.17f, -6.42608f, nullptr}, {792.077f, 1106.53f, 4.81124f, nullptr},
    {800.398f, 1119.48f, 8.5814f, nullptr},   {812.476f, 1131.1f, 10.439f, nullptr},
    {829.704f, 1142.52f, 10.738f, nullptr},   {842.646f, 1143.51f, 11.9984f, nullptr},
    {857.674f, 1146.16f, 11.529f, nullptr},
};

// Farm to Blacksmith
BattleBotPath vPath_AB_Farm_to_Blacksmith = {
    {803.826f, 874.909f, -55.2547f, nullptr}, {808.763f, 887.991f, -57.4437f, nullptr},
    {818.33f, 906.674f, -59.3554f, nullptr},  {828.634f, 924.972f, -60.5664f, nullptr},
    {835.255f, 937.308f, -60.2915f, nullptr}, {845.244f, 955.78f, -60.4208f, nullptr},
    {852.125f, 967.965f, -61.3135f, nullptr}, {863.232f, 983.109f, -62.6402f, nullptr},
    {875.413f, 989.245f, -61.2916f, nullptr}, {895.765f, 994.41f, -63.6287f, nullptr},
    {914.16f, 1001.09f, -58.37f, nullptr},    {932.418f, 1011.44f, -51.9225f, nullptr},
    {944.244f, 1018.92f, -49.1438f, nullptr}, {961.55f, 1030.81f, -45.814f, nullptr},
    {978.122f, 1043.87f, -44.4682f, nullptr},
};

// Stables to Gold Mine
BattleBotPath vPath_AB_Stables_to_GoldMine = {
    {1169.52f, 1198.71f, -56.2742f, nullptr}, {1166.72f, 1183.58f, -56.3633f, nullptr},
    {1172.14f, 1170.99f, -56.4735f, nullptr}, {1185.18f, 1164.02f, -56.4269f, nullptr},
    {1193.98f, 1155.85f, -55.924f, nullptr},  {1201.51f, 1145.65f, -56.4733f, nullptr},
    {1205.39f, 1134.81f, -56.2366f, nullptr}, {1207.57f, 1106.9f, -58.4748f, nullptr},
    {1209.4f, 1085.98f, -63.4022f, nullptr},  {1212.68f, 1065.25f, -66.514f, nullptr},
    {1216.42f, 1037.52f, -72.0457f, nullptr}, {1215.4f, 1011.56f, -78.3338f, nullptr},
    {1209.8f, 992.293f, -83.2433f, nullptr},  {1201.23f, 973.121f, -88.5661f, nullptr},
    {1192.16f, 954.183f, -94.2209f, nullptr}, {1181.88f, 935.894f, -99.5239f, nullptr},
    {1169.86f, 918.68f, -105.588f, nullptr},  {1159.36f, 900.497f, -110.461f, nullptr},
    {1149.32f, 874.429f, -112.142f, nullptr}, {1145.34f, 849.824f, -110.523f, nullptr},
};

// Stables to Lumber Mill
BattleBotPath vPath_AB_Stables_to_LumberMill = {
    {1169.52f, 1198.71f, -56.2742f, nullptr},  {1169.33f, 1203.43f, -56.5457f, nullptr},
    {1164.77f, 1208.73f, -56.1907f, nullptr},  {1141.52f, 1224.99f, -53.8204f, nullptr},
    {1127.54f, 1224.82f, -48.2081f, nullptr},  {1106.56f, 1225.58f, -50.5154f, nullptr},
    {1085.6f, 1226.54f, -53.1863f, nullptr},   {1064.6f, 1226.82f, -50.4381f, nullptr},
    {1043.6f, 1227.27f, -46.5439f, nullptr},   {1022.61f, 1227.72f, -44.7157f, nullptr},
    {1001.61f, 1227.62f, -42.6876f, nullptr},  {980.623f, 1226.93f, -40.4687f, nullptr},
    {959.628f, 1227.1f, -35.3838f, nullptr},   {938.776f, 1226.34f, -23.5399f, nullptr},
    {926.138f, 1217.21f, -16.2176f, nullptr},  {911.966f, 1205.99f, -9.69655f, nullptr},
    {895.135f, 1198.85f, -0.546275f, nullptr}, {873.419f, 1189.27f, 9.3466f, nullptr},
    {863.821f, 1181.72f, 9.76912f, nullptr},   {851.803f, 1166.3f, 10.4423f, nullptr},
    {853.921f, 1150.92f, 11.543f, nullptr},
};

// Farm to Gold Mine
BattleBotPath vPath_AB_Farm_to_GoldMine = {
    {803.826f, 874.909f, -55.2547f, nullptr}, {801.662f, 865.689f, -56.9445f, nullptr},
    {806.433f, 860.776f, -57.5899f, nullptr}, {816.236f, 857.397f, -57.7029f, nullptr},
    {826.717f, 855.846f, -57.9914f, nullptr}, {836.128f, 851.257f, -57.8321f, nullptr},
    {847.933f, 843.837f, -58.1296f, nullptr}, {855.08f, 832.688f, -57.7373f, nullptr},
    {864.513f, 813.663f, -57.574f, nullptr},  {864.229f, 797.762f, -54.2057f, nullptr},
    {862.967f, 787.372f, -53.0276f, nullptr}, {864.163f, 776.33f, -52.0372f, nullptr},
    {872.583f, 777.391f, -48.5342f, nullptr}, {893.575f, 777.922f, -49.1826f, nullptr},
    {915.941f, 783.534f, -53.6598f, nullptr}, {928.105f, 789.929f, -55.4802f, nullptr},
    {946.263f, 800.46f, -59.166f, nullptr},   {958.715f, 806.845f, -62.1494f, nullptr},
    {975.79f, 811.913f, -65.9648f, nullptr},  {989.468f, 814.883f, -71.3089f, nullptr},
    {1010.13f, 818.643f, -80.0817f, nullptr}, {1023.97f, 820.667f, -86.1114f, nullptr},
    {1044.84f, 823.011f, -92.0583f, nullptr}, {1058.77f, 824.482f, -94.1937f, nullptr},
    {1079.13f, 829.402f, -99.1207f, nullptr}, {1092.85f, 836.986f, -102.755f, nullptr},
    {1114.75f, 851.21f, -109.782f, nullptr},  {1128.22f, 851.928f, -111.078f, nullptr},
    {1145.14f, 849.895f, -110.523f, nullptr},
};

// Farm to Lumber Mill
BattleBotPath vPath_AB_Farm_to_LumberMill = {
    {803.826f, 874.909f, -55.2547f, nullptr}, {802.874f, 894.28f, -56.4661f, nullptr},
    {806.844f, 920.39f, -57.3157f, nullptr},  {814.003f, 934.161f, -57.6065f, nullptr},
    {824.594f, 958.47f, -58.4916f, nullptr},  {820.434f, 971.184f, -53.201f, nullptr},
    {808.339f, 987.79f, -47.5705f, nullptr},  {795.98f, 1004.76f, -44.9189f, nullptr},
    {785.497f, 1019.18f, -39.2806f, nullptr}, {783.94f, 1032.46f, -33.5692f, nullptr},
    {784.956f, 1053.41f, -22.8368f, nullptr}, {787.499f, 1074.25f, -12.4232f, nullptr},
    {789.406f, 1088.11f, -5.28606f, nullptr}, {794.617f, 1109.17f, 6.1966f, nullptr},
    {801.514f, 1120.77f, 8.81455f, nullptr},  {817.3f, 1134.59f, 10.6064f, nullptr},
    {828.961f, 1142.98f, 10.7354f, nullptr},  {841.63f, 1147.75f, 11.6916f, nullptr},
    {854.326f, 1150.55f, 11.537f, nullptr},
};

//* ALLIANCE SIDE WAYPOINTS *//
BattleBotPath vPath_AV_AllianceSpawn_To_AllianceCrossroad1 = {
    {768.306f, -492.228f, 97.862f, nullptr}, {758.694f, -489.560f, 96.219f, nullptr},
    {718.949f, -473.185f, 75.285f, nullptr}, {701.759f, -457.199f, 64.398f, nullptr},
    {691.888f, -424.726f, 63.641f, nullptr}, {663.458f, -408.930f, 67.525f, nullptr},
    {650.696f, -400.027f, 67.948f, nullptr}, {621.638f, -383.551f, 67.511f, nullptr},
    {584.176f, -395.038f, 65.210f, nullptr}, {554.333f, -392.831f, 55.495f, nullptr},
    {518.020f, -415.663f, 43.470f, nullptr}, {444.974f, -429.032f, 27.276f, nullptr},
    {418.297f, -409.409f, 9.173f, nullptr},  {400.182f, -392.450f, -1.197f, nullptr}
};

BattleBotPath vPath_AV_AllianceFortress_To_AllianceCrossroad1 = {
    {654.750f, -32.779f, 48.611f, nullptr},  {639.731f, -46.509f, 44.072f, nullptr},
    {630.953f, -64.360f, 41.341f, nullptr},  {634.789f, -85.583f, 41.495f, nullptr},
    {628.271f, -100.943f, 40.328f, nullptr}, {621.330f, -129.900f, 33.687f, nullptr},
    {620.615f, -153.156f, 33.606f, nullptr}, {622.186f, -171.443f, 36.848f, nullptr},
    {624.456f, -188.047f, 38.569f, nullptr}, {629.185f, -222.637f, 38.362f, nullptr},
    {633.239f, -252.286f, 34.275f, nullptr}, {635.941f, -272.053f, 30.130f, nullptr},
    {632.884f, -294.946f, 30.267f, nullptr}, {625.348f, -322.407f, 30.133f, nullptr},
    {608.386f, -335.968f, 30.347f, nullptr}, {588.856f, -331.991f, 30.485f, nullptr},
    {560.140f, -327.872f, 17.396f, nullptr}, {530.523f, -324.481f, 1.718f, nullptr},
    {511.340f, -329.767f, -1.084f, nullptr}, {497.221f, -341.494f, -1.184f, nullptr},
    {465.281f, -365.577f, -1.243f, nullptr}, {437.839f, -376.473f, -1.243f, nullptr},
    {414.292f, -384.557f, -1.243f, nullptr}
};

BattleBotPath vPath_AV_AllianceCrossroad1_To_AllianceCrossroad2 = {
    {391.160f, -391.667f, -1.244f, nullptr}, {369.595f, -388.874f, -0.153f, nullptr},
    {334.986f, -384.282f, -0.726f, nullptr}, {308.373f, -383.026f, -0.336f, nullptr},
    {287.175f, -386.773f, 5.531f, nullptr},  {276.595f, -394.487f, 12.126f, nullptr},
    {264.248f, -405.109f, 23.509f, nullptr}, {253.142f, -412.069f, 31.665f, nullptr},
    {237.243f, -416.570f, 37.103f, nullptr}, {216.805f, -416.987f, 40.497f, nullptr},
    {195.369f, -407.750f, 42.876f, nullptr}, {179.619f, -402.642f, 42.793f, nullptr},
    {157.154f, -391.535f, 43.616f, nullptr}, {137.928f, -380.956f, 43.018f, nullptr},
    {117.990f, -380.243f, 43.571f, nullptr}
};

BattleBotPath vPath_AV_StoneheartGrave_To_AllianceCrossroad2 = {
    {73.141f, -481.863f, 48.479f, nullptr}, {72.974f, -461.923f, 48.498f, nullptr},
    {73.498f, -443.377f, 48.989f, nullptr}, {74.061f, -423.435f, 49.235f, nullptr},
    {83.086f, -403.871f, 47.412f, nullptr}, {102.003f, -388.023f, 45.095f, nullptr}
};

BattleBotPath vPath_AV_AllianceCrossroad2_To_StoneheartBunker = {
    {113.412f, -381.154f, 44.324f, nullptr},  {87.868f, -389.039f, 45.025f, nullptr},
    {62.170f, -388.790f, 45.012f, nullptr},   {30.645f, -390.873f, 45.644f, nullptr},
    {6.239f, -410.405f, 45.157f, nullptr},    {-11.185f, -423.994f, 45.222f, nullptr},
    {-19.595f, -437.877f, 46.383f, nullptr},  {-31.747f, -451.316f, 45.500f, nullptr},
    {-49.279f, -466.706f, 41.101f, nullptr},  {-78.427f, -459.899f, 27.648f, nullptr},
    {-103.623f, -467.766f, 24.806f, nullptr}, {-113.718f, -476.527f, 25.802f, nullptr},
    {-129.793f, -481.160f, 27.735f, nullptr}
};

BattleBotPath vPath_AV_AllianceCrossroad2_To_AllianceCaptain = {
    {117.948f, -363.836f, 43.485f, nullptr}, {110.096f, -342.635f, 41.331f, nullptr},
    {92.672f, -320.463f, 35.312f, nullptr},  {75.147f, -303.007f, 29.411f, nullptr},
    {59.694f, -293.137f, 24.670f, nullptr},  {41.342f, -293.374f, 17.193f, nullptr},
    {22.388f, -299.599f, 14.204f, nullptr}
};

BattleBotPath vPath_AV_AllianceCrossroads3_To_SnowfallGraveyard = {
    {-141.090f, -248.599f, 6.746f, nullptr},  {-147.551f, -233.354f, 9.675f, nullptr},
    {-153.353f, -219.665f, 17.267f, nullptr}, {-159.091f, -205.990f, 26.091f, nullptr},
    {-164.041f, -188.716f, 35.813f, nullptr}, {-170.634f, -166.838f, 51.540f, nullptr},
    {-183.338f, -154.159f, 64.252f, nullptr}, {-193.333f, -139.166f, 74.581f, nullptr},
    {-199.853f, -124.194f, 78.247f, nullptr}
};

BattleBotPath vPath_AV_StoneheartBunker_To_AllianceCrossroad3 = {
    {-129.793f, -481.160f, 27.735f, nullptr}, {-136.500f, -445.000f, 25.500f, nullptr},
    {-140.200f, -361.400f, 8.500f, nullptr},  {-150.000f, -315.000f, 7.000f, nullptr},
    {-154.433f, -272.428f, 8.016f, nullptr},  {-141.090f, -248.599f, 6.746f, nullptr}
};

BattleBotPath vPath_AV_AllianceCaptain_To_AllianceCrossroad3 = {
    {31.023f, -290.783f, 15.966f, nullptr},   {31.857f, -270.165f, 16.040f, nullptr},
    {26.531f, -242.488f, 14.158f, nullptr},   {3.448f, -241.318f, 11.900f, nullptr},
    {-24.158f, -233.744f, 9.802f, nullptr},   {-55.556f, -235.327f, 10.038f, nullptr},
    {-93.670f, -255.273f, 6.264f, nullptr},   {-117.164f, -263.636f, 6.363f, nullptr}
};

BattleBotPath vPath_AV_AllianceCaptain_To_HordeCrossroad3 = {
    {31.023f, -290.783f, 15.966f, nullptr}, {31.857f, -270.165f, 16.040f, nullptr},
    {26.531f, -242.488f, 14.158f, nullptr}, {3.448f, -241.318f, 11.900f, nullptr},
    {-24.158f, -233.744f, 9.802f, nullptr}, {-55.556f, -235.327f, 10.038f, nullptr},
    {-93.670f, -255.273f, 6.264f, nullptr}, {-117.164f, -263.636f, 6.363f, nullptr},
    {-154.433f, -272.428f, 8.016f, nullptr},  {-165.219f, -277.141f, 9.138f, nullptr},
    {-174.154f, -281.561f, 7.062f, nullptr},  {-189.765f, -290.755f, 6.668f, nullptr},
    {-213.121f, -303.227f, 6.668f, nullptr},  {-240.497f, -315.013f, 6.668f, nullptr},
    {-260.829f, -329.400f, 6.677f, nullptr},  {-280.652f, -344.530f, 6.668f, nullptr},
    {-305.708f, -363.655f, 6.668f, nullptr},  {-326.363f, -377.530f, 6.668f, nullptr},
    {-347.706f, -377.546f, 11.493f, nullptr}, {-361.965f, -373.012f, 13.904f, nullptr},
    {-379.646f, -367.389f, 16.907f, nullptr}, {-399.610f, -353.045f, 17.793f, nullptr},
    {-411.324f, -335.019f, 17.564f, nullptr}, {-421.800f, -316.128f, 17.843f, nullptr},
    {-435.855f, -293.863f, 19.553f, nullptr}, {-454.279f, -277.457f, 21.943f, nullptr},
    {-473.868f, -280.536f, 24.837f, nullptr}, {-492.305f, -289.846f, 29.787f, nullptr},
    {-504.724f, -313.745f, 31.938f, nullptr}, {-518.431f, -333.087f, 34.017f, nullptr}
};

BattleBotPath vPath_AV_AllianceCrossroad2_To_HordeCaptain_Bypass = {
    {78.143f, -409.215f, 48.040f, nullptr},   {62.663f, -401.117f, 47.686f, nullptr},
    {44.941f, -391.846f, 46.729f, nullptr},   {23.675f, -380.722f, 46.197f, nullptr},
    {5.068f, -375.800f, 38.360f, nullptr},    {-20.305f, -381.770f, 23.532f, nullptr},
    {-47.915f, -377.135f, 14.841f, nullptr},  {-79.438f, -371.629f, 15.242f, nullptr},
    {-107.020f, -366.811f, 18.567f, nullptr}, {-122.782f, -364.058f, 12.067f, nullptr},
    {-154.762f, -347.950f, 11.750f, nullptr}, {-186.307f, -330.604f, 9.534f, nullptr},
    {-221.358f, -311.330f, 7.410f, nullptr},  {-256.408f, -292.056f, 7.330f, nullptr},
    {-291.458f, -272.782f, 7.387f, nullptr},  {-329.904f, -251.389f, 13.442f, nullptr},
    {-364.410f, -231.156f, 13.523f, nullptr}, {-399.633f, -212.296f, 24.124f, nullptr},
    {-430.368f, -203.385f, 27.834f, nullptr}, {-461.102f, -194.474f, 47.983f, nullptr},
    {-490.680f, -182.415f, 57.973f, nullptr}, {-517.238f, -166.027f, 58.585f, nullptr},
    {-545.230f, -165.350f, 57.015f, nullptr}
};

BattleBotPath vPath_AV_StoneheartBunker_To_HordeCrossroad3 = {
    {-507.656f, -342.031f, 33.079f, nullptr}, {-490.580f, -348.193f, 29.170f, nullptr},
    {-458.194f, -343.101f, 31.685f, nullptr}, {-441.632f, -329.773f, 19.349f, nullptr},
    {-429.951f, -333.153f, 18.078f, nullptr}, {-415.858f, -341.834f, 17.865f, nullptr},
    {-406.698f, -361.322f, 17.764f, nullptr}, {-394.097f, -376.775f, 16.309f, nullptr},
    {-380.107f, -393.161f, 10.662f, nullptr}, {-359.474f, -405.379f, 10.437f, nullptr},
    {-343.166f, -415.036f, 10.177f, nullptr}, {-328.328f, -423.823f, 11.057f, nullptr},
    {-315.454f, -431.447f, 17.709f, nullptr}, {-296.180f, -449.228f, 22.316f, nullptr},
    {-272.700f, -459.303f, 28.764f, nullptr}, {-262.987f, -457.030f, 30.248f, nullptr},
    {-244.145f, -452.620f, 25.754f, nullptr}, {-221.311f, -448.195f, 25.166f, nullptr},
    {-201.762f, -457.441f, 27.413f, nullptr}, {-172.130f, -474.654f, 28.884f, nullptr},
    {-153.135f, -480.692f, 30.459f, nullptr}, {-137.077f, -478.766f, 28.798f, nullptr}
};

BattleBotPath vPath_AV_AllianceCrossroad1_To_AllianceMine = {
    {414.329f, -386.483f, -1.244f, nullptr}, {428.252f, -380.010f, -1.244f, nullptr},
    {446.203f, -372.263f, -1.244f, nullptr}, {468.352f, -360.936f, -1.244f, nullptr},
    {490.502f, -344.252f, -1.228f, nullptr}, {515.565f, -328.078f, -1.085f, nullptr},
    {531.431f, -323.435f, 1.906f, nullptr},  {548.612f, -324.795f, 10.162f, nullptr},
    {565.412f, -326.485f, 20.217f, nullptr}, {579.313f, -331.417f, 28.435f, nullptr},
    {593.026f, -336.281f, 30.215f, nullptr}, {614.580f, -331.387f, 30.236f, nullptr},
    {626.524f, -309.520f, 30.375f, nullptr}, {642.822f, -290.896f, 30.201f, nullptr},
    {661.428f, -285.810f, 29.862f, nullptr}, {670.488f, -281.637f, 27.951f, nullptr},
    {687.522f, -273.793f, 23.510f, nullptr}, {706.938f, -272.231f, 31.122f, nullptr},
    {725.715f, -281.845f, 40.529f, nullptr}, {733.208f, -296.224f, 47.418f, nullptr},
    {742.182f, -308.196f, 52.947f, nullptr}, {749.742f, -319.208f, 56.275f, nullptr},
    {760.992f, -335.660f, 60.279f, nullptr}, {783.734f, -342.468f, 61.410f, nullptr},
    {791.843f, -330.986f, 63.040f, nullptr}, {800.683f, -338.452f, 63.286f, nullptr},
    {812.383f, -337.218f, 64.698f, nullptr}, {826.326f, -331.789f, 64.492f, nullptr},
    {839.641f, -338.601f, 65.461f, nullptr}, {851.763f, -345.123f, 65.935f, nullptr},
    {871.511f, -345.464f, 64.947f, nullptr}, {882.320f, -340.819f, 66.864f, nullptr},
    {898.911f, -333.166f, 67.532f, nullptr}, {910.825f, -334.185f, 66.889f, nullptr},
    {922.109f, -336.746f, 66.120f, nullptr}, {929.915f, -353.314f, 65.902f, nullptr},
    {918.736f, -367.359f, 66.307f, nullptr}, {920.196f, -380.444f, 62.599f, nullptr},
    {920.625f, -400.310f, 59.454f, nullptr}, {920.521f, -410.085f, 57.002f, nullptr},
    {917.319f, -424.261f, 56.972f, nullptr}, {909.302f, -429.930f, 58.459f, nullptr},
    {893.164f, -430.083f, 55.782f, nullptr}, {873.846f, -425.108f, 51.387f, nullptr},
    {860.920f, -421.705f, 51.032f, nullptr}, {839.651f, -412.104f, 47.572f, nullptr},
    {831.515f, -410.477f, 47.778f, nullptr}
};
//* ALLIANCE SIDE WAYPOINTS *//

//* HORDE SIDE WAYPOINTS *//
BattleBotPath vPath_AV_HordeSpawn_To_MainRoad = {
    {-1366.182f, -532.170f, 53.354f, nullptr}, {-1325.007f, -516.055f, 51.554f, nullptr},
    {-1273.492f, -514.574f, 50.360f, nullptr}, {-1232.875f, -515.097f, 51.085f, nullptr},
    {-1201.299f, -500.807f, 51.665f, nullptr}, {-1161.199f, -476.367f, 54.956f, nullptr},
    {-1135.679f, -469.551f, 56.934f, nullptr}, {-1113.439f, -458.271f, 52.196f, nullptr},
    {-1086.750f, -444.735f, 52.903f, nullptr}, {-1061.950f, -434.380f, 51.396f, nullptr},
    {-1031.777f, -424.596f, 51.262f, nullptr}, {-967.556f, -399.110f, 49.213f, nullptr}
};

BattleBotPath vPath_AV_SnowfallRespawn_To_SnowfallGraveyard = {
    {-157.400f, 31.200f, 77.100f, nullptr},   {-158.500f, 20.000f, 77.300f, nullptr},
    {-159.700f, 8.500f, 77.700f, nullptr},    {-161.700f, -2.500f, 78.100f, nullptr},
    {-164.800f, -13.700f, 78.700f, nullptr},  {-168.900f, -25.200f, 79.200f, nullptr},
    {-174.800f, -37.000f, 79.700f, nullptr},  {-182.200f, -50.500f, 80.200f, nullptr},
    {-190.600f, -65.000f, 80.200f, nullptr},  {-199.600f, -81.000f, 79.900f, nullptr},
    {-207.600f, -95.000f, 79.700f, nullptr},  {-213.992f, -103.451f, 79.389f, nullptr}
};

BattleBotPath vPath_AV_SnowfallGraveyard_To_HordeCaptain = {
    {-213.992f, -103.451f, 79.389f, nullptr}, {-222.690f, -95.820f, 77.588f, nullptr},
    {-237.377f, -88.173f, 65.871f, nullptr},  {-253.605f, -85.059f, 56.342f, nullptr},
    {-272.886f, -94.676f, 42.306f, nullptr},  {-289.627f, -108.123f, 26.978f, nullptr},
    {-304.265f, -105.120f, 20.341f, nullptr}, {-316.857f, -92.162f, 22.999f, nullptr},
    {-332.066f, -75.537f, 27.062f, nullptr},  {-358.184f, -76.459f, 27.212f, nullptr},
    {-388.166f, -81.693f, 23.836f, nullptr},  {-407.733f, -90.216f, 23.385f, nullptr},
    {-420.646f, -122.109f, 23.955f, nullptr}, {-418.916f, -143.640f, 24.135f, nullptr},
    {-419.211f, -171.836f, 24.088f, nullptr}, {-425.798f, -195.843f, 26.290f, nullptr},
    {-445.352f, -195.483f, 35.300f, nullptr}, {-464.614f, -194.387f, 49.409f, nullptr},
    {-477.910f, -193.219f, 54.985f, nullptr}, {-488.230f, -187.985f, 56.729f, nullptr}
};

BattleBotPath vPath_AV_HordeCrossroad3_To_IcebloodTower = {
    {-533.792f, -341.435f, 35.860f, nullptr}, {-551.527f, -332.298f, 38.432f, nullptr},
    {-574.093f, -312.653f, 44.791f, nullptr}
};

BattleBotPath vPath_AV_IcebloodTower_To_HordeCaptain = {
    {-569.690f, -295.928f, 49.096f, nullptr}, {-559.809f, -282.641f, 52.074f, nullptr},
    {-546.890f, -261.488f, 53.194f, nullptr}, {-529.471f, -236.931f, 56.746f, nullptr},
    {-518.182f, -222.736f, 56.922f, nullptr}, {-500.372f, -205.938f, 57.364f, nullptr},
    {-494.455f, -190.473f, 57.190f, nullptr}
};

BattleBotPath vPath_AV_IcebloodTower_To_IcebloodGrave = {
    {-584.305f, -313.025f, 47.651f, nullptr}, {-600.831f, -327.032f, 51.026f, nullptr},
    {-613.276f, -343.187f, 54.958f, nullptr}, {-625.873f, -364.812f, 56.829f, nullptr},
    {-625.494f, -390.816f, 58.781f, nullptr}
};

BattleBotPath vPath_AV_IcebloodGrave_To_TowerBottom = {
    {-635.524f, -393.738f, 59.527f, nullptr}, {-659.484f, -386.214f, 63.131f, nullptr},
    {-679.221f, -374.851f, 65.710f, nullptr}, {-694.579f, -368.145f, 66.017f, nullptr},
    {-726.698f, -346.235f, 66.804f, nullptr}, {-743.446f, -345.899f, 66.566f, nullptr},
    {-754.564f, -344.804f, 67.422f, nullptr}
};

BattleBotPath vPath_AV_TowerBottom_To_HordeCrossroad1 = {
    {-764.722f, -339.262f, 67.669f, nullptr},  {-777.559f, -338.964f, 66.287f, nullptr},
    {-796.674f, -341.982f, 61.848f, nullptr},  {-812.721f, -346.317f, 53.286f, nullptr},
    {-826.586f, -350.765f, 50.140f, nullptr},  {-842.410f, -355.642f, 49.750f, nullptr},
    {-861.475f, -361.517f, 50.514f, nullptr},  {-878.634f, -366.805f, 49.987f, nullptr},
    {-897.097f, -374.362f, 48.931f, nullptr},  {-920.177f, -383.808f, 49.487f, nullptr},
    {-947.087f, -392.660f, 48.533f, nullptr},  {-977.268f, -395.606f, 49.426f, nullptr},
    {-993.685f, -394.251f, 50.180f, nullptr},  {-1016.760f, -390.774f, 50.955f, nullptr},
    {-1042.994f, -383.854f, 50.904f, nullptr}, {-1066.925f, -377.541f, 52.535f, nullptr},
    {-1103.309f, -365.939f, 51.502f, nullptr}, {-1127.469f, -354.968f, 51.502f, nullptr}
};

BattleBotPath vPath_AV_HordeCrossroad1_To_FrostwolfGrave = {
    {-1127.565f, -340.254f, 51.753f, nullptr}, {-1112.843f, -337.645f, 53.368f, nullptr},
    {-1089.873f, -334.993f, 54.580f, nullptr}
};

BattleBotPath vPath_AV_HordeCrossroad1_To_HordeFortress = {
    {-1140.070f, -349.834f, 51.090f, nullptr}, {-1161.807f, -352.447f, 51.782f, nullptr},
    {-1182.047f, -361.856f, 52.458f, nullptr}, {-1203.318f, -365.951f, 54.427f, nullptr},
    {-1228.105f, -367.462f, 58.155f, nullptr}, {-1243.013f, -357.409f, 59.866f, nullptr},
    {-1245.382f, -337.206f, 59.322f, nullptr}, {-1236.790f, -323.051f, 60.500f, nullptr},
    {-1227.642f, -311.981f, 63.269f, nullptr}, {-1217.120f, -299.546f, 69.058f, nullptr},
    {-1207.536f, -288.218f, 71.742f, nullptr}, {-1198.808f, -270.859f, 72.431f, nullptr},
    {-1203.695f, -255.038f, 72.498f, nullptr}, {-1220.292f, -252.715f, 73.243f, nullptr},
    {-1236.539f, -252.215f, 73.326f, nullptr}, {-1246.512f, -257.577f, 73.326f, nullptr},
    {-1257.051f, -272.847f, 73.018f, nullptr}, {-1265.600f, -284.769f, 77.939f, nullptr},
    {-1282.656f, -290.696f, 88.334f, nullptr}, {-1292.589f, -290.809f, 90.446f, nullptr},
    {-1307.385f, -290.950f, 90.681f, nullptr}, {-1318.955f, -291.061f, 90.451f, nullptr},
    {-1332.717f, -291.117f, 90.806f, nullptr}, {-1346.880f, -287.015f, 91.066f, nullptr}
};

BattleBotPath vPath_AV_FrostwolfGrave_To_HordeMine = {
    {-1075.070f, -332.081f, 55.758f, nullptr}, {-1055.926f, -326.469f, 57.026f, nullptr},
    {-1036.115f, -328.239f, 59.368f, nullptr}, {-1018.217f, -332.306f, 59.335f, nullptr},
    {-990.396f, -337.397f, 58.342f, nullptr},  {-963.987f, -335.529f, 60.945f, nullptr},
    {-954.477f, -321.258f, 63.429f, nullptr},  {-951.052f, -301.476f, 64.761f, nullptr},
    {-955.257f, -282.794f, 63.683f, nullptr},  {-960.355f, -261.040f, 64.498f, nullptr},
    {-967.410f, -230.933f, 67.408f, nullptr},  {-967.187f, -207.444f, 68.924f, nullptr},
};
//* HORDE SIDE WAYPOINTS *//

BattleBotPath vPath_EY_Horde_Spawn_to_Crossroad1Horde = {
    {1809.102f, 1540.854f, 1267.142f, nullptr}, {1832.335f, 1539.495f, 1256.417f, nullptr},
    {1846.995f, 1539.792f, 1243.077f, nullptr}, {1846.243f, 1530.716f, 1238.477f, nullptr},
    {1883.154f, 1532.143f, 1202.143f, nullptr}, {1941.452f, 1549.086f, 1176.700f, nullptr}};

BattleBotPath vPath_EY_Horde_Crossroad1Horde_to_Crossroad2Horde = {{1951.647f, 1545.187f, 1174.831f, nullptr},
                                                                   {1992.266f, 1546.962f, 1169.816f, nullptr},
                                                                   {2045.865f, 1543.925f, 1163.759f, nullptr}};

BattleBotPath vPath_EY_Crossroad1Horde_to_Blood_Elf_Tower = {{1952.907f, 1539.857f, 1174.638f, nullptr},
                                                             {2000.130f, 1508.182f, 1169.778f, nullptr},
                                                             {2044.239f, 1483.860f, 1166.165f, nullptr},
                                                             {2048.773f, 1389.578f, 1193.903f, nullptr}};

BattleBotPath vPath_EY_Crossroad1Horde_to_Fel_Reaver_Ruins = {{1944.301f, 1557.170f, 1176.370f, nullptr},
                                                              {1992.953f, 1625.188f, 1173.616f, nullptr},
                                                              {2040.421f, 1676.989f, 1177.079f, nullptr},
                                                              {2045.527f, 1736.398f, 1189.661f, nullptr}};

BattleBotPath vPath_EY_Crossroad2Horde_to_Blood_Elf_Tower = {{2049.363f, 1532.337f, 1163.178f, nullptr},
                                                             {2050.149f, 1484.721f, 1165.099f, nullptr},
                                                             {2046.865f, 1423.937f, 1188.882f, nullptr},
                                                             {2048.478f, 1389.491f, 1193.878f, nullptr}};

BattleBotPath vPath_EY_Crossroad2Horde_to_Fel_Reaver_Ruins = {{2052.267f, 1555.692f, 1163.147f, nullptr},
                                                              {2047.684f, 1614.272f, 1165.397f, nullptr},
                                                              {2045.993f, 1668.937f, 1174.978f, nullptr},
                                                              {2044.286f, 1733.128f, 1189.739f, nullptr}};

BattleBotPath vPath_EY_Crossroad2Horde_to_Flag = {{2059.276f, 1546.143f, 1162.394f, nullptr},
                                                  {2115.978f, 1559.244f, 1156.362f, nullptr},
                                                  {2149.140f, 1556.570f, 1158.412f, nullptr},
                                                  {2170.601f, 1567.113f, 1159.456f, nullptr}};

BattleBotPath vPath_EY_Alliance_Spawn_to_Crossroad1Alliance = {
    {2502.110f, 1604.330f, 1260.750f, nullptr}, {2497.077f, 1596.198f, 1257.302f, nullptr},
    {2483.930f, 1597.062f, 1244.660f, nullptr}, {2486.549f, 1617.651f, 1225.837f, nullptr},
    {2449.150f, 1601.792f, 1201.552f, nullptr}, {2395.737f, 1588.287f, 1176.570f, nullptr}};

BattleBotPath vPath_EY_Alliance_Crossroad1Alliance_to_Crossroad2Alliance = {
    {2380.262f, 1586.757f, 1173.567f, nullptr},
    {2333.956f, 1586.052f, 1169.873f, nullptr},
    {2291.210f, 1591.435f, 1166.048f, nullptr},
};

BattleBotPath vPath_EY_Crossroad1Alliance_to_Mage_Tower = {{2380.973f, 1593.445f, 1173.189f, nullptr},
                                                           {2335.762f, 1621.922f, 1169.007f, nullptr},
                                                           {2293.526f, 1643.972f, 1166.501f, nullptr},
                                                           {2288.198f, 1688.568f, 1172.790f, nullptr},
                                                           {2284.286f, 1737.889f, 1189.708f, nullptr}};

BattleBotPath vPath_EY_Crossroad1Alliance_to_Draenei_Ruins = {{2388.687f, 1576.089f, 1175.975f, nullptr},
                                                              {2354.921f, 1522.763f, 1176.060f, nullptr},
                                                              {2300.056f, 1459.208f, 1184.181f, nullptr},
                                                              {2289.880f, 1415.640f, 1196.755f, nullptr},
                                                              {2279.870f, 1387.461f, 1195.003f, nullptr}};

BattleBotPath vPath_EY_Crossroad2Alliance_to_Mage_Tower = {{2282.525f, 1597.721f, 1164.553f, nullptr},
                                                           {2281.028f, 1651.310f, 1165.426f, nullptr},
                                                           {2284.633f, 1736.082f, 1189.708f, nullptr}};

BattleBotPath vPath_EY_Crossroad2Alliance_to_Draenei_Ruins = {{2282.487f, 1581.630f, 1165.318f, nullptr},
                                                              {2284.728f, 1525.618f, 1170.812f, nullptr},
                                                              {2287.697f, 1461.228f, 1183.450f, nullptr},
                                                              {2290.861f, 1413.606f, 1197.115f, nullptr}};

BattleBotPath vPath_EY_Crossroad2Alliance_to_Flag = {{2275.622f, 1586.123f, 1164.469f, nullptr},
                                                     {2221.334f, 1575.123f, 1158.277f, nullptr},
                                                     {2178.372f, 1572.144f, 1159.462f, nullptr}};

BattleBotPath vPath_EY_Draenei_Ruins_to_Blood_Elf_Tower = {
    {2287.925f, 1406.976f, 1197.004f, nullptr}, {2283.283f, 1454.769f, 1184.243f, nullptr},
    {2237.519f, 1398.161f, 1178.191f, nullptr}, {2173.150f, 1388.084f, 1170.185f, nullptr},
    {2105.039f, 1381.507f, 1162.911f, nullptr}, {2074.315f, 1404.387f, 1178.141f, nullptr},
    {2047.649f, 1411.681f, 1192.032f, nullptr}, {2049.197f, 1387.392f, 1193.799f, nullptr}};

BattleBotPath vPath_EY_Fel_Reaver_to_Mage_Tower = {
    {2044.519f, 1726.113f, 1189.395f, nullptr}, {2045.408f, 1682.986f, 1177.574f, nullptr},
    {2097.595f, 1736.117f, 1170.419f, nullptr}, {2158.866f, 1746.998f, 1161.184f, nullptr},
    {2220.635f, 1757.837f, 1151.886f, nullptr}, {2249.922f, 1721.807f, 1161.550f, nullptr},
    {2281.021f, 1694.735f, 1174.020f, nullptr}, {2284.522f, 1728.234f, 1189.015f, nullptr}};

BattleBotPath vPath_IC_Ally_Keep_to_Ally_Front_Crossroad = {
    //{ 351.652f, -834.837f, 48.916f, nullptr },
    {434.768f, -833.976f, 46.090f, nullptr},
    {506.782f, -828.594f, 24.313f, nullptr},
    {524.955f, -799.002f, 19.498f, nullptr}};

BattleBotPath vPath_IC_Ally_Front_Crossroad_to_Workshop = {
    {524.955f, -799.002f, 19.498f, nullptr}, {573.557f, -804.838f, 9.6291f, nullptr},
    {627.977f, -810.197f, 3.5154f, nullptr}, {681.501f, -805.208f, 3.1464f, nullptr},
    {721.905f, -797.917f, 4.5112f, nullptr}, {774.466f, -801.058f, 6.3428f, nullptr}};

BattleBotPath vPath_IC_Ally_Keep_to_Ally_Dock_Crossroad = {{434.768f, -833.976f, 46.090f, nullptr},
                                                           {446.710f, -776.008f, 48.783f, nullptr},
                                                           {463.745f, -742.368f, 48.584f, nullptr},
                                                           {488.201f, -714.563f, 36.564f, nullptr},
                                                           {525.923f, -666.880f, 25.425f, nullptr}};

BattleBotPath vPath_IC_Ally_Front_Crossroad_to_Ally_Dock_Crossroad = {{524.955f, -799.002f, 19.498f, nullptr},
                                                                      {542.225f, -745.142f, 18.348f, nullptr},
                                                                      {545.309f, -712.497f, 22.005f, nullptr},
                                                                      {538.678f, -748.361f, 18.261f, nullptr},
                                                                      {525.923f, -666.880f, 25.425f, nullptr}};

BattleBotPath vPath_IC_Lower_Graveyard_to_Lower_Graveyard_Crossroad = {{443.095f, -310.797f, 51.749f, nullptr},
                                                                       {462.733f, -323.587f, 48.706f, nullptr},
                                                                       {471.540f, -343.914f, 40.706f, nullptr},
                                                                       {475.622f, -360.728f, 34.384f, nullptr},
                                                                       {484.458f, -379.796f, 33.122f, nullptr}};

BattleBotPath vPath_IC_Lower_Graveyard_Crossroad_to_Ally_Docks_Crossroad = {
    {484.458f, -379.796f, 33.122f, nullptr}, {509.786f, -380.592f, 33.122f, nullptr},
    {532.549f, -381.576f, 33.122f, nullptr}, {553.506f, -386.102f, 33.507f, nullptr},
    {580.533f, -398.536f, 33.416f, nullptr}, {605.112f, -409.843f, 33.121f, nullptr},
    {619.212f, -419.169f, 33.121f, nullptr}, {631.702f, -428.763f, 33.070f, nullptr},
    {648.483f, -444.714f, 28.629f, nullptr}};

BattleBotPath vPath_IC_Lower_Graveyard_Crossroad_to_Ally_Docks_Second_Crossroad = {
    {484.458f, -379.796f, 33.122f, nullptr}, {470.771f, -394.789f, 33.112f, nullptr},
    {461.191f, -409.475f, 33.120f, nullptr}, {452.794f, -431.842f, 33.120f, nullptr},
    {452.794f, -456.896f, 33.658f, nullptr}, {453.279f, -481.742f, 33.052f, nullptr},
    {453.621f, -504.979f, 32.956f, nullptr}, {452.006f, -526.792f, 32.221f, nullptr},
    {453.150f, -548.212f, 29.133f, nullptr}, {455.224f, -571.323f, 26.119f, nullptr},
    {465.486f, -585.424f, 25.756f, nullptr}, {475.366f, -598.414f, 25.784f, nullptr},
    {477.702f, -605.757f, 25.714f, nullptr}};

BattleBotPath vPath_IC_Ally_Dock_Crossroad_to_Ally_Docks_Second_Crossroad = {{525.923f, -666.880f, 25.425f, nullptr},
                                                                             {497.190f, -630.709f, 25.626f, nullptr},
                                                                             {477.702f, -605.757f, 25.714f, nullptr}};

BattleBotPath vPath_IC_Ally_Docks_Second_Crossroad_to_Ally_Docks_Crossroad = {{477.702f, -605.757f, 25.714f, nullptr},
                                                                              {493.697f, -555.838f, 26.014f, nullptr},
                                                                              {522.939f, -525.199f, 26.014f, nullptr},
                                                                              {580.398f, -486.274f, 26.013f, nullptr},
                                                                              {650.132f, -445.811f, 28.503f, nullptr}};

BattleBotPath vPath_IC_Ally_Docks_Crossroad_to_Docks_Flag = {{650.132f, -445.811f, 28.503f, nullptr},
                                                             {690.527f, -452.961f, 18.039f, nullptr},
                                                             {706.813f, -430.003f, 13.797f, nullptr},
                                                             {726.427f, -364.849f, 17.815f, nullptr}};

BattleBotPath vPath_IC_Docks_Graveyard_to_Docks_Flag = {
    {638.142f, -283.782f, 11.512f, nullptr}, {655.760f, -284.433f, 13.220f, nullptr},
    {661.656f, -299.912f, 12.756f, nullptr}, {675.068f, -317.192f, 12.627f, nullptr},
    {692.712f, -323.866f, 12.686f, nullptr}, {712.968f, -341.285f, 13.350f, nullptr},
    {726.427f, -364.849f, 17.815f, nullptr}};

BattleBotPath vPath_IC_Ally_Keep_to_Quarry_Crossroad = {
    {320.547f, -919.896f, 48.481f, nullptr}, {335.384f, -922.371f, 49.518f, nullptr},
    {353.471f, -920.316f, 48.660f, nullptr}, {353.305f, -958.823f, 47.665f, nullptr},
    {369.196f, -989.960f, 37.719f, nullptr}, {380.671f, -1023.51f, 29.369f, nullptr}};

BattleBotPath vPath_IC_Quarry_Crossroad_to_Quarry_Flag = {
    {380.671f, -1023.51f, 29.369f, nullptr}, {361.584f, -1052.89f, 27.445f, nullptr},
    {341.853f, -1070.17f, 24.024f, nullptr}, {295.845f, -1075.22f, 16.164f, nullptr},
    {253.030f, -1094.06f, 4.1517f, nullptr}, {211.391f, -1121.80f, 1.9591f, nullptr},
    {187.836f, -1155.66f, 1.9749f, nullptr}, {221.193f, -1186.87f, 8.0247f, nullptr},
    {249.181f, -1162.09f, 16.687f, nullptr}};

BattleBotPath vPath_IC_Ally_Front_Crossroad_to_Hangar_First_Crossroad = {{524.955f, -799.002f, 19.498f, nullptr},
                                                                         {512.563f, -840.166f, 23.913f, nullptr},
                                                                         {513.418f, -877.726f, 26.333f, nullptr},
                                                                         {512.962f, -945.951f, 39.382f, nullptr}};

BattleBotPath vPath_IC_Ally_Keep_to_Hangar_First_Crossroad = {{434.768f, -833.976f, 46.090f, nullptr},
                                                              {486.355f, -909.736f, 26.112f, nullptr},
                                                              {512.962f, -945.951f, 39.382f, nullptr}};

BattleBotPath vPath_IC_Hangar_First_Crossroad_to_Hangar_Second_Crossroad = {{512.962f, -945.951f, 39.382f, nullptr},
                                                                            {499.525f, -985.850f, 47.659f, nullptr},
                                                                            {492.794f, -1016.36f, 49.834f, nullptr},
                                                                            {481.738f, -1052.67f, 60.190f, nullptr}};

BattleBotPath vPath_IC_Quarry_Crossroad_to_Hangar_Second_Crossroad = {{380.671f, -1023.51f, 29.369f, nullptr},
                                                                      {430.997f, -1021.72f, 31.021f, nullptr},
                                                                      {439.528f, -1044.88f, 41.827f, nullptr},
                                                                      {455.062f, -1060.67f, 67.209f, nullptr},
                                                                      {481.738f, -1052.67f, 60.190f, nullptr}};

BattleBotPath vPath_IC_Hangar_Second_Crossroad_to_Hangar_Flag = {
    {508.945f, -1103.30f, 79.054f, nullptr}, {536.397f, -1145.79f, 95.478f, nullptr},
    {573.242f, -1138.19f, 109.26f, nullptr}, {609.051f, -1112.93f, 128.31f, nullptr},
    {645.569f, -1094.58f, 132.13f, nullptr}, {689.621f, -1068.33f, 132.87f, nullptr},
    {730.045f, -1042.67f, 133.03f, nullptr}, {755.322f, -1030.28f, 133.30f, nullptr},
    {801.685f, -1005.46f, 132.39f, nullptr}, {806.404f, -1001.709f, 132.382f, nullptr}};

BattleBotPath vPath_IC_Horde_Keep_to_Horde_Front_Crossroad = {{1128.646f, -763.221f, 48.385f, nullptr},
                                                              {1091.273f, -763.619f, 42.352f, nullptr},
                                                              {1032.825f, -763.024f, 30.420f, nullptr},
                                                              {991.4235f, -807.672f, 21.788f, nullptr}};

BattleBotPath vPath_IC_Horde_Front_Crossroad_to_Horde_Hangar_Crossroad = {{991.4235f, -807.672f, 21.788f, nullptr},
                                                                          {999.1844f, -855.182f, 21.484f, nullptr},
                                                                          {1012.089f, -923.098f, 19.296f, nullptr}};

BattleBotPath vPath_IC_Horde_Keep_to_Horde_Hangar_Crossroad = {{1128.646f, -763.221f, 48.385f, nullptr},
                                                               {1121.090f, -816.666f, 49.008f, nullptr},
                                                               {1107.106f, -851.459f, 48.804f, nullptr},
                                                               {1072.313f, -888.355f, 30.853f, nullptr},
                                                               {1012.089f, -923.098f, 19.296f, nullptr}};

BattleBotPath vPath_IC_Horde_Hangar_Crossroad_to_Hangar_Flag = {
    {1001.745f, -973.174f, 15.784f, nullptr},  {1015.437f, -1019.47f, 15.578f, nullptr},
    {1009.622f, -1067.78f, 15.777f, nullptr},  {988.0692f, -1113.32f, 18.254f, nullptr},
    {943.7221f, -1134.50f, 32.296f, nullptr},  {892.2205f, -1115.16f, 63.319f, nullptr},
    {849.6576f, -1090.88f, 91.943f, nullptr},  {814.9168f, -1056.42f, 117.275f, nullptr},
    {799.0856f, -1034.62f, 129.000f, nullptr}, {801.685f, -1005.46f, 132.39f, nullptr}};

BattleBotPath vPath_IC_Horde_Keep_to_Horde_Dock_Crossroad = {{1128.646f, -763.221f, 48.385f, nullptr},
                                                             {1116.203f, -723.328f, 48.655f, nullptr},
                                                             {1093.246f, -696.880f, 37.041f, nullptr},
                                                             {1034.226f, -653.581f, 24.432f, nullptr}};

BattleBotPath vPath_IC_Horde_Front_Crossroad_to_Horde_Dock_Crossroad = {{991.4235f, -807.672f, 21.788f, nullptr},
                                                                        {1025.305f, -757.165f, 29.241f, nullptr},
                                                                        {1029.308f, -710.366f, 26.366f, nullptr},
                                                                        {1034.226f, -653.581f, 24.432f, nullptr}};

BattleBotPath vPath_IC_Horde_Dock_Crossroad_to_Refinery_Crossroad = {{1034.226f, -653.581f, 24.432f, nullptr},
                                                                     {1102.358f, -617.505f, 5.4963f, nullptr},
                                                                     {1116.255f, -580.956f, 18.184f, nullptr},
                                                                     {1114.414f, -546.731f, 23.422f, nullptr},
                                                                     {1148.358f, -503.947f, 23.423f, nullptr}};

BattleBotPath vPath_IC_Refinery_Crossroad_to_Refinery_Base = {{1148.358f, -503.947f, 23.423f, nullptr},
                                                              {1201.885f, -500.425f, 4.7262f, nullptr},
                                                              {1240.595f, -471.971f, 0.8933f, nullptr},
                                                              {1265.993f, -435.419f, 10.669f, nullptr}};

BattleBotPath vPath_IC_Horde_Side_Gate_to_Refinery_Base = {
    {1218.676f, -660.487f, 47.870f, nullptr}, {1211.677f, -626.181f, 46.085f, nullptr},
    {1212.720f, -562.300f, 19.514f, nullptr}, {1238.803f, -538.997f, 3.9892f, nullptr},
    {1248.875f, -482.852f, 0.8933f, nullptr}, {1265.993f, -435.419f, 10.669f, nullptr}};

BattleBotPath vPath_IC_Refinery_Crossroad_to_Docks_Crossroad = {
    {1148.358f, -503.947f, 23.423f, nullptr}, {1127.010f, -469.451f, 23.422f, nullptr},
    {1100.976f, -431.146f, 21.312f, nullptr}, {1053.812f, -405.457f, 12.749f, nullptr},
    {1005.570f, -375.439f, 12.695f, nullptr}, {963.4349f, -353.282f, 12.356f, nullptr},
    {907.1394f, -380.470f, 11.912f, nullptr}};

BattleBotPath vPath_IC_Horde_Dock_Crossroad_to_Docks_Crossroad = {
    {1034.226f, -653.581f, 24.432f, nullptr}, {1013.435f, -622.066f, 24.486f, nullptr},
    {988.1990f, -547.937f, 24.424f, nullptr}, {982.4955f, -508.332f, 24.524f, nullptr},
    {982.5065f, -462.920f, 16.833f, nullptr}, {948.8842f, -421.200f, 16.877f, nullptr},
    {907.1394f, -380.470f, 11.912f, nullptr}};

BattleBotPath vPath_IC_Docks_Crossroad_to_Docks_Flag = {{907.1394f, -380.470f, 11.912f, nullptr},
                                                        {851.5726f, -382.503f, 11.906f, nullptr},
                                                        {808.1441f, -381.199f, 11.906f, nullptr},
                                                        {761.1740f, -381.854f, 14.504f, nullptr},
                                                        {726.427f, -364.849f, 17.815f, nullptr}};

BattleBotPath vPath_IC_Horde_Front_Crossroad_to_Workshop = {
    {991.4235f, -807.672f, 21.788f, nullptr}, {944.5518f, -800.344f, 13.155f, nullptr},
    {907.1300f, -798.892f, 8.3237f, nullptr}, {842.9721f, -795.224f, 5.2007f, nullptr},
    {804.5959f, -794.269f, 5.9836f, nullptr}, {774.466f, -801.058f, 6.3428f, nullptr}};

BattleBotPath vPath_IC_Central_Graveyard_to_Workshop = {
    {775.377f, -664.151f, 8.388f, nullptr}, {776.299f, -684.079f, 5.036f, nullptr},
    {777.451f, -707.525f, 0.051f, nullptr}, {779.059f, -734.611f, 1.695f, nullptr},
    {779.643f, -767.010f, 4.843f, nullptr}, {774.466f, -801.058f, 6.3428f, nullptr}};

BattleBotPath vPath_IC_Horde_East_Gate_to_Horde_Keep = {
    {1216.1918f, -864.922f, 48.852f, nullptr}, {1197.3117f, -866.054f, 48.916f, nullptr},
    {1174.195f, -867.931f, 48.621f, nullptr},  {1149.671f, -869.240f, 48.096f, nullptr},
    {1128.257f, -860.087f, 49.562f, nullptr},  {1118.730f, -829.959f, 49.074f, nullptr},
    {1123.201f, -806.498f, 48.896f, nullptr},  {1129.685f, -787.156f, 48.680f, nullptr},
    {1128.646f, -763.221f, 48.385f, nullptr}};

BattleBotPath vPath_IC_Workshop_to_Workshop_Keep = {{773.792f, -825.637f, 8.127f, nullptr},
                                                    {772.706f, -841.881f, 11.622f, nullptr},
                                                    {773.057f, -859.936f, 12.418f, nullptr}};

BattleBotPath vPath_IC_Alliance_Base = {
    {402.020f, -832.289f, 48.627f, nullptr}, {384.784f, -832.551f, 48.830f, nullptr},
    {369.413f, -832.480f, 48.916f, nullptr}, {346.677f, -832.355f, 48.916f, nullptr},
    {326.296f, -832.189f, 48.916f, nullptr}, {311.174f, -832.204f, 48.916f, nullptr},
};

BattleBotPath vPath_IC_Horde_Base = {
    {1158.652f, -762.680f, 48.628f, nullptr}, {1171.598f, -762.628f, 48.649f, nullptr},
    {1189.102f, -763.484f, 48.915f, nullptr}, {1208.599f, -764.332f, 48.915f, nullptr},
    {1227.592f, -764.782f, 48.915f, nullptr}, {1253.676f, -765.441f, 48.915f, nullptr},
};

BattleBotPath vPath_IC_Workshop_to_North_West = {
    {786.051f, -801.798f, 5.968f, nullptr},   {801.767f, -800.845f, 6.201f, nullptr},
    {830.190f, -800.059f, 5.163f, nullptr},   {858.827f, -797.691f, 5.602f, nullptr},
    {881.579f, -795.190f, 6.441f, nullptr},   {905.796f, -792.252f, 8.008f, nullptr},
    {929.874f, -788.912f, 10.674f, nullptr},  {960.001f, -784.233f, 15.521f, nullptr},
    {991.890f, -780.605f, 22.402f, nullptr},  {1014.062f, -761.863f, 28.672f, nullptr},
    {1019.925f, -730.822f, 27.421f, nullptr}, {1022.320f, -698.581f, 25.993f, nullptr},
    {1022.539f, -665.405f, 24.574f, nullptr}, {1016.279f, -632.780f, 24.487f, nullptr},
    {1004.839f, -602.680f, 24.501f, nullptr}, {992.826f, -567.833f, 24.558f, nullptr},
    {984.998f, -535.303f, 24.485f, nullptr},
};

BattleBotPath vPath_IC_South_West_Crossroads = {
    {528.932f, -667.953f, 25.413f, nullptr}, {514.800f, -650.381f, 26.171f, nullptr},
    {488.367f, -621.869f, 25.820f, nullptr}, {479.491f, -594.284f, 26.095f, nullptr},
    {498.094f, -557.031f, 26.015f, nullptr}, {528.272f, -528.761f, 26.015f, nullptr},
    {595.746f, -480.009f, 26.007f, nullptr}, {632.156f, -458.182f, 27.416f, nullptr},
    {656.013f, -446.685f, 28.003f, nullptr},
};

BattleBotPath vPath_IC_Hanger_to_Workshop = {
    {808.923f, -1003.441f, 132.380f, nullptr}, {804.031f, -1002.785f, 132.382f, nullptr},
    {798.466f, -1021.472f, 132.292f, nullptr}, {795.472f, -1031.693f, 130.232f, nullptr},
    {804.631f, -1042.672f, 125.310f, nullptr}, {827.549f, -1061.778f, 111.276f, nullptr},
    {847.424f, -1077.930f, 98.061f, nullptr},  {868.225f, -1091.563f, 83.794f, nullptr},
    {894.100f, -1104.828f, 66.570f, nullptr},  {923.615f, -1117.689f, 47.391f, nullptr},
    {954.614f, -1119.716f, 27.880f, nullptr},  {970.397f, -1116.281f, 22.124f, nullptr},
    {985.906f, -1105.714f, 18.572f, nullptr},  {996.308f, -1090.605f, 17.669f, nullptr},
    {1003.693f, -1072.916f, 16.583f, nullptr}, {1008.660f, -1051.310f, 15.970f, nullptr},
    {1008.437f, -1026.678f, 15.623f, nullptr}, {1000.103f, -999.047f, 16.487f, nullptr},
    {988.412f, -971.096f, 17.796f, nullptr},   {971.503f, -945.742f, 14.438f, nullptr},
    {947.420f, -931.306f, 13.209f, nullptr},   {922.920f, -916.960f, 10.859f, nullptr},
    {901.607f, -902.203f, 9.854f, nullptr},    {881.932f, -882.226f, 7.848f, nullptr},
    {861.795f, -862.477f, 6.731f, nullptr},    {844.186f, -845.952f, 6.192f, nullptr},
    {826.565f, -826.551f, 5.171f, nullptr},    {809.497f, -815.351f, 6.179f, nullptr},
    {790.787f, -809.678f, 6.450f, nullptr},
};

std::vector<BattleBotPath*> const vPaths_WS = {
    &vPath_WSG_HordeFlagRoom_to_HordeGraveyard,
    &vPath_WSG_HordeGraveyard_to_HordeTunnel,
    &vPath_WSG_HordeTunnel_to_HordeFlagRoom,
    &vPath_WSG_AllianceFlagRoom_to_AllianceGraveyard,
    &vPath_WSG_AllianceGraveyard_to_AllianceTunnel,
    &vPath_WSG_AllianceTunnel_to_AllianceFlagRoom,
    &vPath_WSG_HordeTunnel_to_HordeBaseRoof,
    &vPath_WSG_AllianceTunnel_to_AllianceBaseRoof,
    &vPath_WSG_AllianceTunnel_to_HordeTunnel,
    &vPath_WSG_AllianceGraveyardLower_to_HordeFlagRoom,
    &vPath_WSG_HordeGraveyardLower_to_AllianceFlagRoom,
    &vPath_WSG_AllianceGraveyardJump,
    &vPath_WSG_HordeGraveyardJump
};

std::vector<BattleBotPath*> const vPaths_AB = {
    &vPath_AB_AllianceBase_to_Stables,  &vPath_AB_AllianceBase_to_GoldMine, &vPath_AB_AllianceBase_to_LumberMill,
    &vPath_AB_Stables_to_Blacksmith,    &vPath_AB_HordeBase_to_Farm,        &vPath_AB_HordeBase_to_GoldMine,
    &vPath_AB_HordeBase_to_LumberMill,  &vPath_AB_Farm_to_Blacksmith,       &vPath_AB_Stables_to_GoldMine,
    &vPath_AB_Stables_to_LumberMill,    &vPath_AB_Farm_to_GoldMine,         &vPath_AB_Farm_to_LumberMill,
    &vPath_AB_Blacksmith_to_LumberMill, &vPath_AB_Blacksmith_to_GoldMine,   &vPath_AB_Farm_to_Stable,
};

std::vector<BattleBotPath*> const vPaths_AV = {
    &vPath_AV_AllianceSpawn_To_AllianceCrossroad1,
    &vPath_AV_AllianceFortress_To_AllianceCrossroad1,
    &vPath_AV_AllianceCrossroad1_To_AllianceCrossroad2,
    &vPath_AV_StoneheartGrave_To_AllianceCrossroad2,
    &vPath_AV_AllianceCrossroad2_To_StoneheartBunker,
    &vPath_AV_AllianceCrossroad2_To_AllianceCaptain,
    &vPath_AV_AllianceCaptain_To_HordeCrossroad3,
    &vPath_AV_AllianceCrossroad2_To_HordeCaptain_Bypass,
    &vPath_AV_AllianceCrossroads3_To_SnowfallGraveyard,
    &vPath_AV_StoneheartBunker_To_AllianceCrossroad3,
    &vPath_AV_AllianceCaptain_To_AllianceCrossroad3,
    &vPath_AV_StoneheartBunker_To_HordeCrossroad3,
    &vPath_AV_AllianceCrossroad1_To_AllianceMine,
    &vPath_AV_HordeSpawn_To_MainRoad,
    &vPath_AV_SnowfallRespawn_To_SnowfallGraveyard,
    &vPath_AV_SnowfallGraveyard_To_HordeCaptain,
    &vPath_AV_HordeCrossroad3_To_IcebloodTower,
    &vPath_AV_IcebloodTower_To_HordeCaptain,
    &vPath_AV_IcebloodTower_To_IcebloodGrave,
    &vPath_AV_IcebloodGrave_To_TowerBottom,
    &vPath_AV_TowerBottom_To_HordeCrossroad1,
    &vPath_AV_HordeCrossroad1_To_FrostwolfGrave,
    &vPath_AV_HordeCrossroad1_To_HordeFortress,
    &vPath_AV_FrostwolfGrave_To_HordeMine
};

std::vector<BattleBotPath*> const vPaths_EY = {
    &vPath_EY_Horde_Spawn_to_Crossroad1Horde,
    &vPath_EY_Horde_Crossroad1Horde_to_Crossroad2Horde,
    &vPath_EY_Crossroad1Horde_to_Blood_Elf_Tower,
    &vPath_EY_Crossroad1Horde_to_Fel_Reaver_Ruins,
    &vPath_EY_Crossroad2Horde_to_Blood_Elf_Tower,
    &vPath_EY_Crossroad2Horde_to_Fel_Reaver_Ruins,
    &vPath_EY_Crossroad2Horde_to_Flag,
    &vPath_EY_Alliance_Spawn_to_Crossroad1Alliance,
    &vPath_EY_Alliance_Crossroad1Alliance_to_Crossroad2Alliance,
    &vPath_EY_Crossroad1Alliance_to_Mage_Tower,
    &vPath_EY_Crossroad1Alliance_to_Draenei_Ruins,
    &vPath_EY_Crossroad2Alliance_to_Mage_Tower,
    &vPath_EY_Crossroad2Alliance_to_Draenei_Ruins,
    &vPath_EY_Crossroad2Alliance_to_Flag,
    &vPath_EY_Draenei_Ruins_to_Blood_Elf_Tower,
    &vPath_EY_Fel_Reaver_to_Mage_Tower,
};

std::vector<BattleBotPath*> const vPaths_IC = {
    &vPath_IC_Ally_Dock_Crossroad_to_Ally_Docks_Second_Crossroad,
    &vPath_IC_Ally_Docks_Crossroad_to_Docks_Flag,
    &vPath_IC_Ally_Docks_Second_Crossroad_to_Ally_Docks_Crossroad,
    &vPath_IC_Lower_Graveyard_Crossroad_to_Ally_Docks_Crossroad,
    &vPath_IC_Lower_Graveyard_Crossroad_to_Ally_Docks_Second_Crossroad,
    &vPath_IC_Lower_Graveyard_to_Lower_Graveyard_Crossroad,
    &vPath_IC_Ally_Front_Crossroad_to_Ally_Dock_Crossroad,
    &vPath_IC_Ally_Front_Crossroad_to_Hangar_First_Crossroad,
    &vPath_IC_Ally_Front_Crossroad_to_Workshop,
    &vPath_IC_Ally_Keep_to_Ally_Dock_Crossroad,
    &vPath_IC_Ally_Keep_to_Ally_Front_Crossroad,
    &vPath_IC_Ally_Keep_to_Hangar_First_Crossroad,
    &vPath_IC_Ally_Keep_to_Quarry_Crossroad,
    &vPath_IC_Docks_Crossroad_to_Docks_Flag,
    &vPath_IC_Docks_Graveyard_to_Docks_Flag,
    &vPath_IC_Hangar_First_Crossroad_to_Hangar_Second_Crossroad,
    &vPath_IC_Hangar_Second_Crossroad_to_Hangar_Flag,
    &vPath_IC_Horde_Dock_Crossroad_to_Docks_Crossroad,
    &vPath_IC_Horde_Dock_Crossroad_to_Refinery_Crossroad,
    &vPath_IC_Horde_Front_Crossroad_to_Horde_Dock_Crossroad,
    &vPath_IC_Horde_Front_Crossroad_to_Horde_Hangar_Crossroad,
    &vPath_IC_Horde_Front_Crossroad_to_Workshop,
    &vPath_IC_Horde_Hangar_Crossroad_to_Hangar_Flag,
    &vPath_IC_Horde_Keep_to_Horde_Dock_Crossroad,
    &vPath_IC_Horde_Keep_to_Horde_Front_Crossroad,
    &vPath_IC_Horde_Keep_to_Horde_Hangar_Crossroad,
    &vPath_IC_Horde_Side_Gate_to_Refinery_Base,
    &vPath_IC_Quarry_Crossroad_to_Hangar_Second_Crossroad,
    &vPath_IC_Quarry_Crossroad_to_Quarry_Flag,
    &vPath_IC_Refinery_Crossroad_to_Docks_Crossroad,
    &vPath_IC_Refinery_Crossroad_to_Refinery_Base,
    &vPath_IC_Central_Graveyard_to_Workshop,
    &vPath_IC_Horde_East_Gate_to_Horde_Keep,
    &vPath_IC_Workshop_to_Workshop_Keep,
    &vPath_IC_Alliance_Base,
    &vPath_IC_Horde_Base,
    &vPath_IC_Workshop_to_North_West,
    &vPath_IC_South_West_Crossroads,
    &vPath_IC_Hanger_to_Workshop,
};

std::vector<BattleBotPath*> const vPaths_NoReverseAllowed = {
    &vPath_WSG_AllianceGraveyardJump,
    &vPath_WSG_HordeGraveyardJump,
    &vPath_IC_Central_Graveyard_to_Workshop,
    &vPath_IC_Docks_Graveyard_to_Docks_Flag,
};

static std::vector<std::pair<uint8, uint32>> AV_AttackObjectives_Horde = {
    // Attack - these are in order they should be attacked
    {BG_AV_NODES_STONEHEART_GRAVE, BG_AV_OBJECT_FLAG_A_STONEHEART_GRAVE},
    {BG_AV_NODES_STONEHEART_BUNKER, BG_AV_OBJECT_FLAG_A_STONEHEART_BUNKER},
    {BG_AV_NODES_ICEWING_BUNKER, BG_AV_OBJECT_FLAG_A_ICEWING_BUNKER},
    {BG_AV_NODES_STORMPIKE_GRAVE, BG_AV_OBJECT_FLAG_A_STORMPIKE_GRAVE},
    {BG_AV_NODES_DUNBALDAR_SOUTH, BG_AV_OBJECT_FLAG_A_DUNBALDAR_SOUTH},
    {BG_AV_NODES_DUNBALDAR_NORTH, BG_AV_OBJECT_FLAG_A_DUNBALDAR_NORTH},
    {BG_AV_NODES_FIRSTAID_STATION, BG_AV_OBJECT_FLAG_A_FIRSTAID_STATION},
};

static std::vector<std::pair<uint8, uint32>> AV_AttackObjectives_Alliance = {
    // Attack - these are in order they should be attacked
    {BG_AV_NODES_ICEBLOOD_GRAVE, BG_AV_OBJECT_FLAG_H_ICEBLOOD_GRAVE},
    {BG_AV_NODES_ICEBLOOD_TOWER, BG_AV_OBJECT_FLAG_H_ICEBLOOD_TOWER},
    {BG_AV_NODES_TOWER_POINT, BG_AV_OBJECT_FLAG_H_TOWER_POINT},
    {BG_AV_NODES_FROSTWOLF_GRAVE, BG_AV_OBJECT_FLAG_H_FROSTWOLF_GRAVE},
    {BG_AV_NODES_FROSTWOLF_ETOWER, BG_AV_OBJECT_FLAG_H_FROSTWOLF_ETOWER},
    {BG_AV_NODES_FROSTWOLF_WTOWER, BG_AV_OBJECT_FLAG_H_FROSTWOLF_WTOWER},
    {BG_AV_NODES_FROSTWOLF_HUT, BG_AV_OBJECT_FLAG_H_FROSTWOLF_HUT},
};

static std::vector<std::pair<uint8, uint32>> AV_DefendObjectives_Horde = {
    {BG_AV_NODES_FROSTWOLF_HUT, BG_AV_OBJECT_FLAG_H_FROSTWOLF_HUT},
    {BG_AV_NODES_FROSTWOLF_WTOWER, BG_AV_OBJECT_FLAG_H_FROSTWOLF_WTOWER},
    {BG_AV_NODES_FROSTWOLF_ETOWER, BG_AV_OBJECT_FLAG_H_FROSTWOLF_ETOWER},
    {BG_AV_NODES_FROSTWOLF_GRAVE, BG_AV_OBJECT_FLAG_H_FROSTWOLF_GRAVE},
    {BG_AV_NODES_TOWER_POINT, BG_AV_OBJECT_FLAG_H_TOWER_POINT},
    {BG_AV_NODES_ICEBLOOD_TOWER, BG_AV_OBJECT_FLAG_H_ICEBLOOD_TOWER},
    {BG_AV_NODES_ICEBLOOD_GRAVE, BG_AV_OBJECT_FLAG_H_ICEBLOOD_GRAVE},
};

static std::vector<std::pair<uint8, uint32>> AV_DefendObjectives_Alliance = {
    {BG_AV_NODES_FIRSTAID_STATION, BG_AV_OBJECT_FLAG_A_FIRSTAID_STATION},
    {BG_AV_NODES_DUNBALDAR_SOUTH, BG_AV_OBJECT_FLAG_A_DUNBALDAR_SOUTH},
    {BG_AV_NODES_DUNBALDAR_NORTH, BG_AV_OBJECT_FLAG_A_DUNBALDAR_NORTH},
    {BG_AV_NODES_STORMPIKE_GRAVE, BG_AV_OBJECT_FLAG_A_STORMPIKE_GRAVE},
    {BG_AV_NODES_ICEWING_BUNKER, BG_AV_OBJECT_FLAG_A_ICEWING_BUNKER},
    {BG_AV_NODES_STONEHEART_BUNKER, BG_AV_OBJECT_FLAG_A_STONEHEART_BUNKER},
    {BG_AV_NODES_STONEHEART_GRAVE, BG_AV_OBJECT_FLAG_A_STONEHEART_GRAVE},
};

static std::vector<std::pair<uint8, uint32>> AV_AllianceGraveyardRecapObjectives = {
    {BG_AV_NODES_FIRSTAID_STATION, BG_AV_OBJECT_FLAG_C_H_FIRSTAID_STATION},
    {BG_AV_NODES_STORMPIKE_GRAVE, BG_AV_OBJECT_FLAG_C_H_STORMPIKE_GRAVE},
    {BG_AV_NODES_STONEHEART_GRAVE, BG_AV_OBJECT_FLAG_C_H_STONEHEART_GRAVE},
    {BG_AV_NODES_SNOWFALL_GRAVE, BG_AV_OBJECT_FLAG_C_H_SNOWFALL_GRAVE},
    {BG_AV_NODES_ICEBLOOD_GRAVE, BG_AV_OBJECT_FLAG_C_H_ICEBLOOD_GRAVE},
    {BG_AV_NODES_FROSTWOLF_GRAVE, BG_AV_OBJECT_FLAG_C_H_FROSTWOLF_GRAVE},
    {BG_AV_NODES_FROSTWOLF_HUT, BG_AV_OBJECT_FLAG_C_H_FROSTWOLF_HUT},
};

static std::vector<std::pair<uint8, uint32>> AV_AllianceTowerRecapObjectives = {
    {BG_AV_NODES_DUNBALDAR_SOUTH, BG_AV_OBJECT_FLAG_C_H_DUNBALDAR_SOUTH},
    {BG_AV_NODES_DUNBALDAR_NORTH, BG_AV_OBJECT_FLAG_C_H_DUNBALDAR_NORTH},
    {BG_AV_NODES_ICEWING_BUNKER, BG_AV_OBJECT_FLAG_C_H_ICEWING_BUNKER},
    {BG_AV_NODES_STONEHEART_BUNKER, BG_AV_OBJECT_FLAG_C_H_STONEHEART_BUNKER},
};

static std::vector<std::pair<uint8, uint32>> AV_AllianceHomeGraveyardReclaimObjectives = {
    {BG_AV_NODES_FIRSTAID_STATION, BG_AV_OBJECT_FLAG_H_FIRSTAID_STATION},
    {BG_AV_NODES_STORMPIKE_GRAVE, BG_AV_OBJECT_FLAG_H_STORMPIKE_GRAVE},
    {BG_AV_NODES_STONEHEART_GRAVE, BG_AV_OBJECT_FLAG_H_STONEHEART_GRAVE},
    {BG_AV_NODES_SNOWFALL_GRAVE, BG_AV_OBJECT_FLAG_H_SNOWFALL_GRAVE},
};

static std::vector<std::pair<uint8, uint32>> AV_AllianceAssaultGuardObjectives = {
    {BG_AV_NODES_ICEBLOOD_GRAVE, BG_AV_OBJECT_FLAG_C_A_ICEBLOOD_GRAVE},
    {BG_AV_NODES_ICEBLOOD_TOWER, BG_AV_OBJECT_FLAG_C_A_ICEBLOOD_TOWER},
    {BG_AV_NODES_TOWER_POINT, BG_AV_OBJECT_FLAG_C_A_TOWER_POINT},
    {BG_AV_NODES_FROSTWOLF_GRAVE, BG_AV_OBJECT_FLAG_C_A_FROSTWOLF_GRAVE},
    {BG_AV_NODES_FROSTWOLF_ETOWER, BG_AV_OBJECT_FLAG_C_A_FROSTWOLF_ETOWER},
    {BG_AV_NODES_FROSTWOLF_WTOWER, BG_AV_OBJECT_FLAG_C_A_FROSTWOLF_WTOWER},
    {BG_AV_NODES_FROSTWOLF_HUT, BG_AV_OBJECT_FLAG_C_A_FROSTWOLF_HUT},
};

static bool IsAllianceHomeGraveyard(uint8 nodeId)
{
    return nodeId == BG_AV_NODES_FIRSTAID_STATION || nodeId == BG_AV_NODES_STORMPIKE_GRAVE ||
           nodeId == BG_AV_NODES_STONEHEART_GRAVE || nodeId == BG_AV_NODES_SNOWFALL_GRAVE;
}

static bool IsAllianceCoreGraveyard(uint8 nodeId)
{
    return nodeId == BG_AV_NODES_STORMPIKE_GRAVE || nodeId == BG_AV_NODES_STONEHEART_GRAVE;
}

static bool IsAllianceDunBaldarTower(uint8 nodeId)
{
    return nodeId == BG_AV_NODES_DUNBALDAR_SOUTH || nodeId == BG_AV_NODES_DUNBALDAR_NORTH;
}

static bool IsAllianceDunBaldarCoreDefense(uint8 nodeId)
{
    return nodeId == BG_AV_NODES_FIRSTAID_STATION || nodeId == BG_AV_NODES_STORMPIKE_GRAVE ||
           IsAllianceDunBaldarTower(nodeId);
}

static bool IsAllianceDefensiveTower(uint8 nodeId)
{
    return IsAllianceDunBaldarTower(nodeId) || nodeId == BG_AV_NODES_ICEWING_BUNKER ||
           nodeId == BG_AV_NODES_STONEHEART_BUNKER;
}

static bool IsAllianceHordeTowerAttackTarget(uint8 nodeId)
{
    return nodeId == BG_AV_NODES_ICEBLOOD_TOWER || nodeId == BG_AV_NODES_TOWER_POINT ||
           nodeId == BG_AV_NODES_FROSTWOLF_ETOWER || nodeId == BG_AV_NODES_FROSTWOLF_WTOWER;
}

static bool IsAlteracTowerOrBunkerNode(uint8 nodeId)
{
    return IsAllianceDefensiveTower(nodeId) || IsAllianceHordeTowerAttackTarget(nodeId);
}

static bool TryGetAlteracTowerOrBunkerFlagNode(Battleground* bg, GameObject* go, uint8& nodeId)
{
    if (!bg || !go)
        return false;

    std::pair<uint8, uint32> const towerFlags[] = {
        {BG_AV_NODES_DUNBALDAR_SOUTH, BG_AV_OBJECT_FLAG_A_DUNBALDAR_SOUTH},
        {BG_AV_NODES_DUNBALDAR_SOUTH, BG_AV_OBJECT_FLAG_C_H_DUNBALDAR_SOUTH},
        {BG_AV_NODES_DUNBALDAR_NORTH, BG_AV_OBJECT_FLAG_A_DUNBALDAR_NORTH},
        {BG_AV_NODES_DUNBALDAR_NORTH, BG_AV_OBJECT_FLAG_C_H_DUNBALDAR_NORTH},
        {BG_AV_NODES_ICEWING_BUNKER, BG_AV_OBJECT_FLAG_A_ICEWING_BUNKER},
        {BG_AV_NODES_ICEWING_BUNKER, BG_AV_OBJECT_FLAG_C_H_ICEWING_BUNKER},
        {BG_AV_NODES_STONEHEART_BUNKER, BG_AV_OBJECT_FLAG_A_STONEHEART_BUNKER},
        {BG_AV_NODES_STONEHEART_BUNKER, BG_AV_OBJECT_FLAG_C_H_STONEHEART_BUNKER},
        {BG_AV_NODES_ICEBLOOD_TOWER, BG_AV_OBJECT_FLAG_H_ICEBLOOD_TOWER},
        {BG_AV_NODES_ICEBLOOD_TOWER, BG_AV_OBJECT_FLAG_C_A_ICEBLOOD_TOWER},
        {BG_AV_NODES_TOWER_POINT, BG_AV_OBJECT_FLAG_H_TOWER_POINT},
        {BG_AV_NODES_TOWER_POINT, BG_AV_OBJECT_FLAG_C_A_TOWER_POINT},
        {BG_AV_NODES_FROSTWOLF_ETOWER, BG_AV_OBJECT_FLAG_H_FROSTWOLF_ETOWER},
        {BG_AV_NODES_FROSTWOLF_ETOWER, BG_AV_OBJECT_FLAG_C_A_FROSTWOLF_ETOWER},
        {BG_AV_NODES_FROSTWOLF_WTOWER, BG_AV_OBJECT_FLAG_H_FROSTWOLF_WTOWER},
        {BG_AV_NODES_FROSTWOLF_WTOWER, BG_AV_OBJECT_FLAG_C_A_FROSTWOLF_WTOWER},
    };

    for (auto const& [candidateNodeId, goId] : towerFlags)
    {
        if (bg->GetBGObject(goId) == go)
        {
            nodeId = candidateNodeId;
            return true;
        }
    }

    return false;
}

static bool IsWrongFloorAlteracTowerFlag(Player* bot, Battleground* bg, GameObject* go)
{
    if (!bot || !bg || !go)
        return false;

    uint8 nodeId = 0;
    if (!TryGetAlteracTowerOrBunkerFlagNode(bg, go, nodeId) || !IsAlteracTowerOrBunkerNode(nodeId))
        return false;

    if (std::fabs(bot->GetPositionZ() - go->GetPositionZ()) <= 4.5f)
        return false;

    return !bot->IsWithinLOSInMap(go);
}

static bool TryGetAlteracTowerOrBunkerObjectiveNode(PositionInfo const& objectivePos, uint8& nodeId)
{
    if (!objectivePos.valueSet)
        return false;

    for (auto const& [candidateNodeId, data] : AVNodeMovementTargets)
    {
        if (!IsAlteracTowerOrBunkerNode(candidateNodeId))
            continue;

        float const dx = objectivePos.x - data.pos.GetPositionX();
        float const dy = objectivePos.y - data.pos.GetPositionY();
        if (dx * dx + dy * dy <= 20.0f * 20.0f &&
            std::fabs(objectivePos.z - data.pos.GetPositionZ()) <= 35.0f)
        {
            nodeId = candidateNodeId;
            return true;
        }
    }

    return false;
}

static bool GetAlteracTowerOrBunkerAccessPath(uint8 nodeId, std::vector<Position> const*& path)
{
    static std::vector<Position> const dunBaldarSouth = {
        Position(562.523f, -74.503f, 37.947f),
        Position(558.097f, -70.984f, 52.488f),
        Position(556.551f, -77.240f, 51.931f),
    };
    static std::vector<Position> const dunBaldarNorth = {
        Position(664.505f, -139.452f, 49.670f),
        Position(674.576f, -147.101f, 56.543f),
        Position(670.664f, -142.031f, 63.666f),
    };
    static std::vector<Position> const icewingBunker = {
        Position(195.369f, -407.750f, 42.876f),
        Position(210.619f, -376.938f, 49.268f),
        Position(196.605f, -369.187f, 56.391f),
        Position(200.310f, -361.232f, 56.387f),
    };
    static std::vector<Position> const stonehearthBunker = {
        Position(-129.793f, -481.160f, 27.735f),
        Position(-154.076f, -466.929f, 41.064f),
        Position(-151.638f, -439.521f, 40.380f),
        Position(-156.302f, -440.032f, 40.403f),
    };
    static std::vector<Position> const icebloodTower = {
        Position(-574.093f, -312.653f, 44.791f),
        Position(-566.035f, -273.907f, 52.958f),
        Position(-572.667f, -267.923f, 56.854f),
        Position(-571.476f, -257.234f, 63.322f),
        Position(-561.021f, -262.689f, 68.459f),
        Position(-568.318f, -267.100f, 75.001f),
        Position(-569.702f, -265.362f, 75.009f),
    };
    static std::vector<Position> const towerPoint = {
        Position(-754.564f, -344.804f, 67.422f),
        Position(-764.109f, -366.069f, 70.093f),
        Position(-773.333f, -364.653f, 79.235f),
        Position(-776.072f, -368.046f, 84.356f),
        Position(-777.564f, -368.521f, 90.670f),
        Position(-768.199f, -363.105f, 90.895f),
    };
    static std::vector<Position> const frostwolfEastTower = {
        Position(-1304.870f, -304.525f, 91.837f),
        Position(-1301.770f, -310.974f, 95.825f),
        Position(-1305.580f, -320.625f, 102.166f),
        Position(-1293.890f, -313.478f, 107.328f),
        Position(-1304.600f, -310.754f, 113.859f),
        Position(-1303.737f, -314.070f, 113.868f),
    };
    static std::vector<Position> const frostwolfWestTower = {
        Position(-1308.240f, -273.260f, 92.051f),
        Position(-1302.260f, -262.858f, 95.927f),
        Position(-1295.550f, -263.865f, 105.033f),
        Position(-1304.430f, -273.682f, 107.612f),
        Position(-1303.410f, -268.237f, 114.151f),
        Position(-1300.648f, -267.356f, 114.151f),
    };

    switch (nodeId)
    {
        case BG_AV_NODES_DUNBALDAR_SOUTH:
            path = &dunBaldarSouth;
            return true;
        case BG_AV_NODES_DUNBALDAR_NORTH:
            path = &dunBaldarNorth;
            return true;
        case BG_AV_NODES_ICEWING_BUNKER:
            path = &icewingBunker;
            return true;
        case BG_AV_NODES_STONEHEART_BUNKER:
            path = &stonehearthBunker;
            return true;
        case BG_AV_NODES_ICEBLOOD_TOWER:
            path = &icebloodTower;
            return true;
        case BG_AV_NODES_TOWER_POINT:
            path = &towerPoint;
            return true;
        case BG_AV_NODES_FROSTWOLF_ETOWER:
            path = &frostwolfEastTower;
            return true;
        case BG_AV_NODES_FROSTWOLF_WTOWER:
            path = &frostwolfWestTower;
            return true;
        default:
            return false;
    }
}

static bool IsAlteracTowerOrBunkerSameFloorOrVisible(Player* bot, Position const& position)
{
    if (!bot)
        return false;

    if (std::fabs(bot->GetPositionZ() - position.GetPositionZ()) <= 4.5f)
        return true;

    return bot->IsWithinLOS(position.GetPositionX(), position.GetPositionY(), position.GetPositionZ());
}

static bool IsUnsafeAlteracTowerOrBunkerDirectMove(Player* bot, PositionInfo const& objectivePos)
{
    if (!bot || !objectivePos.valueSet)
        return false;

    uint8 nodeId = 0;
    if (!TryGetAlteracTowerOrBunkerObjectiveNode(objectivePos, nodeId))
        return false;

    Position const objective(objectivePos.x, objectivePos.y, objectivePos.z);
    return !IsAlteracTowerOrBunkerSameFloorOrVisible(bot, objective);
}

static bool SelectAlteracTowerOrBunkerAccessTarget(Player* bot, PositionInfo const& objectivePos, Position& target)
{
    if (!bot || !objectivePos.valueSet)
        return false;

    uint8 nodeId = 0;
    if (!TryGetAlteracTowerOrBunkerObjectiveNode(objectivePos, nodeId))
        return false;

    float const objective2d = bot->GetDistance2d(objectivePos.x, objectivePos.y);
    float const objectiveZDiff = std::fabs(bot->GetPositionZ() - objectivePos.z);
    if (objective2d > 140.0f || objectiveZDiff < 4.0f)
        return false;

    std::vector<Position> const* path = nullptr;
    if (!GetAlteracTowerOrBunkerAccessPath(nodeId, path) || !path || path->empty())
        return false;

    uint32 const lastPoint = static_cast<uint32>(path->size() - 1);
    uint32 closestPoint = static_cast<uint32>(path->size());
    float closestDistance = FLT_MAX;

    for (uint32 i = 0; i < path->size(); ++i)
    {
        Position const& waypoint = path->at(i);
        if (!IsAlteracTowerOrBunkerSameFloorOrVisible(bot, waypoint))
            continue;

        float const distance = bot->GetDistance(waypoint.GetPositionX(), waypoint.GetPositionY(), waypoint.GetPositionZ());
        if (distance < closestDistance)
        {
            closestDistance = distance;
            closestPoint = i;
        }
    }

    if (closestPoint >= path->size())
        return false;

    uint32 targetPoint = closestPoint;
    bool targetIsAccessProgression = false;
    if (closestPoint == lastPoint)
    {
        if (objective2d > 22.0f || objectiveZDiff < 4.0f)
            return false;

        targetPoint = 0;
    }
    else if (closestDistance <= 8.0f)
    {
        targetPoint = closestPoint + 1;
        targetIsAccessProgression = true;
    }

    if (!targetIsAccessProgression && !IsAlteracTowerOrBunkerSameFloorOrVisible(bot, path->at(targetPoint)))
        return false;

    target = path->at(targetPoint);
    return true;
}

static bool IsAllianceForwardGraveyardAttackTarget(uint8 nodeId)
{
    return nodeId == BG_AV_NODES_FROSTWOLF_GRAVE || nodeId == BG_AV_NODES_FROSTWOLF_HUT;
}

static bool IsAllianceFrostwolfObjective(uint8 nodeId)
{
    return nodeId == BG_AV_NODES_FROSTWOLF_GRAVE || nodeId == BG_AV_NODES_FROSTWOLF_HUT ||
           nodeId == BG_AV_NODES_FROSTWOLF_ETOWER || nodeId == BG_AV_NODES_FROSTWOLF_WTOWER;
}

static bool IsAllianceNodeUnderHordePressure(BattlegroundAV* av, uint8 nodeId)
{
    if (!av)
        return false;

    BG_AV_NodeInfo const& node = av->GetAVNodeInfo(nodeId);
    if (node.State == POINT_ASSAULTED && node.OwnerId == TEAM_HORDE && node.PrevOwnerId == TEAM_ALLIANCE)
        return true;

    if (IsAllianceHomeGraveyard(nodeId) && node.State == POINT_CONTROLLED && node.OwnerId == TEAM_HORDE)
        return true;

    return IsAllianceDefensiveTower(nodeId) && node.State == POINT_DESTROYED;
}

static bool AllianceHasDunBaldarCorePressure(BattlegroundAV* av)
{
    if (!av)
        return false;

    return IsAllianceNodeUnderHordePressure(av, BG_AV_NODES_FIRSTAID_STATION) ||
           IsAllianceNodeUnderHordePressure(av, BG_AV_NODES_STORMPIKE_GRAVE) ||
           IsAllianceNodeUnderHordePressure(av, BG_AV_NODES_DUNBALDAR_SOUTH) ||
           IsAllianceNodeUnderHordePressure(av, BG_AV_NODES_DUNBALDAR_NORTH);
}

static uint32 GetAllianceDefensivePressure(BattlegroundAV* av)
{
    if (!av)
        return 0;

    uint32 pressure = 0;
    for (auto const& [nodeId, _] : AV_DefendObjectives_Alliance)
        if (IsAllianceNodeUnderHordePressure(av, nodeId))
            pressure++;

    return pressure;
}

static bool AllianceHasSnowfallBlocker(BattlegroundAV* av)
{
    if (!av)
        return true;

    uint8 const blockers[] = {
        BG_AV_NODES_STORMPIKE_GRAVE,
        BG_AV_NODES_STONEHEART_GRAVE,
        BG_AV_NODES_DUNBALDAR_SOUTH,
        BG_AV_NODES_DUNBALDAR_NORTH,
        BG_AV_NODES_ICEWING_BUNKER,
        BG_AV_NODES_STONEHEART_BUNKER,
    };

    for (uint8 nodeId : blockers)
        if (IsAllianceNodeUnderHordePressure(av, nodeId))
            return true;

    return false;
}

enum AllianceAVRushLevel : uint8
{
    AV_RUSH_NONE = 0,
    AV_RUSH_FRONT = 1,
    AV_RUSH_DEEP = 2,
    AV_RUSH_DUNBALDAR = 3,
};

enum AllianceAVThreatLevel : uint8
{
    AV_THREAT_NONE = 0,
    AV_THREAT_LOW = 1,
    AV_THREAT_MEDIUM = 2,
    AV_THREAT_HIGH = 3,
    AV_THREAT_CRITICAL = 4,
};

enum AllianceAVBattlefieldMode : uint8
{
    AV_MODE_OPENING_CONTROL = 0,
    AV_MODE_IBGY_PUSH = 1,
    AV_MODE_IBGY_GUARD = 2,
    AV_MODE_IBGY_BREAKTHROUGH = 3,
    AV_MODE_GALVANGAR_STRIKE = 4,
    AV_MODE_SOUTH_TOWER_SPLIT = 5,
    AV_MODE_NORTH_DEFENSE = 6,
    AV_MODE_DUN_BALDAR_EMERGENCY = 7,
    AV_MODE_DREK_PUSH = 8,
    AV_MODE_FROSTWOLF_LOCK = 9,
    AV_MODE_DREK_SETUP = 10,
};

enum AllianceAVObjectiveAssignment : uint8
{
    AV_ASSIGN_NONE = 0,
    AV_ASSIGN_NORTH_DEFENSE = 1,
    AV_ASSIGN_ICEBLOOD_ATTACK = 2,
    AV_ASSIGN_ICEBLOOD_HOLD = 3,
    AV_ASSIGN_SNOWFALL = 4,
    AV_ASSIGN_GALVANGAR = 5,
    AV_ASSIGN_FROSTWOLF_LOCK = 6,
    AV_ASSIGN_DREK_PUSH = 7,
    AV_ASSIGN_ASSAULT_GUARD = 8,
};

struct AllianceAVRushInfo
{
    AllianceAVRushLevel level = AV_RUSH_NONE;
    uint32 hordeNorth = 0;
    uint32 hordeDeepNorth = 0;
    uint32 hordeDunBaldar = 0;
    uint32 allianceNorth = 0;
    uint32 allianceSouth = 0;
    uint32 elapsedMs = 0;

    bool IsActive() const { return level != AV_RUSH_NONE; }
};

static char const* GetAllianceAVRushLevelName(AllianceAVRushLevel level)
{
    switch (level)
    {
        case AV_RUSH_FRONT:
            return "front";
        case AV_RUSH_DEEP:
            return "deep";
        case AV_RUSH_DUNBALDAR:
            return "dunbaldar";
        case AV_RUSH_NONE:
        default:
            return "none";
    }
}

static char const* GetAllianceAVThreatLevelName(AllianceAVThreatLevel level)
{
    switch (level)
    {
        case AV_THREAT_LOW:
            return "low";
        case AV_THREAT_MEDIUM:
            return "medium";
        case AV_THREAT_HIGH:
            return "high";
        case AV_THREAT_CRITICAL:
            return "critical";
        case AV_THREAT_NONE:
        default:
            return "none";
    }
}

static char const* GetAllianceAVBattlefieldModeName(AllianceAVBattlefieldMode mode)
{
    switch (mode)
    {
        case AV_MODE_OPENING_CONTROL:
            return "opening_control";
        case AV_MODE_IBGY_PUSH:
            return "ibgy_push";
        case AV_MODE_IBGY_GUARD:
            return "ibgy_guard";
        case AV_MODE_IBGY_BREAKTHROUGH:
            return "ibgy_breakthrough";
        case AV_MODE_GALVANGAR_STRIKE:
            return "galvangar_strike";
        case AV_MODE_SOUTH_TOWER_SPLIT:
            return "south_tower_split";
        case AV_MODE_NORTH_DEFENSE:
            return "north_defense";
        case AV_MODE_DUN_BALDAR_EMERGENCY:
            return "dun_baldar_emergency";
        case AV_MODE_DREK_PUSH:
            return "drek_push";
        case AV_MODE_FROSTWOLF_LOCK:
            return "frostwolf_lock";
        case AV_MODE_DREK_SETUP:
            return "drek_setup";
        default:
            return "unknown";
    }
}

namespace
{
constexpr uint32 ASYNC_AV_CACHE_LOG_INTERVAL = 30000;
constexpr uint32 ASYNC_AV_CACHE_STALE_MS = 5 * 60 * 1000;

static bool AsyncAVStrategyCacheEnabled()
{
    return sPlayerbotAIConfig.asyncAVStrategyCache || sPlayerbotAIConfig.asyncAVObjectiveAssignments;
}

enum AsyncAVAreaId : uint8
{
    ASYNC_AV_AREA_ICEBLOOD_180 = 0,
    ASYNC_AV_AREA_ICEBLOOD_220,
    ASYNC_AV_AREA_TOWER_POINT_180,
    ASYNC_AV_AREA_SNOWFALL_160,
    ASYNC_AV_AREA_STONEHEARTH_220,
    ASYNC_AV_AREA_STORMPIKE_220,
    ASYNC_AV_AREA_DUNBALDAR_260,
    ASYNC_AV_AREA_FROSTWOLF_260,
    ASYNC_AV_AREA_DREK_220,
    ASYNC_AV_AREA_MAX
};

struct AsyncAVAreaDef
{
    AsyncAVAreaId id;
    float x;
    float y;
    float radius;
};

static std::array<AsyncAVAreaDef, ASYNC_AV_AREA_MAX> const AsyncAVAreas = {{
    {ASYNC_AV_AREA_ICEBLOOD_180, -617.858f, -400.654f, 180.0f},
    {ASYNC_AV_AREA_ICEBLOOD_220, -617.858f, -400.654f, 220.0f},
    {ASYNC_AV_AREA_TOWER_POINT_180, -767.439f, -360.200f, 180.0f},
    {ASYNC_AV_AREA_SNOWFALL_160, -201.298f, -119.661f, 160.0f},
    {ASYNC_AV_AREA_STONEHEARTH_220, 76.108f, -399.602f, 220.0f},
    {ASYNC_AV_AREA_STORMPIKE_220, 665.598f, -292.976f, 220.0f},
    {ASYNC_AV_AREA_DUNBALDAR_260, 640.364f, -36.535f, 260.0f},
    {ASYNC_AV_AREA_FROSTWOLF_260, -1083.803f, -341.520f, 260.0f},
    {ASYNC_AV_AREA_DREK_220, -1361.773f, -248.977f, 220.0f},
}};

struct AsyncAVPlayerSnapshot
{
    uint64 guid = 0;
    TeamId teamId = TEAM_NEUTRAL;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    uint8 role = 0;
    bool alive = false;
    bool inCombat = false;
    bool isBot = false;
};

struct AsyncAVNodeSnapshot
{
    uint8 state = 0;
    TeamId ownerId = TEAM_NEUTRAL;
    TeamId prevOwnerId = TEAM_NEUTRAL;
    TeamId totalOwnerId = TEAM_NEUTRAL;
    uint32 timer = 0;
};

struct AsyncAVAreaCounts
{
    uint32 allianceAlive = 0;
    uint32 hordeAlive = 0;
    uint32 allianceTotal = 0;
    uint32 hordeTotal = 0;
    uint32 allianceNotCombat = 0;
    uint32 hordeNotCombat = 0;

    void Add(TeamId teamId, bool alive, bool inCombat)
    {
        if (teamId == TEAM_ALLIANCE)
        {
            ++allianceTotal;
            if (alive)
                ++allianceAlive;
            if (!inCombat)
                ++allianceNotCombat;
        }
        else if (teamId == TEAM_HORDE)
        {
            ++hordeTotal;
            if (alive)
                ++hordeAlive;
            if (!inCombat)
                ++hordeNotCombat;
        }
    }
};

struct AsyncAVStrategyResult
{
    uint32 generation = 0;
    uint32 instanceId = 0;
    uint32 elapsedMs = 0;
    uint32 lastAccessMs = 0;
    uint32 playerCount = 0;
    uint32 aliveAlliance = 0;
    uint32 aliveHorde = 0;
    uint32 hordeNorth = 0;
    uint32 hordeDeepNorth = 0;
    uint32 hordeDunBaldar = 0;
    uint32 allianceNorth = 0;
    uint32 allianceSouth = 0;
    uint32 hordeTowersDown = 0;
    uint32 hordeTowerProgress = 0;
    uint32 allianceDefensivePressure = 0;
    uint8 allianceStrategy = AV_STRATEGY_BALANCED;
    uint8 hordeStrategy = AV_STRATEGY_BALANCED;
    uint8 allianceRushLevel = AV_RUSH_NONE;
    uint8 allianceThreat = AV_THREAT_NONE;
    uint8 allianceMode = AV_MODE_OPENING_CONTROL;
    uint8 allianceDefenderLimit = 4;
    uint8 allianceReleasedDefenderLimit = 1;
    bool allianceTacticalOrdersValid = false;
    bool allianceFullRecall = false;
    bool allianceFinalDrekWindow = false;
    bool allianceCanReleaseIdleNorthReserve = false;
    bool allianceShouldScreenIdleNorthReserve = false;
    bool allianceNeedsIcebloodMainForce = false;
    bool allianceObjectiveAssignmentsValid = false;
    std::array<AsyncAVAreaCounts, ASYNC_AV_AREA_MAX> areas;
    std::unordered_map<uint64, uint8> allianceObjectiveAssignments;
};

using AsyncAVStrategyResultMap = std::unordered_map<uint32, AsyncAVStrategyResult>;

struct AsyncAVStrategyJob
{
    uint32 generation = 0;
    uint32 instanceId = 0;
    uint32 elapsedMs = 0;
    uint8 allianceStrategy = AV_STRATEGY_BALANCED;
    uint8 hordeStrategy = AV_STRATEGY_BALANCED;
    bool hordeCaptainAlive = false;
    std::vector<AsyncAVPlayerSnapshot> players;
    std::array<AsyncAVNodeSnapshot, BG_AV_NODES_MAX> nodes;
};

static bool AsyncAVAllianceNodeUnderHordePressure(std::array<AsyncAVNodeSnapshot, BG_AV_NODES_MAX> const& nodes,
                                                  uint8 nodeId)
{
    if (nodeId >= BG_AV_NODES_MAX)
        return false;

    AsyncAVNodeSnapshot const& node = nodes[nodeId];
    if (node.state == POINT_ASSAULTED && node.ownerId == TEAM_HORDE && node.prevOwnerId == TEAM_ALLIANCE)
        return true;

    if (IsAllianceHomeGraveyard(nodeId) && node.state == POINT_CONTROLLED && node.ownerId == TEAM_HORDE)
        return true;

    return IsAllianceDefensiveTower(nodeId) && node.state == POINT_DESTROYED;
}

static bool AsyncAVHordeTowerAssaultedNearCompletion(std::array<AsyncAVNodeSnapshot, BG_AV_NODES_MAX> const& nodes,
                                                     uint8 nodeId)
{
    if (nodeId >= BG_AV_NODES_MAX)
        return false;

    AsyncAVNodeSnapshot const& node = nodes[nodeId];
    return node.state == POINT_ASSAULTED && node.ownerId == TEAM_ALLIANCE &&
           node.timer > 0 && node.timer <= 120000;
}

static bool AsyncAVNodeControlledBy(std::array<AsyncAVNodeSnapshot, BG_AV_NODES_MAX> const& nodes, uint8 nodeId,
                                    TeamId teamId)
{
    if (nodeId >= BG_AV_NODES_MAX)
        return false;

    AsyncAVNodeSnapshot const& node = nodes[nodeId];
    return node.state == POINT_CONTROLLED && node.ownerId == teamId;
}

static bool AsyncAVNodeAssaultedBy(std::array<AsyncAVNodeSnapshot, BG_AV_NODES_MAX> const& nodes, uint8 nodeId,
                                   TeamId teamId)
{
    if (nodeId >= BG_AV_NODES_MAX)
        return false;

    AsyncAVNodeSnapshot const& node = nodes[nodeId];
    return node.state == POINT_ASSAULTED && node.ownerId == teamId;
}

static bool AsyncAVAllianceDBTowerTimerLow(std::array<AsyncAVNodeSnapshot, BG_AV_NODES_MAX> const& nodes,
                                           uint8 nodeId)
{
    if (!IsAllianceDunBaldarTower(nodeId) || nodeId >= BG_AV_NODES_MAX)
        return false;

    AsyncAVNodeSnapshot const& node = nodes[nodeId];
    return node.state == POINT_ASSAULTED && node.ownerId == TEAM_HORDE &&
           node.timer > 0 && node.timer <= 90000;
}

static bool AsyncAVAllianceHasForwardDrekRespawn(std::array<AsyncAVNodeSnapshot, BG_AV_NODES_MAX> const& nodes)
{
    return AsyncAVNodeControlledBy(nodes, BG_AV_NODES_FROSTWOLF_GRAVE, TEAM_ALLIANCE) ||
           AsyncAVNodeControlledBy(nodes, BG_AV_NODES_FROSTWOLF_HUT, TEAM_ALLIANCE);
}

static bool AsyncAVAllianceHasAnySouthRespawn(std::array<AsyncAVNodeSnapshot, BG_AV_NODES_MAX> const& nodes)
{
    return AsyncAVAllianceHasForwardDrekRespawn(nodes) ||
           AsyncAVNodeControlledBy(nodes, BG_AV_NODES_ICEBLOOD_GRAVE, TEAM_ALLIANCE);
}

static bool AsyncAVAllianceDunBaldarBaseActuallyLost(std::array<AsyncAVNodeSnapshot, BG_AV_NODES_MAX> const& nodes)
{
    bool const bothDbBunkersLow = AsyncAVAllianceDBTowerTimerLow(nodes, BG_AV_NODES_DUNBALDAR_SOUTH) &&
                                  AsyncAVAllianceDBTowerTimerLow(nodes, BG_AV_NODES_DUNBALDAR_NORTH);
    bool const firstAidLost = AsyncAVNodeControlledBy(nodes, BG_AV_NODES_FIRSTAID_STATION, TEAM_HORDE);
    return bothDbBunkersLow || firstAidLost;
}

static AllianceAVRushLevel AsyncAVGetAllianceRushLevel(AsyncAVStrategyResult const& result,
                                                       AVBotStrategy hordeStrategy)
{
    bool const earlyGame = result.elapsedMs < 6 * 60 * 1000;
    if (result.hordeDunBaldar >= 3 || result.hordeDeepNorth >= 10 || (earlyGame && result.hordeDeepNorth >= 6))
        return result.hordeDunBaldar >= 3 ? AV_RUSH_DUNBALDAR : AV_RUSH_DEEP;

    if (result.hordeNorth >= 8 || (earlyGame && result.hordeNorth >= 5) ||
        (hordeStrategy == AV_STRATEGY_OFFENSIVE && result.hordeNorth >= 3))
        return AV_RUSH_FRONT;

    return AV_RUSH_NONE;
}

static AllianceAVThreatLevel AsyncAVGetAllianceThreat(std::array<AsyncAVNodeSnapshot, BG_AV_NODES_MAX> const& nodes,
                                                      AsyncAVStrategyResult const& result,
                                                      AllianceAVRushLevel rushLevel)
{
    bool const dbSouthAssaulted = AsyncAVNodeAssaultedBy(nodes, BG_AV_NODES_DUNBALDAR_SOUTH, TEAM_HORDE);
    bool const dbNorthAssaulted = AsyncAVNodeAssaultedBy(nodes, BG_AV_NODES_DUNBALDAR_NORTH, TEAM_HORDE);
    bool const stormpikeAssaulted = AsyncAVNodeAssaultedBy(nodes, BG_AV_NODES_STORMPIKE_GRAVE, TEAM_HORDE);
    bool const firstAidLost = AsyncAVNodeControlledBy(nodes, BG_AV_NODES_FIRSTAID_STATION, TEAM_HORDE);

    if (dbSouthAssaulted || dbNorthAssaulted || stormpikeAssaulted || firstAidLost)
        return AV_THREAT_CRITICAL;

    if (result.hordeDunBaldar >= 3 || rushLevel == AV_RUSH_DUNBALDAR)
        return AV_THREAT_HIGH;

    if (result.hordeDeepNorth >= 3 || rushLevel == AV_RUSH_DEEP)
        return AV_THREAT_MEDIUM;

    if (result.hordeNorth >= 3 || rushLevel == AV_RUSH_FRONT)
        return AV_THREAT_LOW;

    return AV_THREAT_NONE;
}

static bool AsyncAVAllianceHasFinalDrekWindow(std::array<AsyncAVNodeSnapshot, BG_AV_NODES_MAX> const& nodes,
                                             AsyncAVStrategyResult const& result, bool hordeCaptainAlive)
{
    if (!AsyncAVAllianceHasAnySouthRespawn(nodes) || hordeCaptainAlive)
        return false;

    if (result.hordeTowersDown >= 4)
    {
        uint32 const minimumSouthCore = AsyncAVAllianceHasForwardDrekRespawn(nodes) ? 6 : 14;
        return result.allianceSouth >= minimumSouthCore;
    }

    return result.hordeTowersDown >= 3 && AsyncAVAllianceHasForwardDrekRespawn(nodes) &&
           result.allianceSouth >= 12;
}

static bool AsyncAVAllianceHasCommittedFrostwolfFoothold(
    std::array<AsyncAVNodeSnapshot, BG_AV_NODES_MAX> const& nodes, AsyncAVStrategyResult const& result,
    AllianceAVThreatLevel threat, bool hordeCaptainAlive)
{
    return threat < AV_THREAT_HIGH && !hordeCaptainAlive && AsyncAVAllianceHasForwardDrekRespawn(nodes) &&
           result.allianceSouth >= 10;
}

static bool AsyncAVAllianceNorthIsQuiet(AsyncAVStrategyResult const& result, AllianceAVThreatLevel threat,
                                        AllianceAVRushLevel rushLevel)
{
    if (threat != AV_THREAT_NONE || rushLevel != AV_RUSH_NONE || result.allianceDefensivePressure > 0)
        return false;

    return result.hordeNorth == 0 && result.hordeDeepNorth == 0 && result.hordeDunBaldar == 0;
}

static bool AsyncAVAllianceHasOnlyMinorNorthContact(AsyncAVStrategyResult const& result,
                                                    AllianceAVThreatLevel threat,
                                                    AllianceAVRushLevel rushLevel)
{
    if (threat != AV_THREAT_NONE || rushLevel != AV_RUSH_NONE || result.allianceDefensivePressure > 0)
        return false;

    return result.hordeNorth <= 2 && result.hordeDeepNorth == 0 && result.hordeDunBaldar == 0;
}

static bool AsyncAVAllianceNeedsIcebloodBreakthrough(
    std::array<AsyncAVNodeSnapshot, BG_AV_NODES_MAX> const& nodes, AsyncAVStrategyResult const& result,
    AllianceAVThreatLevel threat, AllianceAVRushLevel rushLevel, AVBotStrategy hordeStrategy, bool hordeCaptainAlive)
{
    if (hordeStrategy != AV_STRATEGY_DEFENSIVE || threat != AV_THREAT_NONE || rushLevel != AV_RUSH_NONE)
        return false;

    if (hordeCaptainAlive || AsyncAVAllianceHasForwardDrekRespawn(nodes))
        return false;

    if (result.hordeTowersDown > 0 || result.hordeTowerProgress > 0)
        return false;

    return !AsyncAVNodeControlledBy(nodes, BG_AV_NODES_ICEBLOOD_GRAVE, TEAM_ALLIANCE);
}

static bool AsyncAVAllianceNeedsIcebloodMainForce(
    std::array<AsyncAVNodeSnapshot, BG_AV_NODES_MAX> const& nodes, AsyncAVStrategyResult const& result,
    AllianceAVThreatLevel threat, AllianceAVRushLevel rushLevel, AllianceAVBattlefieldMode mode,
    bool hordeCaptainAlive)
{
    if (!AsyncAVAllianceNorthIsQuiet(result, threat, rushLevel))
        return false;

    if (mode == AV_MODE_IBGY_BREAKTHROUGH)
        return true;

    if (mode != AV_MODE_IBGY_PUSH && mode != AV_MODE_IBGY_GUARD)
        return false;

    if (AsyncAVAllianceHasForwardDrekRespawn(nodes) && !hordeCaptainAlive)
        return false;

    if (result.allianceSouth >= 16)
        return false;

    bool const icebloodAssaulted = AsyncAVNodeAssaultedBy(nodes, BG_AV_NODES_ICEBLOOD_GRAVE, TEAM_ALLIANCE);
    bool const icebloodControlled = AsyncAVNodeControlledBy(nodes, BG_AV_NODES_ICEBLOOD_GRAVE, TEAM_ALLIANCE);
    return icebloodAssaulted || icebloodControlled || !hordeCaptainAlive;
}

static bool AsyncAVAllianceCanReleaseIdleNorthReserve(AsyncAVStrategyResult const& result,
                                                      AllianceAVThreatLevel threat,
                                                      AllianceAVRushLevel rushLevel,
                                                      AllianceAVBattlefieldMode mode,
                                                      bool needsIcebloodMainForce)
{
    if (mode == AV_MODE_IBGY_BREAKTHROUGH &&
        AsyncAVAllianceHasOnlyMinorNorthContact(result, threat, rushLevel))
        return true;

    if (!AsyncAVAllianceNorthIsQuiet(result, threat, rushLevel))
        return false;

    if (needsIcebloodMainForce)
        return true;

    if ((mode == AV_MODE_FROSTWOLF_LOCK || mode == AV_MODE_DREK_SETUP || mode == AV_MODE_DREK_PUSH) &&
        result.allianceSouth >= 8)
        return true;

    return result.allianceSouth >= 18 &&
           (mode == AV_MODE_IBGY_PUSH || mode == AV_MODE_IBGY_GUARD ||
            mode == AV_MODE_SOUTH_TOWER_SPLIT || mode == AV_MODE_DREK_SETUP ||
            mode == AV_MODE_FROSTWOLF_LOCK || mode == AV_MODE_DREK_PUSH);
}

static uint8 AsyncAVAllianceReleasedDefenderLimit(AllianceAVBattlefieldMode mode, bool needsIcebloodMainForce)
{
    if (mode == AV_MODE_IBGY_BREAKTHROUGH)
        return 1;

    return needsIcebloodMainForce ? 2 : 1;
}

static bool AsyncAVAllianceShouldScreenIdleNorthReserve(AsyncAVStrategyResult const& result,
                                                        AllianceAVThreatLevel threat,
                                                        AllianceAVRushLevel rushLevel,
                                                        AllianceAVBattlefieldMode mode,
                                                        bool needsIcebloodMainForce)
{
    if (!AsyncAVAllianceNorthIsQuiet(result, threat, rushLevel) || needsIcebloodMainForce ||
        mode == AV_MODE_IBGY_BREAKTHROUGH)
        return false;

    if (mode == AV_MODE_FROSTWOLF_LOCK || mode == AV_MODE_DREK_SETUP || mode == AV_MODE_DREK_PUSH)
        return false;

    return mode == AV_MODE_IBGY_PUSH || mode == AV_MODE_IBGY_GUARD ||
           mode == AV_MODE_SOUTH_TOWER_SPLIT || mode == AV_MODE_DREK_SETUP ||
           mode == AV_MODE_FROSTWOLF_LOCK || mode == AV_MODE_DREK_PUSH;
}

static bool AsyncAVAllianceHasDrekSetupWindow(std::array<AsyncAVNodeSnapshot, BG_AV_NODES_MAX> const& nodes,
                                             AsyncAVStrategyResult const& result,
                                             AllianceAVThreatLevel threat, bool hordeCaptainAlive)
{
    return threat < AV_THREAT_HIGH && !hordeCaptainAlive &&
           AsyncAVNodeControlledBy(nodes, BG_AV_NODES_ICEBLOOD_GRAVE, TEAM_ALLIANCE) &&
           AsyncAVAllianceHasForwardDrekRespawn(nodes) && result.hordeTowerProgress >= 2 &&
           result.allianceSouth >= 10;
}

static bool AsyncAVAllianceNeedsFrostwolfLock(std::array<AsyncAVNodeSnapshot, BG_AV_NODES_MAX> const& nodes,
                                             AsyncAVStrategyResult const& result,
                                             AllianceAVThreatLevel threat, bool hordeCaptainAlive)
{
    return threat < AV_THREAT_HIGH && !hordeCaptainAlive &&
           AsyncAVNodeControlledBy(nodes, BG_AV_NODES_ICEBLOOD_GRAVE, TEAM_ALLIANCE) &&
           !AsyncAVAllianceHasForwardDrekRespawn(nodes) && result.allianceSouth >= 8;
}

static AllianceAVBattlefieldMode AsyncAVAllianceComputeMode(
    std::array<AsyncAVNodeSnapshot, BG_AV_NODES_MAX> const& nodes, AsyncAVStrategyResult const& result,
    AllianceAVThreatLevel threat, AllianceAVRushLevel rushLevel, AVBotStrategy hordeStrategy,
    bool hordeCaptainAlive, bool finalDrekWindow)
{
    bool const dbEmergency = threat == AV_THREAT_CRITICAL &&
        (AsyncAVNodeAssaultedBy(nodes, BG_AV_NODES_DUNBALDAR_SOUTH, TEAM_HORDE) ||
         AsyncAVNodeAssaultedBy(nodes, BG_AV_NODES_DUNBALDAR_NORTH, TEAM_HORDE) ||
         AsyncAVNodeControlledBy(nodes, BG_AV_NODES_FIRSTAID_STATION, TEAM_HORDE));

    if (dbEmergency)
        return AV_MODE_DUN_BALDAR_EMERGENCY;

    if (threat >= AV_THREAT_HIGH)
        return AV_MODE_NORTH_DEFENSE;

    if (finalDrekWindow && !AsyncAVAllianceDunBaldarBaseActuallyLost(nodes))
        return AV_MODE_DREK_PUSH;

    bool const ownsForwardGY = AsyncAVAllianceHasForwardDrekRespawn(nodes);
    bool const committedFrostwolf = AsyncAVAllianceHasCommittedFrostwolfFoothold(nodes, result, threat,
                                                                                  hordeCaptainAlive);
    bool const icebloodControlled = AsyncAVNodeControlledBy(nodes, BG_AV_NODES_ICEBLOOD_GRAVE, TEAM_ALLIANCE);
    bool const icebloodAssaulted = AsyncAVNodeAssaultedBy(nodes, BG_AV_NODES_ICEBLOOD_GRAVE, TEAM_ALLIANCE);

    if (!icebloodControlled)
    {
        if (committedFrostwolf)
            return result.hordeTowerProgress >= 2 ? AV_MODE_DREK_SETUP : AV_MODE_FROSTWOLF_LOCK;

        if (hordeCaptainAlive)
            return AV_MODE_GALVANGAR_STRIKE;

        if (AsyncAVAllianceNeedsIcebloodBreakthrough(nodes, result, threat, rushLevel, hordeStrategy,
                                                     hordeCaptainAlive))
            return AV_MODE_IBGY_BREAKTHROUGH;

        if (icebloodAssaulted)
            return AV_MODE_IBGY_GUARD;

        return result.elapsedMs < 90 * 1000 ? AV_MODE_OPENING_CONTROL : AV_MODE_IBGY_PUSH;
    }

    if (finalDrekWindow || (!hordeCaptainAlive && ownsForwardGY && result.hordeTowersDown >= 3 &&
                            result.allianceSouth >= 12))
        return AV_MODE_DREK_PUSH;

    if (AsyncAVAllianceHasDrekSetupWindow(nodes, result, threat, hordeCaptainAlive))
        return AV_MODE_DREK_SETUP;

    if (AsyncAVAllianceNeedsFrostwolfLock(nodes, result, threat, hordeCaptainAlive))
        return AV_MODE_FROSTWOLF_LOCK;

    if (hordeCaptainAlive && result.elapsedMs >= 3 * 60 * 1000)
        return AV_MODE_GALVANGAR_STRIKE;

    return AV_MODE_SOUTH_TOWER_SPLIT;
}

static uint8 AsyncAVClampDefenderLimit(uint32 value)
{
    return static_cast<uint8>(std::min<uint32>(9, value));
}

static uint8 AsyncAVAllianceRushDefenderLimit(
    std::array<AsyncAVNodeSnapshot, BG_AV_NODES_MAX> const& nodes, AsyncAVStrategyResult const& result,
    AllianceAVThreatLevel threat, AVBotStrategy allianceStrategy, AVBotStrategy hordeStrategy,
    bool finalDrekWindow)
{
    if (allianceStrategy == AV_STRATEGY_ALLIANCE_CONTROL_TEMPO)
    {
        uint8 limit = 4;
        switch (threat)
        {
            case AV_THREAT_LOW:
                limit = result.elapsedMs < 6 * 60 * 1000 && result.hordeNorth >= 10 ? 6 : 4;
                break;
            case AV_THREAT_MEDIUM:
                limit = result.hordeDeepNorth >= 8 ? 7 : 6;
                break;
            case AV_THREAT_HIGH:
                limit = 8;
                break;
            case AV_THREAT_CRITICAL:
                limit = 9;
                if (AsyncAVAllianceDunBaldarBaseActuallyLost(nodes))
                    limit = 9;
                break;
            case AV_THREAT_NONE:
            default:
                break;
        }

        if (hordeStrategy == AV_STRATEGY_OFFENSIVE && threat != AV_THREAT_NONE && limit < 6)
            ++limit;

        if (finalDrekWindow && threat < AV_THREAT_HIGH)
        {
            uint8 const finalDefenseCap = AsyncAVAllianceDunBaldarBaseActuallyLost(nodes) ? 7 : 6;
            limit = std::min<uint8>(limit, finalDefenseCap);
        }

        return std::min<uint8>(limit, 9);
    }

    if (result.allianceRushLevel == AV_RUSH_NONE)
        return 0;

    uint8 limit = 5;
    if (result.allianceRushLevel == AV_RUSH_DEEP)
        limit = 7;
    else if (result.allianceRushLevel == AV_RUSH_DUNBALDAR)
        limit = 8;

    if (hordeStrategy == AV_STRATEGY_OFFENSIVE)
        ++limit;
    if (allianceStrategy == AV_STRATEGY_DEFENSIVE)
        limit = 9;

    return std::min<uint8>(limit, 9);
}

static uint8 AsyncAVAllianceBaseDefenderLimit(AsyncAVStrategyResult const& result, AVBotStrategy allianceStrategy,
                                              AVBotStrategy hordeStrategy)
{
    uint8 defendersProhab = 4;

    switch (allianceStrategy)
    {
        case AV_STRATEGY_BALANCED:
            defendersProhab = 4;
            break;
        case AV_STRATEGY_OFFENSIVE:
            defendersProhab = 1;
            break;
        case AV_STRATEGY_DEFENSIVE:
            defendersProhab = 9;
            break;
        case AV_STRATEGY_ALLIANCE_CONTROL_TEMPO:
            defendersProhab = 4;
            break;
        default:
            break;
    }

    if (hordeStrategy == AV_STRATEGY_DEFENSIVE)
        defendersProhab = std::max<uint8>(defendersProhab, 4);

    if (allianceStrategy != AV_STRATEGY_ALLIANCE_CONTROL_TEMPO && result.allianceDefensivePressure > 0)
        defendersProhab = std::max<uint8>(
            defendersProhab, AsyncAVClampDefenderLimit(4 + result.allianceDefensivePressure * 2));

    return defendersProhab;
}

static void AsyncAVBuildAllianceTacticalOrders(AsyncAVStrategyJob const& job, AsyncAVStrategyResult& result)
{
    AVBotStrategy const allianceStrategy = static_cast<AVBotStrategy>(job.allianceStrategy);
    AVBotStrategy const hordeStrategy = static_cast<AVBotStrategy>(job.hordeStrategy);

    result.allianceStrategy = job.allianceStrategy;
    result.hordeStrategy = job.hordeStrategy;
    result.allianceRushLevel = AsyncAVGetAllianceRushLevel(result, hordeStrategy);
    AllianceAVRushLevel const rushLevel = static_cast<AllianceAVRushLevel>(result.allianceRushLevel);
    AllianceAVThreatLevel const threat = AsyncAVGetAllianceThreat(job.nodes, result, rushLevel);
    result.allianceThreat = static_cast<uint8>(threat);
    result.allianceFullRecall = threat >= AV_THREAT_HIGH || AsyncAVAllianceDunBaldarBaseActuallyLost(job.nodes);
    result.allianceFinalDrekWindow = AsyncAVAllianceHasFinalDrekWindow(job.nodes, result, job.hordeCaptainAlive);

    AllianceAVBattlefieldMode const mode = AsyncAVAllianceComputeMode(job.nodes, result, threat, rushLevel,
                                                                      hordeStrategy, job.hordeCaptainAlive,
                                                                      result.allianceFinalDrekWindow);
    result.allianceMode = static_cast<uint8>(mode);
    result.allianceNeedsIcebloodMainForce = AsyncAVAllianceNeedsIcebloodMainForce(
        job.nodes, result, threat, rushLevel, mode, job.hordeCaptainAlive);
    result.allianceCanReleaseIdleNorthReserve = AsyncAVAllianceCanReleaseIdleNorthReserve(
        result, threat, rushLevel, mode, result.allianceNeedsIcebloodMainForce);
    result.allianceShouldScreenIdleNorthReserve = AsyncAVAllianceShouldScreenIdleNorthReserve(
        result, threat, rushLevel, mode, result.allianceNeedsIcebloodMainForce);
    result.allianceReleasedDefenderLimit = AsyncAVAllianceReleasedDefenderLimit(
        mode, result.allianceNeedsIcebloodMainForce);

    uint8 defenderLimit = AsyncAVAllianceBaseDefenderLimit(result, allianceStrategy, hordeStrategy);
    if (allianceStrategy == AV_STRATEGY_ALLIANCE_CONTROL_TEMPO)
        defenderLimit = std::max<uint8>(defenderLimit,
                                        AsyncAVAllianceRushDefenderLimit(job.nodes, result, threat, allianceStrategy,
                                                                         hordeStrategy,
                                                                         result.allianceFinalDrekWindow));

    if (result.allianceFinalDrekWindow && !result.allianceFullRecall)
    {
        uint8 const finalDefenseCap = AsyncAVAllianceDunBaldarBaseActuallyLost(job.nodes) ? 7 : 6;
        defenderLimit = std::min<uint8>(defenderLimit, finalDefenseCap);
    }
    else if (result.allianceCanReleaseIdleNorthReserve)
        defenderLimit = std::min<uint8>(defenderLimit, result.allianceReleasedDefenderLimit);

    result.allianceDefenderLimit = std::min<uint8>(defenderLimit, 9);
    result.allianceTacticalOrdersValid = true;
}

static bool AsyncAVAllianceShouldTakeSnowfall(std::array<AsyncAVNodeSnapshot, BG_AV_NODES_MAX> const& nodes,
                                              AsyncAVStrategyResult const& result,
                                              AllianceAVThreatLevel threat, AllianceAVBattlefieldMode mode,
                                              uint8 role)
{
    if (threat >= AV_THREAT_HIGH || role != 4)
        return false;

    AsyncAVNodeSnapshot const& snowfall = nodes[BG_AV_NODES_SNOWFALL_GRAVE];
    if (snowfall.state == POINT_CONTROLLED && snowfall.ownerId == TEAM_ALLIANCE)
        return false;

    bool const icebloodSecure = AsyncAVNodeControlledBy(nodes, BG_AV_NODES_ICEBLOOD_GRAVE, TEAM_ALLIANCE);
    bool const icebloodAssaulted = AsyncAVNodeAssaultedBy(nodes, BG_AV_NODES_ICEBLOOD_GRAVE, TEAM_ALLIANCE);

    if (!icebloodSecure && !icebloodAssaulted)
        return threat == AV_THREAT_NONE && result.elapsedMs >= 45 * 1000;

    if (mode == AV_MODE_IBGY_PUSH || mode == AV_MODE_IBGY_GUARD)
        return threat <= AV_THREAT_LOW && result.allianceSouth >= 4;

    return threat <= AV_THREAT_LOW;
}

static AllianceAVObjectiveAssignment AsyncAVBuildAllianceObjectiveAssignment(
    AsyncAVStrategyJob const& job, AsyncAVStrategyResult const& result, AsyncAVPlayerSnapshot const& player)
{
    if (!player.isBot || player.teamId != TEAM_ALLIANCE || !player.alive ||
        job.allianceStrategy != AV_STRATEGY_ALLIANCE_CONTROL_TEMPO)
        return AV_ASSIGN_NONE;

    AllianceAVThreatLevel const threat = static_cast<AllianceAVThreatLevel>(result.allianceThreat);
    AllianceAVBattlefieldMode const mode = static_cast<AllianceAVBattlefieldMode>(result.allianceMode);
    uint8 const defenderLimit = std::max<uint8>(1, result.allianceDefenderLimit);
    bool const snowfallForward =
        ((AV_SNOWFALL_RESPAWN_ALLIANCE.GetPositionX() - player.x) *
             (AV_SNOWFALL_RESPAWN_ALLIANCE.GetPositionX() - player.x) +
         (AV_SNOWFALL_RESPAWN_ALLIANCE.GetPositionY() - player.y) *
             (AV_SNOWFALL_RESPAWN_ALLIANCE.GetPositionY() - player.y) <= 45.0f * 45.0f) ||
        (player.x <= -120.0f && player.x >= -190.0f && player.y >= -10.0f && player.y <= 65.0f &&
         player.z >= 60.0f) ||
        (player.x <= -140.0f && player.x >= -280.0f && player.y <= -70.0f && player.y >= -180.0f);
    bool const southernBot = player.x <= -462.0f || snowfallForward;
    bool const northernBot = player.x > -180.0f && !snowfallForward;
    bool const isDefender = player.role < defenderLimit && !southernBot;
    bool const reserveRole = player.role < std::min<uint8>(9, defenderLimit + 2);

    if (result.allianceFullRecall && player.role < 9)
        return AV_ASSIGN_NORTH_DEFENSE;

    if (result.allianceFinalDrekWindow && !result.allianceFullRecall && !isDefender)
        return AV_ASSIGN_DREK_PUSH;

    if (isDefender || (result.allianceShouldScreenIdleNorthReserve && northernBot && reserveRole))
        return AV_ASSIGN_NORTH_DEFENSE;

    if (AsyncAVAllianceShouldTakeSnowfall(job.nodes, result, threat, mode, player.role))
        return AV_ASSIGN_SNOWFALL;

    bool const icebloodControlled = AsyncAVNodeControlledBy(job.nodes, BG_AV_NODES_ICEBLOOD_GRAVE, TEAM_ALLIANCE);
    bool const icebloodAssaulted = AsyncAVNodeAssaultedBy(job.nodes, BG_AV_NODES_ICEBLOOD_GRAVE, TEAM_ALLIANCE);
    bool const hasForwardDrekRespawn = AsyncAVAllianceHasForwardDrekRespawn(job.nodes);

    if (snowfallForward && !result.allianceFullRecall &&
        (icebloodAssaulted || icebloodControlled || mode == AV_MODE_IBGY_PUSH || mode == AV_MODE_IBGY_GUARD ||
         mode == AV_MODE_IBGY_BREAKTHROUGH))
        return icebloodControlled ? AV_ASSIGN_ICEBLOOD_HOLD : AV_ASSIGN_ICEBLOOD_ATTACK;

    if ((icebloodAssaulted || icebloodControlled) && result.allianceNeedsIcebloodMainForce &&
        !hasForwardDrekRespawn)
        return AV_ASSIGN_ICEBLOOD_HOLD;

    if (!icebloodControlled && (mode == AV_MODE_IBGY_PUSH || mode == AV_MODE_IBGY_GUARD ||
                                mode == AV_MODE_IBGY_BREAKTHROUGH))
        return AV_ASSIGN_ICEBLOOD_ATTACK;

    if (mode == AV_MODE_GALVANGAR_STRIKE && (player.role >= 5 || (result.elapsedMs >= 6 * 60 * 1000 && player.role >= 4)))
        return AV_ASSIGN_GALVANGAR;

    if (mode == AV_MODE_DREK_PUSH)
        return AV_ASSIGN_DREK_PUSH;

    if (mode == AV_MODE_FROSTWOLF_LOCK || mode == AV_MODE_DREK_SETUP)
        return AV_ASSIGN_FROSTWOLF_LOCK;

    if (!job.hordeCaptainAlive && icebloodControlled)
        return AV_ASSIGN_ASSAULT_GUARD;

    if (!icebloodControlled)
        return AV_ASSIGN_ICEBLOOD_ATTACK;

    return AV_ASSIGN_ASSAULT_GUARD;
}

static void AsyncAVBuildAllianceObjectiveAssignments(AsyncAVStrategyJob const& job, AsyncAVStrategyResult& result)
{
    result.allianceObjectiveAssignments.clear();

    for (AsyncAVPlayerSnapshot const& player : job.players)
    {
        AllianceAVObjectiveAssignment const assignment =
            AsyncAVBuildAllianceObjectiveAssignment(job, result, player);
        if (assignment != AV_ASSIGN_NONE && player.guid)
            result.allianceObjectiveAssignments[player.guid] = static_cast<uint8>(assignment);
    }

    result.allianceObjectiveAssignmentsValid = true;
}

static uint32 AsyncAVCountForTeam(AsyncAVAreaCounts const& counts, TeamId teamId, bool aliveOnly,
                                  bool notInCombatOnly)
{
    if (teamId == TEAM_ALLIANCE)
    {
        if (notInCombatOnly)
            return counts.allianceNotCombat;
        return aliveOnly ? counts.allianceAlive : counts.allianceTotal;
    }

    if (teamId == TEAM_HORDE)
    {
        if (notInCombatOnly)
            return counts.hordeNotCombat;
        return aliveOnly ? counts.hordeAlive : counts.hordeTotal;
    }

    if (notInCombatOnly)
        return counts.allianceNotCombat + counts.hordeNotCombat;

    return aliveOnly ? counts.allianceAlive + counts.hordeAlive : counts.allianceTotal + counts.hordeTotal;
}

static bool AsyncAVAreaMatches(AsyncAVAreaDef const& area, float x, float y, float radius)
{
    float const dx = area.x - x;
    float const dy = area.y - y;
    float const centerDeltaSq = dx * dx + dy * dy;
    return centerDeltaSq <= 45.0f * 45.0f && std::fabs(area.radius - radius) <= 8.0f;
}

class AsyncAVStrategyCache
{
public:
    ~AsyncAVStrategyCache() { Stop(); }

    static AsyncAVStrategyCache& Instance()
    {
        static AsyncAVStrategyCache cache;
        return cache;
    }

    void Update(uint32 diff)
    {
        if (!AsyncAVStrategyCacheEnabled())
        {
            Stop();
            return;
        }

        _logTimer += diff;
        if (_logTimer < ASYNC_AV_CACHE_LOG_INTERVAL)
            return;

        _logTimer = 0;
        uint32 now = getMSTime();
        AsyncAVStrategyResult sample;
        uint32 instanceCount = 0;
        {
            std::lock_guard<std::mutex> lock(_resultWriteMutex);
            std::shared_ptr<AsyncAVStrategyResultMap const> results =
                std::atomic_load_explicit(&_results, std::memory_order_acquire);
            if (results)
            {
                std::shared_ptr<AsyncAVStrategyResultMap> retained = std::make_shared<AsyncAVStrategyResultMap>();
                retained->reserve(results->size());

                for (auto const& [instanceId, cached] : *results)
                {
                    if (now >= cached.lastAccessMs && now - cached.lastAccessMs > ASYNC_AV_CACHE_STALE_MS)
                        continue;

                    if (!sample.instanceId)
                        sample = cached;

                    ++instanceCount;
                    (*retained)[instanceId] = cached;
                }

                if (retained->size() != results->size())
                {
                    std::shared_ptr<AsyncAVStrategyResultMap const> published = retained;
                    std::atomic_store_explicit(&_results, published, std::memory_order_release);
                }
            }
        }

        if (sample.instanceId && sample.generation != _lastLoggedGeneration)
        {
            _lastLoggedGeneration = sample.generation;
            AsyncAVAreaCounts const& ibgy = sample.areas[ASYNC_AV_AREA_ICEBLOOD_220];
            LOG_INFO("playerbots",
                     "AsyncAVStrategyCache: instances={} sample={} players={} aliveA={} aliveH={} Hnorth={} Hdeep={} Hdb={} Asouth={} IBGY_A={} IBGY_H={} towersDown={} towerProgress={} defensePressure={} tactical={} assignments={} mode={} threat={} defenders={} workers={}",
                     instanceCount, sample.instanceId, sample.playerCount, sample.aliveAlliance, sample.aliveHorde,
                     sample.hordeNorth, sample.hordeDeepNorth, sample.hordeDunBaldar, sample.allianceSouth,
                     ibgy.allianceAlive, ibgy.hordeAlive, sample.hordeTowersDown, sample.hordeTowerProgress,
                     sample.allianceDefensivePressure, sample.allianceTacticalOrdersValid ? 1 : 0,
                     static_cast<uint32>(sample.allianceObjectiveAssignments.size()),
                     GetAllianceAVBattlefieldModeName(static_cast<AllianceAVBattlefieldMode>(sample.allianceMode)),
                     GetAllianceAVThreatLevelName(static_cast<AllianceAVThreatLevel>(sample.allianceThreat)),
                     static_cast<uint32>(sample.allianceDefenderLimit), static_cast<uint32>(_workers.size()));
        }
    }

    void Request(Battleground* bg, BattlegroundAV* av)
    {
        if (!AsyncAVStrategyCacheEnabled() || !bg || !av || bg->GetStatus() != STATUS_IN_PROGRESS)
            return;

        EnsureStarted();

        uint32 const instanceId = bg->GetInstanceID();
        uint32 const now = getMSTime();
        uint32 const interval = std::max<uint32>(100, sPlayerbotAIConfig.asyncAVStrategyCacheInterval);

        {
            std::lock_guard<std::mutex> lock(_requestMutex);
            auto itr = _lastRequestMs.find(instanceId);
            if (itr != _lastRequestMs.end() && now >= itr->second && now - itr->second < interval)
                return;

            _lastRequestMs[instanceId] = now;
        }

        auto job = std::make_shared<AsyncAVStrategyJob>();
        job->generation = ++_nextGeneration;
        job->instanceId = instanceId;
        job->elapsedMs = bg->GetStartTime();
        job->allianceStrategy = BGTactics::GetBotStrategyForTeam(bg, TEAM_ALLIANCE);
        job->hordeStrategy = BGTactics::GetBotStrategyForTeam(bg, TEAM_HORDE);
        job->hordeCaptainAlive = av->IsCaptainAlive(TEAM_HORDE);

        for (uint8 nodeId = BG_AV_NODES_FIRSTAID_STATION; nodeId < BG_AV_NODES_MAX; ++nodeId)
        {
            BG_AV_NodeInfo const& node = av->GetAVNodeInfo(nodeId);
            job->nodes[nodeId].state = node.State;
            job->nodes[nodeId].ownerId = node.OwnerId;
            job->nodes[nodeId].prevOwnerId = node.PrevOwnerId;
            job->nodes[nodeId].totalOwnerId = node.TotalOwnerId;
            job->nodes[nodeId].timer = node.Timer;
        }

        job->players.reserve(bg->GetPlayers().size());
        for (auto const& playerPair : bg->GetPlayers())
        {
            Player* player = playerPair.second;
            if (!player)
                continue;

            AsyncAVPlayerSnapshot snapshot;
            snapshot.guid = player->GetGUID().GetCounter();
            snapshot.teamId = player->GetTeamId();
            snapshot.x = player->GetPositionX();
            snapshot.y = player->GetPositionY();
            snapshot.z = player->GetPositionZ();
            snapshot.alive = player->IsAlive();
            snapshot.inCombat = player->IsInCombat();

            if (snapshot.teamId == TEAM_ALLIANCE)
            {
                if (PlayerbotAI* playerAI = GET_PLAYERBOT_AI(player))
                {
                    snapshot.isBot = !playerAI->IsRealPlayer();
                    if (snapshot.isBot)
                        snapshot.role = static_cast<uint8>(playerAI->GetAiObjectContext()->GetValue<uint32>("bg role")->Get());
                }
            }

            job->players.push_back(snapshot);
        }

        {
            std::lock_guard<std::mutex> lock(_jobMutex);
            _jobs.push_back(job);
        }
        _jobCondition.notify_one();
    }

    bool TryGet(uint32 instanceId, AsyncAVStrategyResult& result)
    {
        if (!_running.load())
            return false;

        std::shared_ptr<AsyncAVStrategyResultMap const> results =
            std::atomic_load_explicit(&_results, std::memory_order_acquire);
        if (!results)
            return false;

        auto itr = results->find(instanceId);
        if (itr == results->end())
            return false;

        result = itr->second;
        return true;
    }

    bool TryGetPlayersNear(Battleground* bg, TeamId teamId, float x, float y, float radius, bool aliveOnly,
                           bool notInCombatOnly, uint32& count)
    {
        if (!bg)
            return false;

        AsyncAVStrategyResult result;
        if (!TryGet(bg->GetInstanceID(), result))
            return false;

        for (AsyncAVAreaDef const& area : AsyncAVAreas)
        {
            if (!AsyncAVAreaMatches(area, x, y, radius))
                continue;

            count = AsyncAVCountForTeam(result.areas[area.id], teamId, aliveOnly, notInCombatOnly);
            return true;
        }

        return false;
    }

private:
    void EnsureStarted()
    {
        uint32 threadCount = sPlayerbotAIConfig.asyncAVStrategyCacheThreads;
        if (!threadCount)
            threadCount = std::max<uint32>(1u, std::thread::hardware_concurrency());

        if (!_workers.empty() && _workers.size() == threadCount)
            return;

        Stop();

        _stopping.store(false);
        _running.store(true);
        _workers.reserve(threadCount);
        for (uint32 i = 0; i < threadCount; ++i)
            _workers.emplace_back(&AsyncAVStrategyCache::WorkerLoop, this);

        LOG_INFO("playerbots", "AsyncAVStrategyCache started with {} worker thread(s)", threadCount);
    }

    void Stop()
    {
        if (_workers.empty())
            return;

        _running.store(false);
        _stopping.store(true);
        {
            std::lock_guard<std::mutex> lock(_jobMutex);
            _jobs.clear();
        }
        _jobCondition.notify_all();

        for (std::thread& worker : _workers)
            if (worker.joinable())
                worker.join();

        _workers.clear();
        _stopping.store(false);

        {
            std::lock_guard<std::mutex> lock(_resultWriteMutex);
            std::shared_ptr<AsyncAVStrategyResultMap const> emptyResults;
            std::atomic_store_explicit(&_results, emptyResults, std::memory_order_release);
        }

        {
            std::lock_guard<std::mutex> lock(_requestMutex);
            _lastRequestMs.clear();
        }
    }

    void WorkerLoop()
    {
        while (true)
        {
            std::shared_ptr<AsyncAVStrategyJob> job;
            {
                std::unique_lock<std::mutex> lock(_jobMutex);
                _jobCondition.wait(lock, [this]
                {
                    return _stopping.load() || !_jobs.empty();
                });

                if (_stopping.load())
                    return;

                job = _jobs.front();
                _jobs.pop_front();
            }

            Publish(Process(*job));
        }
    }

    AsyncAVStrategyResult Process(AsyncAVStrategyJob const& job)
    {
        AsyncAVStrategyResult result;
        result.generation = job.generation;
        result.instanceId = job.instanceId;
        result.elapsedMs = job.elapsedMs;
        result.lastAccessMs = getMSTime();
        result.playerCount = static_cast<uint32>(job.players.size());

        for (AsyncAVPlayerSnapshot const& player : job.players)
        {
            if (player.teamId == TEAM_ALLIANCE && player.alive)
                ++result.aliveAlliance;
            else if (player.teamId == TEAM_HORDE && player.alive)
                ++result.aliveHorde;

            if (player.alive)
            {
                if (player.teamId == TEAM_HORDE)
                {
                    if (player.x > -120.0f)
                        ++result.hordeNorth;
                    if (player.x > 250.0f)
                        ++result.hordeDeepNorth;
                    if (player.x > 520.0f)
                        ++result.hordeDunBaldar;
                }
                else if (player.teamId == TEAM_ALLIANCE && player.x > -120.0f)
                    ++result.allianceNorth;

                if (player.teamId == TEAM_ALLIANCE && player.x <= -462.0f)
                    ++result.allianceSouth;
            }

            for (AsyncAVAreaDef const& area : AsyncAVAreas)
            {
                float const dx = player.x - area.x;
                float const dy = player.y - area.y;
                if (dx * dx + dy * dy <= area.radius * area.radius)
                    result.areas[area.id].Add(player.teamId, player.alive, player.inCombat);
            }
        }

        for (uint8 towerNode : {BG_AV_NODES_ICEBLOOD_TOWER, BG_AV_NODES_TOWER_POINT,
                               BG_AV_NODES_FROSTWOLF_ETOWER, BG_AV_NODES_FROSTWOLF_WTOWER})
        {
            if (job.nodes[towerNode].state == POINT_DESTROYED)
                ++result.hordeTowersDown;

            if (job.nodes[towerNode].state == POINT_DESTROYED ||
                AsyncAVHordeTowerAssaultedNearCompletion(job.nodes, towerNode))
                ++result.hordeTowerProgress;
        }

        for (auto const& [nodeId, _] : AV_DefendObjectives_Alliance)
            if (AsyncAVAllianceNodeUnderHordePressure(job.nodes, nodeId))
                ++result.allianceDefensivePressure;

        if (sPlayerbotAIConfig.asyncAVTacticalOrders || sPlayerbotAIConfig.asyncAVObjectiveAssignments)
            AsyncAVBuildAllianceTacticalOrders(job, result);

        if (sPlayerbotAIConfig.asyncAVObjectiveAssignments)
            AsyncAVBuildAllianceObjectiveAssignments(job, result);

        return result;
    }

    void Publish(AsyncAVStrategyResult const& result)
    {
        std::lock_guard<std::mutex> lock(_resultWriteMutex);

        std::shared_ptr<AsyncAVStrategyResultMap const> current =
            std::atomic_load_explicit(&_results, std::memory_order_acquire);
        std::shared_ptr<AsyncAVStrategyResultMap> next =
            current ? std::make_shared<AsyncAVStrategyResultMap>(*current) : std::make_shared<AsyncAVStrategyResultMap>();

        (*next)[result.instanceId] = result;

        std::shared_ptr<AsyncAVStrategyResultMap const> published = next;
        std::atomic_store_explicit(&_results, published, std::memory_order_release);
    }

    std::vector<std::thread> _workers;
    std::mutex _jobMutex;
    std::condition_variable _jobCondition;
    std::deque<std::shared_ptr<AsyncAVStrategyJob>> _jobs;
    std::atomic<bool> _running{false};
    std::atomic<bool> _stopping{false};

    std::mutex _resultWriteMutex;
    std::shared_ptr<AsyncAVStrategyResultMap const> _results;

    std::mutex _requestMutex;
    std::unordered_map<uint32, uint32> _lastRequestMs;

    uint32 _nextGeneration = 0;
    uint32 _logTimer = 0;
    uint32 _lastLoggedGeneration = 0;
};

static void RequestAsyncAVStrategyCache(Battleground* bg, BattlegroundAV* av)
{
    AsyncAVStrategyCache::Instance().Request(bg, av);
}

static bool TryGetAsyncAVStrategyResult(Battleground* bg, AsyncAVStrategyResult& result)
{
    return bg && AsyncAVStrategyCache::Instance().TryGet(bg->GetInstanceID(), result);
}

static bool TryGetAsyncAVPlayersNear(Battleground* bg, TeamId teamId, float x, float y, float radius, bool aliveOnly,
                                     bool notInCombatOnly, uint32& count)
{
    return AsyncAVStrategyCache::Instance().TryGetPlayersNear(bg, teamId, x, y, radius, aliveOnly,
                                                              notInCombatOnly, count);
}
}  // namespace

void BGTactics::UpdateAsyncAVStrategyCache(uint32 diff) { AsyncAVStrategyCache::Instance().Update(diff); }

static AllianceAVRushInfo GetAllianceAVRushInfo(Battleground* bg, AVBotStrategy hordeStrategy)
{
    AllianceAVRushInfo info;
    if (!bg)
        return info;

    info.elapsedMs = bg->GetStartTime();
    BattlegroundTypeId bgType = bg->GetBgTypeID();
    if (bgType == BATTLEGROUND_RB)
        bgType = bg->GetBgTypeID(true);

    if (bgType == BATTLEGROUND_AV)
    {
        BattlegroundAV* av = static_cast<BattlegroundAV*>(bg);
        RequestAsyncAVStrategyCache(bg, av);

        AsyncAVStrategyResult cached;
        if (TryGetAsyncAVStrategyResult(bg, cached))
        {
            info.elapsedMs = cached.elapsedMs;
            info.hordeNorth = cached.hordeNorth;
            info.hordeDeepNorth = cached.hordeDeepNorth;
            info.hordeDunBaldar = cached.hordeDunBaldar;
            info.allianceNorth = cached.allianceNorth;
            info.allianceSouth = cached.allianceSouth;

            bool const earlyGame = info.elapsedMs < 6 * 60 * 1000;
            if (info.hordeDunBaldar >= 3 || info.hordeDeepNorth >= 10 || (earlyGame && info.hordeDeepNorth >= 6))
                info.level = info.hordeDunBaldar >= 3 ? AV_RUSH_DUNBALDAR : AV_RUSH_DEEP;
            else if (info.hordeNorth >= 8 || (earlyGame && info.hordeNorth >= 5) ||
                     (hordeStrategy == AV_STRATEGY_OFFENSIVE && info.hordeNorth >= 3))
                info.level = AV_RUSH_FRONT;

            return info;
        }
    }

    for (auto const& playerPair : bg->GetPlayers())
    {
        Player* player = playerPair.second;
        if (!player || !player->IsAlive())
            continue;

        float const x = player->GetPositionX();
        if (player->GetTeamId() == TEAM_HORDE)
        {
            if (x > -120.0f)
                ++info.hordeNorth;
            if (x > 250.0f)
                ++info.hordeDeepNorth;
            if (x > 520.0f)
                ++info.hordeDunBaldar;
        }
        else if (player->GetTeamId() == TEAM_ALLIANCE && x > -120.0f)
            ++info.allianceNorth;

        if (player->GetTeamId() == TEAM_ALLIANCE && x <= -462.0f)
            ++info.allianceSouth;
    }

    bool const earlyGame = info.elapsedMs < 6 * 60 * 1000;
    if (info.hordeDunBaldar >= 3 || info.hordeDeepNorth >= 10 || (earlyGame && info.hordeDeepNorth >= 6))
        info.level = info.hordeDunBaldar >= 3 ? AV_RUSH_DUNBALDAR : AV_RUSH_DEEP;
    else if (info.hordeNorth >= 8 || (earlyGame && info.hordeNorth >= 5) ||
             (hordeStrategy == AV_STRATEGY_OFFENSIVE && info.hordeNorth >= 3))
        info.level = AV_RUSH_FRONT;

    return info;
}

static bool AllianceAVNodeControlledBy(BattlegroundAV* av, uint8 nodeId, TeamId teamId)
{
    if (!av)
        return false;

    BG_AV_NodeInfo const& node = av->GetAVNodeInfo(nodeId);
    return node.State == POINT_CONTROLLED && node.OwnerId == teamId;
}

static bool AllianceAVNodeAssaultedBy(BattlegroundAV* av, uint8 nodeId, TeamId teamId)
{
    if (!av)
        return false;

    BG_AV_NodeInfo const& node = av->GetAVNodeInfo(nodeId);
    return node.State == POINT_ASSAULTED && node.OwnerId == teamId;
}

static uint32 GetAllianceDestroyedHordeTowerCount(BattlegroundAV* av)
{
    if (!av)
        return 0;

    uint32 towersDown = 0;
    for (uint8 towerNode : {BG_AV_NODES_ICEBLOOD_TOWER, BG_AV_NODES_TOWER_POINT,
                           BG_AV_NODES_FROSTWOLF_ETOWER, BG_AV_NODES_FROSTWOLF_WTOWER})
        if (av->GetAVNodeInfo(towerNode).State == POINT_DESTROYED)
            ++towersDown;

    return towersDown;
}

static bool AllianceHordeTowerAssaultedNearCompletion(BattlegroundAV* av, uint8 towerNode)
{
    if (!av)
        return false;

    BG_AV_NodeInfo const& node = av->GetAVNodeInfo(towerNode);
    return node.State == POINT_ASSAULTED && node.OwnerId == TEAM_ALLIANCE &&
           node.Timer > 0 && node.Timer <= 120000;
}

static uint32 GetAllianceHordeTowerProgressCount(BattlegroundAV* av)
{
    if (!av)
        return 0;

    uint32 progress = 0;
    for (uint8 towerNode : {BG_AV_NODES_ICEBLOOD_TOWER, BG_AV_NODES_TOWER_POINT,
                           BG_AV_NODES_FROSTWOLF_ETOWER, BG_AV_NODES_FROSTWOLF_WTOWER})
    {
        BG_AV_NodeInfo const& node = av->GetAVNodeInfo(towerNode);
        if (node.State == POINT_DESTROYED || AllianceHordeTowerAssaultedNearCompletion(av, towerNode))
            ++progress;
    }

    return progress;
}

static bool AllianceDBTowerTimerLow(BattlegroundAV* av, uint8 nodeId)
{
    if (!av || !IsAllianceDunBaldarTower(nodeId))
        return false;

    BG_AV_NodeInfo const& node = av->GetAVNodeInfo(nodeId);
    return node.State == POINT_ASSAULTED && node.OwnerId == TEAM_HORDE && node.Timer > 0 && node.Timer <= 90000;
}

static bool AllianceHasForwardDrekRespawn(BattlegroundAV* av)
{
    return AllianceAVNodeControlledBy(av, BG_AV_NODES_FROSTWOLF_GRAVE, TEAM_ALLIANCE) ||
           AllianceAVNodeControlledBy(av, BG_AV_NODES_FROSTWOLF_HUT, TEAM_ALLIANCE);
}

static bool AllianceHasAnySouthRespawn(BattlegroundAV* av)
{
    return AllianceHasForwardDrekRespawn(av) ||
           AllianceAVNodeControlledBy(av, BG_AV_NODES_ICEBLOOD_GRAVE, TEAM_ALLIANCE);
}

static bool AllianceDunBaldarBaseActuallyLost(BattlegroundAV* av)
{
    if (!av)
        return false;

    bool const bothDbBunkersLow = AllianceDBTowerTimerLow(av, BG_AV_NODES_DUNBALDAR_SOUTH) &&
                                  AllianceDBTowerTimerLow(av, BG_AV_NODES_DUNBALDAR_NORTH);
    bool const firstAidLost = AllianceAVNodeControlledBy(av, BG_AV_NODES_FIRSTAID_STATION, TEAM_HORDE);
    return bothDbBunkersLow || firstAidLost;
}

static bool AllianceHordeCaptainAlive(BattlegroundAV* av);

static bool AllianceHasFinalDrekWindow(BattlegroundAV* av, AllianceAVRushInfo const& rushInfo, uint32 towersDown)
{
    if (!av || !AllianceHasAnySouthRespawn(av) || AllianceHordeCaptainAlive(av))
        return false;

    if (towersDown >= 4)
    {
        uint32 const minimumSouthCore = AllianceHasForwardDrekRespawn(av) ? 6 : 14;
        return rushInfo.allianceSouth >= minimumSouthCore;
    }

    return towersDown >= 3 && AllianceHasForwardDrekRespawn(av) && rushInfo.allianceSouth >= 12;
}

static AllianceAVThreatLevel GetAllianceAVThreatLevel(BattlegroundAV* av, AllianceAVRushInfo const& rushInfo)
{
    if (!av)
        return AV_THREAT_NONE;

    bool const dbSouthAssaulted = AllianceAVNodeAssaultedBy(av, BG_AV_NODES_DUNBALDAR_SOUTH, TEAM_HORDE);
    bool const dbNorthAssaulted = AllianceAVNodeAssaultedBy(av, BG_AV_NODES_DUNBALDAR_NORTH, TEAM_HORDE);
    bool const stormpikeAssaulted = AllianceAVNodeAssaultedBy(av, BG_AV_NODES_STORMPIKE_GRAVE, TEAM_HORDE);
    bool const firstAidLost = AllianceAVNodeControlledBy(av, BG_AV_NODES_FIRSTAID_STATION, TEAM_HORDE);

    if (dbSouthAssaulted || dbNorthAssaulted || stormpikeAssaulted || firstAidLost)
        return AV_THREAT_CRITICAL;

    if (rushInfo.hordeDunBaldar >= 3 || rushInfo.level == AV_RUSH_DUNBALDAR)
        return AV_THREAT_HIGH;

    if (rushInfo.hordeDeepNorth >= 3 || rushInfo.level == AV_RUSH_DEEP)
        return AV_THREAT_MEDIUM;

    if (rushInfo.hordeNorth >= 3 || rushInfo.level == AV_RUSH_FRONT)
        return AV_THREAT_LOW;

    return AV_THREAT_NONE;
}

static bool AllianceAVShouldFullRecallNorth(BattlegroundAV* av, AllianceAVThreatLevel threat)
{
    return av && (threat >= AV_THREAT_HIGH || AllianceDunBaldarBaseActuallyLost(av));
}

static bool AllianceHasCommittedFrostwolfFoothold(BattlegroundAV* av, AllianceAVRushInfo const& rushInfo,
                                                  AllianceAVThreatLevel threat)
{
    return av && threat < AV_THREAT_HIGH && !AllianceHordeCaptainAlive(av) &&
           AllianceHasForwardDrekRespawn(av) && rushInfo.allianceSouth >= 10;
}

static bool AllianceAVNorthIsQuiet(BattlegroundAV* av, AllianceAVRushInfo const& rushInfo,
                                   AllianceAVThreatLevel threat)
{
    if (!av || threat != AV_THREAT_NONE || rushInfo.IsActive() || GetAllianceDefensivePressure(av) > 0)
        return false;

    return rushInfo.hordeNorth == 0 && rushInfo.hordeDeepNorth == 0 && rushInfo.hordeDunBaldar == 0;
}

static bool AllianceAVHasOnlyMinorNorthContact(BattlegroundAV* av, AllianceAVRushInfo const& rushInfo,
                                               AllianceAVThreatLevel threat)
{
    if (!av || threat != AV_THREAT_NONE || rushInfo.IsActive() || GetAllianceDefensivePressure(av) > 0)
        return false;

    return rushInfo.hordeNorth <= 2 && rushInfo.hordeDeepNorth == 0 && rushInfo.hordeDunBaldar == 0;
}

static bool AllianceAVNeedsIcebloodBreakthrough(BattlegroundAV* av, AllianceAVRushInfo const& rushInfo,
                                                AllianceAVThreatLevel threat, AVBotStrategy hordeStrategy)
{
    if (!av || hordeStrategy != AV_STRATEGY_DEFENSIVE || threat != AV_THREAT_NONE || rushInfo.IsActive())
        return false;

    if (AllianceHordeCaptainAlive(av) || AllianceHasForwardDrekRespawn(av))
        return false;

    if (GetAllianceDestroyedHordeTowerCount(av) > 0 || GetAllianceHordeTowerProgressCount(av) > 0)
        return false;

    BG_AV_NodeInfo const& iceblood = av->GetAVNodeInfo(BG_AV_NODES_ICEBLOOD_GRAVE);
    return !(iceblood.State == POINT_CONTROLLED && iceblood.OwnerId == TEAM_ALLIANCE);
}

static bool AllianceAVNeedsIcebloodMainForce(BattlegroundAV* av, AllianceAVRushInfo const& rushInfo,
                                             AllianceAVThreatLevel threat, AllianceAVBattlefieldMode mode)
{
    if (!AllianceAVNorthIsQuiet(av, rushInfo, threat))
        return false;

    if (mode == AV_MODE_IBGY_BREAKTHROUGH)
        return true;

    if (mode != AV_MODE_IBGY_PUSH && mode != AV_MODE_IBGY_GUARD)
        return false;

    if (AllianceHasForwardDrekRespawn(av) && !AllianceHordeCaptainAlive(av))
        return false;

    if (rushInfo.allianceSouth >= 16)
        return false;

    bool const icebloodAssaulted = AllianceAVNodeAssaultedBy(av, BG_AV_NODES_ICEBLOOD_GRAVE, TEAM_ALLIANCE);
    bool const icebloodControlled = AllianceAVNodeControlledBy(av, BG_AV_NODES_ICEBLOOD_GRAVE, TEAM_ALLIANCE);
    return icebloodAssaulted || icebloodControlled || !AllianceHordeCaptainAlive(av);
}

static bool AllianceAVCanReleaseIdleNorthReserve(BattlegroundAV* av, AllianceAVRushInfo const& rushInfo,
                                                 AllianceAVThreatLevel threat, AllianceAVBattlefieldMode mode)
{
    if (mode == AV_MODE_IBGY_BREAKTHROUGH && AllianceAVHasOnlyMinorNorthContact(av, rushInfo, threat))
        return true;

    if (!AllianceAVNorthIsQuiet(av, rushInfo, threat))
        return false;

    if (AllianceAVNeedsIcebloodMainForce(av, rushInfo, threat, mode))
        return true;

    if ((mode == AV_MODE_FROSTWOLF_LOCK || mode == AV_MODE_DREK_SETUP || mode == AV_MODE_DREK_PUSH) &&
        rushInfo.allianceSouth >= 8)
        return true;

    return rushInfo.allianceSouth >= 18 &&
           (mode == AV_MODE_IBGY_PUSH || mode == AV_MODE_IBGY_GUARD ||
           mode == AV_MODE_SOUTH_TOWER_SPLIT || mode == AV_MODE_DREK_SETUP ||
           mode == AV_MODE_FROSTWOLF_LOCK || mode == AV_MODE_DREK_PUSH);
}

static uint8 GetAllianceAVReleasedDefenderLimit(BattlegroundAV* av, AllianceAVRushInfo const& rushInfo,
                                                AllianceAVThreatLevel threat, AllianceAVBattlefieldMode mode)
{
    if (mode == AV_MODE_IBGY_BREAKTHROUGH)
        return 1;

    return AllianceAVNeedsIcebloodMainForce(av, rushInfo, threat, mode) ? 2 : 1;
}

static bool AllianceAVShouldScreenIdleNorthReserve(BattlegroundAV* av, AllianceAVRushInfo const& rushInfo,
                                                   AllianceAVThreatLevel threat, AllianceAVBattlefieldMode mode)
{
    if (!AllianceAVNorthIsQuiet(av, rushInfo, threat))
        return false;

    if (AllianceAVNeedsIcebloodMainForce(av, rushInfo, threat, mode))
        return false;

    if (mode == AV_MODE_IBGY_BREAKTHROUGH)
        return false;

    if (mode == AV_MODE_FROSTWOLF_LOCK || mode == AV_MODE_DREK_SETUP || mode == AV_MODE_DREK_PUSH)
        return false;

    return mode == AV_MODE_IBGY_PUSH || mode == AV_MODE_IBGY_GUARD ||
           mode == AV_MODE_SOUTH_TOWER_SPLIT || mode == AV_MODE_DREK_SETUP ||
           mode == AV_MODE_FROSTWOLF_LOCK || mode == AV_MODE_DREK_PUSH;
}

static bool AllianceHordeCaptainAlive(BattlegroundAV* av)
{
    return av && av->IsCaptainAlive(TEAM_HORDE);
}

static bool AllianceAVIsCastingAnySpell(Player* bot)
{
    if (!bot)
        return false;

    for (uint8 type = CURRENT_MELEE_SPELL; type <= CURRENT_CHANNELED_SPELL; ++type)
        if (bot->GetCurrentSpell(static_cast<CurrentSpellTypes>(type)))
            return true;

    return false;
}

static Creature* GetAllianceHordeCaptain(Battleground* bg, BattlegroundAV* av)
{
    if (!bg || !AllianceHordeCaptainAlive(av))
        return nullptr;

    Creature* captain = bg->GetBGCreature(AV_CREATURE_H_CAPTAIN);
    return captain && captain->IsAlive() ? captain : nullptr;
}

static bool AllianceHasDrekSetupWindow(BattlegroundAV* av, AllianceAVRushInfo const& rushInfo,
                                       AllianceAVThreatLevel threat, uint32 towerProgress)
{
    return av && threat < AV_THREAT_HIGH && !AllianceHordeCaptainAlive(av) &&
           AllianceAVNodeControlledBy(av, BG_AV_NODES_ICEBLOOD_GRAVE, TEAM_ALLIANCE) &&
           AllianceHasForwardDrekRespawn(av) && towerProgress >= 2 && rushInfo.allianceSouth >= 10;
}

static bool AllianceNeedsFrostwolfLock(BattlegroundAV* av, AllianceAVRushInfo const& rushInfo,
                                       AllianceAVThreatLevel threat)
{
    return av && threat < AV_THREAT_HIGH && !AllianceHordeCaptainAlive(av) &&
           AllianceAVNodeControlledBy(av, BG_AV_NODES_ICEBLOOD_GRAVE, TEAM_ALLIANCE) &&
           !AllianceHasForwardDrekRespawn(av) && rushInfo.allianceSouth >= 8;
}

static bool AllianceModeIsSticky(AllianceAVBattlefieldMode mode)
{
    return mode == AV_MODE_FROSTWOLF_LOCK || mode == AV_MODE_DREK_SETUP || mode == AV_MODE_DREK_PUSH;
}

static AllianceAVBattlefieldMode ApplyAllianceAVModeHysteresis(Battleground* bg, BattlegroundAV* av,
                                                               AllianceAVThreatLevel threat,
                                                               AllianceAVBattlefieldMode proposed)
{
    if (!bg)
        return proposed;

    uint32 const instanceId = bg->GetInstanceID();
    uint32 const elapsedMs = bg->GetStartTime();
    auto& state = avAllianceModeStates[instanceId];

    if (state.sinceMs == 0)
    {
        state.mode = static_cast<uint8>(proposed);
        state.sinceMs = elapsedMs;
        return proposed;
    }

    AllianceAVBattlefieldMode const previous = static_cast<AllianceAVBattlefieldMode>(state.mode);
    if (previous != proposed && AllianceModeIsSticky(previous) && threat < AV_THREAT_HIGH &&
        !AllianceDunBaldarBaseActuallyLost(av) && elapsedMs >= state.sinceMs && elapsedMs - state.sinceMs < 60000)
        return previous;

    if (previous != proposed)
    {
        state.mode = static_cast<uint8>(proposed);
        state.sinceMs = elapsedMs;
    }

    return proposed;
}

static AllianceAVBattlefieldMode GetAllianceAVBattlefieldMode(Battleground* bg, BattlegroundAV* av,
                                                              AllianceAVRushInfo const& rushInfo,
                                                              AllianceAVThreatLevel threat)
{
    if (!av)
        return AV_MODE_OPENING_CONTROL;

    uint32 const towersDown = GetAllianceDestroyedHordeTowerCount(av);
    uint32 const towerProgress = GetAllianceHordeTowerProgressCount(av);
    bool const finalDrekWindow = AllianceHasFinalDrekWindow(av, rushInfo, towersDown);

    bool const dbEmergency = threat == AV_THREAT_CRITICAL &&
        (AllianceAVNodeAssaultedBy(av, BG_AV_NODES_DUNBALDAR_SOUTH, TEAM_HORDE) ||
         AllianceAVNodeAssaultedBy(av, BG_AV_NODES_DUNBALDAR_NORTH, TEAM_HORDE) ||
         AllianceAVNodeControlledBy(av, BG_AV_NODES_FIRSTAID_STATION, TEAM_HORDE));

    if (dbEmergency)
        return ApplyAllianceAVModeHysteresis(bg, av, threat, AV_MODE_DUN_BALDAR_EMERGENCY);

    if (threat >= AV_THREAT_HIGH)
        return ApplyAllianceAVModeHysteresis(bg, av, threat, AV_MODE_NORTH_DEFENSE);

    if (finalDrekWindow && !AllianceDunBaldarBaseActuallyLost(av))
        return ApplyAllianceAVModeHysteresis(bg, av, threat, AV_MODE_DREK_PUSH);

    bool const ownsForwardGY = AllianceHasForwardDrekRespawn(av);
    bool const committedFrostwolf = AllianceHasCommittedFrostwolfFoothold(av, rushInfo, threat);
    bool const icebloodControlled = AllianceAVNodeControlledBy(av, BG_AV_NODES_ICEBLOOD_GRAVE, TEAM_ALLIANCE);
    bool const icebloodAssaulted = AllianceAVNodeAssaultedBy(av, BG_AV_NODES_ICEBLOOD_GRAVE, TEAM_ALLIANCE);
    AVBotStrategy const hordeStrategy = bg
        ? static_cast<AVBotStrategy>(BGTactics::GetBotStrategyForTeam(bg, TEAM_HORDE))
        : AV_STRATEGY_BALANCED;
    if (!icebloodControlled)
    {
        if (committedFrostwolf)
            return ApplyAllianceAVModeHysteresis(bg, av, threat,
                                                 towerProgress >= 2 ? AV_MODE_DREK_SETUP : AV_MODE_FROSTWOLF_LOCK);

        if (AllianceHordeCaptainAlive(av))
            return ApplyAllianceAVModeHysteresis(bg, av, threat, AV_MODE_GALVANGAR_STRIKE);

        if (AllianceAVNeedsIcebloodBreakthrough(av, rushInfo, threat, hordeStrategy))
            return ApplyAllianceAVModeHysteresis(bg, av, threat, AV_MODE_IBGY_BREAKTHROUGH);

        if (icebloodAssaulted)
            return ApplyAllianceAVModeHysteresis(bg, av, threat, AV_MODE_IBGY_GUARD);

        return ApplyAllianceAVModeHysteresis(bg, av, threat,
                                             rushInfo.elapsedMs < 90 * 1000 ? AV_MODE_OPENING_CONTROL : AV_MODE_IBGY_PUSH);
    }

    if (finalDrekWindow || (!AllianceHordeCaptainAlive(av) && ownsForwardGY && towersDown >= 3 && rushInfo.allianceSouth >= 12))
        return ApplyAllianceAVModeHysteresis(bg, av, threat, AV_MODE_DREK_PUSH);

    if (AllianceHasDrekSetupWindow(av, rushInfo, threat, towerProgress))
        return ApplyAllianceAVModeHysteresis(bg, av, threat, AV_MODE_DREK_SETUP);

    if (AllianceNeedsFrostwolfLock(av, rushInfo, threat))
        return ApplyAllianceAVModeHysteresis(bg, av, threat, AV_MODE_FROSTWOLF_LOCK);

    if (AllianceHordeCaptainAlive(av) && rushInfo.elapsedMs >= 3 * 60 * 1000)
        return ApplyAllianceAVModeHysteresis(bg, av, threat, AV_MODE_GALVANGAR_STRIKE);

    return ApplyAllianceAVModeHysteresis(bg, av, threat, AV_MODE_SOUTH_TOWER_SPLIT);
}

struct AllianceAVCachedTactics
{
    AllianceAVRushInfo rushInfo;
    AllianceAVThreatLevel threat = AV_THREAT_NONE;
    AllianceAVBattlefieldMode mode = AV_MODE_OPENING_CONTROL;
    uint32 towersDown = 0;
    uint32 towerProgress = 0;
    uint32 defensivePressure = 0;
    uint8 defenderLimit = 4;
    uint8 releasedDefenderLimit = 1;
    bool fullRecall = false;
    bool finalDrekWindow = false;
    bool canReleaseIdleNorthReserve = false;
    bool shouldScreenIdleNorthReserve = false;
    bool needsIcebloodMainForce = false;
};

static bool TryGetAsyncAllianceAVTactics(Battleground* bg, BattlegroundAV* av, AllianceAVCachedTactics& tactics)
{
    if (!sPlayerbotAIConfig.asyncAVTacticalOrders || !bg || !av)
        return false;

    AsyncAVStrategyResult cached;
    if (!TryGetAsyncAVStrategyResult(bg, cached) || !cached.allianceTacticalOrdersValid)
        return false;

    AVBotStrategy const allianceStrategy = static_cast<AVBotStrategy>(BGTactics::GetBotStrategyForTeam(bg, TEAM_ALLIANCE));
    AVBotStrategy const hordeStrategy = static_cast<AVBotStrategy>(BGTactics::GetBotStrategyForTeam(bg, TEAM_HORDE));
    if (cached.allianceStrategy != static_cast<uint8>(allianceStrategy) ||
        cached.hordeStrategy != static_cast<uint8>(hordeStrategy))
        return false;

    tactics.rushInfo.elapsedMs = cached.elapsedMs;
    tactics.rushInfo.hordeNorth = cached.hordeNorth;
    tactics.rushInfo.hordeDeepNorth = cached.hordeDeepNorth;
    tactics.rushInfo.hordeDunBaldar = cached.hordeDunBaldar;
    tactics.rushInfo.allianceNorth = cached.allianceNorth;
    tactics.rushInfo.allianceSouth = cached.allianceSouth;
    tactics.rushInfo.level = static_cast<AllianceAVRushLevel>(cached.allianceRushLevel);
    tactics.threat = static_cast<AllianceAVThreatLevel>(cached.allianceThreat);
    tactics.mode = ApplyAllianceAVModeHysteresis(bg, av, tactics.threat,
                                                 static_cast<AllianceAVBattlefieldMode>(cached.allianceMode));
    tactics.towersDown = cached.hordeTowersDown;
    tactics.towerProgress = cached.hordeTowerProgress;
    tactics.defensivePressure = cached.allianceDefensivePressure;
    tactics.defenderLimit = cached.allianceDefenderLimit;
    tactics.releasedDefenderLimit = cached.allianceReleasedDefenderLimit;
    tactics.fullRecall = cached.allianceFullRecall;
    tactics.finalDrekWindow = cached.allianceFinalDrekWindow;
    tactics.canReleaseIdleNorthReserve = cached.allianceCanReleaseIdleNorthReserve;
    tactics.shouldScreenIdleNorthReserve = cached.allianceShouldScreenIdleNorthReserve;
    tactics.needsIcebloodMainForce = cached.allianceNeedsIcebloodMainForce;
    return true;
}

static bool TryGetAsyncAllianceAVObjectiveAssignment(Battleground* bg, Player* bot,
                                                     AllianceAVObjectiveAssignment& assignment)
{
    if (!sPlayerbotAIConfig.asyncAVObjectiveAssignments || !bg || !bot ||
        bot->GetTeamId() != TEAM_ALLIANCE)
        return false;

    AsyncAVStrategyResult cached;
    if (!TryGetAsyncAVStrategyResult(bg, cached) || !cached.allianceObjectiveAssignmentsValid)
        return false;

    if (cached.allianceStrategy != BGTactics::GetBotStrategyForTeam(bg, TEAM_ALLIANCE) ||
        cached.hordeStrategy != BGTactics::GetBotStrategyForTeam(bg, TEAM_HORDE))
        return false;

    auto itr = cached.allianceObjectiveAssignments.find(bot->GetGUID().GetCounter());
    if (itr == cached.allianceObjectiveAssignments.end())
        return false;

    assignment = static_cast<AllianceAVObjectiveAssignment>(itr->second);
    return assignment != AV_ASSIGN_NONE;
}

static bool AllianceShouldTakeSnowfall(BattlegroundAV* av, AllianceAVRushInfo const& rushInfo,
                                       AllianceAVThreatLevel threat, AllianceAVBattlefieldMode mode, uint8 role)
{
    if (!av || threat >= AV_THREAT_HIGH)
        return false;

    BG_AV_NodeInfo const& snowfall = av->GetAVNodeInfo(BG_AV_NODES_SNOWFALL_GRAVE);
    if (snowfall.State == POINT_CONTROLLED && snowfall.OwnerId == TEAM_ALLIANCE)
        return false;

    if (AllianceHasSnowfallBlocker(av) && threat >= AV_THREAT_MEDIUM)
        return false;

    bool const icebloodSecure = AllianceAVNodeControlledBy(av, BG_AV_NODES_ICEBLOOD_GRAVE, TEAM_ALLIANCE);
    bool const icebloodAssaulted = AllianceAVNodeAssaultedBy(av, BG_AV_NODES_ICEBLOOD_GRAVE, TEAM_ALLIANCE);

    if (!icebloodSecure && !icebloodAssaulted)
        return role == 4 && threat == AV_THREAT_NONE && rushInfo.elapsedMs >= 45 * 1000;

    if (mode == AV_MODE_IBGY_PUSH || mode == AV_MODE_IBGY_GUARD)
        return role == 4 && threat <= AV_THREAT_LOW && rushInfo.allianceSouth >= 4;

    if (mode == AV_MODE_GALVANGAR_STRIKE)
        return role == 4 && threat <= AV_THREAT_LOW;

    if (mode == AV_MODE_FROSTWOLF_LOCK || mode == AV_MODE_DREK_SETUP)
        return role == 4 && threat <= AV_THREAT_LOW;

    if (mode == AV_MODE_SOUTH_TOWER_SPLIT || mode == AV_MODE_DREK_PUSH)
        return role == 4 && threat <= AV_THREAT_LOW;

    return role == 4 && threat <= AV_THREAT_LOW;
}

static bool AllianceControlsIcebloodGraveyard(BattlegroundAV* av);
static bool AllianceAVMustKillCaptainBeforeTowers(BattlegroundAV* av);
static bool AllianceAVPositionIsHordeCaptainRun(PositionInfo const& pos);
static bool AllianceAVPositionIsIcebloodGraveRun(PositionInfo const& pos);
static bool AllianceAVPositionIsNearPosition(PositionInfo const& pos, Position const& target, float radius);
static bool AllianceAVPositionIsSnowfallRun(Battleground* bg, PositionInfo const& pos);
static bool AllianceAVPositionIsSnowfallRespawn(PositionInfo const& pos);
static bool AllianceAVPositionIsIcebloodTowerRun(PositionInfo const& pos);
static bool AllianceAVPositionIsHordeTowerRun(Battleground* bg, PositionInfo const& pos);
static bool AllianceAVPositionIsBeyondIcebloodBeachhead(Battleground* bg, PositionInfo const& pos);
static bool AllianceAVPositionIsForwardFrostwolfGraveyard(Battleground* bg, PositionInfo const& pos);
static bool AllianceAVPositionIsIcebloodHoldPerimeter(Battleground* bg, PositionInfo const& pos, bool strictAssault);
static bool AllianceAVPositionIsAntiRushRally(PositionInfo const& pos);
static bool AllianceAVPositionIsAllianceDefenseRun(Battleground* bg, PositionInfo const& pos);
static bool AllianceAVPositionIsContestedAllianceDefenseObjective(Battleground* bg, BattlegroundAV* av,
                                                                  PositionInfo const& pos);
static bool AllianceAVIsIcebloodGraveFlag(Battleground* bg, GameObject* go);
static bool AllianceAVIsIcebloodTowerAttackFlag(Battleground* bg, GameObject* go);
static bool AllianceAVIsSnowfallFlag(Battleground* bg, GameObject* go);
static bool AllianceAVIsHordeTowerFlag(Battleground* bg, GameObject* go);
static bool AllianceAVIsAllianceRecapOrReclaimFlag(Battleground* bg, GameObject* go);
static GameObject* SelectAllianceIcebloodGraveyardHoldObjective(Battleground* bg);
static bool AllianceAVShouldHoldIcebloodBeachhead(Battleground* bg, BattlegroundAV* av,
                                                  AllianceAVThreatLevel threat,
                                                  AVBotStrategy hordeStrategy);
static bool AllianceAVShouldUseIcebloodBeachheadPlan(Battleground* bg, BattlegroundAV* av,
                                                     AllianceAVThreatLevel threat,
                                                     AVBotStrategy hordeStrategy);
static bool AllianceAVCanBaitIcebloodTowerDuringIcebloodAssault(Player* bot, Battleground* bg, BattlegroundAV* av,
                                                                uint8 role, AllianceAVThreatLevel threat);
static Player* SelectAllianceIcebloodBeachheadEnemy(Player* bot, Battleground* bg, BattlegroundAV* av,
                                                    AllianceAVThreatLevel threat,
                                                    AVBotStrategy hordeStrategy);
static Player* SelectAllianceNorthRushEnemy(Player* bot, Battleground* bg, AllianceAVRushInfo const& rushInfo,
                                            AllianceAVThreatLevel threat, uint8 role, bool isDefender,
                                            uint8 defenderLimit);

static bool AllianceAVCanUseNearbyFlagDuringControlTempo(Player* bot, Battleground* bg, GameObject* go, uint8 role)
{
    if (!bot || !bg || !go || bot->GetTeamId() != TEAM_ALLIANCE)
        return true;

    BattlegroundTypeId bgType = bg->GetBgTypeID();
    if (bgType == BATTLEGROUND_RB)
        bgType = bg->GetBgTypeID(true);

    if (bgType != BATTLEGROUND_AV ||
        BGTactics::GetBotStrategyForTeam(bg, TEAM_ALLIANCE) != AV_STRATEGY_ALLIANCE_CONTROL_TEMPO)
        return true;

    BattlegroundAV* av = static_cast<BattlegroundAV*>(bg);
    AVBotStrategy const hordeStrategy = static_cast<AVBotStrategy>(BGTactics::GetBotStrategyForTeam(bg, TEAM_HORDE));
    AllianceAVRushInfo const rushInfo = GetAllianceAVRushInfo(bg, hordeStrategy);
    AllianceAVThreatLevel const threat = GetAllianceAVThreatLevel(av, rushInfo);
    AllianceAVBattlefieldMode const mode = GetAllianceAVBattlefieldMode(bg, av, rushInfo, threat);
    bool const holdIcebloodBeachhead = AllianceAVShouldHoldIcebloodBeachhead(bg, av, threat, hordeStrategy);
    bool const icebloodBeachheadPlan = AllianceAVShouldUseIcebloodBeachheadPlan(bg, av, threat, hordeStrategy);
    bool const icebloodBreakthrough = AllianceAVNeedsIcebloodBreakthrough(av, rushInfo, threat, hordeStrategy);

    if (AllianceAVIsAllianceRecapOrReclaimFlag(bg, go))
        return true;

    if (AllianceAVIsSnowfallFlag(bg, go))
        return AllianceShouldTakeSnowfall(av, rushInfo, threat, mode, role);

    if (AllianceAVMustKillCaptainBeforeTowers(av))
        return AllianceAVIsIcebloodGraveFlag(bg, go);

    if (AllianceAVIsIcebloodTowerAttackFlag(bg, go) &&
        AllianceAVCanBaitIcebloodTowerDuringIcebloodAssault(bot, bg, av, role, threat))
        return true;

    if (AllianceAVIsHordeTowerFlag(bg, go) &&
        (AllianceAVShouldFullRecallNorth(av, threat) || holdIcebloodBeachhead || icebloodBeachheadPlan ||
         icebloodBreakthrough || mode == AV_MODE_IBGY_BREAKTHROUGH))
        return false;

    return true;
}

static bool AllianceAVShouldResetStalledObjective(Player* bot, Battleground* bg, PositionInfo const& objectivePos)
{
    if (!bot || !bg || !objectivePos.valueSet || bot->isMoving() || bot->IsInCombat() ||
        PlayerHasFlag::IsCapturingFlag(bot) || AllianceAVIsCastingAnySpell(bot))
        return false;

    if (bot->GetDistance(objectivePos.x, objectivePos.y, objectivePos.z) < 8.0f)
    {
        std::lock_guard<std::mutex> guard(avAllianceObjectiveStallStatesMutex);
        avAllianceObjectiveStallStates.erase(bot->GetGUID().GetCounter());
        return false;
    }

    uint64 const botId = bot->GetGUID().GetCounter();
    uint32 const now = bg->GetStartTime();
    float const botX = bot->GetPositionX();
    float const botY = bot->GetPositionY();

    std::lock_guard<std::mutex> guard(avAllianceObjectiveStallStatesMutex);
    auto& state = avAllianceObjectiveStallStates[botId];

    float const movedX = botX - state.botX;
    float const movedY = botY - state.botY;
    float const objectiveMovedX = objectivePos.x - state.objectiveX;
    float const objectiveMovedY = objectivePos.y - state.objectiveY;

    bool const changed = state.sinceMs == 0 ||
        movedX * movedX + movedY * movedY > 9.0f ||
        objectiveMovedX * objectiveMovedX + objectiveMovedY * objectiveMovedY > 25.0f;

    if (changed)
    {
        state.botX = botX;
        state.botY = botY;
        state.objectiveX = objectivePos.x;
        state.objectiveY = objectivePos.y;
        state.sinceMs = now;
        return false;
    }

    return now >= state.sinceMs && now - state.sinceMs >= 15000;
}

static bool AllianceAVShouldResetCurrentObjective(Player* bot, Battleground* bg, BattlegroundAV* av,
                                                  PositionInfo const& objectivePos, uint8 role, bool isDefender,
                                                  AllianceAVRushInfo const& rushInfo, AllianceAVThreatLevel threat,
                                                  AllianceAVBattlefieldMode mode)
{
    if (!bot || !bg || !av || !objectivePos.valueSet || bot->GetTeamId() != TEAM_ALLIANCE ||
        PlayerHasFlag::IsCapturingFlag(bot) || AllianceAVIsCastingAnySpell(bot))
        return false;

    bool const fullRecall = AllianceAVShouldFullRecallNorth(av, threat);
    bool const contestedAllianceDefenseObjective =
        AllianceAVPositionIsContestedAllianceDefenseObjective(bg, av, objectivePos);

    if (contestedAllianceDefenseObjective)
        return false;

    if ((isDefender || (fullRecall && role < 9)) && rushInfo.IsActive() && objectivePos.x < -180.0f &&
        !AllianceAVPositionIsSnowfallRun(bg, objectivePos))
        return true;

    if (fullRecall && role < 9 && objectivePos.x < 250.0f && !contestedAllianceDefenseObjective)
        return true;

    if (!AllianceHordeCaptainAlive(av) && AllianceAVPositionIsHordeCaptainRun(objectivePos) &&
        !AllianceAVPositionIsHordeTowerRun(bg, objectivePos))
        return true;

    if (AllianceAVMustKillCaptainBeforeTowers(av) && AllianceAVPositionIsHordeTowerRun(bg, objectivePos))
        return true;

    AVBotStrategy const hordeStrategy = static_cast<AVBotStrategy>(BGTactics::GetBotStrategyForTeam(bg, TEAM_HORDE));
    if (mode == AV_MODE_IBGY_BREAKTHROUGH)
    {
        if (AllianceAVPositionIsHordeTowerRun(bg, objectivePos))
            return true;

        if (!isDefender && !AllianceAVPositionIsIcebloodGraveRun(objectivePos) &&
            !AllianceAVPositionIsSnowfallRun(bg, objectivePos) && !contestedAllianceDefenseObjective)
            return true;
    }

    bool const allianceAssaultingIBGY = AllianceAVNodeAssaultedBy(av, BG_AV_NODES_ICEBLOOD_GRAVE, TEAM_ALLIANCE);
    PositionInfo const botPos(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId());
    bool const southernIcebloodGroup = bot->GetPositionX() < -180.0f ||
        AllianceAVPositionIsSnowfallRespawn(botPos);
    if (allianceAssaultingIBGY && southernIcebloodGroup && AllianceAVPositionIsIcebloodTowerRun(objectivePos) &&
        AllianceAVCanBaitIcebloodTowerDuringIcebloodAssault(bot, bg, av, role, threat))
        return false;

    if (allianceAssaultingIBGY && southernIcebloodGroup &&
        !AllianceHasCommittedFrostwolfFoothold(av, rushInfo, threat) &&
        !AllianceAVPositionIsIcebloodHoldPerimeter(bg, objectivePos, true) &&
        !AllianceAVPositionIsSnowfallRun(bg, objectivePos))
        return true;

    bool const frostwolfLockObjective =
        mode == AV_MODE_FROSTWOLF_LOCK && AllianceAVPositionIsForwardFrostwolfGraveyard(bg, objectivePos);
    if (!isDefender && AllianceAVShouldUseIcebloodBeachheadPlan(bg, av, threat, hordeStrategy) &&
        AllianceAVPositionIsBeyondIcebloodBeachhead(bg, objectivePos) && !frostwolfLockObjective)
        return true;

    if (Player* rushEnemy = SelectAllianceNorthRushEnemy(bot, bg, rushInfo, threat, role, isDefender, 0))
    {
        float const enemyDx = rushEnemy->GetPositionX() - objectivePos.x;
        float const enemyDy = rushEnemy->GetPositionY() - objectivePos.y;
        bool const objectiveIsRushEnemy = enemyDx * enemyDx + enemyDy * enemyDy <= 45.0f * 45.0f;
        if (objectiveIsRushEnemy)
            return false;

        bool const staticObjective = AllianceAVPositionIsAntiRushRally(objectivePos) ||
                                     (AllianceAVPositionIsAllianceDefenseRun(bg, objectivePos) &&
                                      !contestedAllianceDefenseObjective);
        if (staticObjective)
            return true;
    }

    if (isDefender && rushInfo.IsActive() &&
        (AllianceAVPositionIsAntiRushRally(objectivePos) || AllianceAVPositionIsAllianceDefenseRun(bg, objectivePos)) &&
        bot->GetDistance(objectivePos.x, objectivePos.y, objectivePos.z) < 24.0f &&
        !contestedAllianceDefenseObjective)
        return true;

    if (!isDefender && !AllianceHordeCaptainAlive(av) && !AllianceHasAnySouthRespawn(av) &&
        objectivePos.x < -120.0f &&
        !AllianceAVPositionIsIcebloodGraveRun(objectivePos) && !AllianceAVPositionIsSnowfallRun(bg, objectivePos) &&
        !AllianceAVPositionIsHordeTowerRun(bg, objectivePos))
        return true;

    if (AllianceAVPositionIsSnowfallRun(bg, objectivePos) &&
        !AllianceShouldTakeSnowfall(av, rushInfo, threat, mode, role) &&
        (!IsAllianceNodeUnderHordePressure(av, BG_AV_NODES_SNOWFALL_GRAVE) ||
         threat >= AV_THREAT_HIGH || AllianceHasDunBaldarCorePressure(av)))
        return true;

    return AllianceAVShouldResetStalledObjective(bot, bg, objectivePos);
}

static uint8 GetAllianceAVRushDefenderLimit(BattlegroundAV* av, AllianceAVRushInfo const& rushInfo,
                                            AVBotStrategy allianceStrategy,
                                            AVBotStrategy hordeStrategy)
{
    if (allianceStrategy == AV_STRATEGY_ALLIANCE_CONTROL_TEMPO)
    {
        AllianceAVThreatLevel const threat = GetAllianceAVThreatLevel(av, rushInfo);
        uint8 limit = 4; // roles 0-3 keep the northern line by default

        switch (threat)
        {
            case AV_THREAT_LOW:
                limit = rushInfo.elapsedMs < 6 * 60 * 1000 && rushInfo.hordeNorth >= 10 ? 6 : 4;
                break;
            case AV_THREAT_MEDIUM:
                limit = rushInfo.hordeDeepNorth >= 8 ? 7 : 6;
                break;
            case AV_THREAT_HIGH:
                limit = 8;
                break;
            case AV_THREAT_CRITICAL:
            {
                limit = 9;
                bool const bothDbLow = AllianceDBTowerTimerLow(av, BG_AV_NODES_DUNBALDAR_SOUTH) &&
                                       AllianceDBTowerTimerLow(av, BG_AV_NODES_DUNBALDAR_NORTH);
                if (bothDbLow || AllianceAVNodeControlledBy(av, BG_AV_NODES_FIRSTAID_STATION, TEAM_HORDE))
                    limit = 9;
                break;
            }
            case AV_THREAT_NONE:
            default:
                break;
        }

        if (hordeStrategy == AV_STRATEGY_OFFENSIVE && threat != AV_THREAT_NONE && limit < 6)
            ++limit;

        uint32 const towersDown = GetAllianceDestroyedHordeTowerCount(av);
        if (AllianceHasFinalDrekWindow(av, rushInfo, towersDown) && threat < AV_THREAT_HIGH)
        {
            uint8 const finalDefenseCap = AllianceDunBaldarBaseActuallyLost(av) ? 7 : 6;
            limit = std::min<uint8>(limit, finalDefenseCap);
        }

        return std::min<uint8>(limit, 9);
    }

    if (!rushInfo.IsActive())
        return 0;

    uint8 limit = 5;
    if (rushInfo.level == AV_RUSH_DEEP)
        limit = 7;
    else if (rushInfo.level == AV_RUSH_DUNBALDAR)
        limit = 8;

    if (hordeStrategy == AV_STRATEGY_OFFENSIVE)
        ++limit;
    if (allianceStrategy == AV_STRATEGY_DEFENSIVE)
        limit = 9;

    return std::min<uint8>(limit, 9);
}

static uint8 ClampAVDefenderLimit(uint32 value)
{
    return static_cast<uint8>(std::min<uint32>(9, value));
}

static uint8 GetAVDefenderRoleLimit(TeamId team, AVBotStrategy strategy, AVBotStrategy enemyStrategy, BattlegroundAV* av)
{
    uint8 defendersProhab = 4;

    switch (strategy)
    {
        case AV_STRATEGY_BALANCED:
            defendersProhab = 4;
            break;
        case AV_STRATEGY_OFFENSIVE:
            defendersProhab = 1;
            break;
        case AV_STRATEGY_DEFENSIVE:
            defendersProhab = 9;
            break;
        case AV_STRATEGY_ALLIANCE_CONTROL_TEMPO:
            defendersProhab = team == TEAM_ALLIANCE ? 4 : 4;
            break;
        default:
            break;
    }

    if (enemyStrategy == AV_STRATEGY_DEFENSIVE)
    {
        if (team == TEAM_ALLIANCE)
            defendersProhab = std::max<uint8>(defendersProhab, 4);
        else
            defendersProhab = 0;
    }

    if (team == TEAM_ALLIANCE && strategy != AV_STRATEGY_ALLIANCE_CONTROL_TEMPO)
    {
        uint32 const pressure = GetAllianceDefensivePressure(av);
        if (pressure > 0)
            defendersProhab = std::max<uint8>(defendersProhab, ClampAVDefenderLimit(4 + pressure * 2));
    }

    return defendersProhab;
}

static bool AllianceControlsIcebloodGraveyard(BattlegroundAV* av)
{
    return AllianceAVNodeControlledBy(av, BG_AV_NODES_ICEBLOOD_GRAVE, TEAM_ALLIANCE);
}

static bool AllianceAVMustKillCaptainBeforeTowers(BattlegroundAV* av)
{
    return av && AllianceHordeCaptainAlive(av);
}

static bool AllianceAVPositionNearBGObject(Battleground* bg, PositionInfo const& pos, uint32 goId, float radius)
{
    if (!bg || !pos.valueSet)
        return false;

    GameObject* go = bg->GetBGObject(goId);
    if (!go)
        return false;

    float const dx = go->GetPositionX() - pos.x;
    float const dy = go->GetPositionY() - pos.y;
    return dx * dx + dy * dy <= radius * radius;
}

static bool AllianceAVPositionIsHordeCaptainRun(PositionInfo const& pos)
{
    if (!pos.valueSet)
        return false;

    return pos.x <= -430.0f && pos.x >= -620.0f && pos.y >= -270.0f && pos.y <= -120.0f;
}

static bool AllianceAVIcebloodTowerHasHordeArchers(BattlegroundAV* av)
{
    if (!av)
        return false;

    BG_AV_NodeInfo const& node = av->GetAVNodeInfo(BG_AV_NODES_ICEBLOOD_TOWER);
    return node.State == POINT_CONTROLLED && node.OwnerId == TEAM_HORDE;
}

static bool AllianceAVShouldAvoidIcebloodTowerForGalvangar(Battleground* bg, PositionInfo const& pos)
{
    if (!bg || !pos.valueSet)
        return false;

    BattlegroundTypeId bgType = bg->GetBgTypeID();
    if (bgType == BATTLEGROUND_RB)
        bgType = bg->GetBgTypeID(true);

    if (bgType != BATTLEGROUND_AV)
        return false;

    BattlegroundAV* av = static_cast<BattlegroundAV*>(bg);
    return AllianceHordeCaptainAlive(av) && AllianceAVIcebloodTowerHasHordeArchers(av) &&
           AllianceAVPositionIsHordeCaptainRun(pos);
}

static bool AllianceAVPositionIsIcebloodGraveRun(PositionInfo const& pos)
{
    if (!pos.valueSet)
        return false;

    return pos.x <= -560.0f && pos.x >= -710.0f && pos.y <= -335.0f && pos.y >= -470.0f;
}

static bool AllianceAVPositionIsSnowfallRun(Battleground* bg, PositionInfo const& pos)
{
    if (!pos.valueSet)
        return false;

    uint32 const snowfallGoIds[] = {
        BG_AV_OBJECT_FLAG_N_SNOWFALL_GRAVE,
        BG_AV_OBJECT_FLAG_H_SNOWFALL_GRAVE,
        BG_AV_OBJECT_FLAG_A_SNOWFALL_GRAVE,
        BG_AV_OBJECT_FLAG_C_A_SNOWFALL_GRAVE,
        BG_AV_OBJECT_FLAG_C_H_SNOWFALL_GRAVE,
    };

    for (uint32 goId : snowfallGoIds)
        if (AllianceAVPositionNearBGObject(bg, pos, goId, 65.0f))
            return true;

    return pos.x <= -140.0f && pos.x >= -280.0f && pos.y <= -70.0f && pos.y >= -180.0f;
}

static bool AllianceAVPositionIsSnowfallRespawn(PositionInfo const& pos)
{
    if (!pos.valueSet)
        return false;

    return AllianceAVPositionIsNearPosition(pos, AV_SNOWFALL_RESPAWN_ALLIANCE, 45.0f) ||
           (pos.x <= -120.0f && pos.x >= -190.0f && pos.y >= -10.0f && pos.y <= 65.0f && pos.z >= 60.0f);
}

static bool AllianceAVPositionIsIcewingBunkerRidge(PositionInfo const& pos)
{
    if (!pos.valueSet)
        return false;

    return pos.x >= 170.0f && pos.x <= 235.0f && pos.y >= -395.0f && pos.y <= -335.0f && pos.z >= 38.0f;
}

static bool AllianceAVPositionIsStonehearthGraveObjective(Battleground* bg, PositionInfo const& pos)
{
    return AllianceAVPositionNearBGObject(bg, pos, BG_AV_OBJECT_FLAG_A_STONEHEART_GRAVE, 95.0f) ||
           AllianceAVPositionNearBGObject(bg, pos, BG_AV_OBJECT_FLAG_H_STONEHEART_GRAVE, 95.0f) ||
           AllianceAVPositionNearBGObject(bg, pos, BG_AV_OBJECT_FLAG_C_A_STONEHEART_GRAVE, 95.0f) ||
           AllianceAVPositionNearBGObject(bg, pos, BG_AV_OBJECT_FLAG_C_H_STONEHEART_GRAVE, 95.0f);
}

static bool AVPositionIsSnowfallUpperPlateau(PositionInfo const& pos)
{
    if (!pos.valueSet)
        return false;

    return pos.x <= -120.0f && pos.x >= -240.0f && pos.y <= 70.0f && pos.y >= -130.0f && pos.z >= 60.0f;
}

static bool AVPositionIsSnowfallHighLowTransition(PositionInfo const& from, PositionInfo const& to)
{
    if (!from.valueSet || !to.valueSet)
        return false;

    PositionInfo const& high = from.z >= to.z ? from : to;
    PositionInfo const& low = from.z >= to.z ? to : from;
    if (high.z < 55.0f || low.z > 45.0f || high.z - low.z < 25.0f)
        return false;

    bool const highSnowfall =
        AVPositionIsSnowfallUpperPlateau(high) ||
        AllianceAVPositionIsNearPosition(high, AV_SNOWFALL_RESPAWN_ALLIANCE, 70.0f);
    bool const lowSnowfallValley =
        low.x <= -80.0f && low.x >= -360.0f && low.y <= -170.0f && low.y >= -520.0f;

    return highSnowfall && lowSnowfallValley;
}

static bool AVPathNeedsPreciseSnowfallMovement(BattleBotPath const* path)
{
    return path == &vPath_AV_SnowfallRespawn_To_SnowfallGraveyard ||
           path == &vPath_AV_SnowfallGraveyard_To_HordeCaptain ||
           path == &vPath_AV_AllianceCrossroads3_To_SnowfallGraveyard ||
           path == &vPath_AV_StoneheartBunker_To_AllianceCrossroad3;
}

static bool AllianceAVPositionIsIcebloodTowerRun(PositionInfo const& pos)
{
    if (!pos.valueSet)
        return false;

    return pos.x <= -520.0f && pos.x >= -630.0f && pos.y <= -245.0f && pos.y >= -345.0f;
}

static bool AllianceAVPositionIsHordeTowerRun(Battleground* bg, PositionInfo const& pos)
{
    if (!pos.valueSet)
        return false;

    uint32 const towerGoIds[] = {
        BG_AV_OBJECT_FLAG_H_ICEBLOOD_TOWER,
        BG_AV_OBJECT_FLAG_C_A_ICEBLOOD_TOWER,
        BG_AV_OBJECT_FLAG_H_TOWER_POINT,
        BG_AV_OBJECT_FLAG_C_A_TOWER_POINT,
        BG_AV_OBJECT_FLAG_H_FROSTWOLF_ETOWER,
        BG_AV_OBJECT_FLAG_C_A_FROSTWOLF_ETOWER,
        BG_AV_OBJECT_FLAG_H_FROSTWOLF_WTOWER,
        BG_AV_OBJECT_FLAG_C_A_FROSTWOLF_WTOWER,
    };

    for (uint32 goId : towerGoIds)
        if (AllianceAVPositionNearBGObject(bg, pos, goId, 45.0f))
            return true;

    bool const towerPoint = pos.x <= -720.0f && pos.x >= -890.0f && pos.y <= -295.0f && pos.y >= -430.0f;
    bool const frostwolfTowers = pos.x <= -1230.0f && pos.x >= -1385.0f && pos.y <= -230.0f && pos.y >= -410.0f;
    return AllianceAVPositionIsIcebloodTowerRun(pos) || towerPoint || frostwolfTowers;
}

static bool AllianceAVPositionIsBeyondIcebloodBeachhead(Battleground* bg, PositionInfo const& pos)
{
    if (!pos.valueSet)
        return false;

    if (AllianceAVPositionIsHordeTowerRun(bg, pos))
        return true;

    return pos.x <= -700.0f && pos.x >= -1450.0f && pos.y <= -180.0f && pos.y >= -480.0f;
}

static bool AllianceAVPositionIsForwardFrostwolfGraveyard(Battleground* bg, PositionInfo const& pos)
{
    if (!pos.valueSet)
        return false;

    uint32 const frostwolfGoIds[] = {
        BG_AV_OBJECT_FLAG_H_FROSTWOLF_GRAVE,
        BG_AV_OBJECT_FLAG_C_A_FROSTWOLF_GRAVE,
        BG_AV_OBJECT_FLAG_C_H_FROSTWOLF_GRAVE,
        BG_AV_OBJECT_FLAG_H_FROSTWOLF_HUT,
        BG_AV_OBJECT_FLAG_C_A_FROSTWOLF_HUT,
        BG_AV_OBJECT_FLAG_C_H_FROSTWOLF_HUT,
    };

    for (uint32 goId : frostwolfGoIds)
        if (AllianceAVPositionNearBGObject(bg, pos, goId, 60.0f))
            return true;

    bool const frostwolfGrave = pos.x <= -1010.0f && pos.x >= -1155.0f && pos.y <= -285.0f && pos.y >= -410.0f;
    bool const frostwolfHut = pos.x <= -1320.0f && pos.x >= -1450.0f && pos.y <= -240.0f && pos.y >= -370.0f;
    return frostwolfGrave || frostwolfHut;
}

static bool AllianceAVPositionIsNearPosition(PositionInfo const& pos, Position const& target, float radius)
{
    if (!pos.valueSet)
        return false;

    float const dx = target.GetPositionX() - pos.x;
    float const dy = target.GetPositionY() - pos.y;
    return dx * dx + dy * dy <= radius * radius;
}

static bool AllianceAVPositionIsIcebloodHoldPerimeter(Battleground* bg, PositionInfo const& pos, bool strictAssault)
{
    if (!pos.valueSet || AllianceAVPositionIsHordeTowerRun(bg, pos))
        return false;

    if (strictAssault)
    {
        if (pos.x < -675.0f || pos.y > -335.0f || pos.y < -475.0f)
            return false;

        return AllianceAVPositionIsNearPosition(pos, AV_ALLIANCE_IBGY_FLAG_HOLD, 96.0f) ||
               AllianceAVPositionIsNearPosition(pos, AV_ALLIANCE_IBGY_NORTH_LOCKDOWN, 32.0f) ||
               AllianceAVPositionIsNearPosition(pos, AV_ALLIANCE_IBGY_SOUTH_LOCKDOWN, 42.0f);
    }

    if (AllianceAVPositionIsBeyondIcebloodBeachhead(bg, pos))
        return false;

    return AllianceAVPositionIsNearPosition(pos, AV_ALLIANCE_IBGY_FLAG_HOLD, 130.0f) ||
           AllianceAVPositionIsNearPosition(pos, AV_ALLIANCE_IBGY_NORTH_LOCKDOWN, 55.0f) ||
           AllianceAVPositionIsNearPosition(pos, AV_ALLIANCE_IBGY_SOUTH_LOCKDOWN, 55.0f);
}

static bool AllianceAVShouldEnforceIcebloodAssaultLeash(Player* bot, Battleground* bg)
{
    if (!bot || !bg || bot->GetTeamId() != TEAM_ALLIANCE ||
        BGTactics::GetBotStrategyForTeam(bg, TEAM_ALLIANCE) != AV_STRATEGY_ALLIANCE_CONTROL_TEMPO)
        return false;

    BattlegroundTypeId bgType = bg->GetBgTypeID();
    if (bgType == BATTLEGROUND_RB)
        bgType = bg->GetBgTypeID(true);

    if (bgType != BATTLEGROUND_AV)
        return false;

    BattlegroundAV* av = static_cast<BattlegroundAV*>(bg);
    BG_AV_NodeInfo const& iceblood = av->GetAVNodeInfo(BG_AV_NODES_ICEBLOOD_GRAVE);
    if (iceblood.State != POINT_ASSAULTED || iceblood.OwnerId != TEAM_ALLIANCE)
        return false;

    PositionInfo const botPos(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId());
    return AllianceAVPositionIsIcebloodHoldPerimeter(bg, botPos, true) ||
           AllianceAVPositionIsHordeTowerRun(bg, botPos);
}

static bool AllianceAVIsValidIcebloodAssaultCombatTarget(Battleground* bg, Unit* target)
{
    if (!bg || !target || !target->IsAlive())
        return false;

    PositionInfo const targetPos(target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(),
                                 target->GetMapId());
    return AllianceAVPositionIsIcebloodHoldPerimeter(bg, targetPos, true);
}

static bool AllianceAVShouldBreakIcebloodAssaultCombatLeash(Player* bot, Battleground* bg,
                                                            Unit* currentTarget, Unit* enemyTarget)
{
    if (!AllianceAVShouldEnforceIcebloodAssaultLeash(bot, bg) || AllianceAVIsCastingAnySpell(bot) ||
        PlayerHasFlag::IsCapturingFlag(bot))
        return false;

    Unit* victim = bot->GetVictim();
    if (!victim && !currentTarget && !enemyTarget && !bot->IsInCombat())
        return false;

    if (AllianceAVIsValidIcebloodAssaultCombatTarget(bg, victim) ||
        AllianceAVIsValidIcebloodAssaultCombatTarget(bg, currentTarget) ||
        AllianceAVIsValidIcebloodAssaultCombatTarget(bg, enemyTarget))
        return false;

    return true;
}

static bool AllianceAVPositionIsAntiRushRally(PositionInfo const& pos)
{
    return AllianceAVPositionIsNearPosition(pos, AV_ALLIANCE_ANTIRUSH_STONEHEARTH_ROAD, 24.0f) ||
           AllianceAVPositionIsNearPosition(pos, AV_ALLIANCE_ANTIRUSH_ICEWING_ROAD, 24.0f) ||
           AllianceAVPositionIsNearPosition(pos, AV_ALLIANCE_ANTIRUSH_STORMPIKE_ROAD, 24.0f) ||
           AllianceAVPositionIsNearPosition(pos, AV_ALLIANCE_ANTIRUSH_DUNBALDAR_ROAD, 24.0f);
}

static bool AllianceAVPositionIsAllianceDefenseRun(Battleground* bg, PositionInfo const& pos)
{
    if (!bg || !pos.valueSet)
        return false;

    for (auto const& [nodeId, goId] : AV_DefendObjectives_Alliance)
    {
        if (AllianceAVPositionNearBGObject(bg, pos, goId, 60.0f))
            return true;

        auto itr = AVNodeMovementTargets.find(nodeId);
        if (itr != AVNodeMovementTargets.end() && AllianceAVPositionIsNearPosition(pos, itr->second.pos, 26.0f))
            return true;
    }

    return false;
}

static bool AllianceAVPositionIsContestedAllianceDefenseObjective(Battleground* bg, BattlegroundAV* av,
                                                                  PositionInfo const& pos)
{
    if (!bg || !av || !pos.valueSet)
        return false;

    for (auto const& [nodeId, goId] : AV_DefendObjectives_Alliance)
    {
        if (!IsAllianceNodeUnderHordePressure(av, nodeId))
            continue;

        if (AllianceAVPositionNearBGObject(bg, pos, goId, 75.0f))
            return true;

        auto itr = AVNodeMovementTargets.find(nodeId);
        if (itr != AVNodeMovementTargets.end() && AllianceAVPositionIsNearPosition(pos, itr->second.pos, 36.0f))
            return true;
    }

    return false;
}

static bool AllianceAVPositionIsAllianceStartPlateau(PositionInfo const& pos)
{
    if (!pos.valueSet)
        return false;

    return pos.x >= 730.0f && pos.x <= 890.0f &&
           pos.y >= -535.0f && pos.y <= -440.0f &&
           pos.z >= 85.0f;
}

static bool AllianceAVObjectiveNeedsStartPlateauExit(Battleground* bg, BattlegroundAV* av, PositionInfo const& pos)
{
    if (!bg || !av || !pos.valueSet || pos.z > 75.0f)
        return false;

    if (AllianceAVPositionIsAllianceDefenseRun(bg, pos) ||
        AllianceAVPositionIsContestedAllianceDefenseObjective(bg, av, pos))
        return true;

    return AllianceAVPositionIsNearPosition(pos, AV_ALLIANCE_ANTIRUSH_STORMPIKE_ROAD, 95.0f) ||
           AllianceAVPositionIsNearPosition(pos, AV_ALLIANCE_ANTIRUSH_DUNBALDAR_ROAD, 95.0f);
}

static bool AllianceAVObjectIsOneOf(Battleground* bg, GameObject* go, uint32 const* ids, size_t count)
{
    if (!bg || !go)
        return false;

    for (size_t i = 0; i < count; ++i)
        if (bg->GetBGObject(ids[i]) == go)
            return true;

    return false;
}

static bool AllianceAVIsIcebloodGraveFlag(Battleground* bg, GameObject* go)
{
    uint32 const ids[] = {
        BG_AV_OBJECT_FLAG_H_ICEBLOOD_GRAVE,
        BG_AV_OBJECT_FLAG_A_ICEBLOOD_GRAVE,
        BG_AV_OBJECT_FLAG_C_A_ICEBLOOD_GRAVE,
        BG_AV_OBJECT_FLAG_C_H_ICEBLOOD_GRAVE,
    };

    return AllianceAVObjectIsOneOf(bg, go, ids, sizeof(ids) / sizeof(uint32));
}

static bool AllianceAVIsIcebloodTowerAttackFlag(Battleground* bg, GameObject* go)
{
    return bg && go && bg->GetBGObject(BG_AV_OBJECT_FLAG_H_ICEBLOOD_TOWER) == go;
}

static bool AllianceAVIsSnowfallFlag(Battleground* bg, GameObject* go)
{
    uint32 const ids[] = {
        BG_AV_OBJECT_FLAG_N_SNOWFALL_GRAVE,
        BG_AV_OBJECT_FLAG_H_SNOWFALL_GRAVE,
        BG_AV_OBJECT_FLAG_A_SNOWFALL_GRAVE,
        BG_AV_OBJECT_FLAG_C_A_SNOWFALL_GRAVE,
        BG_AV_OBJECT_FLAG_C_H_SNOWFALL_GRAVE,
    };

    return AllianceAVObjectIsOneOf(bg, go, ids, sizeof(ids) / sizeof(uint32));
}

static bool AllianceAVIsHordeTowerFlag(Battleground* bg, GameObject* go)
{
    uint32 const ids[] = {
        BG_AV_OBJECT_FLAG_H_ICEBLOOD_TOWER,
        BG_AV_OBJECT_FLAG_C_A_ICEBLOOD_TOWER,
        BG_AV_OBJECT_FLAG_H_TOWER_POINT,
        BG_AV_OBJECT_FLAG_C_A_TOWER_POINT,
        BG_AV_OBJECT_FLAG_H_FROSTWOLF_ETOWER,
        BG_AV_OBJECT_FLAG_C_A_FROSTWOLF_ETOWER,
        BG_AV_OBJECT_FLAG_H_FROSTWOLF_WTOWER,
        BG_AV_OBJECT_FLAG_C_A_FROSTWOLF_WTOWER,
    };

    return AllianceAVObjectIsOneOf(bg, go, ids, sizeof(ids) / sizeof(uint32));
}

static bool AllianceAVIsAllianceRecapOrReclaimFlag(Battleground* bg, GameObject* go)
{
    if (!bg || !go)
        return false;

    for (auto const& [nodeId, goId] : AV_AllianceTowerRecapObjectives)
        if (bg->GetBGObject(goId) == go)
            return true;

    for (auto const& [nodeId, goId] : AV_AllianceGraveyardRecapObjectives)
        if (bg->GetBGObject(goId) == go)
            return true;

    for (auto const& [nodeId, goId] : AV_AllianceHomeGraveyardReclaimObjectives)
        if (bg->GetBGObject(goId) == go)
            return true;

    return false;
}

static GameObject* SelectAllianceIcebloodGraveyardObjective(Battleground* bg, BattlegroundAV* av)
{
    if (!bg || !av || AllianceControlsIcebloodGraveyard(av))
        return nullptr;

    BG_AV_NodeInfo const& node = av->GetAVNodeInfo(BG_AV_NODES_ICEBLOOD_GRAVE);
    uint32 preferredGoId = BG_AV_OBJECT_FLAG_H_ICEBLOOD_GRAVE;

    if (node.State == POINT_ASSAULTED)
        preferredGoId = node.OwnerId == TEAM_ALLIANCE ? BG_AV_OBJECT_FLAG_C_A_ICEBLOOD_GRAVE
                                                      : BG_AV_OBJECT_FLAG_C_H_ICEBLOOD_GRAVE;
    else if (node.OwnerId == TEAM_ALLIANCE)
        preferredGoId = BG_AV_OBJECT_FLAG_A_ICEBLOOD_GRAVE;

    if (GameObject* go = bg->GetBGObject(preferredGoId))
        if (go->isSpawned())
            return go;

    uint32 const fallbackGoIds[] = {
        BG_AV_OBJECT_FLAG_H_ICEBLOOD_GRAVE,
        BG_AV_OBJECT_FLAG_C_A_ICEBLOOD_GRAVE,
        BG_AV_OBJECT_FLAG_C_H_ICEBLOOD_GRAVE,
        BG_AV_OBJECT_FLAG_A_ICEBLOOD_GRAVE,
    };

    for (uint32 goId : fallbackGoIds)
        if (GameObject* go = bg->GetBGObject(goId))
            if (go->isSpawned())
                return go;

    return nullptr;
}

static GameObject* SelectAllianceIcebloodGraveyardHoldObjective(Battleground* bg)
{
    if (!bg)
        return nullptr;

    uint32 const preferredGoIds[] = {
        BG_AV_OBJECT_FLAG_A_ICEBLOOD_GRAVE,
        BG_AV_OBJECT_FLAG_C_A_ICEBLOOD_GRAVE,
        BG_AV_OBJECT_FLAG_H_ICEBLOOD_GRAVE,
        BG_AV_OBJECT_FLAG_C_H_ICEBLOOD_GRAVE,
    };

    for (uint32 goId : preferredGoIds)
        if (GameObject* go = bg->GetBGObject(goId))
            if (go->isSpawned())
                return go;

    return nullptr;
}

static GameObject* SelectAllianceSnowfallObjective(Battleground* bg, BattlegroundAV* av)
{
    if (!bg || !av)
        return nullptr;

    BG_AV_NodeInfo const& node = av->GetAVNodeInfo(BG_AV_NODES_SNOWFALL_GRAVE);
    if (node.State == POINT_CONTROLLED && node.OwnerId == TEAM_ALLIANCE)
        return nullptr;

    uint32 preferredGoId = BG_AV_OBJECT_FLAG_N_SNOWFALL_GRAVE;
    if (node.State == POINT_ASSAULTED)
        preferredGoId = node.OwnerId == TEAM_ALLIANCE ? BG_AV_OBJECT_FLAG_C_A_SNOWFALL_GRAVE
                                                      : BG_AV_OBJECT_FLAG_C_H_SNOWFALL_GRAVE;
    else if (node.OwnerId == TEAM_HORDE)
        preferredGoId = BG_AV_OBJECT_FLAG_H_SNOWFALL_GRAVE;
    else if (node.OwnerId == TEAM_ALLIANCE)
        preferredGoId = BG_AV_OBJECT_FLAG_A_SNOWFALL_GRAVE;

    if (GameObject* go = bg->GetBGObject(preferredGoId))
        if (go->isSpawned())
            return go;

    uint32 const fallbackGoIds[] = {
        BG_AV_OBJECT_FLAG_N_SNOWFALL_GRAVE,
        BG_AV_OBJECT_FLAG_H_SNOWFALL_GRAVE,
        BG_AV_OBJECT_FLAG_C_A_SNOWFALL_GRAVE,
        BG_AV_OBJECT_FLAG_C_H_SNOWFALL_GRAVE,
        BG_AV_OBJECT_FLAG_A_SNOWFALL_GRAVE,
    };

    for (uint32 goId : fallbackGoIds)
        if (GameObject* go = bg->GetBGObject(goId))
            if (go->isSpawned())
                return go;

    return nullptr;
}

static GameObject* SelectAllianceAVEmergencyDefenseObjective(Player* bot, Battleground* bg, BattlegroundAV* av,
                                                             uint8 role, bool isDefender, AVBotStrategy strategy,
                                                             uint8 defenderLimit, AllianceAVRushInfo const& rushInfo,
                                                             AllianceAVThreatLevel threat)
{
    if (!bot || !bg || !av || bot->GetTeamId() != TEAM_ALLIANCE)
        return nullptr;

    struct Candidate
    {
        GameObject* go = nullptr;
        uint8 priority = 0;
        uint8 nodeId = 0;
        uint32 timer = 0;
        float distance = 0.0f;
    };

    std::vector<Candidate> candidates;
    bool const coreEmergency = threat >= AV_THREAT_HIGH && AllianceHasDunBaldarCorePressure(av);

    auto addCandidate = [&](uint8 nodeId, uint32 goId, uint8 priority)
    {
        if (coreEmergency && !IsAllianceDunBaldarCoreDefense(nodeId))
            return;

        if (nodeId == BG_AV_NODES_SNOWFALL_GRAVE && role != 4)
            return;

        GameObject* go = bg->GetBGObject(goId);
        if (!go || !go->isSpawned())
            return;

        float const distance = bot->GetDistance(go);
        bool const fullRecall = AllianceAVShouldFullRecallNorth(av, threat);
        bool const committedOffense = bot->GetPositionX() <= -462.0f && !fullRecall;
        bool const homeDefender = isDefender && !committedOffense;
        bool const northernBot = bot->GetPositionX() > -180.0f;
        bool const northUnderPressure = threat >= AV_THREAT_LOW || rushInfo.IsActive();
        bool const forwardRecap = nodeId == BG_AV_NODES_ICEBLOOD_GRAVE ||
                                  IsAllianceForwardGraveyardAttackTarget(nodeId);
        bool const coreDefenseNode = IsAllianceDunBaldarCoreDefense(nodeId);
        uint8 const smartLimit = defenderLimit > 0 ? defenderLimit : 4;
        bool shouldRespond = false;

        if (forwardRecap && northUnderPressure && northernBot && distance > 220.0f)
            return;

        if (northUnderPressure && !coreDefenseNode && distance > 240.0f)
            return;

        if (fullRecall && !coreDefenseNode && distance > 180.0f)
            return;

        if (fullRecall && role < 9)
            shouldRespond = true;
        else if (priority == 0)
        {
            // Alliance tower recaps are urgent, but do not pull the whole southern push back.
            uint8 const roleLimit = strategy == AV_STRATEGY_DEFENSIVE ? 8 : std::max<uint8>(smartLimit, 6);
            shouldRespond = homeDefender || (!committedOffense && role < roleLimit) || distance < 220.0f;
        }
        else if (priority == 1)
        {
            uint8 const roleLimit = strategy == AV_STRATEGY_DEFENSIVE ? 8 :
                (threat >= AV_THREAT_HIGH ? std::max<uint8>(smartLimit, 6) : 5);
            shouldRespond = (!committedOffense && role < roleLimit) || (homeDefender && role < roleLimit) ||
                            distance < 220.0f;
        }
        else if (priority == 2)
        {
            uint8 const roleLimit = strategy == AV_STRATEGY_DEFENSIVE ? 7 : std::max<uint8>(std::min<uint8>(smartLimit, 6), 4);
            shouldRespond = homeDefender || (!committedOffense && role < roleLimit) || distance < 220.0f;
        }
        else if (priority == 4)
        {
            uint8 const roleLimit = threat >= AV_THREAT_HIGH ? std::min<uint8>(smartLimit, 5) : 3;
            float const nearbyRange = threat >= AV_THREAT_HIGH ? 180.0f : 120.0f;
            shouldRespond = (!committedOffense && role < roleLimit) || (homeDefender && role < roleLimit) ||
                            distance < nearbyRange;
        }
        else
        {
            shouldRespond = homeDefender || (!committedOffense && role < 3) || distance < 180.0f;
        }

        if (!shouldRespond)
            return;

        BG_AV_NodeInfo const& node = av->GetAVNodeInfo(nodeId);
        candidates.push_back({go, priority, nodeId, node.Timer, distance});
    };

    for (auto const& [nodeId, goId] : AV_AllianceTowerRecapObjectives)
    {
        BG_AV_NodeInfo const& node = av->GetAVNodeInfo(nodeId);
        if (node.State == POINT_ASSAULTED && node.OwnerId == TEAM_HORDE && node.PrevOwnerId == TEAM_ALLIANCE &&
            node.TotalOwnerId == TEAM_ALLIANCE)
        {
            uint8 priority = 3;
            if (node.Timer > 0 && node.Timer <= 90000)
                priority = 0;
            else if (IsAllianceDunBaldarTower(nodeId))
                priority = 2;

            addCandidate(nodeId, goId, priority);
        }
    }

    for (auto const& [nodeId, goId] : AV_AllianceGraveyardRecapObjectives)
    {
        BG_AV_NodeInfo const& node = av->GetAVNodeInfo(nodeId);
        if (node.State == POINT_ASSAULTED && node.OwnerId == TEAM_HORDE && node.PrevOwnerId == TEAM_ALLIANCE)
        {
            if (!AllianceHasAnySouthRespawn(av) && IsAllianceForwardGraveyardAttackTarget(nodeId))
                continue;

            uint8 priority = 5;
            if (IsAllianceCoreGraveyard(nodeId))
                priority = 1;
            else if (IsAllianceHomeGraveyard(nodeId))
                priority = 4;

            addCandidate(nodeId, goId, priority);
        }
    }

    for (auto const& [nodeId, goId] : AV_AllianceHomeGraveyardReclaimObjectives)
    {
        BG_AV_NodeInfo const& node = av->GetAVNodeInfo(nodeId);
        if (node.State == POINT_CONTROLLED && node.OwnerId == TEAM_HORDE)
            addCandidate(nodeId, goId, 4);
    }

    if (candidates.empty())
        return nullptr;

    std::sort(candidates.begin(), candidates.end(), [](Candidate const& left, Candidate const& right)
    {
        if (left.priority != right.priority)
            return left.priority < right.priority;

        if (left.timer != right.timer && left.priority <= 2)
            return left.timer < right.timer;

        return left.distance < right.distance;
    });

    return candidates.front().go;
}

static bool ShouldAllianceGuardAssaultedNode(uint8 nodeId, uint8 role, AVBotStrategy strategy)
{
    if (strategy != AV_STRATEGY_ALLIANCE_CONTROL_TEMPO)
        return strategy == AV_STRATEGY_DEFENSIVE ? role < 6 : (role >= 2 && role <= 4);

    switch (nodeId)
    {
        case BG_AV_NODES_ICEBLOOD_GRAVE:
        case BG_AV_NODES_ICEBLOOD_TOWER:
            return role == 5 || role == 6;
        case BG_AV_NODES_TOWER_POINT:
            return role == 7;
        case BG_AV_NODES_FROSTWOLF_GRAVE:
            return role >= 5;
        case BG_AV_NODES_FROSTWOLF_ETOWER:
        case BG_AV_NODES_FROSTWOLF_WTOWER:
            return role >= 8;
        case BG_AV_NODES_FROSTWOLF_HUT:
            return role >= 7;
        default:
            return false;
    }
}

static GameObject* SelectAllianceAssaultGuardObjective(Player* bot, Battleground* bg, BattlegroundAV* av, uint8 role,
                                                       AVBotStrategy strategy,
                                                       bool holdIcebloodBeachhead = false,
                                                       bool deepFrostwolfOnly = false)
{
    if (!bot || !bg || !av || bot->GetTeamId() != TEAM_ALLIANCE)
        return nullptr;

    struct Candidate
    {
        GameObject* go = nullptr;
        uint8 priority = 0;
        uint32 timer = 0;
        float distance = 0.0f;
    };

    std::vector<Candidate> candidates;
    for (auto const& [nodeId, goId] : AV_AllianceAssaultGuardObjectives)
    {
        BG_AV_NodeInfo const& node = av->GetAVNodeInfo(nodeId);
        if (node.State != POINT_ASSAULTED || node.OwnerId != TEAM_ALLIANCE)
            continue;

        if (deepFrostwolfOnly && bot->GetPositionX() <= -900.0f && !IsAllianceFrostwolfObjective(nodeId))
            continue;

        if ((AllianceAVMustKillCaptainBeforeTowers(av) || holdIcebloodBeachhead) &&
            IsAllianceHordeTowerAttackTarget(nodeId))
            continue;

        if (!AllianceHasAnySouthRespawn(av) && IsAllianceForwardGraveyardAttackTarget(nodeId))
            continue;

        GameObject* go = bg->GetBGObject(goId);
        if (!go || !go->isSpawned())
            continue;

        float const distance = bot->GetDistance(go);
        bool const guardRole = ShouldAllianceGuardAssaultedNode(nodeId, role, strategy);
        bool const frostwolfLockIncomplete = strategy == AV_STRATEGY_ALLIANCE_CONTROL_TEMPO &&
                                             !AllianceHasForwardDrekRespawn(av);
        if (frostwolfLockIncomplete && IsAllianceHordeTowerAttackTarget(nodeId) &&
            !IsAllianceFrostwolfObjective(nodeId) && !guardRole)
            continue;

        if (!guardRole && distance > 120.0f)
            continue;

        uint8 priority = 3;
        if (nodeId == BG_AV_NODES_ICEBLOOD_GRAVE)
            priority = 0;
        else if (IsAllianceHordeTowerAttackTarget(nodeId))
            priority = 1;
        else if (nodeId == BG_AV_NODES_FROSTWOLF_GRAVE || nodeId == BG_AV_NODES_FROSTWOLF_HUT)
            priority = 2;

        candidates.push_back({go, priority, node.Timer, distance});
    }

    if (candidates.empty())
        return nullptr;

    std::sort(candidates.begin(), candidates.end(), [](Candidate const& left, Candidate const& right)
    {
        if (left.priority != right.priority)
            return left.priority < right.priority;

        if (left.timer != right.timer)
            return left.timer < right.timer;

        return left.distance < right.distance;
    });

    return candidates.front().go;
}

static uint8 GetAllianceAVDefenderPriority(uint8 nodeId, BG_AV_NodeInfo const& node,
                                           AllianceAVRushInfo const& rushInfo)
{
    if (node.State == POINT_ASSAULTED && node.OwnerId == TEAM_HORDE && node.PrevOwnerId == TEAM_ALLIANCE)
    {
        if (IsAllianceDunBaldarTower(nodeId) && node.Timer > 0 && node.Timer <= 90000)
            return 0;
        if (nodeId == BG_AV_NODES_STORMPIKE_GRAVE)
            return 1;
        if (nodeId == BG_AV_NODES_STONEHEART_GRAVE)
            return 2;
        if (nodeId == BG_AV_NODES_ICEWING_BUNKER || nodeId == BG_AV_NODES_STONEHEART_BUNKER)
            return 3;
        if (nodeId == BG_AV_NODES_FIRSTAID_STATION)
            return rushInfo.level == AV_RUSH_DUNBALDAR ? 4 : 6;
        if (IsAllianceDunBaldarTower(nodeId))
            return 5;
        if (nodeId == BG_AV_NODES_SNOWFALL_GRAVE)
            return 8;
        return 7;
    }

    if (rushInfo.level == AV_RUSH_DUNBALDAR)
    {
        if (IsAllianceDunBaldarTower(nodeId))
            return 1;
        if (nodeId == BG_AV_NODES_STORMPIKE_GRAVE || nodeId == BG_AV_NODES_FIRSTAID_STATION)
            return 2;
        if (nodeId == BG_AV_NODES_ICEWING_BUNKER)
            return 3;
        return 5;
    }

    if (rushInfo.IsActive())
    {
        if (nodeId == BG_AV_NODES_STONEHEART_GRAVE)
            return 1;
        if (nodeId == BG_AV_NODES_ICEWING_BUNKER || nodeId == BG_AV_NODES_STONEHEART_BUNKER)
            return 2;
        if (nodeId == BG_AV_NODES_STORMPIKE_GRAVE)
            return 3;
        if (IsAllianceDunBaldarTower(nodeId))
            return 4;
        return 5;
    }

    if (nodeId == BG_AV_NODES_STONEHEART_GRAVE)
        return 1;
    if (nodeId == BG_AV_NODES_ICEWING_BUNKER || nodeId == BG_AV_NODES_STONEHEART_BUNKER)
        return 2;
    if (nodeId == BG_AV_NODES_STORMPIKE_GRAVE)
        return 3;
    if (IsAllianceDunBaldarTower(nodeId))
        return 4;
    return 5;
}

static GameObject* SelectAllianceAVDefenderObjective(Player* bot, Battleground* bg, BattlegroundAV* av,
                                                     AllianceAVRushInfo const& rushInfo)
{
    if (!bot || !bg || !av)
        return nullptr;

    struct Candidate
    {
        GameObject* go = nullptr;
        uint8 priority = 0;
        uint32 timer = 0;
        float distance = 0.0f;
    };

    std::vector<Candidate> candidates;
    for (auto const& [nodeId, goId] : AV_DefendObjectives_Alliance)
    {
        BG_AV_NodeInfo const& node = av->GetAVNodeInfo(nodeId);
        if (node.State == POINT_DESTROYED)
            continue;

        if (rushInfo.IsActive() &&
            !(node.State == POINT_ASSAULTED && node.OwnerId == TEAM_HORDE && node.PrevOwnerId == TEAM_ALLIANCE))
            continue;

        GameObject* go = bg->GetBGObject(goId);
        if (!go || !go->isSpawned())
            continue;

        candidates.push_back({go, GetAllianceAVDefenderPriority(nodeId, node, rushInfo), node.Timer,
                              bot->GetDistance(go)});
    }

    if (candidates.empty())
        return nullptr;

    std::sort(candidates.begin(), candidates.end(), [](Candidate const& left, Candidate const& right)
    {
        if (left.priority != right.priority)
            return left.priority < right.priority;

        if (left.timer != right.timer && left.priority <= 3)
            return left.timer < right.timer;

        return left.distance < right.distance;
    });

    return candidates.front().go;
}

static bool AllianceControlsNode(BattlegroundAV* av, uint8 nodeId)
{
    if (!av)
        return false;

    BG_AV_NodeInfo const& node = av->GetAVNodeInfo(nodeId);
    return node.State == POINT_CONTROLLED && node.OwnerId == TEAM_ALLIANCE;
}

static GameObject* SelectAllianceGuardedAssaultObjectiveForNode(Battleground* bg, BattlegroundAV* av, uint8 nodeId)
{
    if (!bg || !av)
        return nullptr;

    BG_AV_NodeInfo const& node = av->GetAVNodeInfo(nodeId);
    if (node.State != POINT_ASSAULTED || node.OwnerId != TEAM_ALLIANCE)
        return nullptr;

    for (auto const& [guardNodeId, goId] : AV_AllianceAssaultGuardObjectives)
    {
        if (guardNodeId != nodeId)
            continue;

        if (GameObject* go = bg->GetBGObject(goId))
            if (go->isSpawned())
                return go;
    }

    return nullptr;
}

static GameObject* SelectAllianceAttackNodeObjective(Battleground* bg, BattlegroundAV* av, uint8 nodeId, uint32 goId,
                                                     uint8 role, AVBotStrategy strategy,
                                                     bool holdIcebloodBeachhead = false)
{
    if (!bg || !av)
        return nullptr;

    BG_AV_NodeInfo const& node = av->GetAVNodeInfo(nodeId);
    if (node.State == POINT_DESTROYED || (node.State == POINT_CONTROLLED && node.OwnerId == TEAM_ALLIANCE) ||
        node.TotalOwnerId == TEAM_ALLIANCE)
        return nullptr;

    if ((AllianceAVMustKillCaptainBeforeTowers(av) || holdIcebloodBeachhead) &&
        IsAllianceHordeTowerAttackTarget(nodeId))
        return nullptr;

    if (!AllianceHasAnySouthRespawn(av) && IsAllianceForwardGraveyardAttackTarget(nodeId))
        return nullptr;

    if (GameObject* guard = SelectAllianceGuardedAssaultObjectiveForNode(bg, av, nodeId))
    {
        if (ShouldAllianceGuardAssaultedNode(nodeId, role, strategy))
            return guard;

        return nullptr;
    }

    if (node.State == POINT_ASSAULTED)
        return nullptr;

    if (GameObject* go = bg->GetBGObject(goId))
        if (go->isSpawned())
            return go;

    return nullptr;
}

static GameObject* SelectFirstAllianceAttackNode(Battleground* bg, BattlegroundAV* av,
                                                 std::vector<std::pair<uint8, uint32>> const& objectives, uint8 role,
                                                 AVBotStrategy strategy, bool holdIcebloodBeachhead = false)
{
    for (auto const& [nodeId, goId] : objectives)
        if (GameObject* go = SelectAllianceAttackNodeObjective(bg, av, nodeId, goId, role, strategy,
                                                               holdIcebloodBeachhead))
            return go;

    return nullptr;
}

static uint32 CountBattlegroundPlayersNear(Battleground* bg, TeamId teamId, WorldObject const* center, float radius,
                                           bool aliveOnly)
{
    if (!bg || !center)
        return 0;

    uint32 cachedCount = 0;
    if (TryGetAsyncAVPlayersNear(bg, teamId, center->GetPositionX(), center->GetPositionY(), radius, aliveOnly, false,
                                 cachedCount))
        return cachedCount;

    float const radiusSq = radius * radius;
    uint32 count = 0;
    for (auto const& playerPair : bg->GetPlayers())
    {
        Player* player = playerPair.second;
        if (!player || player->GetTeamId() != teamId)
            continue;

        if (aliveOnly && !player->IsAlive())
            continue;

        float const dx = player->GetPositionX() - center->GetPositionX();
        float const dy = player->GetPositionY() - center->GetPositionY();
        if (dx * dx + dy * dy <= radiusSq)
            ++count;
    }

    return count;
}

static Player* SelectAllianceNorthRushEnemy(Player* bot, Battleground* bg, AllianceAVRushInfo const& rushInfo,
                                            AllianceAVThreatLevel threat, uint8 role, bool isDefender,
                                            uint8 defenderLimit)
{
    if (!bot || !bg || bot->GetTeamId() != TEAM_ALLIANCE || (!rushInfo.IsActive() && threat == AV_THREAT_NONE))
        return nullptr;

    PositionInfo const botPos(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId());
    bool const snowfallForward =
        AllianceAVPositionIsSnowfallRespawn(botPos) || AllianceAVPositionIsSnowfallRun(bg, botPos);
    bool const northernBot = bot->GetPositionX() > -180.0f && !snowfallForward;
    bool const reserveRole = defenderLimit > 0 && role < std::min<uint8>(9, defenderLimit + 2);
    BattlegroundAV* av = static_cast<BattlegroundAV*>(bg);

    if (snowfallForward && !AllianceAVShouldFullRecallNorth(av, threat) && threat < AV_THREAT_HIGH)
    {
        AVBotStrategy const hordeStrategy = static_cast<AVBotStrategy>(BGTactics::GetBotStrategyForTeam(bg, TEAM_HORDE));
        if (AllianceAVShouldUseIcebloodBeachheadPlan(bg, av, threat, hordeStrategy) ||
            AllianceAVNodeAssaultedBy(av, BG_AV_NODES_ICEBLOOD_GRAVE, TEAM_ALLIANCE) ||
            AllianceControlsIcebloodGraveyard(av))
            return nullptr;
    }

    if (AllianceHasCommittedFrostwolfFoothold(av, rushInfo, threat) && !northernBot)
        return nullptr;

    if (AllianceAVNodeAssaultedBy(av, BG_AV_NODES_ICEBLOOD_GRAVE, TEAM_ALLIANCE) &&
        !AllianceAVShouldFullRecallNorth(av, threat) && !northernBot)
        return nullptr;

    if (!isDefender && !reserveRole && !northernBot && threat < AV_THREAT_MEDIUM)
        return nullptr;

    float const maxDistance = threat >= AV_THREAT_HIGH ? 1200.0f :
        (threat >= AV_THREAT_MEDIUM ? 1000.0f : 850.0f);
    float const maxDistanceSq = maxDistance * maxDistance;
    Player* best = nullptr;
    float bestScore = 100000000.0f;

    for (auto const& playerPair : bg->GetPlayers())
    {
        Player* enemy = playerPair.second;
        if (!enemy || enemy->GetTeamId() != TEAM_HORDE || !enemy->IsAlive())
            continue;

        float const enemyX = enemy->GetPositionX();
        if (enemyX <= -220.0f)
            continue;

        float const dx = enemy->GetPositionX() - bot->GetPositionX();
        float const dy = enemy->GetPositionY() - bot->GetPositionY();
        float const distanceSq = dx * dx + dy * dy;
        if (distanceSq > maxDistanceSq)
            continue;

        float score = distanceSq;
        if (enemyX > 250.0f)
            score -= 120000.0f;
        else if (enemyX > -120.0f)
            score -= 60000.0f;

        if (enemy->GetPositionX() > bot->GetPositionX())
            score -= 15000.0f;

        if (score < bestScore)
        {
            bestScore = score;
            best = enemy;
        }
    }

    return best;
}

static bool AllianceAVShouldHoldIcebloodBeachhead(Battleground* bg, BattlegroundAV* av,
                                                  AllianceAVThreatLevel threat,
                                                  AVBotStrategy hordeStrategy)
{
    if (!bg || !av || AllianceAVShouldFullRecallNorth(av, threat))
        return false;

    BG_AV_NodeInfo const& node = av->GetAVNodeInfo(BG_AV_NODES_ICEBLOOD_GRAVE);
    bool const allianceAssaulting = node.State == POINT_ASSAULTED && node.OwnerId == TEAM_ALLIANCE;
    bool const allianceControlled = node.State == POINT_CONTROLLED && node.OwnerId == TEAM_ALLIANCE;
    uint32 const instanceId = bg->GetInstanceID();

    if (!allianceAssaulting && !allianceControlled)
    {
        avAllianceIcebloodBeachheadStates.erase(instanceId);
        return false;
    }

    if (AllianceHasForwardDrekRespawn(av) && !AllianceHordeCaptainAlive(av))
    {
        avAllianceIcebloodBeachheadStates.erase(instanceId);
        return false;
    }

    uint32 const now = bg->GetStartTime();
    auto& state = avAllianceIcebloodBeachheadStates[instanceId];
    if (!state.allianceOwned || state.sinceMs == 0)
    {
        state.allianceOwned = true;
        state.sinceMs = now;
    }

    if (allianceControlled)
    {
        if (state.controlledSinceMs == 0)
            state.controlledSinceMs = now;
    }
    else
        state.controlledSinceMs = 0;

    GameObject* center = SelectAllianceIcebloodGraveyardHoldObjective(bg);
    uint32 const hordeNear = CountBattlegroundPlayersNear(bg, TEAM_HORDE, center, 220.0f, true);
    uint32 const allianceNear = CountBattlegroundPlayersNear(bg, TEAM_ALLIANCE, center, 220.0f, true);
    bool const hordeDefensive = hordeStrategy == AV_STRATEGY_DEFENSIVE;
    bool const localHordePressure = hordeNear >= 5 || (allianceAssaulting && hordeNear >= 2);
    bool const allianceLocalControl = allianceNear >= 12 && allianceNear >= hordeNear + 6;
    uint32 const stableMs = hordeDefensive ? 90000 : 60000;
    bool const controlledFresh = allianceControlled && state.controlledSinceMs > 0 &&
                                 now >= state.controlledSinceMs &&
                                 now - state.controlledSinceMs < stableMs;

    if (allianceAssaulting)
        return true;

    if (controlledFresh)
        return true;

    if (localHordePressure && !allianceLocalControl)
        return true;

    return hordeDefensive && hordeNear >= 4 && !allianceLocalControl;
}

static bool AllianceAVShouldUseIcebloodBeachheadPlan(Battleground* bg, BattlegroundAV* av,
                                                     AllianceAVThreatLevel threat,
                                                     AVBotStrategy hordeStrategy)
{
    if (!bg || !av || AllianceAVShouldFullRecallNorth(av, threat))
        return false;

    BG_AV_NodeInfo const& node = av->GetAVNodeInfo(BG_AV_NODES_ICEBLOOD_GRAVE);
    bool const allianceAssaulting = node.State == POINT_ASSAULTED && node.OwnerId == TEAM_ALLIANCE;
    bool const allianceControlled = node.State == POINT_CONTROLLED && node.OwnerId == TEAM_ALLIANCE;

    if (allianceAssaulting || allianceControlled)
        return AllianceAVShouldHoldIcebloodBeachhead(bg, av, threat, hordeStrategy);

    return false;
}

static bool AllianceAVShouldThrottlePreIcebloodTowerPressure(Battleground* bg, BattlegroundAV* av,
                                                             AVBotStrategy hordeStrategy)
{
    if (!bg || !av || hordeStrategy != AV_STRATEGY_DEFENSIVE)
        return false;

    if (AllianceAVNodeAssaultedBy(av, BG_AV_NODES_ICEBLOOD_GRAVE, TEAM_ALLIANCE) ||
        AllianceControlsIcebloodGraveyard(av))
        return false;

    GameObject* center = SelectAllianceIcebloodGraveyardHoldObjective(bg);
    uint32 const hordeNear = CountBattlegroundPlayersNear(bg, TEAM_HORDE, center, 220.0f, true);
    return hordeNear >= 8;
}

static Player* SelectAllianceIcebloodBeachheadEnemy(Player* bot, Battleground* bg, BattlegroundAV* av,
                                                    AllianceAVThreatLevel threat,
                                                    AVBotStrategy hordeStrategy)
{
    if (!bot || !bg || !av || !AllianceAVShouldUseIcebloodBeachheadPlan(bg, av, threat, hordeStrategy))
        return nullptr;

    BG_AV_NodeInfo const& node = av->GetAVNodeInfo(BG_AV_NODES_ICEBLOOD_GRAVE);
    bool const allianceAssaulting = node.State == POINT_ASSAULTED && node.OwnerId == TEAM_ALLIANCE;
    bool const allianceControlled = node.State == POINT_CONTROLLED && node.OwnerId == TEAM_ALLIANCE;
    if (!allianceAssaulting && !allianceControlled)
        return nullptr;

    float centerX = AV_ALLIANCE_IBGY_FLAG_HOLD.GetPositionX();
    float centerY = AV_ALLIANCE_IBGY_FLAG_HOLD.GetPositionY();
    if (GameObject* center = SelectAllianceIcebloodGraveyardHoldObjective(bg))
    {
        centerX = center->GetPositionX();
        centerY = center->GetPositionY();
    }

    float const defenseRadius = allianceAssaulting ? 100.0f : 125.0f;
    float const defenseRadiusSq = defenseRadius * defenseRadius;
    float const maxBotDistance = bot->GetPositionX() <= -180.0f ? 260.0f : 320.0f;
    Player* bestEnemy = nullptr;
    float bestScore = 0.0f;

    for (auto const& playerPair : bg->GetPlayers())
    {
        Player* enemy = playerPair.second;
        if (!enemy || !enemy->IsAlive() || enemy->GetTeamId() != TEAM_HORDE)
            continue;

        PositionInfo enemyPos(enemy->GetPositionX(), enemy->GetPositionY(), enemy->GetPositionZ(),
                              enemy->GetMapId());
        if (!AllianceAVPositionIsIcebloodHoldPerimeter(bg, enemyPos, allianceAssaulting))
            continue;

        float const centerDx = enemy->GetPositionX() - centerX;
        float const centerDy = enemy->GetPositionY() - centerY;
        float const centerDistSq = centerDx * centerDx + centerDy * centerDy;
        if (centerDistSq > defenseRadiusSq)
            continue;

        float const botDistance = bot->GetDistance(enemy);
        if (botDistance > maxBotDistance)
            continue;

        float const score = centerDistSq + botDistance * botDistance * 0.25f;
        if (!bestEnemy || score < bestScore)
        {
            bestEnemy = enemy;
            bestScore = score;
        }
    }

    return bestEnemy;
}

static bool AllianceAVCanCommitToIcebloodBeachhead(Player* bot, uint8 role, bool isDefender, uint8 defenderLimit,
                                                   AllianceAVRushInfo const& rushInfo,
                                                   AllianceAVThreatLevel threat)
{
    if (!bot || isDefender)
        return false;

    PositionInfo const botPos(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId());
    if (bot->GetPositionX() <= -180.0f || AllianceAVPositionIsSnowfallRespawn(botPos))
        return true;

    if (threat >= AV_THREAT_MEDIUM || rushInfo.level == AV_RUSH_DEEP || rushInfo.level == AV_RUSH_DUNBALDAR)
        return false;

    uint64 const botId = bot->GetGUID().GetCounter();
    bool const quietNorth = threat == AV_THREAT_NONE && !rushInfo.IsActive() &&
                            rushInfo.hordeNorth == 0 && rushInfo.hordeDeepNorth == 0 &&
                            rushInfo.hordeDunBaldar == 0;
    if (quietNorth && rushInfo.allianceSouth < 16)
        return role >= defenderLimit;

    if (threat == AV_THREAT_LOW || rushInfo.IsActive())
        return role == defenderLimit && botId % 2 == 0;

    if (role == defenderLimit)
        return true;

    return defenderLimit < 9 && role == defenderLimit + 1 && botId % 4 == 0;
}

static bool SetAllianceIcebloodBeachheadPosition(Player* bot, Battleground* bg, BattlegroundAV* av,
                                                 PositionMap& posMap, PositionInfo& pos, uint8 role,
                                                 uint8 defenderLimit, char const*& objectiveReason)
{
    if (!bot || !bg || !av)
        return false;

    BG_AV_NodeInfo const& node = av->GetAVNodeInfo(BG_AV_NODES_ICEBLOOD_GRAVE);
    bool const allianceAssaulting = node.State == POINT_ASSAULTED && node.OwnerId == TEAM_ALLIANCE;
    bool const allianceControlled = node.State == POINT_CONTROLLED && node.OwnerId == TEAM_ALLIANCE;
    uint8 const firstOffenseRole = std::min<uint8>(defenderLimit, 9);
    uint8 const relativeRole = role >= firstOffenseRole ? role - firstOffenseRole : role;
    uint8 const holdSlot = allianceAssaulting ? relativeRole % 8 : relativeRole % 6;

    Position const* target = &AV_ALLIANCE_IBGY_FLAG_HOLD;
    float radius = 6.0f;

    switch (holdSlot)
    {
        case 0:
        case 1:
        case 2:
        case 3:
            target = &AV_ALLIANCE_IBGY_FLAG_HOLD;
            radius = allianceControlled ? 7.0f : 5.0f;
            break;
        case 4:
            if (!allianceAssaulting)
            {
                target = &AV_ALLIANCE_IBGY_NORTH_LOCKDOWN;
                radius = 6.0f;
                break;
            }
            target = &AV_ALLIANCE_IBGY_FLAG_HOLD;
            radius = 5.0f;
            break;
        case 5:
            if (!allianceAssaulting)
            {
                target = &AV_ALLIANCE_IBGY_SOUTH_LOCKDOWN;
                radius = 6.0f;
                break;
            }
            target = &AV_ALLIANCE_IBGY_FLAG_HOLD;
            radius = 5.0f;
            break;
        case 6:
            target = &AV_ALLIANCE_IBGY_NORTH_LOCKDOWN;
            radius = 6.0f;
            break;
        case 7:
            target = &AV_ALLIANCE_IBGY_SOUTH_LOCKDOWN;
            radius = 6.0f;
            break;
    }
    objectiveReason = allianceControlled ? "alliance iceblood controlled hold" : "alliance iceblood assault hold";

    float rx, ry, rz;
    bot->GetRandomPoint(*target, radius, rx, ry, rz);
    ResolveBattleGroundGroundZ(bot, rx, ry, rz);

    pos.Set(rx, ry, rz, bot->GetMapId());
    posMap["bg objective"] = pos;
    return true;
}

static bool SetAllianceNorthReservePosition(Player* bot, PositionMap& posMap, PositionInfo& pos, uint8 role,
                                            char const*& objectiveReason)
{
    if (!bot)
        return false;

    Position const* target = &AV_ALLIANCE_ANTIRUSH_STONEHEARTH_ROAD;
    switch (role % 4)
    {
        case 0:
            target = &AV_ALLIANCE_ANTIRUSH_STONEHEARTH_ROAD;
            break;
        case 1:
            target = &AV_ALLIANCE_ANTIRUSH_ICEWING_ROAD;
            break;
        case 2:
            target = &AV_ALLIANCE_ANTIRUSH_STORMPIKE_ROAD;
            break;
        default:
            target = &AV_ALLIANCE_ANTIRUSH_DUNBALDAR_ROAD;
            break;
    }

    float rx, ry, rz;
    bot->GetRandomPoint(*target, 10.0f, rx, ry, rz);
    ResolveBattleGroundGroundZ(bot, rx, ry, rz);

    pos.Set(rx, ry, rz, bot->GetMapId());
    posMap["bg objective"] = pos;
    objectiveReason = "alliance north reserve screen";
    return true;
}

static Creature* GetAllianceHordeBoss(Player* bot, Battleground* bg)
{
    Creature* boss = bg ? bg->GetBGCreature(AV_CREATURE_H_BOSS) : nullptr;
    if (boss && boss->IsAlive())
        return boss;

    if (!bot)
        return nullptr;

    boss = bot->FindNearestCreature(AV_NPC_DREKTHAR_ENTRY, 300.0f, true);
    return boss && boss->IsAlive() ? boss : nullptr;
}

static Creature* SelectAllianceDrekPushObjective(Player* bot, Battleground* bg, BattlegroundAV* av,
                                                  AllianceAVThreatLevel threat, AllianceAVBattlefieldMode mode,
                                                  AllianceAVRushInfo const& rushInfo, uint32 towersDown,
                                                  uint32 defensivePressure)
{
    if (!bot || !bg || !av)
        return nullptr;

    bool const hasForwardRespawn = AllianceHasForwardDrekRespawn(av);
    bool const captainGateOpen = !AllianceHordeCaptainAlive(av) || towersDown >= 4;
    bool const finalPushWindow = AllianceHasFinalDrekWindow(av, rushInfo, towersDown);
    bool const northSafeForBoss = threat < AV_THREAT_HIGH && (defensivePressure == 0 || towersDown >= 4);

    if (AllianceAVShouldFullRecallNorth(av, threat))
        return nullptr;

    if (!captainGateOpen || (!northSafeForBoss && !finalPushWindow))
        return nullptr;

    if (mode != AV_MODE_DREK_PUSH && !finalPushWindow)
        return nullptr;

    Creature* boss = GetAllianceHordeBoss(bot, bg);
    if (!boss || !boss->IsAlive())
        return nullptr;

    if (finalPushWindow || towersDown >= 4)
        return boss;

    if (mode == AV_MODE_DREK_PUSH && hasForwardRespawn && towersDown >= 3)
        return boss;

    if (towersDown >= 3 && CountBattlegroundPlayersNear(bg, TEAM_ALLIANCE, boss, 200.0f, false) >= 12)
        return boss;

    return nullptr;
}

static bool SetAllianceDrekPushRallyPosition(Player* bot, PositionMap& posMap, PositionInfo& pos,
                                             char const*& objectiveReason)
{
    if (!bot)
        return false;

    float rx, ry, rz;
    bot->GetRandomPoint(AV_BOSS_WAIT_A, 4.0f, rx, ry, rz);
    ResolveBattleGroundGroundZ(bot, rx, ry, rz);

    pos.Set(rx, ry, rz, bot->GetMapId());
    posMap["bg objective"] = pos;
    objectiveReason = "alliance committed drek rally";
    return true;
}

static GameObject* SelectAllianceFrostwolfLockObjective(Player* bot, Battleground* bg, BattlegroundAV* av, uint8 role,
                                                        AllianceAVBattlefieldMode mode, char const*& objectiveReason)
{
    if (!bot || !bg || !av || bot->GetTeamId() != TEAM_ALLIANCE || role < 5)
        return nullptr;

    bool const fwGraveControlled = AllianceAVNodeControlledBy(av, BG_AV_NODES_FROSTWOLF_GRAVE, TEAM_ALLIANCE);
    bool const fwHutControlled = AllianceAVNodeControlledBy(av, BG_AV_NODES_FROSTWOLF_HUT, TEAM_ALLIANCE);
    bool const deepFrostwolfBot = bot->GetPositionX() <= -900.0f;

    if (!fwGraveControlled)
    {
        std::vector<std::pair<uint8, uint32>> objectives = {
            {BG_AV_NODES_FROSTWOLF_GRAVE, BG_AV_OBJECT_FLAG_H_FROSTWOLF_GRAVE},
        };

        if (GameObject* target = SelectFirstAllianceAttackNode(bg, av, objectives, role,
                                                               AV_STRATEGY_ALLIANCE_CONTROL_TEMPO))
        {
            objectiveReason = "control tempo frostwolf grave lock";
            return target;
        }
    }

    if (!fwHutControlled && (mode == AV_MODE_FROSTWOLF_LOCK || role >= 7))
    {
        std::vector<std::pair<uint8, uint32>> objectives = {
            {BG_AV_NODES_FROSTWOLF_HUT, BG_AV_OBJECT_FLAG_H_FROSTWOLF_HUT},
        };

        if (GameObject* target = SelectFirstAllianceAttackNode(bg, av, objectives, role,
                                                               AV_STRATEGY_ALLIANCE_CONTROL_TEMPO))
        {
            objectiveReason = "control tempo frostwolf hut lock";
            return target;
        }
    }

    if (mode != AV_MODE_DREK_SETUP)
        return nullptr;

    std::vector<std::pair<uint8, uint32>> objectives;
    if (deepFrostwolfBot)
    {
        objectives = {
            {BG_AV_NODES_FROSTWOLF_ETOWER, BG_AV_OBJECT_FLAG_H_FROSTWOLF_ETOWER},
            {BG_AV_NODES_FROSTWOLF_WTOWER, BG_AV_OBJECT_FLAG_H_FROSTWOLF_WTOWER},
            {BG_AV_NODES_FROSTWOLF_HUT, BG_AV_OBJECT_FLAG_H_FROSTWOLF_HUT},
            {BG_AV_NODES_FROSTWOLF_GRAVE, BG_AV_OBJECT_FLAG_H_FROSTWOLF_GRAVE},
        };
    }
    else switch (role)
    {
        case 5:
        case 6:
            objectives = {
                {BG_AV_NODES_ICEBLOOD_TOWER, BG_AV_OBJECT_FLAG_H_ICEBLOOD_TOWER},
                {BG_AV_NODES_TOWER_POINT, BG_AV_OBJECT_FLAG_H_TOWER_POINT},
                {BG_AV_NODES_FROSTWOLF_ETOWER, BG_AV_OBJECT_FLAG_H_FROSTWOLF_ETOWER},
                {BG_AV_NODES_FROSTWOLF_WTOWER, BG_AV_OBJECT_FLAG_H_FROSTWOLF_WTOWER},
            };
            break;
        case 7:
            objectives = {
                {BG_AV_NODES_TOWER_POINT, BG_AV_OBJECT_FLAG_H_TOWER_POINT},
                {BG_AV_NODES_FROSTWOLF_ETOWER, BG_AV_OBJECT_FLAG_H_FROSTWOLF_ETOWER},
                {BG_AV_NODES_FROSTWOLF_WTOWER, BG_AV_OBJECT_FLAG_H_FROSTWOLF_WTOWER},
            };
            break;
        default:
            objectives = {
                {BG_AV_NODES_FROSTWOLF_ETOWER, BG_AV_OBJECT_FLAG_H_FROSTWOLF_ETOWER},
                {BG_AV_NODES_FROSTWOLF_WTOWER, BG_AV_OBJECT_FLAG_H_FROSTWOLF_WTOWER},
                {BG_AV_NODES_TOWER_POINT, BG_AV_OBJECT_FLAG_H_TOWER_POINT},
                {BG_AV_NODES_ICEBLOOD_TOWER, BG_AV_OBJECT_FLAG_H_ICEBLOOD_TOWER},
            };
            break;
    }

    if (GameObject* target = SelectFirstAllianceAttackNode(bg, av, objectives, role,
                                                           AV_STRATEGY_ALLIANCE_CONTROL_TEMPO))
    {
        objectiveReason = "control tempo drek setup towers";
        return target;
    }

    return nullptr;
}

static bool AllianceAVCanBaitIcebloodTowerDuringIcebloodAssault(Player* bot, Battleground* bg, BattlegroundAV* av,
                                                                uint8 role, AllianceAVThreatLevel threat)
{
    if (!bot || !bg || !av || bot->GetTeamId() != TEAM_ALLIANCE || role != 9 ||
        AllianceAVShouldFullRecallNorth(av, threat) || threat >= AV_THREAT_MEDIUM ||
        AllianceHordeCaptainAlive(av) || bot->GetPositionX() > -180.0f)
        return false;

    BG_AV_NodeInfo const& iceblood = av->GetAVNodeInfo(BG_AV_NODES_ICEBLOOD_GRAVE);
    if (iceblood.State != POINT_ASSAULTED || iceblood.OwnerId != TEAM_ALLIANCE)
        return false;

    BG_AV_NodeInfo const& tower = av->GetAVNodeInfo(BG_AV_NODES_ICEBLOOD_TOWER);
    if (tower.State != POINT_CONTROLLED || tower.OwnerId != TEAM_HORDE)
        return false;

    GameObject* icebloodCenter = SelectAllianceIcebloodGraveyardHoldObjective(bg);
    uint32 const allianceNearIceblood = CountBattlegroundPlayersNear(bg, TEAM_ALLIANCE, icebloodCenter, 180.0f, true);
    uint32 const hordeNearIceblood = CountBattlegroundPlayersNear(bg, TEAM_HORDE, icebloodCenter, 180.0f, true);
    return allianceNearIceblood >= 8 && hordeNearIceblood <= allianceNearIceblood + 5;
}

static GameObject* SelectAllianceIcebloodTowerBaitObjective(Player* bot, Battleground* bg, BattlegroundAV* av,
                                                            uint8 role, AllianceAVThreatLevel threat)
{
    if (!AllianceAVCanBaitIcebloodTowerDuringIcebloodAssault(bot, bg, av, role, threat))
        return nullptr;

    std::vector<std::pair<uint8, uint32>> objectives = {
        {BG_AV_NODES_ICEBLOOD_TOWER, BG_AV_OBJECT_FLAG_H_ICEBLOOD_TOWER},
    };

    return SelectFirstAllianceAttackNode(bg, av, objectives, role, AV_STRATEGY_ALLIANCE_CONTROL_TEMPO);
}

static bool ShouldAllianceStrikeHordeCaptain(uint8 role, AllianceAVRushInfo const& rushInfo,
                                              AllianceAVBattlefieldMode mode)
{
    if (mode != AV_MODE_GALVANGAR_STRIKE)
        return false;

    return role >= 5 || (rushInfo.elapsedMs >= 6 * 60 * 1000 && role >= 4);
}

static WorldObject* SelectAllianceControlTempoObjective(Player* bot, Battleground* bg, BattlegroundAV* av, uint8 role,
                                                        AllianceAVRushInfo const& rushInfo,
                                                        AllianceAVThreatLevel threat,
                                                        AllianceAVBattlefieldMode mode,
                                                        char const*& objectiveReason)
{
    if (!bot || !bg || !av || bot->GetTeamId() != TEAM_ALLIANCE)
        return nullptr;

    AVBotStrategy const hordeStrategy = static_cast<AVBotStrategy>(BGTactics::GetBotStrategyForTeam(bg, TEAM_HORDE));
    bool const holdIcebloodBeachhead = AllianceAVShouldHoldIcebloodBeachhead(bg, av, threat, hordeStrategy);
    bool const icebloodControlled = AllianceControlsNode(av, BG_AV_NODES_ICEBLOOD_GRAVE);
    bool const committedFrostwolf = AllianceHasCommittedFrostwolfFoothold(av, rushInfo, threat);
    uint32 const towersDown = GetAllianceDestroyedHordeTowerCount(av);
    uint32 const defensivePressure = GetAllianceDefensivePressure(av);
    if (!icebloodControlled)
    {
        if (committedFrostwolf)
        {
            if (Creature* boss = SelectAllianceDrekPushObjective(bot, bg, av, threat, mode, rushInfo, towersDown,
                                                                 defensivePressure))
            {
                objectiveReason = towersDown >= 4 ? "control tempo drek final push" : "control tempo drek push";
                return boss;
            }

            if (mode == AV_MODE_FROSTWOLF_LOCK || mode == AV_MODE_DREK_SETUP || mode == AV_MODE_DREK_PUSH)
                if (GameObject* target = SelectAllianceFrostwolfLockObjective(bot, bg, av, role, mode, objectiveReason))
                    return target;

            if (GameObject* guard = SelectAllianceAssaultGuardObjective(bot, bg, av, role,
                                                                        AV_STRATEGY_ALLIANCE_CONTROL_TEMPO,
                                                                        false, true))
            {
                objectiveReason = "control tempo committed frostwolf guard";
                return guard;
            }

            objectiveReason = "control tempo committed frostwolf hold";
            if (GameObject* go = bg->GetBGObject(BG_AV_OBJECT_FLAG_A_FROSTWOLF_GRAVE))
                if (go->isSpawned())
                    return go;
        }

        if (holdIcebloodBeachhead)
        {
            if (GameObject* bait = SelectAllianceIcebloodTowerBaitObjective(bot, bg, av, role, threat))
            {
                objectiveReason = "control tempo iceblood tower bait";
                return bait;
            }

            objectiveReason = "control tempo iceblood beachhead";
            return SelectAllianceIcebloodGraveyardObjective(bg, av);
        }

        if (ShouldAllianceStrikeHordeCaptain(role, rushInfo, mode))
        {
            if (Creature* captain = GetAllianceHordeCaptain(bg, av))
            {
                objectiveReason = "control tempo galvangar strike";
                return captain;
            }
        }

        if (AllianceShouldTakeSnowfall(av, rushInfo, threat, mode, role))
        {
            if (GameObject* snowfall = SelectAllianceSnowfallObjective(bg, av))
            {
                objectiveReason = "control tempo snowfall";
                return snowfall;
            }
        }

        GameObject* icebloodCenter = SelectAllianceIcebloodGraveyardHoldObjective(bg);
        uint32 const hordeNearIceblood = CountBattlegroundPlayersNear(bg, TEAM_HORDE, icebloodCenter, 220.0f, true);
        bool const canPressureTowersBeforeIceblood =
            hordeStrategy != AV_STRATEGY_DEFENSIVE && threat == AV_THREAT_NONE && !rushInfo.IsActive() &&
            rushInfo.allianceSouth >= 12 && hordeNearIceblood <= 3 &&
            !AllianceAVShouldThrottlePreIcebloodTowerPressure(bg, av, hordeStrategy);

        if (!AllianceHordeCaptainAlive(av) && role >= 8 && canPressureTowersBeforeIceblood)
        {
            std::vector<std::pair<uint8, uint32>> towerPressureObjectives = {
                {BG_AV_NODES_ICEBLOOD_TOWER, BG_AV_OBJECT_FLAG_H_ICEBLOOD_TOWER},
                {BG_AV_NODES_TOWER_POINT, BG_AV_OBJECT_FLAG_H_TOWER_POINT},
            };

            if (GameObject* target = SelectFirstAllianceAttackNode(bg, av, towerPressureObjectives, role,
                                                                   AV_STRATEGY_ALLIANCE_CONTROL_TEMPO))
            {
                objectiveReason = "control tempo tower pressure before iceblood";
                return target;
            }
        }

        objectiveReason = mode == AV_MODE_IBGY_GUARD ? "control tempo iceblood guard" : "control tempo iceblood first";
        return SelectAllianceIcebloodGraveyardObjective(bg, av);
    }

    if (Creature* boss = SelectAllianceDrekPushObjective(bot, bg, av, threat, mode, rushInfo, towersDown, defensivePressure))
    {
        objectiveReason = towersDown >= 4 ? "control tempo drek final push" : "control tempo drek push";
        return boss;
    }

    if (holdIcebloodBeachhead)
    {
        objectiveReason = "control tempo iceblood beachhead";
        return SelectAllianceIcebloodGraveyardHoldObjective(bg);
    }

    if (ShouldAllianceStrikeHordeCaptain(role, rushInfo, mode))
    {
        if (Creature* captain = GetAllianceHordeCaptain(bg, av))
        {
            objectiveReason = "control tempo galvangar strike";
            return captain;
        }
    }

    if (mode == AV_MODE_FROSTWOLF_LOCK || mode == AV_MODE_DREK_SETUP)
        if (GameObject* target = SelectAllianceFrostwolfLockObjective(bot, bg, av, role, mode, objectiveReason))
            return target;

    if (AllianceShouldTakeSnowfall(av, rushInfo, threat, mode, role))
    {
        if (GameObject* snowfall = SelectAllianceSnowfallObjective(bg, av))
        {
            objectiveReason = "control tempo snowfall";
            return snowfall;
        }
    }

    if (GameObject* guard = SelectAllianceAssaultGuardObjective(bot, bg, av, role,
                                                                AV_STRATEGY_ALLIANCE_CONTROL_TEMPO,
                                                                holdIcebloodBeachhead, committedFrostwolf))
    {
        objectiveReason = "control tempo guard assault";
        return guard;
    }

    std::vector<std::pair<uint8, uint32>> roleObjectives;
    switch (role)
    {
        case 5:
            roleObjectives = {
                {BG_AV_NODES_ICEBLOOD_TOWER, BG_AV_OBJECT_FLAG_H_ICEBLOOD_TOWER},
            };
            break;
        case 6:
            roleObjectives = {
                {BG_AV_NODES_ICEBLOOD_TOWER, BG_AV_OBJECT_FLAG_H_ICEBLOOD_TOWER},
                {BG_AV_NODES_TOWER_POINT, BG_AV_OBJECT_FLAG_H_TOWER_POINT},
            };
            break;
        case 7:
            roleObjectives = {
                {BG_AV_NODES_TOWER_POINT, BG_AV_OBJECT_FLAG_H_TOWER_POINT},
            };
            break;
        case 8:
            roleObjectives = {
                {BG_AV_NODES_FROSTWOLF_GRAVE, BG_AV_OBJECT_FLAG_H_FROSTWOLF_GRAVE},
                {BG_AV_NODES_TOWER_POINT, BG_AV_OBJECT_FLAG_H_TOWER_POINT},
            };
            break;
        case 9:
            roleObjectives = {
                {BG_AV_NODES_FROSTWOLF_ETOWER, BG_AV_OBJECT_FLAG_H_FROSTWOLF_ETOWER},
                {BG_AV_NODES_FROSTWOLF_WTOWER, BG_AV_OBJECT_FLAG_H_FROSTWOLF_WTOWER},
                {BG_AV_NODES_FROSTWOLF_HUT, BG_AV_OBJECT_FLAG_H_FROSTWOLF_HUT},
            };
            break;
        default:
            roleObjectives = {
                {BG_AV_NODES_ICEBLOOD_TOWER, BG_AV_OBJECT_FLAG_H_ICEBLOOD_TOWER},
                {BG_AV_NODES_TOWER_POINT, BG_AV_OBJECT_FLAG_H_TOWER_POINT},
            };
            break;
    }

    if (GameObject* target = SelectFirstAllianceAttackNode(bg, av, roleObjectives, role,
                                                           AV_STRATEGY_ALLIANCE_CONTROL_TEMPO))
    {
        objectiveReason = "control tempo role objective";
        return target;
    }

    std::vector<std::pair<uint8, uint32>> fallbackObjectives = {
        {BG_AV_NODES_ICEBLOOD_TOWER, BG_AV_OBJECT_FLAG_H_ICEBLOOD_TOWER},
        {BG_AV_NODES_TOWER_POINT, BG_AV_OBJECT_FLAG_H_TOWER_POINT},
        {BG_AV_NODES_FROSTWOLF_GRAVE, BG_AV_OBJECT_FLAG_H_FROSTWOLF_GRAVE},
        {BG_AV_NODES_FROSTWOLF_ETOWER, BG_AV_OBJECT_FLAG_H_FROSTWOLF_ETOWER},
        {BG_AV_NODES_FROSTWOLF_WTOWER, BG_AV_OBJECT_FLAG_H_FROSTWOLF_WTOWER},
        {BG_AV_NODES_FROSTWOLF_HUT, BG_AV_OBJECT_FLAG_H_FROSTWOLF_HUT},
    };

    if (GameObject* target = SelectFirstAllianceAttackNode(bg, av, fallbackObjectives, role,
                                                           AV_STRATEGY_ALLIANCE_CONTROL_TEMPO))
    {
        objectiveReason = "control tempo fallback objective";
        return target;
    }

    if (Creature* boss = SelectAllianceDrekPushObjective(bot, bg, av, threat, mode, rushInfo, towersDown, defensivePressure))
    {
        objectiveReason = towersDown >= 4 ? "control tempo drek final fallback" : "control tempo drek fallback";
        return boss;
    }

    objectiveReason = "control tempo hold forward";
    if (GameObject* go = bg->GetBGObject(BG_AV_OBJECT_FLAG_A_FROSTWOLF_GRAVE))
        if (go->isSpawned())
            return go;

    return nullptr;
}

static bool AllianceAVEnemyIsHomeThreat(Unit* enemy)
{
    return enemy && enemy->GetPositionX() > -120.0f;
}

static void LogAllianceAVState(Battleground* bg, BattlegroundAV* av, AllianceAVRushInfo const& rushInfo,
                               AllianceAVThreatLevel threat, AllianceAVBattlefieldMode mode, uint8 defendersLimit,
                               AVBotStrategy allianceStrategy, AVBotStrategy hordeStrategy)
{
    if (!bg || !av)
        return;

    uint32 const instanceId = bg->GetInstanceID();
    uint32 const elapsedMs = bg->GetStartTime();
    auto itr = avAllianceDebugLogTimers.find(instanceId);
    if (itr != avAllianceDebugLogTimers.end() && elapsedMs >= itr->second && elapsedMs - itr->second < 15000)
        return;

    avAllianceDebugLogTimers[instanceId] = elapsedMs;

    BG_AV_NodeInfo const& shGy = av->GetAVNodeInfo(BG_AV_NODES_STONEHEART_GRAVE);
    BG_AV_NodeInfo const& spGy = av->GetAVNodeInfo(BG_AV_NODES_STORMPIKE_GRAVE);
    BG_AV_NodeInfo const& dbNorth = av->GetAVNodeInfo(BG_AV_NODES_DUNBALDAR_NORTH);
    BG_AV_NodeInfo const& dbSouth = av->GetAVNodeInfo(BG_AV_NODES_DUNBALDAR_SOUTH);
    BG_AV_NodeInfo const& ibGy = av->GetAVNodeInfo(BG_AV_NODES_ICEBLOOD_GRAVE);
    BG_AV_NodeInfo const& fwGy = av->GetAVNodeInfo(BG_AV_NODES_FROSTWOLF_GRAVE);
    BG_AV_NodeInfo const& fwHut = av->GetAVNodeInfo(BG_AV_NODES_FROSTWOLF_HUT);
    uint32 const towersDown = GetAllianceDestroyedHordeTowerCount(av);
    uint32 const towerProgress = GetAllianceHordeTowerProgressCount(av);
    bool const finalDrek = AllianceHasFinalDrekWindow(av, rushInfo, towersDown);

    LOG_INFO("playerbots",
             "AV alliance state instance={} elapsed={}s AStrategy={} HStrategy={} mode={} threat={} rush={} Hnorth={} Hdeep={} Hdb={} Asouth={} defenders={} towersDown={} towerProgress={} finalDrek={} galvAlive={} SHGY={}/{} SPGY={}/{} DBN={}/{} DBS={}/{} IBGY={}/{} FWGY={}/{} FWHut={}/{}",
             instanceId, elapsedMs / 1000, static_cast<uint32>(allianceStrategy), static_cast<uint32>(hordeStrategy),
             GetAllianceAVBattlefieldModeName(mode), GetAllianceAVThreatLevelName(threat),
             GetAllianceAVRushLevelName(rushInfo.level), rushInfo.hordeNorth, rushInfo.hordeDeepNorth,
             rushInfo.hordeDunBaldar, rushInfo.allianceSouth, static_cast<uint32>(defendersLimit),
             towersDown, towerProgress, finalDrek ? 1 : 0, AllianceHordeCaptainAlive(av) ? 1 : 0, static_cast<uint32>(shGy.State),
             static_cast<uint32>(shGy.OwnerId), static_cast<uint32>(spGy.State), static_cast<uint32>(spGy.OwnerId),
             static_cast<uint32>(dbNorth.State), static_cast<uint32>(dbNorth.OwnerId), static_cast<uint32>(dbSouth.State),
             static_cast<uint32>(dbSouth.OwnerId), static_cast<uint32>(ibGy.State), static_cast<uint32>(ibGy.OwnerId),
             static_cast<uint32>(fwGy.State), static_cast<uint32>(fwGy.OwnerId), static_cast<uint32>(fwHut.State),
             static_cast<uint32>(fwHut.OwnerId));
}

static void LogAllianceAVMoveDebug(Player* bot, Battleground* bg, PositionInfo const& objectivePos,
                                   char const* reason, uint8 role, uint8 defenderLimit, bool isDefender,
                                   Unit* enemyTarget, float nearestPathDistance = -1.0f, uint32 pathCount = 0)
{
    if (!sPlayerbotAIConfig.allianceAVMoveDebug)
        return;

    if (!bot || !bg || bot->GetTeamId() != TEAM_ALLIANCE)
        return;

    BattlegroundTypeId bgType = bg->GetBgTypeID();
    if (bgType == BATTLEGROUND_RB)
        bgType = bg->GetBgTypeID(true);

    if (bgType != BATTLEGROUND_AV)
        return;

    uint32 const elapsedMs = bg->GetStartTime();
    uint64 const botId = bot->GetGUID().GetCounter();
    uint64 reasonHash = 1469598103934665603ULL;
    for (char const* itr = reason; itr && *itr; ++itr)
    {
        reasonHash ^= static_cast<uint8>(*itr);
        reasonHash *= 1099511628211ULL;
    }

    uint64 const logKey = botId ^ (reasonHash << 1);
    auto itr = avAllianceMoveDebugLogTimers.find(logKey);
    if (itr != avAllianceMoveDebugLogTimers.end() && elapsedMs >= itr->second && elapsedMs - itr->second < 5000)
        return;

    avAllianceMoveDebugLogTimers[logKey] = elapsedMs;

    BattlegroundAV* av = static_cast<BattlegroundAV*>(bg);
    AVBotStrategy const allianceStrategy = static_cast<AVBotStrategy>(BGTactics::GetBotStrategyForTeam(bg, TEAM_ALLIANCE));
    AVBotStrategy const hordeStrategy = static_cast<AVBotStrategy>(BGTactics::GetBotStrategyForTeam(bg, TEAM_HORDE));
    AllianceAVRushInfo rushInfo = GetAllianceAVRushInfo(bg, hordeStrategy);
    AllianceAVThreatLevel threat = AV_THREAT_NONE;
    AllianceAVBattlefieldMode mode = AV_MODE_OPENING_CONTROL;
    AllianceAVCachedTactics cachedAllianceTactics;
    bool const hasCachedAllianceTactics = TryGetAsyncAllianceAVTactics(bg, av, cachedAllianceTactics);
    if (hasCachedAllianceTactics)
    {
        rushInfo = cachedAllianceTactics.rushInfo;
        threat = cachedAllianceTactics.threat;
        mode = cachedAllianceTactics.mode;
    }
    else
    {
        threat = GetAllianceAVThreatLevel(av, rushInfo);
        mode = GetAllianceAVBattlefieldMode(bg, av, rushInfo, threat);
    }

    if (defenderLimit == 255)
    {
        defenderLimit = hasCachedAllianceTactics ?
            cachedAllianceTactics.defenderLimit : GetAVDefenderRoleLimit(TEAM_ALLIANCE, allianceStrategy, hordeStrategy, av);
        if (!hasCachedAllianceTactics)
            defenderLimit = std::max<uint8>(
                defenderLimit, GetAllianceAVRushDefenderLimit(av, rushInfo, allianceStrategy, hordeStrategy));
        isDefender = role < defenderLimit;
    }

    bool const objectiveSet = objectivePos.valueSet;
    float const objectiveDistance = objectiveSet ?
        ServerFacade::instance().GetDistance2d(bot, objectivePos.x, objectivePos.y) : -1.0f;
    uint32 const motionType = bot->GetMotionMaster() ?
        static_cast<uint32>(bot->GetMotionMaster()->GetCurrentMovementGeneratorType()) : 0;
    bool const hasEnemyTarget = enemyTarget != nullptr;
    bool const inCombat = bot->GetVehicle() ? hasEnemyTarget : bot->IsInCombat();
    bool const casting = AllianceAVIsCastingAnySpell(bot);
    uint32 const hordeNear80 = CountBattlegroundPlayersNear(bg, TEAM_HORDE, bot, 80.0f, true);
    uint32 const hordeNear200 = CountBattlegroundPlayersNear(bg, TEAM_HORDE, bot, 200.0f, true);

    Unit* victim = bot->GetVictim();
    Unit* selected = bot->GetSelectedUnit();
    Unit* debugTarget = victim ? victim : (enemyTarget ? enemyTarget : selected);
    char const* targetSource = victim ? "victim" : (enemyTarget ? "enemy" : (selected ? "selected" : "none"));
    char const* targetType = "none";
    std::string targetName = "-";
    uint32 targetEntry = 0;
    float targetDistance = -1.0f;
    float targetIcebloodDistance = -1.0f;
    bool targetTowerRun = false;

    if (debugTarget)
    {
        targetDistance = bot->GetDistance(debugTarget);
        targetIcebloodDistance = debugTarget->GetDistance2d(AV_ALLIANCE_IBGY_FLAG_HOLD.GetPositionX(),
                                                            AV_ALLIANCE_IBGY_FLAG_HOLD.GetPositionY());
        targetName = debugTarget->GetName();

        if (Player* targetPlayer = debugTarget->ToPlayer())
        {
            targetType = "player";
            targetEntry = static_cast<uint32>(targetPlayer->GetGUID().GetCounter());
        }
        else if (Creature* targetCreature = debugTarget->ToCreature())
        {
            targetType = "creature";
            targetEntry = targetCreature->GetEntry();
        }
        else
            targetType = "unit";

        PositionInfo targetPos(debugTarget->GetPositionX(), debugTarget->GetPositionY(),
                               debugTarget->GetPositionZ(), debugTarget->GetMapId());
        targetTowerRun = AllianceAVPositionIsHordeTowerRun(bg, targetPos);
    }

    PositionInfo botPos(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId());
    float const botIcebloodDistance = bot->GetDistance2d(AV_ALLIANCE_IBGY_FLAG_HOLD.GetPositionX(),
                                                         AV_ALLIANCE_IBGY_FLAG_HOLD.GetPositionY());
    bool const objectiveIcebloodRun = AllianceAVPositionIsIcebloodGraveRun(objectivePos);
    bool const botIcebloodRun = AllianceAVPositionIsIcebloodGraveRun(botPos);
    bool const botTowerRun = AllianceAVPositionIsHordeTowerRun(bg, botPos);
    bool const ibgyLeashBreak = objectiveIcebloodRun && !botIcebloodRun && botIcebloodDistance > 120.0f;

    LOG_INFO("playerbots",
             "AV alliance move-debug reason={} bot={} elapsed={}s role={} defenderLimit={} defender={} AStrategy={} HStrategy={} mode={} threat={} rush={} pos=({:.1f},{:.1f},{:.1f}) objSet={} obj=({:.1f},{:.1f},{:.1f}) objDist={:.1f} moving={} stopped={} combat={} casting={} enemyTarget={} motion={} pathNearest={:.1f} pathCount={} Hnear80={} Hnear200={} Hnorth={} Hdeep={} Hdb={} Asouth={} botIBGY={:.1f} objIBGY={} botAtIBGY={} botTower={} ibgyLeashBreak={} targetSrc={} targetType={} targetEntry={} targetName={} targetDist={:.1f} targetIBGY={:.1f} targetTower={}",
             reason ? reason : "unknown", bot->GetName(), elapsedMs / 1000, static_cast<uint32>(role),
             static_cast<uint32>(defenderLimit), isDefender ? 1 : 0, static_cast<uint32>(allianceStrategy),
             static_cast<uint32>(hordeStrategy), GetAllianceAVBattlefieldModeName(mode),
             GetAllianceAVThreatLevelName(threat), GetAllianceAVRushLevelName(rushInfo.level),
             bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), objectiveSet ? 1 : 0,
             objectiveSet ? objectivePos.x : 0.0f, objectiveSet ? objectivePos.y : 0.0f,
             objectiveSet ? objectivePos.z : 0.0f, objectiveDistance, bot->isMoving() ? 1 : 0,
             bot->IsStopped() ? 1 : 0, inCombat ? 1 : 0, casting ? 1 : 0, hasEnemyTarget ? 1 : 0,
             motionType, nearestPathDistance, pathCount, hordeNear80, hordeNear200, rushInfo.hordeNorth,
             rushInfo.hordeDeepNorth, rushInfo.hordeDunBaldar, rushInfo.allianceSouth, botIcebloodDistance,
             objectiveIcebloodRun ? 1 : 0, botIcebloodRun ? 1 : 0, botTowerRun ? 1 : 0,
             ibgyLeashBreak ? 1 : 0, targetSource, targetType, targetEntry, targetName, targetDistance,
             targetIcebloodDistance, targetTowerRun ? 1 : 0);
}

static uint32 AB_AttackObjectives[] = {
    BG_AB_NODE_STABLES,
    BG_AB_NODE_BLACKSMITH,
    BG_AB_NODE_FARM, BG_AB_NODE_LUMBER_MILL,
    BG_AB_NODE_GOLD_MINE
};

static std::tuple<uint32, uint32, uint32> EY_AttackObjectives[] = {
    {POINT_FEL_REAVER, BG_EY_OBJECT_FLAG_FEL_REAVER, AT_FEL_REAVER_POINT},
    {POINT_BLOOD_ELF, BG_EY_OBJECT_FLAG_BLOOD_ELF, AT_BLOOD_ELF_POINT},
    {POINT_DRAENEI_RUINS, BG_EY_OBJECT_FLAG_DRAENEI_RUINS, AT_DRAENEI_RUINS_POINT},
    {POINT_MAGE_TOWER, BG_EY_OBJECT_FLAG_MAGE_TOWER, AT_MAGE_TOWER_POINT}
};

static std::unordered_map<uint32, Position> EY_NodePositions = {
    {POINT_FEL_REAVER, Position(2044.173f, 1727.503f, 1189.505f)},
    {POINT_BLOOD_ELF, Position(2048.277f, 1395.093f, 1194.255f)},
    {POINT_DRAENEI_RUINS, Position(2286.245f, 1404.683f, 1196.991f)},
    {POINT_MAGE_TOWER, Position(2284.720f, 1728.457f, 1189.153f)}
};

static std::pair<uint32, uint32> IC_AttackObjectives[] = {
    {NODE_TYPE_WORKSHOP, BG_IC_GO_WORKSHOP_BANNER},
    {NODE_TYPE_DOCKS, BG_IC_GO_DOCKS_BANNER},
    {NODE_TYPE_HANGAR, BG_IC_GO_HANGAR_BANNER},
};

// useful commands for fixing BG bugs and checking waypoints/paths
bool BGTactics::HandleConsoleCommand(ChatHandler* handler, char const* args)
{
    if (!sPlayerbotAIConfig.enabled)
    {
        handler->PSendSysMessage("|cffff0000Playerbot system is currently disabled!");
        return true;
    }
    WorldSession* session = handler->GetSession();
    if (!session)
    {
        handler->PSendSysMessage("Command can only be used from an active session");
        return true;
    }
    std::string const commandOutput = HandleConsoleCommandPrivate(session, args);
    if (!commandOutput.empty())
        handler->PSendSysMessage(commandOutput.c_str());
    return true;
}

// has different name to above as some compilers get confused
std::string const BGTactics::HandleConsoleCommandPrivate(WorldSession* session, char const* args)
{
    Player* player = session->GetPlayer();
    if (!player)
        return "Error - session player not found";
    if (player->GetSession()->GetSecurity() < SEC_GAMEMASTER)
        return "Command can only be used by a GM";
    Battleground* bg = player->GetBattleground();
    if (!bg)
        return "Command can only be used within a battleground";
    BattlegroundTypeId bgType = bg->GetBgTypeID();
    if (bgType == BATTLEGROUND_RB)
        bgType = bg->GetBgTypeID(true);
    char* cmd = strtok((char*)args, " ");
    // char* charname = strtok(nullptr, " ");

    if (!strncmp(cmd, "showpath", 8))
    {
        int num = -1;
        if (!strncmp(cmd, "showpath=all", 12))
        {
            num = -2;
        }
        else if (!strncmp(cmd, "showpath=", 9))
        {
            if (sscanf(cmd, "showpath=%d", &num) == -1 || num < 0)
                return "Bad showpath parameter";
        }
        std::vector<BattleBotPath*> const* vPaths;
        switch (bgType)
        {
            case BATTLEGROUND_AB:
                vPaths = &vPaths_AB;
                break;
            case BATTLEGROUND_AV:
                vPaths = &vPaths_AV;
                break;
            case BATTLEGROUND_WS:
                vPaths = &vPaths_WS;
                break;
            case BATTLEGROUND_EY:
                vPaths = &vPaths_EY;
                break;
            case BATTLEGROUND_IC:
                vPaths = &vPaths_IC;
                break;
            default:
                vPaths = nullptr;
                break;
        }
        if (!vPaths)
            return "This battleground has no paths and is unsupported";
        if (num == -1)
        {
            float closestPoint = FLT_MAX;
            for (uint32 j = 0; j < vPaths->size(); j++)
            {
                auto const& path = (*vPaths)[j];
                for (uint32 i = 0; i < path->size(); i++)
                {
                    BattleBotWaypoint& waypoint = ((*path)[i]);
                    float dist = player->GetDistance(waypoint.x, waypoint.y, waypoint.z);
                    if (closestPoint > dist)
                    {
                        closestPoint = dist;
                        num = j;
                    }
                }
            }
        }
        uint32 min = 0u;
        uint32 max = vPaths->size() - 1;
        if (num >= 0)  // num specified or found
        {
            uint32 const selectedPath = static_cast<uint32>(num);
            if (selectedPath > max)
                return fmt::format("Path {} of range of 0 - {}", num, max);
            min = selectedPath;
            max = selectedPath;
        }
        for (uint32 j = min; j <= max; j++)
        {
            auto const& path = (*vPaths)[j];
            for (uint32 i = 0; i < path->size(); i++)
            {
                BattleBotWaypoint& waypoint = ((*path)[i]);
                Creature* wpCreature = player->SummonCreature(15631, waypoint.x, waypoint.y, waypoint.z, 0,
                                                              TEMPSUMMON_TIMED_DESPAWN, 15000u);
                wpCreature->SetOwnerGUID(player->GetGUID());
            }
        }
        if (num >= 0)
            return fmt::format("Showing path {}", num);
        return fmt::format("Showing paths 0 - {}", max);
    }

    if (!strncmp(cmd, "showcreature=", 13))
    {
        uint32 num;
        if (sscanf(cmd, "showcreature=%u", &num) == -1)
            return "Bad showcreature parameter";
        if (num >= bg->BgCreatures.size())
            return fmt::format("Creature out of range of 0 - {}", bg->BgCreatures.size() - 1);
        Creature* c = bg->GetBGCreature(num);
        if (!c)
            return "Creature not found";
        Creature* wpCreature = player->SummonCreature(15631, c->GetPositionX(), c->GetPositionY(), c->GetPositionZ(), 0,
                                                      TEMPSUMMON_TIMED_DESPAWN, 15000u);
        wpCreature->SetOwnerGUID(player->GetGUID());
        float distance = player->GetDistance(c);
        float exactDistance = player->GetExactDist(c);
        return fmt::format("Showing Creature {} location={:.3f},{:.3f},{:.3f} distance={} exactDistance={}",
            num, c->GetPositionX(), c->GetPositionY(), c->GetPositionZ(), distance, exactDistance);
    }

    if (!strncmp(cmd, "showobject=", 11))
    {
        uint32 num;
        if (sscanf(cmd, "showobject=%u", &num) == -1)
            return "Bad showobject parameter";
        if (num >= bg->BgObjects.size())
            return fmt::format("Object out of range of 0 - {}", bg->BgObjects.size() - 1);
        GameObject* o = bg->GetBGObject(num);
        if (!o)
            return "GameObject not found";
        Creature* wpCreature = player->SummonCreature(15631, o->GetPositionX(), o->GetPositionY(), o->GetPositionZ(), 0,
                                                      TEMPSUMMON_TIMED_DESPAWN, 15000u);
        wpCreature->SetOwnerGUID(player->GetGUID());
        float distance = player->GetDistance(o);
        float exactDistance = player->GetExactDist(o);
        return fmt::format("Showing GameObject {} location={:.3f},{:.3f},{:.3f} distance={} exactDistance={}",
            num, o->GetPositionX(), o->GetPositionY(), o->GetPositionZ(), distance, exactDistance);
    }

    return "usage: showpath(=[num]) / showcreature=[num] / showobject=[num]";
}

// Depends on OnBattlegroundStart in playerbots.cpp
uint8 BGTactics::GetBotStrategyForTeam(Battleground* bg, TeamId teamId)
{
    auto itr = bgStrategies.find(bg->GetInstanceID());
    if (itr == bgStrategies.end())
        return 0;

    return teamId == TEAM_ALLIANCE ? itr->second.allianceStrategy : itr->second.hordeStrategy;
}

bool BGTactics::wsJumpDown()
{
    Battleground* bg = bot->GetBattleground();
    if (!bg)
        return false;

    TeamId team = bot->GetTeamId();
    uint32 mapId = bg->GetMapId();

    if (team == TEAM_HORDE)
    {
        if (bot->GetDistance({1038.220f, 1420.197f, 340.099f}) < 4.0f)
        {
            MoveTo(mapId, 1029.242f, 1387.024f, 340.866f);
            return true;
        }
        if (bot->GetDistance({1029.242f, 1387.024f, 340.866f}) < 4.0f)
        {
            MoveTo(mapId, 1045.764f, 1389.831f, 340.825f);
            return true;
        }
        if (bot->GetDistance({1045.764f, 1389.831f, 340.825f}) < 4.0f)
        {
            MoveTo(mapId, 1057.076f, 1393.081f, 339.505f);
            return true;
        }
        if (bot->GetDistance({1057.076f, 1393.081f, 339.505f}) < 4.0f)
        {
            JumpTo(mapId, 1075.233f, 1398.645f, 323.669f);
            return true;
        }
        if (bot->GetDistance({1075.233f, 1398.645f, 323.669f}) < 4.0f)
        {
            MoveTo(mapId, 1096.590f, 1395.070f, 317.016f);
            return true;
        }
        if (bot->GetDistance({1096.590f, 1395.070f, 317.016f}) < 4.0f)
        {
            MoveTo(mapId, 1134.380f, 1370.130f, 312.741f);
            return true;
        }
    }
    else if (team == TEAM_ALLIANCE)
    {
        if (bot->GetDistance({1415.548f, 1554.538f, 343.164f}) < 4.0f)
        {
            MoveTo(mapId, 1407.234f, 1551.658f, 343.432f);
            return true;
        }
        if (bot->GetDistance({1407.234f, 1551.658f, 343.432f}) < 4.0f)
        {
            JumpTo(mapId, 1385.325f, 1544.592f, 322.047f);
            return true;
        }
        if (bot->GetDistance({1385.325f, 1544.592f, 322.047f}) < 4.0f)
        {
            MoveTo(mapId, 1370.710f, 1543.550f, 321.585f);
            return true;
        }
        if (bot->GetDistance({1370.710f, 1543.550f, 321.585f}) < 4.0f)
        {
            MoveTo(mapId, 1339.410f, 1533.420f, 313.336f);
            return true;
        }
    }

    return false;
}

bool BGTactics::eyJumpDown()
{
    Battleground* bg = bot->GetBattleground();
    if (!bg)
        return false;
    Position const hordeJumpPositions[] = {
        EY_WAITING_POS_HORDE,
        {1838.007f, 1539.856f, 1253.383f},
        {1846.264f, 1535.062f, 1240.796f},
        {1849.813f, 1527.303f, 1237.262f},
        {1849.041f, 1518.884f, 1223.624f},
    };
    Position const allianceJumpPositions[] = {
        EY_WAITING_POS_ALLIANCE,
        {2492.955f, 1597.769f, 1254.828f},
        {2484.601f, 1598.209f, 1244.344f},
        {2478.424f, 1609.539f, 1238.651f},
        {2475.926f, 1619.658f, 1218.706f},
    };
    Position const* positons = bot->GetTeamId() == TEAM_HORDE ? hordeJumpPositions : allianceJumpPositions;
    {
        if (bot->GetDistance(positons[0]) < 16.0f)
        {
            MoveTo(bg->GetMapId(), positons[1].GetPositionX(), positons[1].GetPositionY(), positons[1].GetPositionZ());
            return true;
        }
        if (bot->GetDistance(positons[1]) < 4.0f)
        {
            JumpTo(bg->GetMapId(), positons[2].GetPositionX(), positons[2].GetPositionY(), positons[2].GetPositionZ());
            return true;
        }
        if (bot->GetDistance(positons[2]) < 4.0f)
        {
            MoveTo(bg->GetMapId(), positons[3].GetPositionX(), positons[3].GetPositionY(), positons[3].GetPositionZ());
            return true;
        }
        if (bot->GetDistance(positons[3]) < 4.0f)
        {
            JumpTo(bg->GetMapId(), positons[4].GetPositionX(), positons[4].GetPositionY(), positons[4].GetPositionZ());
            return true;
        }
    }
    return false;
}

//
// actual bg tactics below
//
bool BGTactics::Execute(Event /*event*/)
{
    Battleground* bg = bot->GetBattleground();
    if (!bg)
    {
        botAI->ResetStrategies();
        return false;
    }

    if (bg->GetStatus() == STATUS_WAIT_LEAVE)
        return BGStatusAction::LeaveBG(botAI);

    if (bg->isArena())
    {
        // can't use this in arena - no vPaths/vFlagIds (will crash server)
        botAI->ResetStrategies();
        return false;
    }

    if (bg->GetStatus() == STATUS_IN_PROGRESS)
        botAI->ChangeStrategy("-buff", BOT_STATE_NON_COMBAT);

    std::vector<BattleBotPath*> const* vPaths;
    std::vector<uint32> const* vFlagIds;
    BattlegroundTypeId bgType = bg->GetBgTypeID();
    if (bgType == BATTLEGROUND_RB)
        bgType = bot->GetBattleground()->GetBgTypeID(true);

    switch (bgType)
    {
        case BATTLEGROUND_AB:
        {
            vPaths = &vPaths_AB;
            vFlagIds = &vFlagsAB;
            break;
        }
        case BATTLEGROUND_AV:
        {
            vPaths = &vPaths_AV;
            vFlagIds = &vFlagsAV;
            break;
        }
        case BATTLEGROUND_WS:
        {
            vPaths = &vPaths_WS;
            vFlagIds = &vFlagsWS;
            break;
        }
        case BATTLEGROUND_EY:
        {
            vPaths = &vPaths_EY;
            vFlagIds = &vFlagsEY;
            break;
        }
        case BATTLEGROUND_IC:
        {
            vPaths = &vPaths_IC;
            vFlagIds = &vFlagsIC;
            break;
        }
        default:
            // can't use this in this BG - no vPaths/vFlagIds (will crash server)
            botAI->ResetStrategies();
            return false;
    }

    if (getName() == "move to start")
        return moveToStart();

    if (getName() == "reset objective force")
    {
        bool isCarryingFlag =
            bot->HasAura(BG_WS_SPELL_WARSONG_FLAG) ||
            bot->HasAura(BG_WS_SPELL_SILVERWING_FLAG) ||
            bot->HasAura(BG_EY_NETHERSTORM_FLAG_SPELL);

        if (!isCarryingFlag)
        {
            bot->StopMoving();
            bot->GetMotionMaster()->Clear();
            return resetObjective();  // Reset objective to not use "old" data
        }
    }

    if (getName() == "select objective")
        return selectObjective();

    if (getName() == "protect fc")
    {
        if (protectFC())
            return true;
    }

    if (getName() == "move to objective")
    {
        if (bg->GetStatus() == STATUS_WAIT_JOIN)
            return false;

        bool ignoreIcebloodAssaultCombat = false;
        if (bgType == BATTLEGROUND_AV && bot->GetTeamId() == TEAM_ALLIANCE)
        {
            Unit* currentTarget = context->GetValue<Unit*>("current target")->Get();
            Unit* enemyPlayerTarget = AI_VALUE(Unit*, "enemy player target");
            if (AllianceAVShouldBreakIcebloodAssaultCombatLeash(bot, bg,
                                                                 currentTarget, enemyPlayerTarget))
            {
                context->GetValue<Unit*>("current target")->Set(nullptr);
                // Keep existing movement when we are only breaking an invalid leash combat state.
                // Hard-stop chase only if a concrete target was engaged.
                if (bot->GetVictim() || currentTarget || enemyPlayerTarget)
                {
                    bot->AttackStop();
                    bot->SetTarget(ObjectGuid::Empty);
                    bot->SetSelection(ObjectGuid());
                    botAI->ChangeEngine(BOT_STATE_NON_COMBAT);
                }
                ignoreIcebloodAssaultCombat = true;
            }
        }

        if (bot->isMoving() && !ignoreIcebloodAssaultCombat)
            return false;

        if (!bot->IsStopped() && !ignoreIcebloodAssaultCombat)
            return false;

        switch (bot->GetMotionMaster()->GetCurrentMovementGeneratorType())
        {
            // TODO: should ESCORT_MOTION_TYPE be here seeing as bots use it by default?
            case IDLE_MOTION_TYPE:
            case CHASE_MOTION_TYPE:
            case POINT_MOTION_TYPE:
                break;
            default:
                return true;
        }

        if (vFlagIds && atFlag(*vPaths, *vFlagIds))
            return true;

        if (useBuff())
            return true;

        // NOTE: can't use IsInCombat() when in vehicle as player is stuck in combat forever while in vehicle (ac bug?)
        bool inCombat = bot->GetVehicle() ? (bool)AI_VALUE(Unit*, "enemy player target") : bot->IsInCombat();
        if (inCombat && !PlayerHasFlag::IsCapturingFlag(bot) && !ignoreIcebloodAssaultCombat)
        {
            if (ai::pvp::ShouldPrioritizeBattlegroundCapture(botAI))
            {
                context->GetValue<Unit*>("current target")->Set(nullptr);
                bot->AttackStop();
                bot->SetTarget(ObjectGuid::Empty);
                bot->SetSelection(ObjectGuid());
                botAI->ChangeEngine(BOT_STATE_NON_COMBAT);
            }
            else
            {
                // bot->GetMotionMaster()->MovementExpired();
                return false;
            }
        }

        if (!moveToObjective(false))
        {
            if (selectObjectiveWp(*vPaths))
                return true;

            bool const moved = moveToObjective(true);
            if (!moved && bg->GetBgTypeID() == BATTLEGROUND_AV && bot->GetTeamId() == TEAM_ALLIANCE)
            {
                uint8 const role = context->GetValue<uint32>("bg role")->Get();
                PositionInfo const objectivePos = context->GetValue<PositionMap&>("position")->Get()["bg objective"];
                LogAllianceAVMoveDebug(bot, bg, objectivePos, "move_false_wp_false_force_false", role, 255,
                                       false, AI_VALUE(Unit*, "enemy player target"));
            }
            return moved;
        }

        if (bg->GetBgTypeID() == BATTLEGROUND_AV && bot->GetTeamId() == TEAM_ALLIANCE)
            return true;

        // bot with flag should only move to objective
        if (bot->HasAura(BG_WS_SPELL_WARSONG_FLAG) || bot->HasAura(BG_WS_SPELL_SILVERWING_FLAG) ||
            bot->HasAura(BG_EY_NETHERSTORM_FLAG_SPELL))
            return false;

        if (!startNewPathBegin(*vPaths))
        {
            bool const moved = moveToObjective(true);
            if (!moved && bg->GetBgTypeID() == BATTLEGROUND_AV && bot->GetTeamId() == TEAM_ALLIANCE)
            {
                uint8 const role = context->GetValue<uint32>("bg role")->Get();
                PositionInfo const objectivePos = context->GetValue<PositionMap&>("position")->Get()["bg objective"];
                LogAllianceAVMoveDebug(bot, bg, objectivePos, "path_begin_false_force_false", role, 255, false,
                                       AI_VALUE(Unit*, "enemy player target"));
            }
            return moved;
        }

        if (!startNewPathFree(*vPaths))
        {
            bool const moved = moveToObjective(true);
            if (!moved && bg->GetBgTypeID() == BATTLEGROUND_AV && bot->GetTeamId() == TEAM_ALLIANCE)
            {
                uint8 const role = context->GetValue<uint32>("bg role")->Get();
                PositionInfo const objectivePos = context->GetValue<PositionMap&>("position")->Get()["bg objective"];
                LogAllianceAVMoveDebug(bot, bg, objectivePos, "path_free_false_force_false", role, 255, false,
                                       AI_VALUE(Unit*, "enemy player target"));
            }
            return moved;
        }
    }

    if (getName() == "use buff")
        return useBuff();

    if (getName() == "check flag")
    {
        if (vFlagIds && atFlag(*vPaths, *vFlagIds))
            return true;
    }

    if (getName() == "check objective")
        return resetObjective();

    return false;
}

bool BGTactics::moveToStart(bool force)
{
    Battleground* bg = bot->GetBattleground();
    if (!bg)
        return false;

    if (!force && bg->GetStatus() != STATUS_WAIT_JOIN)
        return false;

    BattlegroundTypeId bgType = bg->GetBgTypeID();
    if (bgType == BATTLEGROUND_RB)
        bgType = bg->GetBgTypeID(true);

    if (bgType == BATTLEGROUND_WS)
    {
        uint32 role = context->GetValue<uint32>("bg role")->Get();

        int startSpot = role < 4 ? BB_WSG_WAIT_SPOT_LEFT : role > 6 ? BB_WSG_WAIT_SPOT_RIGHT : BB_WSG_WAIT_SPOT_SPAWN;
        if (startSpot == BB_WSG_WAIT_SPOT_RIGHT)
        {
            if (bot->GetTeamId() == TEAM_HORDE)
                MoveTo(bg->GetMapId(), WS_WAITING_POS_HORDE_1.GetPositionX() + frand(-4.0f, 4.0f),
                       WS_WAITING_POS_HORDE_1.GetPositionY() + frand(-4.0f, 4.0f),
                       WS_WAITING_POS_HORDE_1.GetPositionZ());
            else
                MoveTo(bg->GetMapId(), WS_WAITING_POS_ALLIANCE_1.GetPositionX() + frand(-4.0f, 4.0f),
                       WS_WAITING_POS_ALLIANCE_1.GetPositionY() + frand(-4.0f, 4.0f),
                       WS_WAITING_POS_ALLIANCE_1.GetPositionZ());
        }
        else if (startSpot == BB_WSG_WAIT_SPOT_LEFT)
        {
            if (bot->GetTeamId() == TEAM_HORDE)
                MoveTo(bg->GetMapId(), WS_WAITING_POS_HORDE_2.GetPositionX() + frand(-4.0f, 4.0f),
                       WS_WAITING_POS_HORDE_2.GetPositionY() + frand(-4.0f, 4.0f),
                       WS_WAITING_POS_HORDE_2.GetPositionZ());
            else
                MoveTo(bg->GetMapId(), WS_WAITING_POS_ALLIANCE_2.GetPositionX() + frand(-4.0f, 4.0f),
                       WS_WAITING_POS_ALLIANCE_2.GetPositionY() + frand(-4.0f, 4.0f),
                       WS_WAITING_POS_ALLIANCE_2.GetPositionZ());
        }
        else  // BB_WSG_WAIT_SPOT_SPAWN
        {
            if (bot->GetTeamId() == TEAM_HORDE)
                MoveTo(bg->GetMapId(), WS_WAITING_POS_HORDE_3.GetPositionX() + frand(-10.0f, 10.0f),
                       WS_WAITING_POS_HORDE_3.GetPositionY() + frand(-10.0f, 10.0f),
                       WS_WAITING_POS_HORDE_3.GetPositionZ());
            else
                MoveTo(bg->GetMapId(), WS_WAITING_POS_ALLIANCE_3.GetPositionX() + frand(-10.0f, 10.0f),
                       WS_WAITING_POS_ALLIANCE_3.GetPositionY() + frand(-10.0f, 10.0f),
                       WS_WAITING_POS_ALLIANCE_3.GetPositionZ());
        }
    }
    else if (bgType == BATTLEGROUND_AB)
    {
        if (bot->GetTeamId() == TEAM_HORDE)
            MoveTo(bg->GetMapId(), AB_WAITING_POS_HORDE.GetPositionX() + frand(-7.0f, 7.0f),
                   AB_WAITING_POS_HORDE.GetPositionY() + frand(-2.0f, 2.0f), AB_WAITING_POS_HORDE.GetPositionZ());
        else
            MoveTo(bg->GetMapId(), AB_WAITING_POS_ALLIANCE.GetPositionX() + frand(-7.0f, 7.0f),
                   AB_WAITING_POS_ALLIANCE.GetPositionY() + frand(-2.0f, 2.0f), AB_WAITING_POS_ALLIANCE.GetPositionZ());
    }
    else if (bgType == BATTLEGROUND_AV)
    {
        if (bot->GetTeamId() == TEAM_HORDE)
            MoveTo(bg->GetMapId(), AV_WAITING_POS_HORDE.GetPositionX() + frand(-5.0f, 5.0f),
                   AV_WAITING_POS_HORDE.GetPositionY() + frand(-3.0f, 3.0f), AV_WAITING_POS_HORDE.GetPositionZ());
        else
            MoveTo(bg->GetMapId(), AV_WAITING_POS_ALLIANCE.GetPositionX() + frand(-5.0f, 5.0f),
                   AV_WAITING_POS_ALLIANCE.GetPositionY() + frand(-3.0f, 3.0f), AV_WAITING_POS_ALLIANCE.GetPositionZ());
    }
    else if (bgType == BATTLEGROUND_EY)
    {
        /* Disabled: Not needed here and sometimes the bots can go out of map (at least with my map files)
        if (bot->GetTeamId() == TEAM_HORDE)
        {
            MoveTo(bg->GetMapId(), EY_WAITING_POS_HORDE.GetPositionX(), EY_WAITING_POS_HORDE.GetPositionY(), EY_WAITING_POS_HORDE.GetPositionZ());
        }
        else
        {
            MoveTo(bg->GetMapId(), EY_WAITING_POS_ALLIANCE.GetPositionX(), EY_WAITING_POS_ALLIANCE.GetPositionZ(), EY_WAITING_POS_ALLIANCE.GetPositionZ());
        }
        */
    }
    else if (bgType == BATTLEGROUND_IC)
    {
        uint32 role = context->GetValue<uint32>("bg role")->Get();

        if (bot->GetTeamId() == TEAM_HORDE)
        {
            if (role == 9)  // refinery
                MoveTo(bg->GetMapId(), IC_WEST_WAITING_POS_HORDE.GetPositionX() + frand(-5.0f, 5.0f),
                       IC_WEST_WAITING_POS_HORDE.GetPositionY() + frand(-5.0f, 5.0f),
                       IC_WEST_WAITING_POS_HORDE.GetPositionZ());
            else if (role >= 3 && role < 6)  // hanger
                MoveTo(bg->GetMapId(), IC_EAST_WAITING_POS_HORDE.GetPositionX() + frand(-5.0f, 5.0f),
                       IC_EAST_WAITING_POS_HORDE.GetPositionY() + frand(-5.0f, 5.0f),
                       IC_EAST_WAITING_POS_HORDE.GetPositionZ());
            else  // everything else
                MoveTo(bg->GetMapId(), IC_WAITING_POS_HORDE.GetPositionX() + frand(-5.0f, 5.0f),
                       IC_WAITING_POS_HORDE.GetPositionY() + frand(-5.0f, 5.0f), IC_WAITING_POS_HORDE.GetPositionZ());
        }
        else
        {
            if (role < 3)  // docks
                MoveTo(
                    bg->GetMapId(), IC_WAITING_POS_ALLIANCE.GetPositionX() + frand(-5.0f, 5.0f),
                    IC_WAITING_POS_ALLIANCE.GetPositionY() + frand(-5.0f, 5.0f),
                    IC_WAITING_POS_ALLIANCE.GetPositionZ());  // dont bother using west, there's no paths to use anyway
            else if (role == 9 || (role >= 3 && role < 6))    // quarry and hanger
                MoveTo(bg->GetMapId(), IC_EAST_WAITING_POS_ALLIANCE.GetPositionX() + frand(-5.0f, 5.0f),
                       IC_EAST_WAITING_POS_ALLIANCE.GetPositionY() + frand(-5.0f, 5.0f),
                       IC_EAST_WAITING_POS_ALLIANCE.GetPositionZ());
            else  // everything else
                MoveTo(bg->GetMapId(), IC_WAITING_POS_ALLIANCE.GetPositionX() + frand(-5.0f, 5.0f),
                       IC_WAITING_POS_ALLIANCE.GetPositionY() + frand(-5.0f, 5.0f),
                       IC_WAITING_POS_ALLIANCE.GetPositionZ());
        }
    }

    return true;
}

bool BGTactics::selectObjective(bool reset)
{
    Battleground* bg = bot->GetBattleground();
    if (!bg)
        return false;

    if (bg->GetStatus() != STATUS_IN_PROGRESS)
        return false;

    PositionMap& posMap = context->GetValue<PositionMap&>("position")->Get();
    PositionInfo pos = context->GetValue<PositionMap&>("position")->Get()["bg objective"];
    if (pos.isSet() && !reset)
        return false;

    WorldObject* BgObjective = nullptr;
    char const* objectiveReason = "none";

    BattlegroundTypeId bgType = bg->GetBgTypeID();
    if (bgType == BATTLEGROUND_RB)
        bgType = bg->GetBgTypeID(true);
    switch (bgType)
    {
        case BATTLEGROUND_AV:
        {
            BattlegroundAV* av = static_cast<BattlegroundAV*>(bg);
            TeamId team = bot->GetTeamId();
            uint8 role = context->GetValue<uint32>("bg role")->Get();
            AVBotStrategy strategyHorde = static_cast<AVBotStrategy>(GetBotStrategyForTeam(bg, TEAM_HORDE));
            AVBotStrategy strategyAlliance = static_cast<AVBotStrategy>(GetBotStrategyForTeam(bg, TEAM_ALLIANCE));
            AVBotStrategy strategy = (team == TEAM_ALLIANCE) ? strategyAlliance : strategyHorde;
            AVBotStrategy enemyStrategy = (team == TEAM_ALLIANCE) ? strategyHorde : strategyAlliance;
            AllianceAVRushInfo allianceRushInfo = GetAllianceAVRushInfo(bg, strategyHorde);
            AllianceAVThreatLevel allianceThreat = AV_THREAT_NONE;
            AllianceAVBattlefieldMode allianceMode = AV_MODE_OPENING_CONTROL;
            AllianceAVCachedTactics cachedAllianceTactics;
            bool const hasCachedAllianceTactics = team == TEAM_ALLIANCE &&
                TryGetAsyncAllianceAVTactics(bg, av, cachedAllianceTactics);
            AllianceAVObjectiveAssignment cachedAllianceAssignment = AV_ASSIGN_NONE;
            bool const hasCachedAllianceAssignment = team == TEAM_ALLIANCE &&
                TryGetAsyncAllianceAVObjectiveAssignment(bg, bot, cachedAllianceAssignment);
            if (hasCachedAllianceTactics)
            {
                allianceRushInfo = cachedAllianceTactics.rushInfo;
                allianceThreat = cachedAllianceTactics.threat;
                allianceMode = cachedAllianceTactics.mode;
            }
            else
            {
                allianceThreat = GetAllianceAVThreatLevel(av, allianceRushInfo);
                allianceMode = GetAllianceAVBattlefieldMode(bg, av, allianceRushInfo, allianceThreat);
            }

            bool enableMineCapture = true;
            bool enableSnowfall = true;

            switch (strategy)
            {
                case AV_STRATEGY_BALANCED:
                    break;
                case AV_STRATEGY_OFFENSIVE:
                    enableMineCapture = false;
                    break;
                case AV_STRATEGY_DEFENSIVE:
                    enableSnowfall = false;
                    break;
                case AV_STRATEGY_ALLIANCE_CONTROL_TEMPO:
                    enableMineCapture = false;
                    break;
                default:
                    break;
            }

            auto const& attackObjectives =
                (team == TEAM_HORDE) ? AV_AttackObjectives_Horde : AV_AttackObjectives_Alliance;
            auto const& defendObjectives =
                (team == TEAM_HORDE) ? AV_DefendObjectives_Horde : AV_DefendObjectives_Alliance;

            uint8 defendersProhab = hasCachedAllianceTactics ?
                cachedAllianceTactics.defenderLimit : GetAVDefenderRoleLimit(team, strategy, enemyStrategy, av);
            if (team == TEAM_ALLIANCE && !hasCachedAllianceTactics)
                defendersProhab = std::max<uint8>(defendersProhab,
                                                  GetAllianceAVRushDefenderLimit(av, allianceRushInfo, strategyAlliance,
                                                                                 strategyHorde));
            uint32 const allianceHordeTowersDown = hasCachedAllianceTactics ?
                cachedAllianceTactics.towersDown : (team == TEAM_ALLIANCE ? GetAllianceDestroyedHordeTowerCount(av) : 0);
            bool const allianceFinalDrekPush = team == TEAM_ALLIANCE && strategy == AV_STRATEGY_ALLIANCE_CONTROL_TEMPO &&
                (hasCachedAllianceTactics ? cachedAllianceTactics.finalDrekWindow :
                    AllianceHasFinalDrekWindow(av, allianceRushInfo, allianceHordeTowersDown));
            bool const allianceNorthEmergency = team == TEAM_ALLIANCE &&
                (hasCachedAllianceTactics ? cachedAllianceTactics.fullRecall :
                    AllianceAVShouldFullRecallNorth(av, allianceThreat));
            if (!hasCachedAllianceTactics && allianceFinalDrekPush && !allianceNorthEmergency)
            {
                uint8 const finalDefenseCap = AllianceDunBaldarBaseActuallyLost(av) ? 7 : 6;
                defendersProhab = std::min<uint8>(defendersProhab, finalDefenseCap);
            }
            else if (!hasCachedAllianceTactics && team == TEAM_ALLIANCE &&
                     AllianceAVCanReleaseIdleNorthReserve(av, allianceRushInfo, allianceThreat, allianceMode))
            {
                defendersProhab = std::min<uint8>(
                    defendersProhab,
                    GetAllianceAVReleasedDefenderLimit(av, allianceRushInfo, allianceThreat, allianceMode));
            }

            bool isDefender = role < defendersProhab;
            bool isAdvanced = !isDefender && role > 8;

            uint32 destroyedNodes = 0;
            for (auto const& [nodeId, _] : defendObjectives)
                if (av->GetAVNodeInfo(nodeId).State == POINT_DESTROYED)
                    destroyedNodes++;

            float botX = bot->GetPositionX();
            PositionInfo const botPos(botX, bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId());
            bool const allianceSnowfallForward = team == TEAM_ALLIANCE &&
                (AllianceAVPositionIsSnowfallRespawn(botPos) || AllianceAVPositionIsSnowfallRun(bg, botPos));
            bool const allianceSnowfallIcebloodForward = allianceSnowfallForward && !allianceNorthEmergency &&
                strategy == AV_STRATEGY_ALLIANCE_CONTROL_TEMPO &&
                (allianceMode == AV_MODE_IBGY_PUSH || allianceMode == AV_MODE_IBGY_GUARD ||
                 allianceMode == AV_MODE_IBGY_BREAKTHROUGH ||
                 AllianceAVShouldUseIcebloodBeachheadPlan(bg, av, allianceThreat, strategyHorde));
            if (isDefender)
            {
                if ((team == TEAM_HORDE && botX >= -62.0f) ||
                    (team == TEAM_ALLIANCE && (botX <= -462.0f || allianceSnowfallIcebloodForward)))
                    isDefender = false;
            }

            if (isDefender && destroyedNodes > 0)
            {
                if (team != TEAM_ALLIANCE)
                {
                    uint32 switchChance = 20 + (destroyedNodes * 15);
                    if (urand(0, 99) < switchChance)
                        isDefender = false;
                }
            }

            if (team == TEAM_ALLIANCE)
                LogAllianceAVState(bg, av, allianceRushInfo, allianceThreat, allianceMode, defendersProhab,
                                   strategyAlliance, strategyHorde);

            auto selectAllianceRecapObjective = [&](float maxDistance) -> GameObject*
            {
                if (team != TEAM_ALLIANCE)
                    return nullptr;

                std::vector<GameObject*> recapObjectives;
                for (auto const& [nodeId, goId] : AV_AllianceGraveyardRecapObjectives)
                {
                    BG_AV_NodeInfo const& node = av->GetAVNodeInfo(nodeId);
                    if (node.State != POINT_ASSAULTED || node.OwnerId != TEAM_HORDE || node.PrevOwnerId != TEAM_ALLIANCE)
                        continue;

                    if (!AllianceHasAnySouthRespawn(av) && IsAllianceForwardGraveyardAttackTarget(nodeId))
                        continue;

                    if (nodeId == BG_AV_NODES_SNOWFALL_GRAVE &&
                        !AllianceShouldTakeSnowfall(av, allianceRushInfo, allianceThreat, allianceMode, role))
                        continue;

                    if (allianceThreat >= AV_THREAT_HIGH && AllianceHasDunBaldarCorePressure(av) &&
                        !IsAllianceDunBaldarCoreDefense(nodeId))
                        continue;

                    GameObject* go = bg->GetBGObject(goId);
                    if (!go || !go->isSpawned())
                        continue;

                    float const distance = bot->GetDistance(go);
                    bool const northernDefender = isDefender && botX > -180.0f && !allianceSnowfallForward;
                    bool const forwardRecap = nodeId == BG_AV_NODES_ICEBLOOD_GRAVE ||
                                              IsAllianceForwardGraveyardAttackTarget(nodeId);
                    if (northernDefender && allianceRushInfo.IsActive() && forwardRecap && distance > 220.0f)
                        continue;

                    if (maxDistance > 0.0f && distance > maxDistance)
                        continue;

                    recapObjectives.push_back(go);
                }

                if (recapObjectives.empty())
                    return nullptr;

                return recapObjectives[urand(0, recapObjectives.size() - 1)];
            };

            auto logCachedAssignmentPosition = [&]()
            {
                LOG_DEBUG("playerbots",
                          "AV objective bot={} role={} strategy={} enemyStrategy={} reason={} node={} state={} owner={} prevOwner={} timer={} distance={:.1f}",
                          bot->GetName(), static_cast<uint32>(role), static_cast<uint32>(strategy),
                          static_cast<uint32>(enemyStrategy), objectiveReason, 255, 255, 255, 255, 0,
                          ServerFacade::instance().GetDistance2d(bot, pos.x, pos.y));
            };

            auto selectCachedAllianceAssignment = [&]() -> bool
            {
                if (!hasCachedAllianceAssignment || team != TEAM_ALLIANCE ||
                    strategy != AV_STRATEGY_ALLIANCE_CONTROL_TEMPO)
                    return false;

                switch (cachedAllianceAssignment)
                {
                    case AV_ASSIGN_NORTH_DEFENSE:
                    {
                        if ((hasCachedAllianceTactics ? cachedAllianceTactics.shouldScreenIdleNorthReserve :
                             AllianceAVShouldScreenIdleNorthReserve(av, allianceRushInfo, allianceThreat, allianceMode)) &&
                            SetAllianceNorthReservePosition(bot, posMap, pos, role, objectiveReason))
                        {
                            logCachedAssignmentPosition();
                            return true;
                        }

                        BgObjective = SelectAllianceAVDefenderObjective(bot, bg, av, allianceRushInfo);
                        if (BgObjective)
                            objectiveReason = "async assignment north defense";
                        break;
                    }
                    case AV_ASSIGN_ICEBLOOD_HOLD:
                    {
                        if (Player* enemy = SelectAllianceIcebloodBeachheadEnemy(bot, bg, av, allianceThreat, strategyHorde))
                        {
                            BgObjective = enemy;
                            objectiveReason = "async assignment iceblood enemy";
                            break;
                        }

                        if (SetAllianceIcebloodBeachheadPosition(bot, bg, av, posMap, pos, role, defendersProhab,
                                                                 objectiveReason))
                        {
                            logCachedAssignmentPosition();
                            return true;
                        }

                        BgObjective = SelectAllianceIcebloodGraveyardHoldObjective(bg);
                        if (BgObjective)
                            objectiveReason = "async assignment iceblood hold";
                        break;
                    }
                    case AV_ASSIGN_ICEBLOOD_ATTACK:
                    {
                        BgObjective = SelectAllianceIcebloodGraveyardObjective(bg, av);
                        if (BgObjective)
                            objectiveReason = "async assignment iceblood attack";
                        break;
                    }
                    case AV_ASSIGN_SNOWFALL:
                    {
                        BgObjective = SelectAllianceSnowfallObjective(bg, av);
                        if (BgObjective)
                            objectiveReason = "async assignment snowfall";
                        break;
                    }
                    case AV_ASSIGN_GALVANGAR:
                    {
                        BgObjective = GetAllianceHordeCaptain(bg, av);
                        if (BgObjective)
                            objectiveReason = "async assignment galvangar";
                        break;
                    }
                    case AV_ASSIGN_FROSTWOLF_LOCK:
                    {
                        BgObjective = SelectAllianceFrostwolfLockObjective(bot, bg, av, role, allianceMode,
                                                                           objectiveReason);
                        break;
                    }
                    case AV_ASSIGN_DREK_PUSH:
                    {
                        uint32 const defensivePressure = hasCachedAllianceTactics ?
                            cachedAllianceTactics.defensivePressure : GetAllianceDefensivePressure(av);
                        BgObjective = SelectAllianceDrekPushObjective(bot, bg, av, allianceThreat, allianceMode,
                                                                      allianceRushInfo, allianceHordeTowersDown,
                                                                      defensivePressure);
                        if (BgObjective)
                            objectiveReason = "async assignment drek";
                        else if (SetAllianceDrekPushRallyPosition(bot, posMap, pos, objectiveReason))
                        {
                            logCachedAssignmentPosition();
                            return true;
                        }
                        break;
                    }
                    case AV_ASSIGN_ASSAULT_GUARD:
                    {
                        BgObjective = SelectAllianceAssaultGuardObjective(bot, bg, av, role, strategy);
                        if (BgObjective)
                            objectiveReason = "async assignment assault guard";
                        break;
                    }
                    case AV_ASSIGN_NONE:
                    default:
                        break;
                }

                return BgObjective != nullptr;
            };

            if (!BgObjective && allianceFinalDrekPush && !allianceNorthEmergency && !isDefender)
            {
                uint32 const defensivePressure = hasCachedAllianceTactics ?
                    cachedAllianceTactics.defensivePressure : GetAllianceDefensivePressure(av);
                BgObjective = SelectAllianceDrekPushObjective(bot, bg, av, allianceThreat, allianceMode,
                                                              allianceRushInfo, allianceHordeTowersDown,
                                                              defensivePressure);
                if (BgObjective)
                    objectiveReason = "alliance committed drek";
                else if (SetAllianceDrekPushRallyPosition(bot, posMap, pos, objectiveReason))
                {
                    LOG_DEBUG("playerbots",
                              "AV objective bot={} role={} strategy={} enemyStrategy={} reason={} towersDown={} Asouth={} threat={} defenders={}",
                              bot->GetName(), static_cast<uint32>(role), static_cast<uint32>(strategy),
                              static_cast<uint32>(enemyStrategy), objectiveReason, allianceHordeTowersDown,
                              allianceRushInfo.allianceSouth, GetAllianceAVThreatLevelName(allianceThreat),
                              static_cast<uint32>(defendersProhab));
                    return true;
                }
            }

            if (!BgObjective)
            {
                BgObjective = SelectAllianceAVEmergencyDefenseObjective(bot, bg, av, role, isDefender, strategy,
                                                                        defendersProhab, allianceRushInfo,
                                                                        allianceThreat);
                if (BgObjective)
                    objectiveReason = "alliance emergency defense";
            }

            if (!BgObjective)
            {
                BgObjective = selectAllianceRecapObjective(isDefender ? 0.0f : 160.0f);
                if (BgObjective)
                    objectiveReason = "alliance graveyard recap";
            }

            if (!BgObjective && selectCachedAllianceAssignment())
            {
                if (!BgObjective)
                    return true;
            }

            if (!BgObjective && team == TEAM_ALLIANCE)
            {
                if (Player* enemy = SelectAllianceNorthRushEnemy(bot, bg, allianceRushInfo, allianceThreat, role,
                                                                 isDefender, defendersProhab))
                {
                    BgObjective = enemy;
                    objectiveReason = "alliance north rush enemy";
                }
            }

            if (!BgObjective && team == TEAM_ALLIANCE && !isDefender &&
                AllianceAVShouldUseIcebloodBeachheadPlan(bg, av, allianceThreat, strategyHorde))
            {
                bool const canCommitToIceblood = AllianceAVCanCommitToIcebloodBeachhead(bot, role, isDefender,
                                                                                        defendersProhab,
                                                                                        allianceRushInfo,
                                                                                        allianceThreat);
                if (!canCommitToIceblood && bot->GetPositionX() > -180.0f)
                {
                    if ((hasCachedAllianceTactics ? cachedAllianceTactics.shouldScreenIdleNorthReserve :
                         AllianceAVShouldScreenIdleNorthReserve(av, allianceRushInfo, allianceThreat, allianceMode)) &&
                        SetAllianceNorthReservePosition(bot, posMap, pos, role, objectiveReason))
                    {
                        LOG_DEBUG("playerbots",
                                  "AV objective bot={} role={} strategy={} enemyStrategy={} reason={} node={} state={} owner={} prevOwner={} timer={} distance={:.1f}",
                                  bot->GetName(), static_cast<uint32>(role), static_cast<uint32>(strategy),
                                  static_cast<uint32>(enemyStrategy), objectiveReason, 255, 255, 255, 255, 0,
                                  ServerFacade::instance().GetDistance2d(bot, pos.x, pos.y));
                        return true;
                    }

                    BgObjective = SelectAllianceAVDefenderObjective(bot, bg, av, allianceRushInfo);
                    if (BgObjective)
                        objectiveReason = "alliance north reserve hold";
                    else if (SetAllianceNorthReservePosition(bot, posMap, pos, role, objectiveReason))
                    {
                        LOG_DEBUG("playerbots",
                                  "AV objective bot={} role={} strategy={} enemyStrategy={} reason={} node={} state={} owner={} prevOwner={} timer={} distance={:.1f}",
                                  bot->GetName(), static_cast<uint32>(role), static_cast<uint32>(strategy),
                                  static_cast<uint32>(enemyStrategy), objectiveReason, 255, 255, 255, 255, 0,
                                  ServerFacade::instance().GetDistance2d(bot, pos.x, pos.y));
                        return true;
                    }
                }
                else if (canCommitToIceblood)
                {
                    if (Player* enemy = SelectAllianceIcebloodBeachheadEnemy(bot, bg, av, allianceThreat, strategyHorde))
                    {
                        BgObjective = enemy;
                        objectiveReason = "alliance iceblood lockdown enemy";
                    }
                    else if (SetAllianceIcebloodBeachheadPosition(bot, bg, av, posMap, pos, role, defendersProhab,
                                                                  objectiveReason))
                    {
                        LOG_DEBUG("playerbots",
                                  "AV objective bot={} role={} strategy={} enemyStrategy={} reason={} node={} state={} owner={} prevOwner={} timer={} distance={:.1f}",
                                  bot->GetName(), static_cast<uint32>(role), static_cast<uint32>(strategy),
                                  static_cast<uint32>(enemyStrategy), objectiveReason,
                                  static_cast<uint32>(BG_AV_NODES_ICEBLOOD_GRAVE),
                                  static_cast<uint32>(av->GetAVNodeInfo(BG_AV_NODES_ICEBLOOD_GRAVE).State),
                                  static_cast<uint32>(av->GetAVNodeInfo(BG_AV_NODES_ICEBLOOD_GRAVE).OwnerId),
                                  static_cast<uint32>(av->GetAVNodeInfo(BG_AV_NODES_ICEBLOOD_GRAVE).PrevOwnerId),
                                  av->GetAVNodeInfo(BG_AV_NODES_ICEBLOOD_GRAVE).Timer,
                                  ServerFacade::instance().GetDistance2d(bot, pos.x, pos.y));
                        return true;
                    }
                }
            }

            if (!BgObjective && team == TEAM_ALLIANCE && !isDefender &&
                strategy == AV_STRATEGY_ALLIANCE_CONTROL_TEMPO)
            {
                BgObjective = SelectAllianceControlTempoObjective(bot, bg, av, role, allianceRushInfo,
                                                                  allianceThreat, allianceMode, objectiveReason);
            }

            if (!BgObjective && team == TEAM_ALLIANCE && !isDefender &&
                strategy != AV_STRATEGY_ALLIANCE_CONTROL_TEMPO)
            {
                BgObjective = SelectAllianceIcebloodGraveyardObjective(bg, av);
                if (BgObjective)
                    objectiveReason = "alliance iceblood graveyard first";
            }

            // --- Mine Capture (rarely works, needs some improvement) ---
            if (!BgObjective && enableMineCapture && team != TEAM_ALLIANCE && role == 0)
            {
                BG_AV_OTHER_VALUES mineType = (team == TEAM_HORDE) ? AV_SOUTH_MINE : AV_NORTH_MINE;
                if (av->GetMineOwner(mineType) != team)
                {
                    uint32 bossEntry = (team == TEAM_HORDE) ? AV_CPLACE_MINE_S_3 : AV_CPLACE_MINE_N_3;
                    Creature* mBossNeutral = bg->GetBGCreature(bossEntry);
                    const Position* minePositions[] = {(team == TEAM_HORDE) ? &AV_MINE_SOUTH_1 : &AV_MINE_NORTH_1,
                                                       (team == TEAM_HORDE) ? &AV_MINE_SOUTH_2 : &AV_MINE_NORTH_2,
                                                       (team == TEAM_HORDE) ? &AV_MINE_SOUTH_3 : &AV_MINE_NORTH_3};

                    const Position* chosen = minePositions[urand(0, 2)];
                    pos.Set(chosen->GetPositionX(), chosen->GetPositionY(), chosen->GetPositionZ(), bot->GetMapId());
                    posMap["bg objective"] = pos;
                    BgObjective = mBossNeutral;
                    objectiveReason = "mine capture";
                }
            }

            // --- Nearby Enemy ---
            if (!BgObjective && urand(0, 99) < 8)
            {
                if (Unit* enemy = AI_VALUE(Unit*, "enemy player target"))
                {
                    bool const canChaseEnemy = team != TEAM_ALLIANCE || !isDefender || AllianceAVEnemyIsHomeThreat(enemy);
                    if (canChaseEnemy && bot->GetDistance(enemy) < 500.0f)
                    {
                        pos.Set(enemy->GetPositionX(), enemy->GetPositionY(), enemy->GetPositionZ(), bot->GetMapId());
                        posMap["bg objective"] = pos;
                        BgObjective = enemy;
                        objectiveReason = "nearby enemy";
                    }
                }
            }

            // --- Snowfall ---
            bool hasSnowfallRole = enableSnowfall &&
                                    ((team == TEAM_ALLIANCE && !isDefender &&
                                      ((strategy == AV_STRATEGY_ALLIANCE_CONTROL_TEMPO &&
                                        AllianceShouldTakeSnowfall(av, allianceRushInfo, allianceThreat, allianceMode, role)) ||
                                       (strategy != AV_STRATEGY_ALLIANCE_CONTROL_TEMPO && !allianceRushInfo.IsActive() &&
                                        role >= 7 && !AllianceHasSnowfallBlocker(av)))) ||
                                     (team == TEAM_HORDE && role < 5));

            if (!BgObjective && hasSnowfallRole)
            {
                const BG_AV_NodeInfo& snowfallNode = av->GetAVNodeInfo(BG_AV_NODES_SNOWFALL_GRAVE);
                if (snowfallNode.OwnerId == TEAM_NEUTRAL)
                {
                    if (GameObject* go = bg->GetBGObject(BG_AV_OBJECT_FLAG_N_SNOWFALL_GRAVE))
                    {
                        Position objPos = go->GetPosition();
                        float rx, ry, rz;
                        bot->GetRandomPoint(objPos, frand(2.0f, 4.0f), rx, ry, rz);
                        ResolveBattleGroundGroundZ(bot, rx, ry, rz);

                        pos.Set(rx, ry, rz, go->GetMapId());
                        posMap["bg objective"] = pos;
                        BgObjective = go;
                        objectiveReason = "snowfall graveyard";
                    }
                }
            }

            if (!BgObjective && team == TEAM_ALLIANCE && !isDefender)
            {
                BgObjective = SelectAllianceAssaultGuardObjective(bot, bg, av, role, strategy);
                if (BgObjective)
                    objectiveReason = "alliance assaulted objective guard";
            }

            // --- Captain ---
            uint32 const captainChance = team == TEAM_ALLIANCE ? 60 : 90;
            if (!BgObjective && !(team == TEAM_ALLIANCE && strategy == AV_STRATEGY_ALLIANCE_CONTROL_TEMPO) &&
                (team != TEAM_ALLIANCE || !isDefender) && urand(0, 99) < captainChance)
            {
                if (av->IsCaptainAlive(team == TEAM_HORDE ? TEAM_ALLIANCE : TEAM_HORDE))
                {
                    uint32 creatureId = (team == TEAM_HORDE) ? AV_CREATURE_A_CAPTAIN : AV_CREATURE_H_CAPTAIN;
                    if (Creature* captain = bg->GetBGCreature(creatureId))
                    {
                        if (captain->IsAlive())
                        {
                            BgObjective = captain;
                            objectiveReason = "captain";
                        }
                    }
                }
            }

            // --- Defender Logic ---
            if (!BgObjective && isDefender)
            {
                if (team == TEAM_ALLIANCE)
                {
                    if ((hasCachedAllianceTactics ? cachedAllianceTactics.shouldScreenIdleNorthReserve :
                         AllianceAVShouldScreenIdleNorthReserve(av, allianceRushInfo, allianceThreat, allianceMode)) &&
                        SetAllianceNorthReservePosition(bot, posMap, pos, role, objectiveReason))
                    {
                        LOG_DEBUG("playerbots",
                                  "AV objective bot={} role={} strategy={} enemyStrategy={} reason={} node={} state={} owner={} prevOwner={} timer={} distance={:.1f}",
                                  bot->GetName(), static_cast<uint32>(role), static_cast<uint32>(strategy),
                                  static_cast<uint32>(enemyStrategy), objectiveReason, 255, 255, 255, 255, 0,
                                  ServerFacade::instance().GetDistance2d(bot, pos.x, pos.y));
                        return true;
                    }

                    BgObjective = SelectAllianceAVDefenderObjective(bot, bg, av, allianceRushInfo);
                    if (BgObjective)
                        objectiveReason = "alliance defender weighted objective";
                }
                else
                {
                    std::vector<GameObject*> contestedObjectives;
                    std::vector<GameObject*> availableObjectives;

                    for (auto const& [nodeId, goId] : defendObjectives)
                    {
                        const BG_AV_NodeInfo& node = av->GetAVNodeInfo(nodeId);
                        if (node.State == POINT_DESTROYED)
                            continue;

                        GameObject* go = bg->GetBGObject(goId);
                        if (!go)
                            continue;

                        if (node.State == POINT_ASSAULTED)
                            contestedObjectives.push_back(go);
                        else
                            availableObjectives.push_back(go);
                    }

                    if (!contestedObjectives.empty())
                    {
                        BgObjective = contestedObjectives[urand(0, contestedObjectives.size() - 1)];
                        objectiveReason = "defender contested objective";
                    }
                    else if (!availableObjectives.empty())
                    {
                        BgObjective = availableObjectives[urand(0, availableObjectives.size() - 1)];
                        objectiveReason = "defender available objective";
                    }
                }
            }

            // --- Enemy Boss ---
            if (!BgObjective && !(team == TEAM_ALLIANCE && strategy == AV_STRATEGY_ALLIANCE_CONTROL_TEMPO))
            {
                uint32 towersDown = 0;
                for (auto const& [nodeId, _] : attackObjectives)
                    if (av->GetAVNodeInfo(nodeId).State == POINT_DESTROYED)
                        towersDown++;

                if ((towersDown >= 2) || (strategy == AV_STRATEGY_OFFENSIVE))
                {
                    uint8 lastGY = (team == TEAM_HORDE) ? BG_AV_NODES_FIRSTAID_STATION : BG_AV_NODES_FROSTWOLF_HUT;
                    bool ownsFinalGY = av->GetAVNodeInfo(lastGY).OwnerId == team;

                    uint32 bossId = (team == TEAM_HORDE) ? AV_CREATURE_A_BOSS : AV_CREATURE_H_BOSS;
                    if (Creature* boss = bg->GetBGCreature(bossId))
                    {
                        if (boss->IsAlive())
                        {
                            uint32 nearbyCount = getPlayersInArea(team, boss->GetPosition(), 200.0f, false);
                            if (ownsFinalGY || nearbyCount >= 20)
                            {
                                BgObjective = boss;
                                objectiveReason = "enemy boss";
                            }
                        }
                    }
                }
            }

            // --- Attacker Logic ---
            if (!BgObjective && !(team == TEAM_ALLIANCE && strategy == AV_STRATEGY_ALLIANCE_CONTROL_TEMPO))
            {
                std::vector<GameObject*> candidates;

                for (auto const& [nodeId, goId] : attackObjectives)
                {
                    if (team == TEAM_ALLIANCE && IsAllianceHordeTowerAttackTarget(nodeId) &&
                        AllianceAVMustKillCaptainBeforeTowers(av))
                        continue;

                    if (team == TEAM_ALLIANCE && !AllianceHasAnySouthRespawn(av) &&
                        IsAllianceForwardGraveyardAttackTarget(nodeId))
                        continue;

                    const BG_AV_NodeInfo& node = av->GetAVNodeInfo(nodeId);
                    GameObject* go = bg->GetBGObject(goId);
                    if (!go || node.State == POINT_DESTROYED || node.TotalOwnerId == team)
                        continue;

                    if (node.State == POINT_ASSAULTED && urand(0, 99) >= 1)
                        continue;

                    candidates.push_back(go);

                    if (((strategy == AV_STRATEGY_BALANCED && candidates.size() >= 2) ||
                         (strategy == AV_STRATEGY_OFFENSIVE && candidates.size() >= 3) ||
                         (strategy == AV_STRATEGY_DEFENSIVE && candidates.size() >= 1)) ||
                        isAdvanced)
                        break;
                }

                if (!candidates.empty())
                {
                    BgObjective = candidates[urand(0, candidates.size() - 1)];
                    objectiveReason = "attacker objective";
                }
                else
                {
                    if (team == TEAM_ALLIANCE && !AllianceHasAnySouthRespawn(av))
                    {
                        BgObjective = SelectAllianceIcebloodGraveyardObjective(bg, av);
                        if (BgObjective)
                        {
                            objectiveReason = "alliance iceblood fallback";
                        }
                    }

                    if (!BgObjective)
                    {
                        // Fallback: move to boss wait position
                        const Position& waitPos = (team == TEAM_HORDE) ? AV_BOSS_WAIT_H : AV_BOSS_WAIT_A;

                        float rx, ry, rz;
                        bot->GetRandomPoint(waitPos, 5.0f, rx, ry, rz);
                        ResolveBattleGroundGroundZ(bot, rx, ry, rz);

                        pos.Set(rx, ry, rz, bot->GetMapId());
                        posMap["bg objective"] = pos;

                        uint32 bossId = (team == TEAM_HORDE) ? AV_CREATURE_A_BOSS : AV_CREATURE_H_BOSS;
                        if (Creature* boss = bg->GetBGCreature(bossId))
                            if (boss->IsAlive())
                            {
                                BgObjective = boss;
                                objectiveReason = "boss wait fallback";
                            }
                    }
                }
            }

            // --- Movement logic for any valid objective ---
            if (BgObjective)
            {
                Position objPos = BgObjective->GetPosition();

                Optional<uint8> linkedNodeId;
                for (auto const& [nodeId, goId] : attackObjectives)
                    if (bg->GetBGObject(goId) == BgObjective)
                        linkedNodeId = nodeId;

                if (!linkedNodeId)
                {
                    for (auto const& [nodeId, goId] : defendObjectives)
                        if (bg->GetBGObject(goId) == BgObjective)
                            linkedNodeId = nodeId;
                }

                if (team == TEAM_ALLIANCE && !linkedNodeId)
                {
                    for (auto const& [nodeId, goId] : AV_AllianceAssaultGuardObjectives)
                        if (bg->GetBGObject(goId) == BgObjective)
                            linkedNodeId = nodeId;
                }

                if (team == TEAM_ALLIANCE && !linkedNodeId)
                {
                    if (bg->GetBGObject(BG_AV_OBJECT_FLAG_A_ICEBLOOD_GRAVE) == BgObjective)
                        linkedNodeId = BG_AV_NODES_ICEBLOOD_GRAVE;
                }

                if (team == TEAM_ALLIANCE && !linkedNodeId)
                {
                    uint32 const snowfallGoIds[] = {
                        BG_AV_OBJECT_FLAG_N_SNOWFALL_GRAVE,
                        BG_AV_OBJECT_FLAG_A_SNOWFALL_GRAVE,
                        BG_AV_OBJECT_FLAG_C_A_SNOWFALL_GRAVE,
                    };

                    for (uint32 goId : snowfallGoIds)
                        if (bg->GetBGObject(goId) == BgObjective)
                            linkedNodeId = BG_AV_NODES_SNOWFALL_GRAVE;
                }

                if (team == TEAM_ALLIANCE && !linkedNodeId)
                {
                    for (auto const& [nodeId, goId] : AV_AllianceTowerRecapObjectives)
                        if (bg->GetBGObject(goId) == BgObjective)
                            linkedNodeId = nodeId;
                }

                if (team == TEAM_ALLIANCE && !linkedNodeId)
                {
                    for (auto const& [nodeId, goId] : AV_AllianceGraveyardRecapObjectives)
                        if (bg->GetBGObject(goId) == BgObjective)
                            linkedNodeId = nodeId;
                }

                if (team == TEAM_ALLIANCE && !linkedNodeId)
                {
                    for (auto const& [nodeId, goId] : AV_AllianceHomeGraveyardReclaimObjectives)
                        if (bg->GetBGObject(goId) == BgObjective)
                            linkedNodeId = nodeId;
                }

                if (!linkedNodeId)
                {
                    if (team == TEAM_ALLIANCE && BgObjective == GetAllianceHordeCaptain(bg, av))
                        objPos = AV_ICEBLOOD_GARRISON_ATTACKING_ALLIANCE;
                    else if (IsInvalidBattleGroundZ(objPos.GetPositionZ()))
                        objPos.Relocate(objPos.GetPositionX(), objPos.GetPositionY(), bot->GetPositionZ());
                }

                float rx, ry, rz;
                bool const multiFloorTowerObjective = linkedNodeId && IsAlteracTowerOrBunkerNode(*linkedNodeId);
                if (linkedNodeId && AVNodeMovementTargets.count(*linkedNodeId))
                {
                    const AVNodePositionData& data = AVNodeMovementTargets[*linkedNodeId];
                    float maxRadius = data.maxRadius;
                    if (multiFloorTowerObjective)
                        maxRadius = std::min(maxRadius, 1.0f);
                    else if (team == TEAM_ALLIANCE && IsAllianceDefensiveTower(*linkedNodeId) && maxRadius <= 0.0f)
                        maxRadius = 5.0f;

                    float const radius = maxRadius > 0.0f ? frand(0.8f, maxRadius) : 0.0f;
                    bot->GetRandomPoint(data.pos, radius, rx, ry, rz);
                }
                else
                    bot->GetRandomPoint(objPos, frand(-2.0f, 2.0f), rx, ry, rz);

                if (!multiFloorTowerObjective)
                    ResolveBattleGroundGroundZ(bot, rx, ry, rz);

                pos.Set(rx, ry, rz, BgObjective->GetMapId());
                posMap["bg objective"] = pos;

                if (team == TEAM_ALLIANCE)
                {
                    uint32 nodeId = linkedNodeId ? *linkedNodeId : 255;
                    uint32 nodeState = 255;
                    uint32 nodeOwner = 255;
                    uint32 nodePrevOwner = 255;
                    uint32 nodeTimer = 0;
                    if (linkedNodeId)
                    {
                        BG_AV_NodeInfo const& node = av->GetAVNodeInfo(*linkedNodeId);
                        nodeState = node.State;
                        nodeOwner = node.OwnerId;
                        nodePrevOwner = node.PrevOwnerId;
                        nodeTimer = node.Timer;
                    }

                    LOG_DEBUG("playerbots", "AV objective bot={} role={} strategy={} enemyStrategy={} reason={} node={} state={} owner={} prevOwner={} timer={} distance={:.1f}",
                              bot->GetName(), static_cast<uint32>(role), static_cast<uint32>(strategy), static_cast<uint32>(enemyStrategy),
                              objectiveReason, nodeId, nodeState, nodeOwner, nodePrevOwner, nodeTimer,
                              bot->GetDistance(BgObjective));
                }

                return true;
            }

            if (team == TEAM_ALLIANCE)
                LogAllianceAVMoveDebug(bot, bg, pos, "select_objective_none", role, defendersProhab, isDefender,
                                       AI_VALUE(Unit*, "enemy player target"));

            break;
        }
        case BATTLEGROUND_WS:
        {
            Position target;
            TeamId team = bot->GetTeamId();

            // Utility to safely relocate a position with optional random radius
            auto SetSafePos = [&](Position const& origin, float radius = 0.0f) -> void
            {
                float rx, ry, rz;
                if (radius > 0.0f)
                {
                    bot->GetRandomPoint(origin, radius, rx, ry, rz);
                    if (rz == VMAP_INVALID_HEIGHT_VALUE)
                        target.Relocate(rx, ry, rz);
                    else
                        target.Relocate(origin);
                }
                else
                {
                    target.Relocate(origin);
                }
            };

            // Check if the bot is carrying the flag
            bool hasFlag = bot->HasAura(BG_WS_SPELL_WARSONG_FLAG) || bot->HasAura(BG_WS_SPELL_SILVERWING_FLAG);

            // Retrieve role
            uint8 role = context->GetValue<uint32>("bg role")->Get();
            WSBotStrategy strategyHorde = static_cast<WSBotStrategy>(GetBotStrategyForTeam(bg, TEAM_HORDE));
            WSBotStrategy strategyAlliance = static_cast<WSBotStrategy>(GetBotStrategyForTeam(bg, TEAM_ALLIANCE));
            WSBotStrategy strategy = (team == TEAM_ALLIANCE) ? strategyAlliance : strategyHorde;
            WSBotStrategy enemyStrategy = (team == TEAM_ALLIANCE) ? strategyHorde : strategyAlliance;

            uint8 defendersProhab = 3;  // Default balanced

            switch (strategy)
            {
                case WS_STRATEGY_OFFENSIVE:
                    defendersProhab = 1;
                    break;
                case WS_STRATEGY_DEFENSIVE:
                    defendersProhab = 6;
                    break;
                case WS_STRATEGY_BALANCED:
                default:
                    defendersProhab = 3;
                    break;
            }

            if (enemyStrategy == WS_STRATEGY_DEFENSIVE)
                defendersProhab = 2;

            // Role check
            bool isDefender = role < defendersProhab;

            // Retrieve flag carriers
            Unit* enemyFC = AI_VALUE(Unit*, "enemy flag carrier");
            Unit* teamFC = AI_VALUE(Unit*, "team flag carrier");

            // Retrieve current score
            uint8 allianceScore = bg->GetTeamScore(TEAM_ALLIANCE);
            uint8 hordeScore = bg->GetTeamScore(TEAM_HORDE);

            // Check if both teams currently have the flag
            bool bothFlagsTaken = enemyFC && teamFC;
            if (!hasFlag && bothFlagsTaken)
            {
                // If both flags taken: Bots have 20% chance to support own flag carrier, otherwise attack enemy FC
                if (urand(0, 99) < 20 && teamFC)
                {
                    target.Relocate(teamFC->GetPositionX(), teamFC->GetPositionY(), teamFC->GetPositionZ());
                    if (ServerFacade::instance().GetDistance2d(bot, teamFC) < 33.0f)
                        Follow(teamFC);
                }
                else
                    target.Relocate(enemyFC->GetPositionX(), enemyFC->GetPositionY(), enemyFC->GetPositionZ());
            }
            // Graveyard Camping if in lead
            else if (!hasFlag && role < 8 &&
                ((team == TEAM_ALLIANCE && allianceScore == 2 && hordeScore == 0) ||
                (team == TEAM_HORDE && hordeScore == 2 && allianceScore == 0)))
            {
                if (team == TEAM_ALLIANCE)
                    SetSafePos(WS_GY_CAMPING_HORDE, 10.0f);
                else
                    SetSafePos(WS_GY_CAMPING_ALLIANCE, 10.0f);
            }
            else if (hasFlag)
            {
                // If carrying the flag, either hide or return to base
                if (team == TEAM_ALLIANCE)
                    SetSafePos(teamFlagTaken() ? WS_FLAG_HIDE_ALLIANCE[urand(0, 2)] : WS_FLAG_POS_ALLIANCE);
                else
                    SetSafePos(teamFlagTaken() ? WS_FLAG_HIDE_HORDE[urand(0, 2)] : WS_FLAG_POS_HORDE);
            }
            else
            {
                if (isDefender)
                {
                    if (enemyFC)
                    {
                        // Defenders attack enemy FC if found
                        target.Relocate(enemyFC->GetPositionX(), enemyFC->GetPositionY(), enemyFC->GetPositionZ());
                    }
                    else if (urand(0, 99) < 33)
                    {
                        // 33% chance to roam near own base
                        SetSafePos(team == TEAM_ALLIANCE ? WS_FLAG_HIDE_ALLIANCE[urand(0, 2)] : WS_FLAG_HIDE_HORDE[urand(0, 2)], 5.0f);
                    }
                    else if (teamFC)
                    {
                        // 70% chance to support own FC
                        if (urand(0, 99) < 70)
                        {
                            target.Relocate(teamFC->GetPositionX(), teamFC->GetPositionY(), teamFC->GetPositionZ());
                            if (ServerFacade::instance().GetDistance2d(bot, teamFC) < 33.0f)
                                Follow(teamFC);
                        }
                    }
                    else
                    {
                        // Roam around central area
                        SetSafePos(WS_ROAM_POS, 75.0f);
                    }
                }
                else  // attacker logic
                {
                    if (enemyFC && urand(0, 99) < 70)
                    {
                        // 70% chance to pursue enemy FC
                        target.Relocate(enemyFC->GetPositionX(), enemyFC->GetPositionY(), enemyFC->GetPositionZ());
                    }
                    else if (teamFC)
                    {
                        // Assist own FC if not pursuing enemy FC
                        target.Relocate(teamFC->GetPositionX(), teamFC->GetPositionY(), teamFC->GetPositionZ());
                        if (ServerFacade::instance().GetDistance2d(bot, teamFC) < 33.0f)
                            Follow(teamFC);
                    }
                    else if (urand(0, 99) < 5)
                    {
                        // 5% chance to free roam
                        SetSafePos(WS_ROAM_POS, 75.0f);
                    }
                    else
                    {
                        // Push toward enemy flag base
                        SetSafePos(team == TEAM_ALLIANCE ? WS_FLAG_POS_HORDE : WS_FLAG_POS_ALLIANCE);
                    }
                }
            }

            // Save the final target position
            if (target.IsPositionValid())
            {
                pos.Set(target.GetPositionX(), target.GetPositionY(), target.GetPositionZ(), bot->GetMapId());
                posMap["bg objective"] = pos;
                return true;
            }

            break;
        }
        case BATTLEGROUND_AB:
        {
            if (!bg)
                break;

            BattlegroundAB* ab = static_cast<BattlegroundAB*>(bg);
            TeamId team = bot->GetTeamId();

            uint8 role = context->GetValue<uint32>("bg role")->Get();
            ABBotStrategy strategyHorde = static_cast<ABBotStrategy>(GetBotStrategyForTeam(bg, TEAM_HORDE));
            ABBotStrategy strategyAlliance = static_cast<ABBotStrategy>(GetBotStrategyForTeam(bg, TEAM_ALLIANCE));
            ABBotStrategy strategy = (team == TEAM_ALLIANCE) ? strategyAlliance : strategyHorde;
            ABBotStrategy enemyStrategy = (team == TEAM_ALLIANCE) ? strategyHorde : strategyAlliance;

            uint8 defendersProhab = 3;
            if (strategy == AB_STRATEGY_OFFENSIVE)
                defendersProhab = 1;
            else if (strategy == AB_STRATEGY_DEFENSIVE)
                defendersProhab = 6;

            if (enemyStrategy == AB_STRATEGY_DEFENSIVE)
                defendersProhab = 2;

            bool isDefender = role < defendersProhab;
            bool isSilly = urand(0, 99) < 20;

            BgObjective = nullptr;

            // --- PRIORITY 1: Nearby enemy (rare aggressive impulse)
            if (urand(0, 99) < 5)
            {
                if (Unit* enemy = AI_VALUE(Unit*, "enemy player target"))
                {
                    if (bot->GetDistance(enemy) < 500.0f)
                    {
                        pos.Set(enemy->GetPositionX(), enemy->GetPositionY(), enemy->GetPositionZ(), bot->GetMapId());
                        posMap["bg objective"] = pos;
                        break;
                    }
                }
            }

            // --- PRIORITY 2: No valid nodes? Camp or attack visible enemy
            bool hasValidTarget = false;
            for (uint32 nodeId : AB_AttackObjectives)
            {
                uint8 state = ab->GetCapturePointInfo(nodeId)._state;
                if (state == BG_AB_NODE_STATE_NEUTRAL ||
                    (team == TEAM_ALLIANCE &&
                     (state == BG_AB_NODE_STATE_HORDE_OCCUPIED || state == BG_AB_NODE_STATE_HORDE_CONTESTED)) ||
                    (team == TEAM_HORDE &&
                     (state == BG_AB_NODE_STATE_ALLY_OCCUPIED || state == BG_AB_NODE_STATE_ALLY_CONTESTED)))
                {
                    hasValidTarget = true;
                    break;
                }
            }

            if (!hasValidTarget)
            {
                if (Unit* enemy = AI_VALUE(Unit*, "enemy player target"))
                {
                    if (bot->GetDistance(enemy) < 500.0f)
                    {
                        pos.Set(enemy->GetPositionX(), enemy->GetPositionY(), enemy->GetPositionZ(), bot->GetMapId());
                        posMap["bg objective"] = pos;
                        break;
                    }
                }

                // Camp enemy GY fallback
                Position camp = (team == TEAM_ALLIANCE) ? AB_GY_CAMPING_HORDE : AB_GY_CAMPING_ALLIANCE;
                float rx, ry, rz;
                bot->GetRandomPoint(camp, 10.0f, rx, ry, rz);
                if (Map* map = bot->GetMap())
                {
                    float groundZ = map->GetHeight(rx, ry, rz);
                    if (groundZ != VMAP_INVALID_HEIGHT_VALUE)
                        rz = groundZ;
                }
                pos.Set(rx, ry, rz, bot->GetMapId());
                posMap["bg objective"] = pos;
                break;
            }

            // --- PRIORITY 3: Defender logic ---
            if (isDefender && urand(0, 99) < 85)
            {
                float closestDist = FLT_MAX;
                for (uint32 nodeId : AB_AttackObjectives)
                {
                    uint8 state = ab->GetCapturePointInfo(nodeId)._state;

                    bool isContested = (team == TEAM_ALLIANCE && state == BG_AB_NODE_STATE_HORDE_CONTESTED) ||
                                       (team == TEAM_HORDE && state == BG_AB_NODE_STATE_ALLY_CONTESTED);
                    bool isOwned = (team == TEAM_ALLIANCE && state == BG_AB_NODE_STATE_ALLY_OCCUPIED) ||
                                   (team == TEAM_HORDE && state == BG_AB_NODE_STATE_HORDE_OCCUPIED);

                    if (!isContested && !isOwned)
                        continue;

                    GameObject* go = bg->GetBGObject(nodeId * BG_AB_OBJECTS_PER_NODE);
                    if (!go)
                        continue;

                    float dist = bot->GetDistance(go);
                    if (dist < closestDist)
                    {
                        closestDist = dist;
                        BgObjective = go;
                    }
                }
            }

            // --- PRIORITY 4: Attack objectives ---
            if (!BgObjective)
            {
                std::vector<GameObject*> objectivePool;

                for (uint8 i = 0; i < 3; ++i)
                {
                    float bestDist = isSilly ? 0.0f : FLT_MAX;
                    GameObject* chosen = nullptr;

                    for (uint32 nodeId : AB_AttackObjectives)
                    {
                        uint8 state = ab->GetCapturePointInfo(nodeId)._state;

                        bool isNeutral = state == BG_AB_NODE_STATE_NEUTRAL;
                        bool isEnemyOccupied = (team == TEAM_ALLIANCE && state == BG_AB_NODE_STATE_HORDE_OCCUPIED) ||
                                               (team == TEAM_HORDE && state == BG_AB_NODE_STATE_ALLY_OCCUPIED);
                        bool isFriendlyContested =
                            (team == TEAM_ALLIANCE && state == BG_AB_NODE_STATE_ALLY_CONTESTED) ||
                            (team == TEAM_HORDE && state == BG_AB_NODE_STATE_HORDE_CONTESTED);

                        if (!(isNeutral || isEnemyOccupied || isFriendlyContested))
                            continue;

                        GameObject* go = bg->GetBGObject(nodeId * BG_AB_OBJECTS_PER_NODE);
                        if (!go || std::find(objectivePool.begin(), objectivePool.end(), go) != objectivePool.end())
                            continue;

                        float dist = bot->GetDistance(go);
                        if ((isSilly && dist > bestDist) || (!isSilly && dist < bestDist))
                        {
                            bestDist = dist;
                            chosen = go;
                        }
                    }

                    if (chosen)
                        objectivePool.push_back(chosen);
                }

                if (!objectivePool.empty())
                    BgObjective = objectivePool[urand(0, objectivePool.size() - 1)];
            }

            // --- Final move ---
            if (BgObjective)
            {
                float rx, ry, rz;
                Position objPos = BgObjective->GetPosition();
                bot->GetRandomPoint(objPos, frand(5.0f, 15.0f), rx, ry, rz);

                if (Map* map = bot->GetMap())
                {
                    float groundZ = map->GetHeight(rx, ry, rz);
                    if (groundZ != VMAP_INVALID_HEIGHT_VALUE)
                        rz = groundZ;
                }

                pos.Set(rx, ry, rz, BgObjective->GetMapId());
                posMap["bg objective"] = pos;
            }

            return true;
        }
        case BATTLEGROUND_EY:
        {
            BattlegroundEY* eyeOfTheStormBG = (BattlegroundEY*)bg;
            TeamId team = bot->GetTeamId();
            uint8 role = context->GetValue<uint32>("bg role")->Get();

            EYBotStrategy strategyHorde = static_cast<EYBotStrategy>(GetBotStrategyForTeam(bg, TEAM_HORDE));
            EYBotStrategy strategyAlliance = static_cast<EYBotStrategy>(GetBotStrategyForTeam(bg, TEAM_ALLIANCE));
            EYBotStrategy strategy = (team == TEAM_ALLIANCE) ? strategyAlliance : strategyHorde;

            auto IsOwned = [&](uint32 nodeId) -> bool
            { return eyeOfTheStormBG->GetCapturePointInfo(nodeId)._ownerTeamId == team; };

            uint8 defendersProhab = 4;
            switch (strategy)
            {
                case EY_STRATEGY_FLAG_FOCUS:
                    defendersProhab = 2;
                    break;
                case EY_STRATEGY_FRONT_FOCUS:
                case EY_STRATEGY_BACK_FOCUS:
                    defendersProhab = 3;
                    break;
                default:
                    defendersProhab = 4;
                    break;
            }

            bool isDefender = role < defendersProhab;

            std::tuple<uint32, uint32, uint32> front[2];
            std::tuple<uint32, uint32, uint32> back[2];

            if (team == TEAM_HORDE)
            {
                front[0] = EY_AttackObjectives[0];
                front[1] = EY_AttackObjectives[1];
                back[0] = EY_AttackObjectives[2];
                back[1] = EY_AttackObjectives[3];
            }
            else
            {
                front[0] = EY_AttackObjectives[2];
                front[1] = EY_AttackObjectives[3];
                back[0] = EY_AttackObjectives[0];
                back[1] = EY_AttackObjectives[1];
            }

            bool foundObjective = false;

            // --- PRIORITY 1: FLAG CARRIER ---
            if (bot->HasAura(BG_EY_NETHERSTORM_FLAG_SPELL))
            {
                uint32 bestNodeId = 0;
                float bestDist = FLT_MAX;
                uint32 bestTrigger = 0;

                for (auto const& [nodeId, _, areaTrigger] : EY_AttackObjectives)
                {
                    if (!IsOwned(nodeId))
                        continue;

                    auto it = EY_NodePositions.find(nodeId);
                    if (it == EY_NodePositions.end())
                        continue;

                    float dist = bot->GetDistance(it->second);
                    if (dist < bestDist)
                    {
                        bestDist = dist;
                        bestNodeId = nodeId;
                        bestTrigger = areaTrigger;
                    }
                }

                if (bestNodeId != 0 && EY_NodePositions.contains(bestNodeId))
                {
                    const Position& targetPos = EY_NodePositions[bestNodeId];
                    float rx, ry, rz;
                    bot->GetRandomPoint(targetPos, 5.0f, rx, ry, rz);

                    if (Map* map = bot->GetMap())
                    {
                        float groundZ = map->GetHeight(rx, ry, rz);
                        if (groundZ != VMAP_INVALID_HEIGHT_VALUE)
                            rz = groundZ;
                    }

                    pos.Set(rx, ry, rz, bot->GetMapId());

                    // Check AreaTrigger activation range
                    if (bestTrigger && bot->IsWithinDist3d(pos.x, pos.y, pos.z, INTERACTION_DISTANCE))
                    {
                        WorldPacket data(CMSG_AREATRIGGER);
                        data << uint32(bestTrigger);
                        bot->GetSession()->HandleAreaTriggerOpcode(data);
                        pos.Reset();
                    }

                    foundObjective = true;
                }
                else
                {
                    const Position& fallback = (team == TEAM_ALLIANCE) ? EY_FLAG_RETURN_POS_RETREAT_ALLIANCE
                                                                       : EY_FLAG_RETURN_POS_RETREAT_HORDE;

                    float rx, ry, rz;
                    bot->GetRandomPoint(fallback, 5.0f, rx, ry, rz);

                    if (Map* map = bot->GetMap())
                    {
                        float groundZ = map->GetHeight(rx, ry, rz);
                        if (groundZ != VMAP_INVALID_HEIGHT_VALUE)
                            rz = groundZ;
                    }

                    pos.Set(rx, ry, rz, bot->GetMapId());
                    foundObjective = true;
                }
            }

            // --- PRIORITY 2: Nearby unowned contested node ---
            if (!foundObjective)
            {
                for (auto const& [nodeId, _, __] : EY_AttackObjectives)
                {
                    if (IsOwned(nodeId))
                        continue;

                    auto it = EY_NodePositions.find(nodeId);
                    if (it == EY_NodePositions.end())
                        continue;

                    const Position& p = it->second;
                    if (bot->IsWithinDist2d(p.GetPositionX(), p.GetPositionY(), 125.0f))
                    {
                        float rx, ry, rz;
                        bot->GetRandomPoint(p, 5.0f, rx, ry, rz);
                        rz = bot->GetMap()->GetHeight(rx, ry, rz);
                        pos.Set(rx, ry, rz, bot->GetMapId());
                        foundObjective = true;
                    }
                }
            }

            // --- PRIORITY 3: Random nearby enemy (20%) ---
            if (!foundObjective && urand(0, 99) < 20)
            {
                if (Unit* enemy = AI_VALUE(Unit*, "enemy player target"))
                {
                    if (bot->GetDistance(enemy) < 250.0f)
                    {
                        pos.Set(enemy->GetPositionX(), enemy->GetPositionY(), enemy->GetPositionZ(), bot->GetMapId());
                        foundObjective = true;
                    }
                }
            }

            // --- PRIORITY 4: Defender Logic ---
            if (!foundObjective && isDefender && urand(0, 99) <= 80)
            {
                // 1. Chase enemy flag carrier
                if (Unit* enemyFC = AI_VALUE(Unit*, "enemy flag carrier"))
                {
                    if (bot->CanSeeOrDetect(enemyFC, false, false, true) && enemyFC->IsAlive())
                    {
                        pos.Set(enemyFC->GetPositionX(), enemyFC->GetPositionY(), enemyFC->GetPositionZ(), bot->GetMapId());
                        foundObjective = true;
                    }
                }

                // 2. Support friendly flag carrier
                if (!foundObjective)
                {
                    if (Unit* friendlyFC = AI_VALUE(Unit*, "team flag carrier"))
                    {
                        if (friendlyFC->IsAlive())
                        {
                            pos.Set(friendlyFC->GetPositionX(), friendlyFC->GetPositionY(), friendlyFC->GetPositionZ(), bot->GetMapId());
                            foundObjective = true;
                        }
                    }
                }

                // 3. Pick up the flag
                if (!foundObjective)
                {
                    if (GameObject* flag = bg->GetBGObject(BG_EY_OBJECT_FLAG_NETHERSTORM); flag && flag->isSpawned())
                    {
                        pos.Set(flag->GetPositionX(), flag->GetPositionY(), flag->GetPositionZ(), flag->GetMapId());
                        foundObjective = true;
                    }
                }

                // 4. Default: defend owned node
                if (!foundObjective && urand(0, 99) < 50)
                {
                    std::vector<uint32> owned;
                    for (auto const& obj : front)
                    {
                        uint32 nodeId = std::get<0>(obj);
                        if (IsOwned(nodeId))
                            owned.push_back(nodeId);
                    }

                    if (!owned.empty())
                    {
                        uint32 chosenId = owned[urand(0, owned.size() - 1)];
                        if (EY_NodePositions.contains(chosenId))
                        {
                            const Position& p = EY_NodePositions[chosenId];
                            float rx, ry, rz;
                            bot->GetRandomPoint(p, 5.0f, rx, ry, rz);
                            rz = bot->GetMap()->GetHeight(rx, ry, rz);
                            pos.Set(rx, ry, rz, bot->GetMapId());
                            foundObjective = true;
                        }
                    }
                }
            }

            // --- PRIORITY 5: Flag Strategy ---
            if (!foundObjective && strategy == EY_STRATEGY_FLAG_FOCUS)
            {
                bool ownsAny = false;
                for (auto const& [nodeId, _, __] : EY_AttackObjectives)
                {
                    if (IsOwned(nodeId))
                    {
                        ownsAny = true;
                        break;
                    }
                }

                if (ownsAny)
                {
                    if (Unit* fc = AI_VALUE(Unit*, "enemy flag carrier"))
                    {
                        pos.Set(fc->GetPositionX(), fc->GetPositionY(), fc->GetPositionZ(), bot->GetMapId());
                        foundObjective = true;
                    }
                    else if (GameObject* flag = bg->GetBGObject(BG_EY_OBJECT_FLAG_NETHERSTORM);
                             flag && flag->isSpawned())
                    {
                        pos.Set(flag->GetPositionX(), flag->GetPositionY(), flag->GetPositionZ(), flag->GetMapId());
                        foundObjective = true;
                    }
                }
                else
                {
                    float bestDist = FLT_MAX;
                    Optional<uint32> bestNode;

                    for (auto const& [nodeId, _, __] : EY_AttackObjectives)
                    {
                        if (IsOwned(nodeId))
                            continue;

                        auto it = EY_NodePositions.find(nodeId);
                        if (it == EY_NodePositions.end())
                            continue;

                        float dist = bot->GetDistance(it->second);
                        if (dist < bestDist)
                        {
                            bestDist = dist;
                            bestNode = nodeId;
                        }
                    }

                    if (bestNode && EY_NodePositions.contains(*bestNode))
                    {
                        const Position& p = EY_NodePositions[*bestNode];
                        float rx, ry, rz;
                        bot->GetRandomPoint(p, 5.0f, rx, ry, rz);
                        rz = bot->GetMap()->GetHeight(rx, ry, rz);
                        pos.Set(rx, ry, rz, bot->GetMapId());
                        foundObjective = true;
                    }
                }
            }

            // --- PRIORITY 6: Strategy Objectives ---
            if (!foundObjective)
            {
                std::vector<std::tuple<uint32, uint32, uint32>> priority, secondary;

                switch (strategy)
                {
                    case EY_STRATEGY_FRONT_FOCUS:
                        priority = {front[0], front[1]};
                        secondary = {back[0], back[1]};
                        break;
                    case EY_STRATEGY_BACK_FOCUS:
                        priority = {back[0], back[1]};
                        secondary = {front[0], front[1]};
                        break;
                    case EY_STRATEGY_BALANCED:
                    default:
                        priority = {front[0], front[1], back[0], back[1]};
                        break;
                }

                std::vector<uint32> candidates;

                for (auto const& obj : priority)
                {
                    uint32 nodeId = std::get<0>(obj);
                    if (!IsOwned(nodeId))
                        candidates.push_back(nodeId);
                }

                if (candidates.empty())
                {
                    for (auto const& obj : secondary)
                    {
                        uint32 nodeId = std::get<0>(obj);
                        if (!IsOwned(nodeId))
                            candidates.push_back(nodeId);
                    }
                }

                if (!candidates.empty())
                {
                    uint32 chosen = candidates[urand(0, candidates.size() - 1)];
                    if (EY_NodePositions.contains(chosen))
                    {
                        const Position& p = EY_NodePositions[chosen];
                        pos.Set(p.GetPositionX(), p.GetPositionY(), p.GetPositionZ(), bot->GetMapId());
                        foundObjective = true;
                    }
                }
            }

            // --- PRIORITY 7: Camp GY if everything is owned ---
            bool ownsAll = IsOwned(std::get<0>(front[0])) && IsOwned(std::get<0>(front[1])) &&
                           IsOwned(std::get<0>(back[0])) && IsOwned(std::get<0>(back[1]));

            if (!foundObjective && ownsAll)
            {
                Position camp = (team == TEAM_HORDE) ? EY_GY_CAMPING_ALLIANCE : EY_GY_CAMPING_HORDE;
                float rx, ry, rz;
                bot->GetRandomPoint(camp, 10.0f, rx, ry, rz);
                rz = bot->GetMap()->GetHeight(rx, ry, rz);
                pos.Set(rx, ry, rz, bot->GetMapId());
                foundObjective = true;
            }

            if (foundObjective)
                posMap["bg objective"] = pos;

            return true;
        }
        case BATTLEGROUND_IC:
        {
            BattlegroundIC* isleOfConquestBG = (BattlegroundIC*)bg;

            uint32 role = context->GetValue<uint32>("bg role")->Get();
            bool inVehicle = botAI->IsInVehicle();
            bool controlsVehicle = botAI->IsInVehicle(true);
            uint32 vehicleId = inVehicle ? bot->GetVehicleBase()->GetEntry() : 0;

            // skip if not the driver
            if (inVehicle && !controlsVehicle)
                return false;

            /* TACTICS */
            if (bot->GetTeamId() == TEAM_HORDE)  // HORDE
            {
                bool gateOpen = false;
                if (GameObject* pGO = bg->GetBGObject(BG_IC_GO_DOODAD_PORTCULLISACTIVE02))
                {
                    if (pGO->isSpawned() && pGO->getLootState() == GO_ACTIVATED)
                    {
                        gateOpen = true;
                        PositionInfo siegePos = context->GetValue<PositionMap&>("position")->Get()["bg siege"];
                        siegePos.Reset();
                        posMap["bg siege"] = siegePos;
                    }
                }
                if (gateOpen && !controlsVehicle &&
                    isleOfConquestBG->GetICNodePoint(NODE_TYPE_GRAVEYARD_A).nodeState ==
                        NODE_STATE_CONTROLLED_H)  // target enemy boss
                {
                    if (Creature* enemyBoss = bg->GetBGCreature(BG_IC_NPC_HIGH_COMMANDER_HALFORD_WYRMBANE))
                    {
                        if (enemyBoss->IsVisible())
                        {
                            BgObjective = enemyBoss;
                            // LOG_INFO("playerbots", "bot={} attack boss", bot->GetName());
                        }
                    }
                }

                if (!BgObjective && gateOpen && !controlsVehicle)
                {
                    if (GameObject* pGO = bg->GetBGObject(BG_IC_GO_ALLIANCE_BANNER))  // capture flag within keep
                    {
                        BgObjective = pGO;
                        // LOG_INFO("playerbots", "bot={} attack keep", bot->GetName());
                    }
                }

                if (!BgObjective && !gateOpen && controlsVehicle)  // attack gates
                {
                    // TODO: check for free vehicles
                    if (GameObject* gate = bg->GetBGObject(BG_IC_GO_ALLIANCE_GATE_3))
                    {
                        if (vehicleId == NPC_SIEGE_ENGINE_H)  // target gate directly if siege engine
                        {
                            BgObjective = gate;
                            // LOG_INFO("playerbots", "bot={} (in siege-engine) attack gate", bot->GetName());
                        }
                        else  // target gate directly at range if other vehicle
                        {
                            // just make bot stay where it is if already close
                            // (stops them shifting around between the random spots)
                            if (bot->GetDistance(IC_GATE_ATTACK_POS_HORDE) < 8.0f)
                                pos.Set(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId());
                            else
                                pos.Set(IC_GATE_ATTACK_POS_HORDE.GetPositionX() + frand(-5.0f, +5.0f),
                                        IC_GATE_ATTACK_POS_HORDE.GetPositionY() + frand(-5.0f, +5.0f),
                                        IC_GATE_ATTACK_POS_HORDE.GetPositionZ(), bot->GetMapId());
                            posMap["bg objective"] = pos;
                            // set siege position
                            PositionInfo siegePos = context->GetValue<PositionMap&>("position")->Get()["bg siege"];
                            siegePos.Set(gate->GetPositionX(), gate->GetPositionY(), gate->GetPositionZ(),
                                         bot->GetMapId());
                            posMap["bg siege"] = siegePos;
                            // LOG_INFO("playerbots", "bot={} (in vehicle={}) attack gate", bot->GetName(), vehicleId);
                            return true;
                        }
                    }
                }

                // If gates arent down and not in vehicle, split tasks
                if (!BgObjective && !controlsVehicle && role == 9)  // Capture Side base
                {
                    // Capture Refinery
                    ICNodePoint const& nodePoint = isleOfConquestBG->GetICNodePoint(NODE_TYPE_REFINERY);
                    if (nodePoint.nodeState != NODE_STATE_CONFLICT_H && nodePoint.nodeState != NODE_STATE_CONTROLLED_H)
                    {
                        BgObjective = bg->GetBGObject(BG_IC_GO_REFINERY_BANNER);
                        // LOG_INFO("playerbots", "bot={} attack refinery", bot->GetName());
                    }
                }

                if (!BgObjective && !controlsVehicle && role < 3)  // Capture Docks
                {
                    ICNodePoint const& nodePoint = isleOfConquestBG->GetICNodePoint(NODE_TYPE_DOCKS);
                    if (nodePoint.nodeState != NODE_STATE_CONFLICT_H && nodePoint.nodeState != NODE_STATE_CONTROLLED_H)
                    {
                        if (GameObject* pGO = bg->GetBGObject(BG_IC_GO_DOCKS_BANNER))
                        {
                            BgObjective = pGO;
                            // LOG_INFO("playerbots", "bot={} attack docks", bot->GetName());
                        }
                    }
                }
                if (!BgObjective && !controlsVehicle && role < 6)  // Capture Hangar
                {
                    ICNodePoint const& nodePoint = isleOfConquestBG->GetICNodePoint(NODE_TYPE_HANGAR);
                    if (nodePoint.nodeState != NODE_STATE_CONFLICT_H && nodePoint.nodeState != NODE_STATE_CONTROLLED_H)
                    {
                        if (GameObject* pGO = bg->GetBGObject(BG_IC_GO_HANGAR_BANNER))
                        {
                            BgObjective = pGO;
                            // LOG_INFO("playerbots", "bot={} attack hangar", bot->GetName());
                        }
                    }
                }
                if (!BgObjective && !controlsVehicle)  // Capture Workshop
                {
                    ICNodePoint const& nodePoint = isleOfConquestBG->GetICNodePoint(NODE_TYPE_WORKSHOP);
                    if (nodePoint.nodeState != NODE_STATE_CONFLICT_H && nodePoint.nodeState != NODE_STATE_CONTROLLED_H)
                    {
                        if (GameObject* pGO = bg->GetBGObject(BG_IC_GO_WORKSHOP_BANNER))
                        {
                            BgObjective = pGO;
                            // LOG_INFO("playerbots", "bot={} attack workshop", bot->GetName());
                        }
                    }
                }
                if (!BgObjective)  // Guard point that's not fully capped (also gets them in place to board vehicle)
                {
                    uint32 len = end(IC_AttackObjectives) - begin(IC_AttackObjectives);
                    for (uint32 i = 0; i < len; i++)
                    {
                        auto const& objective =
                            IC_AttackObjectives[(i + role) %
                                                len];  // use role to determine which objective checked first
                        if (isleOfConquestBG->GetICNodePoint(objective.first).nodeState != NODE_STATE_CONTROLLED_H)
                        {
                            if (GameObject* pGO = bg->GetBGObject(objective.second))
                            {
                                BgObjective = pGO;
                                // LOG_INFO("playerbots", "bot={} guard point while it captures", bot->GetName());
                                break;
                            }
                        }
                    }
                }
                if (!BgObjective)  // guard vehicles as they seige
                {
                    // just make bot stay where it is if already close
                    // (stops them shifting around between the random spots)
                    if (bot->GetDistance(IC_GATE_ATTACK_POS_HORDE) < 8.0f)
                        pos.Set(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId());
                    else
                        pos.Set(IC_GATE_ATTACK_POS_HORDE.GetPositionX() + frand(-5.0f, +5.0f),
                                IC_GATE_ATTACK_POS_HORDE.GetPositionY() + frand(-5.0f, +5.0f),
                                IC_GATE_ATTACK_POS_HORDE.GetPositionZ(), bot->GetMapId());
                    posMap["bg objective"] = pos;
                    // LOG_INFO("playerbots", "bot={} guard vehicles as they attack gate", bot->GetName());
                    return true;
                }
            }

            if (bot->GetTeamId() == TEAM_ALLIANCE)  // ALLIANCE
            {
                bool gateOpen = false;
                if (GameObject* pGO = bg->GetBGObject(BG_IC_GO_HORDE_KEEP_PORTCULLIS))
                {
                    if (pGO->isSpawned() && pGO->getLootState() == GO_ACTIVATED)
                    {
                        gateOpen = true;
                        PositionInfo siegePos = context->GetValue<PositionMap&>("position")->Get()["bg siege"];
                        siegePos.Reset();
                        posMap["bg siege"] = siegePos;
                    }
                }

                if (gateOpen && !controlsVehicle &&
                    isleOfConquestBG->GetICNodePoint(NODE_TYPE_GRAVEYARD_H).nodeState ==
                        NODE_STATE_CONTROLLED_A)  // target enemy boss
                {
                    if (Creature* enemyBoss = bg->GetBGCreature(BG_IC_NPC_OVERLORD_AGMAR))
                    {
                        if (enemyBoss->IsVisible())
                        {
                            BgObjective = enemyBoss;
                            // LOG_INFO("playerbots", "bot={} attack boss", bot->GetName());
                        }
                    }
                }

                if (!BgObjective && gateOpen && !controlsVehicle)
                {
                    if (GameObject* pGO = bg->GetBGObject(BG_IC_GO_HORDE_BANNER))  // capture flag within keep
                    {
                        BgObjective = pGO;
                        // LOG_INFO("playerbots", "bot={} attack keep", bot->GetName());
                    }
                }

                if (!BgObjective && !gateOpen && controlsVehicle)  // attack gates
                {
                    // TODO: check for free vehicles
                    if (GameObject* gate = bg->GetBGObject(BG_IC_GO_HORDE_GATE_1))
                    {
                        if (vehicleId == NPC_SIEGE_ENGINE_A)  // target gate directly if siege engine
                        {
                            BgObjective = gate;
                            // LOG_INFO("playerbots", "bot={} (in siege-engine) attack gate", bot->GetName());
                        }
                        else  // target gate directly at range if other vehicle
                        {
                            // just make bot stay where it is if already close
                            // (stops them shifting around between the random spots)
                            if (bot->GetDistance(IC_GATE_ATTACK_POS_ALLIANCE) < 8.0f)
                                pos.Set(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId());
                            else
                                pos.Set(IC_GATE_ATTACK_POS_ALLIANCE.GetPositionX() + frand(-5.0f, +5.0f),
                                        IC_GATE_ATTACK_POS_ALLIANCE.GetPositionY() + frand(-5.0f, +5.0f),
                                        IC_GATE_ATTACK_POS_ALLIANCE.GetPositionZ(), bot->GetMapId());
                            posMap["bg objective"] = pos;
                            // set siege position
                            PositionInfo siegePos = context->GetValue<PositionMap&>("position")->Get()["bg siege"];
                            siegePos.Set(gate->GetPositionX(), gate->GetPositionY(), gate->GetPositionZ(),
                                         bot->GetMapId());
                            posMap["bg siege"] = siegePos;
                            // LOG_INFO("playerbots", "bot={} (in vehicle={}) attack gate", bot->GetName(), vehicleId);
                            return true;
                        }
                    }
                }

                // If gates arent down and not in vehicle, split tasks
                if (!BgObjective && !controlsVehicle && role == 9)  // Capture Side base
                {
                    // Capture Refinery
                    ICNodePoint const& nodePoint = isleOfConquestBG->GetICNodePoint(NODE_TYPE_QUARRY);
                    if (nodePoint.nodeState != NODE_STATE_CONFLICT_A && nodePoint.nodeState != NODE_STATE_CONTROLLED_A)
                    {
                        BgObjective = bg->GetBGObject(BG_IC_GO_QUARRY_BANNER);
                        // LOG_INFO("playerbots", "bot={} attack quarry", bot->GetName());
                    }
                }

                if (!BgObjective && !controlsVehicle && role < 3)  // Capture Docks
                {
                    ICNodePoint const& nodePoint = isleOfConquestBG->GetICNodePoint(NODE_TYPE_DOCKS);
                    if (nodePoint.nodeState != NODE_STATE_CONFLICT_A && nodePoint.nodeState != NODE_STATE_CONTROLLED_A)
                    {
                        if (GameObject* pGO = bg->GetBGObject(BG_IC_GO_DOCKS_BANNER))
                        {
                            BgObjective = pGO;
                            // LOG_INFO("playerbots", "bot={} attack docks", bot->GetName());
                        }
                    }
                }
                if (!BgObjective && !controlsVehicle && role < 6)  // Capture Hangar
                {
                    ICNodePoint const& nodePoint = isleOfConquestBG->GetICNodePoint(NODE_TYPE_HANGAR);
                    if (nodePoint.nodeState != NODE_STATE_CONFLICT_A && nodePoint.nodeState != NODE_STATE_CONTROLLED_A)
                    {
                        if (GameObject* pGO = bg->GetBGObject(BG_IC_GO_HANGAR_BANNER))
                        {
                            BgObjective = pGO;
                            // LOG_INFO("playerbots", "bot={} attack hangar", bot->GetName());
                        }
                    }
                }
                if (!BgObjective && !controlsVehicle)  // Capture Workshop
                {
                    ICNodePoint const& nodePoint = isleOfConquestBG->GetICNodePoint(NODE_TYPE_WORKSHOP);
                    if (nodePoint.nodeState != NODE_STATE_CONFLICT_A && nodePoint.nodeState != NODE_STATE_CONTROLLED_A)
                    {
                        if (GameObject* pGO = bg->GetBGObject(BG_IC_GO_WORKSHOP_BANNER))
                        {
                            BgObjective = pGO;
                            // LOG_INFO("playerbots", "bot={} attack workshop", bot->GetName());
                        }
                    }
                }
                if (!BgObjective)  // Guard point that's not fully capped (also gets them in place to board vehicle)
                {
                    uint32 len = end(IC_AttackObjectives) - begin(IC_AttackObjectives);
                    for (uint32 i = 0; i < len; i++)
                    {
                        auto const& objective =
                            IC_AttackObjectives[(i + role) %
                                                len];  // use role to determine which objective checked first
                        if (isleOfConquestBG->GetICNodePoint(objective.first).nodeState != NODE_STATE_CONTROLLED_H)
                        {
                            if (GameObject* pGO = bg->GetBGObject(objective.second))
                            {
                                BgObjective = pGO;
                                // LOG_INFO("playerbots", "bot={} guard point while it captures", bot->GetName());
                                break;
                            }
                        }
                    }
                }
                if (!BgObjective)  // guard vehicles as they seige
                {
                    // just make bot stay where it is if already close
                    // (stops them shifting around between the random spots)
                    if (bot->GetDistance(IC_GATE_ATTACK_POS_ALLIANCE) < 8.0f)
                        pos.Set(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId());
                    else
                        pos.Set(IC_GATE_ATTACK_POS_ALLIANCE.GetPositionX() + frand(-5.0f, +5.0f),
                                IC_GATE_ATTACK_POS_ALLIANCE.GetPositionY() + frand(-5.0f, +5.0f),
                                IC_GATE_ATTACK_POS_ALLIANCE.GetPositionZ(), bot->GetMapId());
                    posMap["bg objective"] = pos;
                    // LOG_INFO("playerbots", "bot={} guard vehicles as they attack gate", bot->GetName());
                    return true;
                }
            }

            if (BgObjective)
            {
                pos.Set(BgObjective->GetPositionX(), BgObjective->GetPositionY(), BgObjective->GetPositionZ(),
                        bot->GetMapId());
                posMap["bg objective"] = pos;
                return true;
            }
            break;
        }
        default:
            break;
    }

    return false;
}

bool BGTactics::moveToObjective(bool ignoreDist)
{
    Battleground* bg = bot->GetBattleground();
    if (!bg)
        return false;

    BattlegroundTypeId bgType = bg->GetBgTypeID();
    if (bgType == BATTLEGROUND_RB)
        bgType = bg->GetBgTypeID(true);

    PositionInfo pos = context->GetValue<PositionMap&>("position")->Get()["bg objective"];
    if (!pos.isSet())
    {
        bool const selected = selectObjective();
        if (!selected && bgType == BATTLEGROUND_AV && bot->GetTeamId() == TEAM_ALLIANCE)
        {
            uint8 const role = context->GetValue<uint32>("bg role")->Get();
            LogAllianceAVMoveDebug(bot, bg, pos, "no_objective_select_failed", role, 255, false,
                                   AI_VALUE(Unit*, "enemy player target"));
        }
        return selected;
    }
    else
    {
        if (bgType == BATTLEGROUND_AV && bot->GetTeamId() == TEAM_ALLIANCE)
        {
            BattlegroundAV* av = static_cast<BattlegroundAV*>(bg);
            uint8 const role = context->GetValue<uint32>("bg role")->Get();
            AVBotStrategy const strategy = static_cast<AVBotStrategy>(GetBotStrategyForTeam(bg, TEAM_ALLIANCE));
            AVBotStrategy const enemyStrategy = static_cast<AVBotStrategy>(GetBotStrategyForTeam(bg, TEAM_HORDE));
            AllianceAVRushInfo const rushInfo = GetAllianceAVRushInfo(bg, enemyStrategy);
            AllianceAVThreatLevel threat = AV_THREAT_NONE;
            AllianceAVBattlefieldMode mode = AV_MODE_OPENING_CONTROL;
            AllianceAVRushInfo effectiveRushInfo = rushInfo;
            AllianceAVCachedTactics cachedAllianceTactics;
            bool const hasCachedAllianceTactics = TryGetAsyncAllianceAVTactics(bg, av, cachedAllianceTactics);
            if (hasCachedAllianceTactics)
            {
                effectiveRushInfo = cachedAllianceTactics.rushInfo;
                threat = cachedAllianceTactics.threat;
                mode = cachedAllianceTactics.mode;
            }
            else
            {
                threat = GetAllianceAVThreatLevel(av, effectiveRushInfo);
                mode = GetAllianceAVBattlefieldMode(bg, av, effectiveRushInfo, threat);
            }

            uint8 defenderLimit = hasCachedAllianceTactics ?
                cachedAllianceTactics.defenderLimit : GetAVDefenderRoleLimit(TEAM_ALLIANCE, strategy, enemyStrategy, av);
            if (!hasCachedAllianceTactics)
                defenderLimit = std::max<uint8>(defenderLimit,
                                                GetAllianceAVRushDefenderLimit(av, effectiveRushInfo, strategy,
                                                                               enemyStrategy));
            uint32 const towersDown = hasCachedAllianceTactics ?
                cachedAllianceTactics.towersDown : GetAllianceDestroyedHordeTowerCount(av);
            bool const finalDrekPush = strategy == AV_STRATEGY_ALLIANCE_CONTROL_TEMPO &&
                (hasCachedAllianceTactics ? cachedAllianceTactics.finalDrekWindow :
                    AllianceHasFinalDrekWindow(av, effectiveRushInfo, towersDown));
            if (!hasCachedAllianceTactics && finalDrekPush && !AllianceAVShouldFullRecallNorth(av, threat))
            {
                uint8 const finalDefenseCap = AllianceDunBaldarBaseActuallyLost(av) ? 7 : 6;
                defenderLimit = std::min<uint8>(defenderLimit, finalDefenseCap);
            }
            else if (!hasCachedAllianceTactics && AllianceAVCanReleaseIdleNorthReserve(av, effectiveRushInfo, threat, mode))
            {
                defenderLimit = std::min<uint8>(
                    defenderLimit, GetAllianceAVReleasedDefenderLimit(av, effectiveRushInfo, threat, mode));
            }
            bool const fullRecall = hasCachedAllianceTactics ? cachedAllianceTactics.fullRecall :
                AllianceAVShouldFullRecallNorth(av, threat);
            bool isDefender = role < defenderLimit;
            if (isDefender)
            {
                PositionInfo const botPos(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
                                          bot->GetMapId());
                bool const snowfallForward = AllianceAVPositionIsSnowfallRespawn(botPos) ||
                    AllianceAVPositionIsSnowfallRun(bg, botPos);
                bool const snowfallIcebloodForward = snowfallForward && !fullRecall &&
                    strategy == AV_STRATEGY_ALLIANCE_CONTROL_TEMPO &&
                    (mode == AV_MODE_IBGY_PUSH || mode == AV_MODE_IBGY_GUARD ||
                     mode == AV_MODE_IBGY_BREAKTHROUGH ||
                     AllianceAVShouldUseIcebloodBeachheadPlan(bg, av, threat, enemyStrategy));
                if (bot->GetPositionX() <= -462.0f || snowfallIcebloodForward)
                    isDefender = false;
            }

            if (AllianceAVShouldResetCurrentObjective(bot, bg, av, pos, role, isDefender, effectiveRushInfo, threat, mode))
            {
                LogAllianceAVMoveDebug(bot, bg, pos, "reset_current_objective", role, defenderLimit, isDefender,
                                       AI_VALUE(Unit*, "enemy player target"));
                return resetObjective();
            }

            bool const canResetForMapState = !PlayerHasFlag::IsCapturingFlag(bot) && !AllianceAVIsCastingAnySpell(bot);
            if (canResetForMapState && (isDefender || (fullRecall && role < 9)) && effectiveRushInfo.IsActive() &&
                pos.x < -180.0f && !AllianceAVPositionIsSnowfallRun(bg, pos))
            {
                LogAllianceAVMoveDebug(bot, bg, pos, "reset_map_state_rush", role, defenderLimit, isDefender,
                                       AI_VALUE(Unit*, "enemy player target"));
                return resetObjective();
            }

            if (GameObject* emergency = SelectAllianceAVEmergencyDefenseObjective(bot, bg, av, role, isDefender,
                                                                                  strategy, defenderLimit, effectiveRushInfo,
                                                                                  threat))
            {
                float const dx = emergency->GetPositionX() - pos.x;
                float const dy = emergency->GetPositionY() - pos.y;
                if (AllianceAVPositionIsAntiRushRally(pos) || (dx * dx + dy * dy) > 1600.0f)
                {
                    LogAllianceAVMoveDebug(bot, bg, pos, "reset_emergency_defense", role, defenderLimit, isDefender,
                                           AI_VALUE(Unit*, "enemy player target"));
                    return resetObjective();
                }
            }

        }

        // Use portals in Isle of Conquest Base
        if (bgType == BATTLEGROUND_IC)
        {
            if (IsLockedInsideKeep())
                return true;
        }

        float directMoveLimit = 100.0f;
        if (bgType == BATTLEGROUND_AV && bot->GetTeamId() == TEAM_ALLIANCE)
        {
            BattlegroundAV* av = static_cast<BattlegroundAV*>(bg);
            if (AllianceAVPositionIsContestedAllianceDefenseObjective(bg, av, pos) ||
                AllianceAVPositionIsAllianceDefenseRun(bg, pos) ||
                AllianceAVPositionIsAntiRushRally(pos))
                directMoveLimit = 240.0f;
            else if (AllianceAVPositionIsIcebloodGraveRun(pos))
                directMoveLimit = 180.0f;
        }

        if (bgType == BATTLEGROUND_AV)
        {
            PositionInfo const botPos(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId());
            if (AVPositionIsSnowfallHighLowTransition(botPos, pos))
                return selectObjectiveWp(vPaths_AV);
        }

        if (bgType == BATTLEGROUND_AV && IsUnsafeAlteracTowerOrBunkerDirectMove(bot, pos))
        {
            Position accessTarget;
            if (SelectAlteracTowerOrBunkerAccessTarget(bot, pos, accessTarget))
            {
                bool const moved = MoveTo(bot->GetMapId(), accessTarget.GetPositionX(), accessTarget.GetPositionY(),
                                          accessTarget.GetPositionZ());
                if (bot->GetTeamId() == TEAM_ALLIANCE)
                {
                    uint8 const role = context->GetValue<uint32>("bg role")->Get();
                    LogAllianceAVMoveDebug(bot, bg, pos,
                                           moved ? "move_tower_access_direct_true" : "move_tower_access_direct_false",
                                           role, 255, false, AI_VALUE(Unit*, "enemy player target"),
                                           bot->GetDistance2d(accessTarget.GetPositionX(), accessTarget.GetPositionY()),
                                           0);
                }
                return moved;
            }

            if (bot->GetTeamId() == TEAM_ALLIANCE)
            {
                uint8 const role = context->GetValue<uint32>("bg role")->Get();
                LogAllianceAVMoveDebug(bot, bg, pos, "move_tower_direct_blocked", role, 255, false,
                                       AI_VALUE(Unit*, "enemy player target"));
            }
            return false;
        }

        if (!ignoreDist && ServerFacade::instance().IsDistanceGreaterThan(ServerFacade::instance().GetDistance2d(bot, pos.x, pos.y), directMoveLimit))
        {
            // std::ostringstream out;
            // out << "It is too far away! " << pos.x << ", " << pos.y << ", Distance: " <<
            // ServerFacade::instance().GetDistance2d(bot, pos.x, pos.y); bot->Say(out.str(), LANG_UNIVERSAL);
            if (bgType == BATTLEGROUND_AV && bot->GetTeamId() == TEAM_ALLIANCE)
            {
                uint8 const role = context->GetValue<uint32>("bg role")->Get();
                LogAllianceAVMoveDebug(bot, bg, pos, "move_direct_too_far", role, 255, false,
                                       AI_VALUE(Unit*, "enemy player target"));
            }
            return false;
        }

        // don't try to move if already close
        if (bot->GetDistance(pos.x, pos.y, pos.z) < 4.0f)
        {
            resetObjective();
            return true;
        }

        // std::ostringstream out; out << "Moving to objective " << pos.x << ", " << pos.y << ", Distance: " <<
        // ServerFacade::instance().GetDistance2d(bot, pos.x, pos.y); bot->Say(out.str(), LANG_UNIVERSAL);

        // dont increase from 1.5 will cause bugs with horde capping AV towers
        bool const moved = MoveNear(bot->GetMapId(), pos.x, pos.y, pos.z, 1.5f);
        if (bgType == BATTLEGROUND_AV && bot->GetTeamId() == TEAM_ALLIANCE)
        {
            uint8 const role = context->GetValue<uint32>("bg role")->Get();
            LogAllianceAVMoveDebug(bot, bg, pos, moved ? "move_direct_true" : "move_direct_false", role, 255, false,
                                   AI_VALUE(Unit*, "enemy player target"));
        }

        return moved;
    }
    return false;
}

bool BGTactics::selectObjectiveWp(std::vector<BattleBotPath*> const& vPaths)
{
    Battleground* bg = bot->GetBattleground();
    if (!bg)
        return false;

    BattlegroundTypeId bgType = bg->GetBgTypeID();
    if (bgType == BATTLEGROUND_RB)
        bgType = bg->GetBgTypeID(true);

    PositionInfo pos = context->GetValue<PositionMap&>("position")->Get()["bg objective"];
    if (!pos.isSet())
        return false;

    if (bgType == BATTLEGROUND_WS)
    {
        if (wsJumpDown())
            return true;
    }
    else if (bgType == BATTLEGROUND_AV)
    {
        // get bots out of cave when they respawn there (otherwise path selection happens while they're deep within cave
        // and the results arent good)
        Position const caveSpawn = bot->GetTeamId() == TEAM_ALLIANCE ? AV_CAVE_SPAWN_ALLIANCE : AV_CAVE_SPAWN_HORDE;
        if (bot->GetDistance(caveSpawn) < 16.0f)
        {
            return moveToStart(true);
        }

        PositionInfo const botPos(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId());
        if (bot->GetTeamId() == TEAM_ALLIANCE)
        {
            BattlegroundAV* av = static_cast<BattlegroundAV*>(bg);
            if (AllianceAVPositionIsAllianceStartPlateau(botPos) &&
                AllianceAVObjectiveNeedsStartPlateauExit(bg, av, pos))
            {
                BattleBotPath& exitPath = vPath_AV_AllianceSpawn_To_AllianceCrossroad1;
                uint32 closestPoint = 0;
                float closestDistance = FLT_MAX;
                for (uint32 i = 0; i < exitPath.size(); ++i)
                {
                    BattleBotWaypoint const& waypoint = exitPath[i];
                    float const distance = bot->GetDistance(waypoint.x, waypoint.y, waypoint.z);
                    if (distance < closestDistance)
                    {
                        closestDistance = distance;
                        closestPoint = i;
                    }
                }

                uint32 targetPoint = closestPoint;
                if (closestDistance <= 10.0f && closestPoint + 1 < exitPath.size())
                    targetPoint = closestPoint + 1;

                BattleBotWaypoint const& waypoint = exitPath[targetPoint];
                bool const moved = MoveTo(bot->GetMapId(), waypoint.x, waypoint.y, waypoint.z);
                uint8 const role = context->GetValue<uint32>("bg role")->Get();
                LogAllianceAVMoveDebug(bot, bg, pos,
                                       moved ? "move_alliance_start_exit_true" : "move_alliance_start_exit_false",
                                       role, 255, false, AI_VALUE(Unit*, "enemy player target"),
                                       ServerFacade::instance().GetDistance2d(bot, waypoint.x, waypoint.y),
                                       exitPath.size());
                return moved;
            }
        }

        if (bot->GetTeamId() == TEAM_ALLIANCE && AllianceAVPositionIsIcewingBunkerRidge(botPos) &&
            AllianceAVPositionIsStonehearthGraveObjective(bg, pos))
        {
            Position const icewingRoadExit(195.369f, -407.750f, 42.876f);
            if (!AllianceAVPositionIsNearPosition(botPos, icewingRoadExit, 8.0f))
                return MoveTo(bot->GetMapId(), icewingRoadExit.GetPositionX(), icewingRoadExit.GetPositionY(),
                              icewingRoadExit.GetPositionZ(), false, false, false, true);
        }

        bool const snowfallExitApproach =
            AllianceAVPositionIsSnowfallRespawn(botPos) ||
            (botPos.x <= -150.0f && botPos.x >= -215.0f &&
             botPos.y <= 70.0f && botPos.y >= -105.0f &&
             botPos.z >= 60.0f);
        if (snowfallExitApproach && !AVPositionIsSnowfallUpperPlateau(pos))
        {
            BattleBotPath* exitPath = &vPath_AV_SnowfallRespawn_To_SnowfallGraveyard;
            BattleBotWaypoint const& snowfallFlagAnchor = exitPath->back();
            if (bot->GetDistance(snowfallFlagAnchor.x, snowfallFlagAnchor.y, snowfallFlagAnchor.z) > 12.0f)
                return MoveTo(bot->GetMapId(), snowfallFlagAnchor.x, snowfallFlagAnchor.y, snowfallFlagAnchor.z);
        }

        Position accessTarget;
        if (SelectAlteracTowerOrBunkerAccessTarget(bot, pos, accessTarget))
        {
            bool const moved = MoveTo(bot->GetMapId(), accessTarget.GetPositionX(), accessTarget.GetPositionY(),
                                      accessTarget.GetPositionZ());
            if (bot->GetTeamId() == TEAM_ALLIANCE)
            {
                uint8 const role = context->GetValue<uint32>("bg role")->Get();
                LogAllianceAVMoveDebug(bot, bg, pos, moved ? "move_tower_access_true" : "move_tower_access_false",
                                       role, 255, false, AI_VALUE(Unit*, "enemy player target"),
                                       bot->GetDistance2d(accessTarget.GetPositionX(), accessTarget.GetPositionY()),
                                       0);
            }
            return moved;
        }
    }
    else if (bgType == BATTLEGROUND_EY)
    {
        if (eyJumpDown())
            return true;
    }

    float chosenPathScore = FLT_MAX;  // lower score is better
    BattleBotPath* chosenPath = nullptr;
    uint32 chosenPathPoint = 0;
    bool chosenPathReverse = false;
    float nearestPathDistance = FLT_MAX;

    float botDistanceLimit = 50.0f;         // limit for how far path can be from bot
    float botDistanceScoreSubtract = 8.0f;  // path score modifier - lower = less likely to chose a further path (it's
                                            // basically the distance from bot that's ignored)
    float botDistanceScoreMultiply =
        3.0f;  // path score modifier - higher = less likely to chose a further path (it's basically a multiplier on
               // distance from bot - makes distance from bot more signifcant than distance from destination)

    if (bgType == BATTLEGROUND_IC)
    {
        // botDistanceLimit = 80.0f;
        botDistanceScoreMultiply = 8.0f;
        botDistanceScoreSubtract = 2.0f;
    }
    else if (bgType == BATTLEGROUND_AV)
    {
        botDistanceLimit = 80.0f;
        botDistanceScoreMultiply = 8.0f;
    }
    else if (bgType == BATTLEGROUND_AB || bgType == BATTLEGROUND_EY || bgType == BATTLEGROUND_WS)
    {
        botDistanceScoreSubtract = 2.0f;
        botDistanceScoreMultiply = 4.0f;
    }

    bool const avoidIcebloodTowerForGalvangar =
        bot->GetTeamId() == TEAM_ALLIANCE && AllianceAVShouldAvoidIcebloodTowerForGalvangar(bg, pos);

    // uint32 index = -1;
    // uint32 chosenPathIndex = -1;
    for (auto const& path : vPaths)
    {
        if (path == &vPath_AV_AllianceCrossroad2_To_HordeCaptain_Bypass && !avoidIcebloodTowerForGalvangar)
            continue;

        if (avoidIcebloodTowerForGalvangar &&
            (path == &vPath_AV_HordeCrossroad3_To_IcebloodTower || path == &vPath_AV_IcebloodTower_To_HordeCaptain))
            continue;

        // index++;
        // TODO need to remove sqrt from these two and distToBot but it totally throws path scoring out of
        // whack if you do that without changing how its implemented (I'm amazed it works as well as it does
        // using sqrt'ed distances)
        // In a reworked version maybe compare the differences of path distances to point (ie: against best path)
        // or maybe ratio's (where if a path end is twice the difference in distance from destination we basically
        // use that to multiply the total score?
        BattleBotWaypoint& startPoint = ((*path)[0]);
        float const startPointDistToDestination =
            sqrt(Position(pos.x, pos.y, pos.z, 0.f).GetExactDist(startPoint.x, startPoint.y, startPoint.z));
        BattleBotWaypoint& endPoint = ((*path)[path->size() - 1]);
        float const endPointDistToDestination =
            sqrt(Position(pos.x, pos.y, pos.z, 0.f).GetExactDist(endPoint.x, endPoint.y, endPoint.z));

        bool reverse = startPointDistToDestination < endPointDistToDestination;

        // dont travel reverse if it's a reverse paths
        if (reverse && std::find(vPaths_NoReverseAllowed.begin(), vPaths_NoReverseAllowed.end(), path) !=
                           vPaths_NoReverseAllowed.end())
            continue;

        int closestPointIndex = -1;
        float closestPointDistToBot = FLT_MAX;
        for (uint32 i = 0; i < path->size(); i++)
        {
            BattleBotWaypoint& waypoint = ((*path)[i]);
            float const distToBot = sqrt(bot->GetDistance(waypoint.x, waypoint.y, waypoint.z));
            if (closestPointDistToBot > distToBot)
            {
                closestPointDistToBot = distToBot;
                closestPointIndex = i;
            }
        }

        if (closestPointDistToBot < nearestPathDistance)
            nearestPathDistance = closestPointDistToBot;

        // don't pick path where bot is already closest to the paths closest point to target (it means path cant lead it
        // anywhere) don't pick path where closest point is too far away
        uint32 const terminalPathPoint = reverse ? 0u : static_cast<uint32>(path->size() - 1);
        if (closestPointIndex < 0 || static_cast<uint32>(closestPointIndex) == terminalPathPoint ||
            closestPointDistToBot > botDistanceLimit)
            continue;

        // creates a score based on dist-to-bot and dist-to-destination, where lower is better, and dist-to-bot is more
        // important (when its beyond a certain distance) dist-to-bot is more important because otherwise they cant
        // reach it at all (or will fly through air with MM::MovePoint()), also bot may need to use multiple paths (one
        // after another) anyway
        float distToDestination = reverse ? startPointDistToDestination : endPointDistToDestination;
        float pathScore = (closestPointDistToBot < botDistanceScoreSubtract
                               ? 0.0f
                               : ((closestPointDistToBot - botDistanceScoreSubtract) * botDistanceScoreMultiply)) +
                          distToDestination;

        // LOG_INFO("playerbots", "bot={}\t{:6.1f}\t{:4.1f}\t{:4.1f}\t{}", bot->GetName(), pathScore,
        // closestPointDistToBot, distToDestination, vPaths_AB_name[pathNum]);

        if (chosenPathScore > pathScore)
        {
            chosenPathScore = pathScore;
            chosenPath = path;
            chosenPathPoint = closestPointIndex;
            chosenPathReverse = reverse;
            // chosenPathIndex = index;
        }
    }

    if (!chosenPath)
    {
        if (bgType == BATTLEGROUND_AV && bot->GetTeamId() == TEAM_ALLIANCE)
        {
            uint8 const role = context->GetValue<uint32>("bg role")->Get();
            LogAllianceAVMoveDebug(bot, bg, pos, "select_wp_none", role, 255, false,
                                   AI_VALUE(Unit*, "enemy player target"),
                                   nearestPathDistance == FLT_MAX ? -1.0f : nearestPathDistance, vPaths.size());
        }
        return false;
    }

    // LOG_INFO("playerbots", "{} bot={} path={}", (bot->GetTeamId() == TEAM_HORDE ? "HORDE" : "ALLIANCE"),
    // bot->GetName(), chosenPathIndex);

    return moveToObjectiveWp(chosenPath, chosenPathPoint, chosenPathReverse);
}

bool BGTactics::resetObjective()
{
    Battleground* bg = bot->GetBattleground();
    if (!bg)
        return false;

    // Adjust role-change chance based on battleground type
    uint32 oddsToChangeRole = 1;  // default low
    BattlegroundTypeId bgType = bg->GetBgTypeID();

    if (bgType == BATTLEGROUND_WS)
        oddsToChangeRole = 2;
    else if (bgType == BATTLEGROUND_EY || bgType == BATTLEGROUND_IC || bgType == BATTLEGROUND_AB)
        oddsToChangeRole = 1;
    else if (bgType == BATTLEGROUND_AV)
        oddsToChangeRole = 0;

    if (bgType == BATTLEGROUND_AV && bot->GetTeamId() == TEAM_ALLIANCE)
    {
        std::lock_guard<std::mutex> guard(avAllianceObjectiveStallStatesMutex);
        avAllianceObjectiveStallStates.erase(bot->GetGUID().GetCounter());
    }

    bool isCarryingFlag =
        bot->HasAura(BG_WS_SPELL_WARSONG_FLAG) ||
        bot->HasAura(BG_WS_SPELL_SILVERWING_FLAG) ||
        bot->HasAura(BG_EY_NETHERSTORM_FLAG_SPELL);

    // Change role if allowed by odds and not carrying flag
    if (urand(0, 99) < oddsToChangeRole && !isCarryingFlag)
        context->GetValue<uint32>("bg role")->Set(urand(0, 9));

    // Reset objective position
    PositionMap& posMap = context->GetValue<PositionMap&>("position")->Get();
    PositionInfo pos = context->GetValue<PositionMap&>("position")->Get()["bg objective"];
    pos.Reset();
    posMap["bg objective"] = pos;

    return selectObjective(true);
}

bool BGTactics::moveToObjectiveWp(BattleBotPath* const& currentPath, uint32 currentPoint, bool reverse)
{
    if (!currentPath)
        return false;

    uint32 const lastPointInPath = reverse ? 0 : ((*currentPath).size() - 1);

    // NOTE: can't use IsInCombat() when in vehicle as player is stuck in combat forever while in vehicle (ac bug?)
    bool inCombat = bot->GetVehicle() ? (bool)AI_VALUE(Unit*, "enemy player target") : bot->IsInCombat();
    if (inCombat && !PlayerHasFlag::IsCapturingFlag(bot))
    {
        if (ai::pvp::ShouldPrioritizeBattlegroundCapture(botAI))
        {
            context->GetValue<Unit*>("current target")->Set(nullptr);
            bot->AttackStop();
            bot->SetTarget(ObjectGuid::Empty);
            bot->SetSelection(ObjectGuid());
            botAI->ChangeEngine(BOT_STATE_NON_COMBAT);
        }
        else
        {
            Battleground* bg = bot->GetBattleground();
            BattlegroundTypeId bgType = bg ? bg->GetBgTypeID() : BATTLEGROUND_TYPE_NONE;
            if (bgType == BATTLEGROUND_RB && bg)
                bgType = bg->GetBgTypeID(true);

            if (bgType == BATTLEGROUND_AV && bot->GetTeamId() == TEAM_ALLIANCE)
            {
                Unit* currentTarget = context->GetValue<Unit*>("current target")->Get();
                Unit* enemyTarget = AI_VALUE(Unit*, "enemy player target");
                if (AllianceAVShouldBreakIcebloodAssaultCombatLeash(bot, bg, currentTarget, enemyTarget))
                {
                    context->GetValue<Unit*>("current target")->Set(nullptr);
                    if (bot->GetVictim() || currentTarget || enemyTarget)
                    {
                        bot->AttackStop();
                        bot->SetTarget(ObjectGuid::Empty);
                        bot->SetSelection(ObjectGuid());
                        botAI->ChangeEngine(BOT_STATE_NON_COMBAT);
                    }
                }
                else
                    return false;
            }
            else if (bgType == BATTLEGROUND_AV)
                return false;

            if (bgType != BATTLEGROUND_AV)
            {
                resetObjective();
                return false;
            }
        }
    }

    if (currentPoint == lastPointInPath || !bot->IsAlive())
    {
        // Path is over.
        // std::ostringstream out;
        // out << "Reached path end!";
        // bot->Say(out.str(), LANG_UNIVERSAL);
        resetObjective();
        return false;
    }

    uint32 currPoint = currentPoint;

    if (reverse)
        currPoint--;
    else
        currPoint++;

    bool const preciseSnowfallPath = AVPathNeedsPreciseSnowfallMovement(currentPath);
    uint32 const nPoint = preciseSnowfallPath ? currPoint
        : (reverse ? currPoint - std::min(urand(1, 5), currPoint)
                   : std::min(currPoint + urand(1, 5), lastPointInPath));

    BattleBotWaypoint& nextPoint = currentPath->at(nPoint);

    // std::ostringstream out;
    // out << "WP: ";
    // reverse ? out << currPoint << " <<< -> " << nPoint : out << currPoint << ">>> ->" << nPoint;
    // out << ", " << nextPoint.x << ", " << nextPoint.y << " Path Size: " << currentPath->size() << ", Dist: " <<
    // ServerFacade::instance().GetDistance2d(bot, nextPoint.x, nextPoint.y); bot->Say(out.str(), LANG_UNIVERSAL);

    float const waypointJitter = preciseSnowfallPath ? 0.25f : 2.0f;
    float const targetX = nextPoint.x + frand(-waypointJitter, waypointJitter);
    float const targetY = nextPoint.y + frand(-waypointJitter, waypointJitter);
    bool const moved = MoveTo(bot->GetMapId(), targetX, targetY, nextPoint.z);

    if (Battleground* bg = bot->GetBattleground())
    {
        BattlegroundTypeId bgType = bg->GetBgTypeID();
        if (bgType == BATTLEGROUND_RB)
            bgType = bg->GetBgTypeID(true);

        if (bgType == BATTLEGROUND_AV && bot->GetTeamId() == TEAM_ALLIANCE)
        {
            uint8 const role = context->GetValue<uint32>("bg role")->Get();
            PositionInfo const objectivePos = context->GetValue<PositionMap&>("position")->Get()["bg objective"];
            PositionInfo const waypointPos(targetX, targetY, nextPoint.z, bot->GetMapId());
            LogAllianceAVMoveDebug(bot, bg, objectivePos, moved ? "move_wp_true" : "move_wp_false", role, 255, false,
                                   AI_VALUE(Unit*, "enemy player target"),
                                   ServerFacade::instance().GetDistance2d(bot, waypointPos.x, waypointPos.y),
                                   currentPath->size());
        }
    }

    return moved;
}

bool BGTactics::startNewPathBegin(std::vector<BattleBotPath*> const& vPaths)
{
    Battleground* bg = bot->GetBattleground();
    if (!bg)
        return false;

    BattlegroundTypeId bgType = bg->GetBgTypeID();
    if (bgType == BATTLEGROUND_RB)
        bgType = bg->GetBgTypeID(true);

    if (bgType == BATTLEGROUND_IC)
        return false;

    PositionInfo pos = context->GetValue<PositionMap&>("position")->Get()["bg objective"];
    if (!pos.isSet())
        return false;

    // Important for eye of the storm bg, so that bots stay near bg objective (see selectObjective for more Info)
    if (bot->GetDistance(pos.x, pos.y, pos.z) < 25.0f)
        return false;

    struct AvailablePath
    {
        AvailablePath(BattleBotPath* pPath_, bool reverse_) : pPath(pPath_), reverse(reverse_) {}

        BattleBotPath* pPath = nullptr;
        bool reverse = false;
    };

    std::vector<AvailablePath> availablePaths;
    for (auto const& pPath : vPaths)
    {
        // TODO remove sqrt
        BattleBotWaypoint* pStart = &((*pPath)[0]);
        if (sqrt(bot->GetDistance(pStart->x, pStart->y, pStart->z)) < INTERACTION_DISTANCE)
            availablePaths.emplace_back(AvailablePath(pPath, false));

        // Some paths are not allowed backwards.
        if (std::find(vPaths_NoReverseAllowed.begin(), vPaths_NoReverseAllowed.end(), pPath) !=
            vPaths_NoReverseAllowed.end())
            continue;

        // TODO remove sqrt
        BattleBotWaypoint* pEnd = &((*pPath)[(*pPath).size() - 1]);
        if (sqrt(bot->GetDistance(pEnd->x, pEnd->y, pEnd->z)) < INTERACTION_DISTANCE)
            availablePaths.emplace_back(AvailablePath(pPath, true));
    }

    if (availablePaths.empty())
        return false;

    uint32 randomPath = urand(0, availablePaths.size() - 1);
    AvailablePath const* chosenPath = &availablePaths[randomPath];

    BattleBotPath* currentPath = chosenPath->pPath;
    bool reverse = chosenPath->reverse;
    uint32 currentPoint = reverse ? currentPath->size() - 1 : 0;

    return moveToObjectiveWp(currentPath, currentPoint, reverse);
}

bool BGTactics::startNewPathFree(std::vector<BattleBotPath*> const& vPaths)
{
    Battleground* bg = bot->GetBattleground();
    if (!bg)
        return false;

    BattlegroundTypeId bgType = bg->GetBgTypeID();
    if (bgType == BATTLEGROUND_RB)
        bgType = bg->GetBgTypeID(true);

    if (bgType == BATTLEGROUND_IC)
        return false;

    PositionInfo pos = context->GetValue<PositionMap&>("position")->Get()["bg objective"];
    if (!pos.isSet())
        return false;

    // Important for eye of the storm bg, so that bots stay near bg objective (see selectObjective for more Info)
    if (bot->GetDistance(pos.x, pos.y, pos.z) < 25.0f)
        return false;

    BattleBotPath* pClosestPath = nullptr;
    uint32 closestPoint = 0;
    float closestDistance = FLT_MAX;

    for (auto const& pPath : vPaths)
    {
        for (uint32 i = 0; i < pPath->size(); i++)
        {
            BattleBotWaypoint& waypoint = ((*pPath)[i]);
            // TODO remove sqrt
            float const distanceToPoint = sqrt(bot->GetDistance(waypoint.x, waypoint.y, waypoint.z));
            if (distanceToPoint < closestDistance)
            {
                pClosestPath = pPath;
                closestPoint = i;
                closestDistance = distanceToPoint;
            }
        }
    }

    if (!pClosestPath)
        return false;

    BattleBotPath* currentPath = pClosestPath;
    bool reverse = false;
    uint32 currentPoint = closestPoint == 0 ? 0 : closestPoint - 1;
    if (AVPathNeedsPreciseSnowfallMovement(currentPath))
        currentPoint = closestPoint;

    return moveToObjectiveWp(currentPath, currentPoint, reverse);
}

/**
 * @brief Handles flag/base capturing gameplay in battlegrounds
 *
 * This function manages the logic for capturing flags and bases in various battlegrounds.
 * It handles:
 * - Enemy detection and combat near objectives
 * - Coordination with friendly players who are capturing
 * - Different capture mechanics for each battleground type
 * - Proper positioning and movement
 *
 * @param vPaths Vector of possible paths the bot can take
 * @param vFlagIds Vector of flag/base GameObjects that can be captured
 * @return true if handling a flag/base action, false otherwise
 */
bool BGTactics::atFlag(std::vector<BattleBotPath*> const& vPaths, std::vector<uint32> const& vFlagIds)
{
    (void)vPaths;

    // Basic sanity checks
    Battleground* bg = bot->GetBattleground();
    if (!bg)
        return false;

    // Get the actual BG type (in case of random BG)
    BattlegroundTypeId bgType = bg->GetBgTypeID();
    if (bgType == BATTLEGROUND_RB)
        bgType = bg->GetBgTypeID(true);

    // Initialize vectors for nearby objects and players
    GuidVector closeObjects;
    GuidVector closePlayers;
    float flagRange = 0.0f;
    float flagSearchRange = 0.0f;
    uint8 bgRole = 0;
    if (bgType == BATTLEGROUND_AV && bot->GetTeamId() == TEAM_ALLIANCE)
        bgRole = context->GetValue<uint32>("bg role")->Get();

    // Eye of the Storm helpers used later when handling capture positioning
    BattlegroundEY* eyeBg = nullptr;
    GameObject* eyCenterFlag = nullptr;
    if (bgType == BATTLEGROUND_EY)
    {
        eyeBg = static_cast<BattlegroundEY*>(bg);
        if (eyeBg)
            eyCenterFlag = eyeBg->GetBGObject(BG_EY_OBJECT_FLAG_NETHERSTORM);
    }

    // Set up appropriate search ranges and object lists based on BG type
    switch (bgType)
    {
        case BATTLEGROUND_AV:
        {
            closeObjects = *context->GetValue<GuidVector>("nearest game objects no los");
            closePlayers = *context->GetValue<GuidVector>("closest friendly players");
            flagRange = INTERACTION_DISTANCE;
            flagSearchRange = 20.0f;
            break;
        }
        case BATTLEGROUND_AB:
        case BATTLEGROUND_IC:
        {
            // For territory control BGs, use standard interaction range
            closeObjects = *context->GetValue<GuidVector>("closest game objects");
            closePlayers = *context->GetValue<GuidVector>("closest friendly players");
            flagRange = INTERACTION_DISTANCE;
            flagSearchRange = flagRange;
            break;
        }
        case BATTLEGROUND_WS:
        case BATTLEGROUND_EY:
        {
            // For flag carrying BGs, use wider range and ignore LOS
            closeObjects = *context->GetValue<GuidVector>("nearest game objects no los");
            closePlayers = *context->GetValue<GuidVector>("closest friendly players");
            flagRange = 25.0f;
            flagSearchRange = flagRange;
            break;
        }
        default:
            break;
    }

    if (closeObjects.empty())
        return false;

    auto keepStationaryWhileCapturing = [&](CurrentSpellTypes spellType)
    {
        Spell* currentSpell = bot->GetCurrentSpell(spellType);
        if (!currentSpell || !currentSpell->m_spellInfo || currentSpell->m_spellInfo->Id != SPELL_CAPTURE_BANNER)
            return false;

        // If the capture target is no longer available (another bot already captured it), stop channeling
        if (GameObject* targetFlag = currentSpell->m_targets.GetGOTarget())
        {
            if (!targetFlag->isSpawned() || targetFlag->GetGoState() != GO_STATE_READY)
            {
                bot->InterruptNonMeleeSpells(true);
                resetObjective();
                return false;
            }
        }
        else
        {
            bot->InterruptNonMeleeSpells(true);
            resetObjective();
            return false;
        }

        if (bot->IsMounted())
        {
            bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
        }

        if (bot->IsInDisallowedMountForm())
        {
            bot->RemoveAurasByType(SPELL_AURA_MOD_SHAPESHIFT);
        }

        if (bot->isMoving())
        {
            bot->StopMoving();
        }

        return true;
    };

    // If we are already channeling the capture spell, keep the bot stationary and dismounted
    if (keepStationaryWhileCapturing(CURRENT_CHANNELED_SPELL) || keepStationaryWhileCapturing(CURRENT_GENERIC_SPELL))
        return true;

    // First identify which flag/base we're trying to interact with
    GameObject* targetFlag = nullptr;
    for (ObjectGuid const guid : closeObjects)
    {
        GameObject* go = botAI->GetGameObject(guid);
        if (!go)
        {
            continue;
        }

        bool const isEyCenterFlag = eyeBg && eyCenterFlag && eyCenterFlag->GetGUID() == go->GetGUID();

        // Check if this object is a valid capture target
        std::vector<uint32>::const_iterator f = std::find(vFlagIds.begin(), vFlagIds.end(), go->GetEntry());
        if (f == vFlagIds.end() && !isEyCenterFlag)
        {
            continue;
        }

        // Verify the object is active and ready
        if (!go->isSpawned() || go->GetGoState() != GO_STATE_READY)
        {
            continue;
        }

        if (!AllianceAVCanUseNearbyFlagDuringControlTempo(bot, bg, go, bgRole))
            continue;

        if (bgType == BATTLEGROUND_AV && IsWrongFloorAlteracTowerFlag(bot, bg, go))
            continue;

        // Check if we're close enough to approach or interact with the flag.
        float const dist = bot->GetDistance(go);
        if (flagSearchRange && dist > flagSearchRange)
        {
            continue;
        }

        targetFlag = go;
        break;
    }

    if (targetFlag && bgType == BATTLEGROUND_AV)
    {
        float const dist = bot->GetDistance(targetFlag);
        if (dist > flagRange)
            return MoveNear(bot->GetMapId(), targetFlag->GetPositionX(), targetFlag->GetPositionY(),
                            targetFlag->GetPositionZ(), 1.5f);
    }

    // If we found a valid flag/base to interact with, only self-defense may interrupt capture duty.
    if (bgType != BATTLEGROUND_WS && bgType != BATTLEGROUND_AV)
    {
        if (targetFlag)
        {
            Unit* enemyPlayer = AI_VALUE(Unit*, "enemy player target");
            if (enemyPlayer && enemyPlayer->IsAlive() &&
                (ai::pvp::IsSelfDefenseTarget(bot, enemyPlayer) ||
                 ai::pvp::IsCaptureObjectiveThreat(botAI, enemyPlayer)))
            {
                float enemyDist = enemyPlayer->GetDistance(targetFlag);
                if (enemyDist < flagRange * 2.0f)
                {
                    context->GetValue<Unit*>("current target")->Set(enemyPlayer);
                    return false;
                }
            }
        }
    }

    // Check if friendly players are already capturing
    if (!closePlayers.empty() && bgType != BATTLEGROUND_EY)
    {
        // Track number of friendly players capturing and the closest one
        uint32 numCapturing = 0;
        Unit* capturingPlayer = nullptr;
        for (auto& guid : closePlayers)
        {
            if (Unit* pFriend = botAI->GetUnit(guid))
            {
                // Check if they're casting or channeling the capture spell
                Spell* spell = pFriend->GetCurrentSpell(CURRENT_GENERIC_SPELL);
                if (!spell)
                {
                    spell = pFriend->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
                }

                if (spell && spell->m_spellInfo && spell->m_spellInfo->Id == SPELL_CAPTURE_BANNER)
                {
                    numCapturing++;
                    capturingPlayer = pFriend;
                }
            }
        }

        // If friendlies are capturing, stay on the objective instead of switching to free PvP.
        if (numCapturing > 0 && capturingPlayer && bot->GetGUID() != capturingPlayer->GetGUID())
        {
            // Move away if too close to avoid crowding
            if (bot->GetDistance2d(capturingPlayer) < 3.0f)
            {
                float angle = bot->GetAngle(capturingPlayer);
                float x = bot->GetPositionX() + 5.0f * cos(angle);
                float y = bot->GetPositionY() + 5.0f * sin(angle);
                MoveTo(bot->GetMapId(), x, y, bot->GetPositionZ());
            }

            botAI->SetNextCheckDelay(250);
            return true;
        }
    }

    // Area is clear of enemies and no friendlies are capturing
    // Proceed with capture mechanics
    for (ObjectGuid const guid : closeObjects)
    {
        GameObject* go = botAI->GetGameObject(guid);
        if (!go)
            continue;

        bool const isEyCenterFlag = eyeBg && eyCenterFlag && eyCenterFlag->GetGUID() == go->GetGUID();

        // Validate this is a capture target
        std::vector<uint32>::const_iterator f = std::find(vFlagIds.begin(), vFlagIds.end(), go->GetEntry());
        if (f == vFlagIds.end() && !isEyCenterFlag)
            continue;

        // Check object is active
        if (!go->isSpawned() || go->GetGoState() != GO_STATE_READY)
            continue;

        if (!AllianceAVCanUseNearbyFlagDuringControlTempo(bot, bg, go, bgRole))
            continue;

        if (bgType == BATTLEGROUND_AV && IsWrongFloorAlteracTowerFlag(bot, bg, go))
            continue;

        // Verify we can interact with it
        if (!bot->CanUseBattlegroundObject(go) && bgType != BATTLEGROUND_WS)
            continue;

        float const dist = bot->GetDistance(go);
        if (flagRange && dist > flagRange)
            continue;

        // Special handling for WSG and EY base flags
        bool isWsBaseFlag = bgType == BATTLEGROUND_WS && go->GetEntry() == vFlagsWS[bot->GetTeamId()];
        bool isEyBaseFlag = bgType == BATTLEGROUND_EY && go->GetEntry() == vFlagsEY[0];

        // Ensure bots are inside the Eye of the Storm capture circle before casting
        if (bgType == BATTLEGROUND_EY)
        {
            GameObject* captureFlag = (isEyBaseFlag && eyCenterFlag) ? eyCenterFlag : go;
            float const requiredRange = 2.5f;
            if (!bot->IsWithinDistInMap(captureFlag, requiredRange))
            {
                // Stay mounted while relocating to avoid mount/dismount loops
                return MoveTo(bot->GetMapId(), captureFlag->GetPositionX(), captureFlag->GetPositionY(),
                              captureFlag->GetPositionZ());
            }

            // Once inside the circle, dismount and stop before starting the channel
            if (bot->IsMounted())
            {
                bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
            }

            if (bot->IsInDisallowedMountForm())
            {
                bot->RemoveAurasByType(SPELL_AURA_MOD_SHAPESHIFT);
            }

            if (bot->isMoving())
            {
                bot->StopMoving();
            }
        }

        // Don't capture own flag in WSG unless carrying enemy flag
        if (isWsBaseFlag && bgType == BATTLEGROUND_WS &&
            !(bot->HasAura(BG_WS_SPELL_WARSONG_FLAG) || bot->HasAura(BG_WS_SPELL_SILVERWING_FLAG)))
            continue;

        // Handle capture mechanics based on BG type
        switch (bgType)
        {
            case BATTLEGROUND_AV:
            case BATTLEGROUND_AB:
            case BATTLEGROUND_IC:
            {
                // Prevent capturing from inside flag pole
                if (dist < 0.75f)
                {
                    float const moveDist = bot->GetObjectSize() + go->GetObjectSize() + 0.1f;
                    return MoveTo(bot->GetMapId(), go->GetPositionX() + (urand(0, 1) ? -moveDist : moveDist),
                                  go->GetPositionY() + (urand(0, 1) ? -moveDist : moveDist), go->GetPositionZ());
                }

                // Dismount before capturing
                if (bot->IsMounted())
                    bot->RemoveAurasByType(SPELL_AURA_MOUNTED);

                if (bot->IsInDisallowedMountForm())
                    bot->RemoveAurasByType(SPELL_AURA_MOD_SHAPESHIFT);

                // Cast the capture spell
                SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(SPELL_CAPTURE_BANNER);
                if (!spellInfo)
                    return false;

                Spell* spell = new Spell(bot, spellInfo, TRIGGERED_NONE);
                spell->m_targets.SetGOTarget(go);
                spell->prepare(&spell->m_targets);

                botAI->WaitForSpellCast(spell);

                resetObjective();
                return true;
            }
            case BATTLEGROUND_WS:
            {
                if (dist < INTERACTION_DISTANCE)
                {
                    // Handle flag capture at base
                    if (isWsBaseFlag)
                    {
                        if (bot->GetTeamId() == TEAM_HORDE)
                        {
                            WorldPacket data(CMSG_AREATRIGGER);
                            data << uint32(BG_WS_TRIGGER_HORDE_FLAG_SPAWN);
                            bot->GetSession()->HandleAreaTriggerOpcode(data);
                        }
                        else
                        {
                            WorldPacket data(CMSG_AREATRIGGER);
                            data << uint32(BG_WS_TRIGGER_ALLIANCE_FLAG_SPAWN);
                            bot->GetSession()->HandleAreaTriggerOpcode(data);
                        }
                        return true;
                    }

                    // Dismount before picking up flag
                    if (bot->IsMounted())
                        bot->RemoveAurasByType(SPELL_AURA_MOUNTED);

                    if (bot->IsInDisallowedMountForm())
                        bot->RemoveAurasByType(SPELL_AURA_MOD_SHAPESHIFT);

                    // Pick up the flag
                    WorldPacket data(CMSG_GAMEOBJ_USE);
                    data << go->GetGUID();
                    bot->GetSession()->HandleGameObjectUseOpcode(data);

                    resetObjective();
                    return true;
                }
                else
                {
                    // Move to flag if not in range
                    return MoveTo(bot->GetMapId(), go->GetPositionX(), go->GetPositionY(), go->GetPositionZ());
                }
            }
            case BATTLEGROUND_EY:
            {  // Handle Netherstorm flag capture requiring a channel
                if (dist < INTERACTION_DISTANCE)
                {
                    // Dismount before interacting
                    if (bot->IsMounted())
                    {
                        bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
                    }

                    if (bot->IsInDisallowedMountForm())
                    {
                        bot->RemoveAurasByType(SPELL_AURA_MOD_SHAPESHIFT);
                    }

                    // Handle center flag differently (requires spell cast)
                    if (isEyCenterFlag)
                    {
                        for (uint8 type = CURRENT_MELEE_SPELL; type <= CURRENT_CHANNELED_SPELL; ++type)
                        {
                            if (Spell* currentSpell = bot->GetCurrentSpell(static_cast<CurrentSpellTypes>(type)))
                            {
                                // m_spellInfo may be null in some states: protect access
                                if (currentSpell->m_spellInfo && currentSpell->m_spellInfo->Id == SPELL_CAPTURE_BANNER)
                                {
                                    bot->StopMoving();
                                    botAI->SetNextCheckDelay(500);
                                    return true;
                                }
                            }
                        }

                        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(SPELL_CAPTURE_BANNER);
                        if (!spellInfo)
                            return false;

                        Spell* spell = new Spell(bot, spellInfo, TRIGGERED_NONE);
                        spell->m_targets.SetGOTarget(go);

                        bot->StopMoving();
                        spell->prepare(&spell->m_targets);

                        botAI->WaitForSpellCast(spell);
                        resetObjective();
                        return true;
                    }

                    // Pick up dropped flag
                    WorldPacket data(CMSG_GAMEOBJ_USE);
                    data << go->GetGUID();
                    bot->GetSession()->HandleGameObjectUseOpcode(data);

                    resetObjective();
                    return true;
                }
                else
                {
                    // Move to flag if not in range
                    return MoveTo(bot->GetMapId(), go->GetPositionX(), go->GetPositionY(), go->GetPositionZ());
                }
            }
            default:
                break;
        }
    }

    return false;
}

bool BGTactics::flagTaken()
{
    BattlegroundWS* bg = (BattlegroundWS*)bot->GetBattleground();
    if (!bg)
        return false;

    return !bg->GetFlagPickerGUID(bg->GetOtherTeamId(bot->GetTeamId())).IsEmpty();
}

bool BGTactics::teamFlagTaken()
{
    BattlegroundWS* bg = (BattlegroundWS*)bot->GetBattleground();
    if (!bg)
        return false;

    return !bg->GetFlagPickerGUID(bot->GetTeamId()).IsEmpty();
}

bool BGTactics::protectFC()
{
    Battleground* bg = bot->GetBattleground();
    if (!bg)
        return false;

    Unit* teamFC = AI_VALUE(Unit*, "team flag carrier");

    if (!teamFC || teamFC == bot)
    {
        return false;
    }

    if (!bot->IsInCombat() && !bot->IsWithinDistInMap(teamFC, 20.0f))
    {
        // Get the flag carrier's position
        float fcX = teamFC->GetPositionX();
        float fcY = teamFC->GetPositionY();
        float fcZ = teamFC->GetPositionZ();
        uint32 mapId = bot->GetMapId();

        return MoveNear(mapId, fcX, fcY, fcZ, 5.0f, MovementPriority::MOVEMENT_NORMAL);
    }

    return false;
}

bool BGTactics::useBuff()
{
    Battleground* bg = bot->GetBattleground();
    if (!bg)
        return false;

    BattlegroundTypeId bgType = bg->GetBgTypeID();
    if (bgType == BATTLEGROUND_RB)
        bgType = bg->GetBgTypeID(true);

    GuidVector closeObjects = AI_VALUE(GuidVector, "nearest game objects no los");
    if (closeObjects.empty())
        return false;

    bool needRegen = bot->GetHealthPct() < sPlayerbotAIConfig.mediumHealth ||
                     (AI_VALUE2(bool, "has mana", "self target") &&
                      AI_VALUE2(uint8, "mana", "self target") < sPlayerbotAIConfig.mediumMana);
    bool needSpeed = (bgType != BATTLEGROUND_WS || bot->HasAura(BG_WS_SPELL_WARSONG_FLAG) ||
                      bot->HasAura(BG_WS_SPELL_SILVERWING_FLAG) || bot->HasAura(BG_EY_NETHERSTORM_FLAG_SPELL)) ||
                     !(teamFlagTaken() || flagTaken());
    bool foundBuff = false;

    for (ObjectGuid const guid : closeObjects)
    {
        GameObject* go = botAI->GetGameObject(guid);
        if (!go)
            continue;

        if (!go->isSpawned())
            continue;

        // use speed buff only if close
        if (ServerFacade::instance().IsDistanceGreaterThan(ServerFacade::instance().GetDistance2d(bot, go),
                                                 go->GetEntry() == Buff_Entries[0] ? 20.0f : 50.0f))
            continue;

        if (needSpeed && go->GetEntry() == Buff_Entries[0])
            foundBuff = true;

        if (needRegen && go->GetEntry() == Buff_Entries[1])
            foundBuff = true;

        // do not move to Berserk buff if bot is healer or has flag
        if (!(bot->HasAura(BG_WS_SPELL_WARSONG_FLAG) || bot->HasAura(BG_WS_SPELL_SILVERWING_FLAG) ||
              bot->HasAura(BG_EY_NETHERSTORM_FLAG_SPELL)) &&
            !botAI->IsHeal(bot) && go->GetEntry() == Buff_Entries[2])
            foundBuff = true;

        if (foundBuff)
        {
            // std::ostringstream out;
            // out << "Moving to buff...";
            // bot->Say(out.str(), LANG_UNIVERSAL);
            return MoveTo(go->GetMapId(), go->GetPositionX(), go->GetPositionY(), go->GetPositionZ());
        }
    }

    return false;
}

uint32 BGTactics::getPlayersInArea(TeamId teamId, Position point, float range, bool combat)
{
    uint32 defCount = 0;

    if (!bot->InBattleground())
        return false;

    Battleground* bg = bot->GetBattleground();
    if (!bg)
        return 0;

    uint32 cachedCount = 0;
    if (TryGetAsyncAVPlayersNear(bg, teamId, point.GetPositionX(), point.GetPositionY(), range, true, !combat,
                                 cachedCount))
        return cachedCount;

    for (auto& guid : bg->GetBgMap()->GetPlayers())
    {
        Player* player = guid.GetSource();
        if (!player)
            continue;

        if (player->IsAlive() && (teamId == TEAM_NEUTRAL || teamId == player->GetTeamId()))
        {
            if (!combat && player->IsInCombat())
                continue;

            if (ServerFacade::instance().GetDistance2d(player, point.GetPositionX(), point.GetPositionY()) < range)
                ++defCount;
        }
    }

    return defCount;
}

// check Isle of Conquest Keep position
bool BGTactics::IsLockedInsideKeep()
{
    Battleground* bg = bot->GetBattleground();
    if (!bg)
        return false;

    BattlegroundTypeId bgType = bg->GetBgTypeID();
    if (bgType == BATTLEGROUND_RB)
        bgType = bg->GetBgTypeID(true);

    if (bgType != BATTLEGROUND_IC)
        return false;

    bool isInside = false;
    if (bot->GetTeamId() == TEAM_ALLIANCE && bot->GetPositionX() < 410.0f && bot->GetPositionY() > -900.0f &&
        bot->GetPositionY() < -765.0f)
        isInside = true;
    if (bot->GetTeamId() == TEAM_HORDE && bot->GetPositionX() > 1153.0f && bot->GetPositionY() > -849.0f &&
        bot->GetPositionY() < -679.0f)
        isInside = true;

    if (!isInside)
        return false;

    GuidVector closeObjects;
    closeObjects = *context->GetValue<GuidVector>("nearest game objects no los");
    if (closeObjects.empty())
        return moveToStart(true);

    GameObject* closestPortal = nullptr;
    float closestDistance = 100.0f;
    bool gateLock = false;

    // check inner gates status
    // ALLIANCE
    if (bot->GetTeamId() == TEAM_ALLIANCE)
    {
        if (GameObject* go = bg->GetBGObject(BG_IC_GO_DOODAD_PORTCULLISACTIVE02))
        {
            if (go->isSpawned())
            {
                gateLock = go->getLootState() != GO_ACTIVATED;
            }
            else
            {
                gateLock = false;
            }
        }
    }
    // HORDE
    if (bot->GetTeamId() == TEAM_HORDE)
    {
        if (GameObject* go = bg->GetBGObject(BG_IC_GO_HORDE_KEEP_PORTCULLIS))
        {
            if (go->isSpawned())
            {
                gateLock = go->getLootState() != GO_ACTIVATED;
            }
            else
            {
                gateLock = false;
            }
        }
    }

    for (ObjectGuid const& guid : closeObjects)
    {
        GameObject* go = botAI->GetGameObject(guid);
        if (!go)
            continue;

        // ALLIANCE
        // get closest portal
        if (bot->GetTeamId() == TEAM_ALLIANCE && go->GetEntry() == GO_TELEPORTER_4)
        {
            float tempDist = ServerFacade::instance().GetDistance2d(bot, go->GetPositionX(), go->GetPositionY());

            if (ServerFacade::instance().IsDistanceLessThan(tempDist, closestDistance))
            {
                closestDistance = tempDist;
                closestPortal = go;
            }
        }

        // HORDE
        // get closest portal
        if (bot->GetTeamId() == TEAM_HORDE && go->GetEntry() == GO_TELEPORTER_2)
        {
            float tempDist = ServerFacade::instance().GetDistance2d(bot, go->GetPositionX(), go->GetPositionY());

            if (ServerFacade::instance().IsDistanceLessThan(tempDist, closestDistance))
            {
                closestDistance = tempDist;
                closestPortal = go;
            }
        }
    }

    // portal not found, move closer
    if (gateLock && !closestPortal)
        return moveToStart(true);

    // portal not found, move closer
    if (!gateLock && !closestPortal)
        return moveToStart(true);

    // nothing found, allow move through
    if (!gateLock || !closestPortal)
        return false;

    // portal found
    if (closestPortal)
    {
        // if close
        if (bot->IsWithinDistInMap(closestPortal, INTERACTION_DISTANCE))
        {
            WorldPacket data(CMSG_GAMEOBJ_USE);
            data << closestPortal->GetGUID();
            bot->GetSession()->HandleGameObjectUseOpcode(data);
            return true;
        }
        else
        {
            return MoveTo(bot->GetMapId(), closestPortal->GetPositionX(), closestPortal->GetPositionY(),
                          closestPortal->GetPositionZ());
        }
    }

    return moveToStart(true);

    return false;
}

bool ArenaTactics::Execute(Event /*event*/)
{
    if (!bot->InBattleground())
    {
        bool IsRandomBot = sRandomPlayerbotMgr.IsRandomBot(bot->GetGUID().GetCounter());
        botAI->ChangeStrategy("-arena", BOT_STATE_COMBAT);
        botAI->ChangeStrategy("-arena", BOT_STATE_NON_COMBAT);
        botAI->ResetStrategies(!IsRandomBot);
        return false;
    }

    Battleground* bg = bot->GetBattleground();
    if (!bg)
        return false;

    if (bg->GetStatus() == STATUS_WAIT_LEAVE)
        return BGStatusAction::LeaveBG(botAI);

    if (bg->GetStatus() != STATUS_IN_PROGRESS)
        return false;

    if (bot->isDead())
        return false;

    if (bot->isMoving())
        return false;

    // startup phase
    if (bg->GetStartDelayTime() > 0)
        return false;

    if (botAI->HasStrategy("collision", BOT_STATE_NON_COMBAT))
        botAI->ChangeStrategy("-collision", BOT_STATE_NON_COMBAT);

    if (botAI->HasStrategy("buff", BOT_STATE_NON_COMBAT))
        botAI->ChangeStrategy("-buff", BOT_STATE_NON_COMBAT);

    Unit* target = bot->GetVictim();
    if (target)
    {
        bool losBlocked = !bot->IsWithinLOSInMap(target) || fabs(bot->GetPositionZ() - target->GetPositionZ()) > 5.0f;

        if (losBlocked)
        {
            PathGenerator path(bot);
            path.CalculatePath(target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(), false);

            if (path.GetPathType() != PATHFIND_NOPATH)
            {
                // If you are casting a spell and lost your target due to LoS, interrupt the cast and move
                if (bot->IsNonMeleeSpellCast(false, true, true, false, true))
                    bot->InterruptNonMeleeSpells(true);

                float x, y, z;
                target->GetPosition(x, y, z);
                botAI->TellMasterNoFacing("Repositioning to exit the LoS target!");
                return MoveTo(target->GetMapId(), x + frand(-1, +1), y + frand(-1, +1), z, false, true);
            }
        }
    }

    if (!bot->IsInCombat())
        return moveToCenter(bg);

    return true;
}

bool ArenaTactics::moveToCenter(Battleground* bg)
{
    // Sanity check
    if (!bg)
    {
        return true;
    }
    uint32 Preference = 6;

    switch (bot->getClass())
    {
        case CLASS_PRIEST:
        case CLASS_SHAMAN:
        case CLASS_DRUID:
            Preference = 3;
            break;
        case CLASS_WARRIOR:
        case CLASS_PALADIN:
        case CLASS_ROGUE:
        case CLASS_DEATH_KNIGHT:
            Preference = 6;
            break;
        case CLASS_HUNTER:
        case CLASS_MAGE:
        case CLASS_WARLOCK:
            Preference = 9;
            break;
    }

    switch (bg->GetBgTypeID())
    {
        case BATTLEGROUND_BE:
            if (bg->GetTeamStartPosition(bot->GetBgTeamId())->GetPositionY() < 240)
            {
                if (Preference == 3)
                    MoveTo(bg->GetMapId(), 6226.65f + frand(-1, +1), 264.36f + frand(-1, +1), 1.31f, false, true);
                else if (Preference == 6)
                    MoveTo(bg->GetMapId(), 6239.89f + frand(-1, +1), 261.11f + frand(-1, +1), 0.89f, false, true);
                else
                    MoveTo(bg->GetMapId(), 6235.60f + frand(-1, +1), 258.27f + frand(-1, +1), 0.89f, false, true);
            }
            else
            {
                if (Preference == 3)
                    MoveTo(bg->GetMapId(), 6265.72f + frand(-1, +1), 271.92f + frand(-1, +1), 3.65f, false, true);
                else if (Preference == 6)
                    MoveTo(bg->GetMapId(), 6239.89f + frand(-1, +1), 261.11f + frand(-1, +1), 0.89f, false, true);
                else
                    MoveTo(bg->GetMapId(), 6250.50f + frand(-1, +1), 266.66f + frand(-1, +1), 2.63f, false, true);
            }
            break;
        case BATTLEGROUND_RL:
            if (bg->GetTeamStartPosition(bot->GetBgTeamId())->GetPositionY() < 1600)
            {
                if (Preference == 3)
                    MoveTo(bg->GetMapId(), 1262.14f + frand(-1, +1), 1657.63f + frand(-1, +1), 33.76f, false, true);
                else if (Preference == 6)
                    MoveTo(bg->GetMapId(), 1266.85f + frand(-1, +1), 1663.52f + frand(-1, +1), 34.04f, false, true);
                else
                    MoveTo(bg->GetMapId(), 1274.07f + frand(-1, +1), 1656.36f + frand(-1, +1), 34.58f, false, true);
            }
            else
            {
                if (Preference == 3)
                    MoveTo(bg->GetMapId(), 1261.93f + frand(-1, +1), 1669.27f + frand(-1, +1), 34.25f, false, true);
                else if (Preference == 6)
                    MoveTo(bg->GetMapId(), 1266.85f + frand(-1, +1), 1663.52f + frand(-1, +1), 34.04f, false, true);
                else
                    MoveTo(bg->GetMapId(), 1266.37f + frand(-1, +1), 1672.40f + frand(-1, +1), 34.21f, false, true);
            }
            break;
        case BATTLEGROUND_NA:
            if (bg->GetTeamStartPosition(bot->GetBgTeamId())->GetPositionY() < 2870)
            {
                if (Preference == 3)
                    MoveTo(bg->GetMapId(), 4068.85f + frand(-1, +1), 2911.98f + frand(-1, +1), 12.99f, false, true);
                else if (Preference == 6)
                    MoveTo(bg->GetMapId(), 4056.99f + frand(-1, +1), 2919.75f + frand(-1, +1), 13.51f, false, true);
                else
                    MoveTo(bg->GetMapId(), 4056.27f + frand(-1, +1), 2905.33f + frand(-1, +1), 12.90f, false, true);
            }
            else
            {
                if (Preference == 3)
                    MoveTo(bg->GetMapId(), 4043.66f + frand(-1, +1), 2927.93f + frand(-1, +1), 13.17f, false, true);
                else if (Preference == 6)
                    MoveTo(bg->GetMapId(), 4056.99f + frand(-1, +1), 2919.75f + frand(-1, +1), 13.51f, false, true);
                else
                    MoveTo(bg->GetMapId(), 4054.80f + frand(-1, +1), 2934.28f + frand(-1, +1), 13.72f, false, true);
            }
            break;
        case BATTLEGROUND_DS:
            if (!MoveTo(bg->GetMapId(), 1291.58f + frand(-5, +5), 790.87f + frand(-5, +5), 7.8f, false, true))
            {
                // they like to hang around at the tip of the pipes doing nothing, so we just teleport them down
                if (bot->GetDistance(1333.07f, 817.18f, 13.35f) < 4)
                {
                    bot->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_TELEPORTED | AURA_INTERRUPT_FLAG_CHANGE_MAP);
                    bot->TeleportTo(bg->GetMapId(), 1330.96f, 816.75f, 3.2f, bot->GetOrientation());
                }
                if (bot->GetDistance(1250.13f, 764.79f, 13.34f) < 4)
                {
                    bot->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_TELEPORTED | AURA_INTERRUPT_FLAG_CHANGE_MAP);
                    bot->TeleportTo(bg->GetMapId(), 1252.19f, 765.41f, 3.2f, bot->GetOrientation());
                }
            }
            break;
        case BATTLEGROUND_RV:
            MoveTo(bg->GetMapId(), 764.65f + frand(-2, +2), -283.85f + frand(-2, +2), 28.28f, false, true);
            break;
        default:
            break;
    }

    return true;
}
