/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Playerbots.h"

#include "BattlefieldScript.h"
#include "Channel.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "DatabaseLoader.h"
#include "GlobalScript.h"
#include "GuildTaskMgr.h"
#include "LFG.h"
#include "Map.h"
#include "PlayerScript.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotGuildMgr.h"
#include "PlayerbotSpellRepository.h"
#include "PlayerbotWorldThreadProcessor.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"
#include "PlayerbotCommandScript.h"
#include "cmath"
#include "BattleGroundTactics.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace
{
thread_local bool zoneParallelBotAIWorkerThread = false;

class ZoneParallelBotAIWorkerScope
{
public:
    ZoneParallelBotAIWorkerScope() { zoneParallelBotAIWorkerThread = true; }
    ~ZoneParallelBotAIWorkerScope() { zoneParallelBotAIWorkerThread = false; }
};

class ZoneParallelBotAIUpdatePool
{
public:
    ~ZoneParallelBotAIUpdatePool()
    {
        Stop();
    }

    void Run(std::vector<std::function<void()>>&& jobs, uint32 threadCount)
    {
        if (jobs.empty())
            return;

        std::unique_lock<std::mutex> runLock(_runMutex);
        EnsureStarted(std::max<uint32>(1, threadCount));

        {
            std::lock_guard<std::mutex> lock(_queueMutex);
            _pending += jobs.size();
            for (std::function<void()>& job : jobs)
                _jobs.emplace_back(std::move(job));
        }

        _queueCv.notify_all();

        std::unique_lock<std::mutex> lock(_queueMutex);
        _doneCv.wait(lock, [this]() { return _pending == 0 && _jobs.empty(); });
    }

    void Stop()
    {
        {
            std::lock_guard<std::mutex> runLock(_runMutex);
            {
                std::lock_guard<std::mutex> lock(_queueMutex);
                _stop = true;
                _jobs.clear();
                _pending = 0;
            }

            _queueCv.notify_all();
            _doneCv.notify_all();

            for (std::thread& worker : _workers)
                if (worker.joinable())
                    worker.join();

            _workers.clear();
            _threadCount = 0;
            _stop = false;
        }
    }

private:
    void EnsureStarted(uint32 threadCount)
    {
        threadCount = std::max<uint32>(1, threadCount);

        if (_threadCount == threadCount && !_workers.empty())
            return;

        StopWorkersLocked();

        _stop = false;
        _threadCount = threadCount;
        _workers.reserve(threadCount);

        for (uint32 i = 0; i < threadCount; ++i)
            _workers.emplace_back(&ZoneParallelBotAIUpdatePool::WorkerLoop, this);

        LOG_INFO("playerbots", "ZoneParallelBotAI started with {} worker thread(s)", threadCount);
    }

    void StopWorkersLocked()
    {
        {
            std::lock_guard<std::mutex> lock(_queueMutex);
            _stop = true;
            _jobs.clear();
            _pending = 0;
        }

        _queueCv.notify_all();
        _doneCv.notify_all();

        for (std::thread& worker : _workers)
            if (worker.joinable())
                worker.join();

        _workers.clear();
        _threadCount = 0;
    }

    void WorkerLoop()
    {
        for (;;)
        {
            std::function<void()> job;

            {
                std::unique_lock<std::mutex> lock(_queueMutex);
                _queueCv.wait(lock, [this]() { return _stop || !_jobs.empty(); });

                if (_stop && _jobs.empty())
                    return;

                job = std::move(_jobs.front());
                _jobs.pop_front();
            }

            {
                ZoneParallelBotAIWorkerScope workerScope;
                job();
            }

            {
                std::lock_guard<std::mutex> lock(_queueMutex);
                if (_pending > 0)
                    --_pending;

                if (_pending == 0 && _jobs.empty())
                    _doneCv.notify_all();
            }
        }
    }

    std::mutex _runMutex;
    std::mutex _queueMutex;
    std::condition_variable _queueCv;
    std::condition_variable _doneCv;
    std::deque<std::function<void()>> _jobs;
    std::vector<std::thread> _workers;
    size_t _pending = 0;
    uint32 _threadCount = 0;
    bool _stop = false;
};

ZoneParallelBotAIUpdatePool& GetZoneParallelBotAIPool()
{
    static ZoneParallelBotAIUpdatePool pool;
    return pool;
}

struct ZoneBotAIEntry
{
    Player* bot;
    PlayerbotAI* ai;
};

uint32 GetZoneParallelBotAIThreadCount()
{
    uint32 threadCount = sPlayerbotAIConfig.zoneParallelBotAIThreads;

    if (threadCount == 0)
        threadCount = std::max<uint32>(1, std::thread::hardware_concurrency());

    return std::max<uint32>(1, threadCount);
}

bool IsZoneParallelBotAIMap(Map const* map)
{
    return sPlayerbotAIConfig.zoneParallelBotAI && map && map->IsWorldMap() && !map->Instanceable() &&
           !map->IsBattlegroundOrArena() && !map->IsDungeon();
}

bool ShouldDeferBotAIToZoneParallelUpdate(Player* player, PlayerbotAI* botAI)
{
    return botAI && !botAI->IsRealPlayer() && player && player->IsInWorld() && sRandomPlayerbotMgr.IsRandomBot(player) &&
           IsZoneParallelBotAIMap(player->GetMap());
}

void UpdateBotAI(ZoneBotAIEntry const& entry, uint32 diff)
{
    if (!entry.bot || !entry.bot->IsInWorld() || !entry.ai)
        return;

    if (entry.ai->IsRealPlayer())
        return;

    entry.ai->UpdateAI(diff);
}

void UpdateZoneParallelBotAI(uint32 diff)
{
    std::unordered_map<uint32, std::vector<ZoneBotAIEntry>> botsByZone;
    botsByZone.reserve(64);
    size_t totalBots = 0;
    PlayerBotMap const bots = sRandomPlayerbotMgr.GetAllBots();

    for (PlayerBotMap::const_iterator itr = bots.begin(); itr != bots.end(); ++itr)
    {
        Player* const player = itr->second;

        if (!player || !player->IsInWorld() || !sRandomPlayerbotMgr.IsRandomBot(player))
            continue;

        Map* const map = player->GetMap();
        if (!IsZoneParallelBotAIMap(map))
            continue;

        PlayerbotAI* const botAI = PlayerbotsMgr::instance().GetPlayerbotAI(player);

        if (!botAI || botAI->IsRealPlayer())
            continue;

        uint32 const mapId = player->GetMapId();
        uint32 const zoneId = player->GetZoneId();
        uint32 const bucket = (mapId << 16) ^ zoneId;
        botsByZone[bucket].push_back({player, botAI});
        ++totalBots;
    }

    if (totalBots == 0)
        return;

    if (totalBots < sPlayerbotAIConfig.zoneParallelBotAIMinBots || botsByZone.size() < 2)
    {
        for (auto& zoneBots : botsByZone)
            for (ZoneBotAIEntry const& bot : zoneBots.second)
                UpdateBotAI(bot, diff);

        return;
    }

    std::vector<std::function<void()>> jobs;
    jobs.reserve(botsByZone.size());

    for (auto& zoneBots : botsByZone)
    {
        jobs.emplace_back([bots = std::move(zoneBots.second), diff]()
        {
            for (ZoneBotAIEntry const& bot : bots)
                UpdateBotAI(bot, diff);
        });
    }

    GetZoneParallelBotAIPool().Run(std::move(jobs), GetZoneParallelBotAIThreadCount());
}

bool IsBotOutgoingPacketHandled(uint16 opcode)
{
    switch (opcode)
    {
        case SMSG_SPELL_FAILURE:
        case SMSG_SPELL_DELAYED:
        case SMSG_EMOTE:
        case SMSG_MESSAGECHAT:
        case SMSG_FORCE_MOVE_ROOT:
        case SMSG_FORCE_MOVE_UNROOT:
        case SMSG_MOVE_KNOCK_BACK:
        case SMSG_PETITION_SHOW_SIGNATURES:
        case SMSG_GROUP_INVITE:
        case SMSG_GUILD_INVITE:
        case BUY_ERR_NOT_ENOUGHT_MONEY:
        case BUY_ERR_REPUTATION_REQUIRE:
        case SMSG_GROUP_SET_LEADER:
        case SMSG_FORCE_RUN_SPEED_CHANGE:
        case SMSG_RESURRECT_REQUEST:
        case SMSG_INVENTORY_CHANGE_FAILURE:
        case SMSG_TRADE_STATUS:
        case SMSG_TRADE_STATUS_EXTENDED:
        case SMSG_LOOT_RESPONSE:
        case SMSG_ITEM_PUSH_RESULT:
        case SMSG_LOOT_ROLL_WON:
        case SMSG_PARTY_COMMAND_RESULT:
        case SMSG_LEVELUP_INFO:
        case SMSG_LOG_XPGAIN:
        case SMSG_CAST_FAILED:
        case SMSG_DUEL_REQUESTED:
        case SMSG_BATTLEFIELD_STATUS:
        case SMSG_LFG_ROLE_CHECK_UPDATE:
        case SMSG_LFG_PROPOSAL_UPDATE:
        case SMSG_TEXT_EMOTE:
        case SMSG_LOOT_START_ROLL:
        case SMSG_ARENA_TEAM_INVITE:
        case SMSG_GROUP_DESTROYED:
        case SMSG_GROUP_LIST:
        case SMSG_QUESTUPDATE_COMPLETE:
        case SMSG_QUESTUPDATE_ADD_KILL:
        case SMSG_QUEST_CONFIRM_ACCEPT:
            return true;
        default:
            return false;
    }
}

bool IsMasterOutgoingPacketHandled(uint16 opcode)
{
    switch (opcode)
    {
        case SMSG_PARTY_COMMAND_RESULT:
        case MSG_RAID_READY_CHECK:
        case MSG_RAID_READY_CHECK_FINISHED:
        case SMSG_QUESTGIVER_OFFER_REWARD:
            return true;
        default:
            return false;
    }
}
}  // namespace

bool IsZoneParallelBotAIWorkerThread()
{
    return zoneParallelBotAIWorkerThread;
}

class PlayerbotsDatabaseScript : public DatabaseScript
{
public:
    PlayerbotsDatabaseScript() : DatabaseScript("PlayerbotsDatabaseScript") {}

    bool OnDatabasesLoading() override
    {
        DatabaseLoader playerbotLoader("server.playerbots");
        playerbotLoader.SetUpdateFlags(sConfigMgr->GetOption<bool>("Playerbots.Updates.EnableDatabases", true)
                                           ? DatabaseLoader::DATABASE_PLAYERBOTS
                                           : 0);
        playerbotLoader.AddDatabase(PlayerbotsDatabase, "Playerbots");

        return playerbotLoader.Load();
    }

    void OnDatabasesKeepAlive() override { PlayerbotsDatabase.KeepAlive(); }

    void OnDatabasesClosing() override { PlayerbotsDatabase.Close(); }

    void OnDatabaseWarnAboutSyncQueries(bool apply) override { PlayerbotsDatabase.WarnAboutSyncQueries(apply); }

    void OnDatabaseSelectIndexLogout(Player* player, uint32& statementIndex, uint32& statementParam) override
    {
        statementIndex = CHAR_UPD_CHAR_OFFLINE;
        statementParam = player->GetGUID().GetCounter();
    }

    void OnDatabaseGetDBRevision(std::string& revision) override
    {
        if (QueryResult resultPlayerbot =
                PlayerbotsDatabase.Query("SELECT date FROM version_db_playerbots ORDER BY date DESC LIMIT 1"))
        {
            Field* fields = resultPlayerbot->Fetch();
            revision = fields[0].Get<std::string>();
        }

        if (revision.empty())
            revision = "Unknown Playerbots Database Revision";
    }
};

class PlayerbotsPlayerScript : public PlayerScript
{
public:
    PlayerbotsPlayerScript() : PlayerScript("PlayerbotsPlayerScript", {
        PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_ON_AFTER_UPDATE,
        PLAYERHOOK_ON_BEFORE_CRITERIA_PROGRESS,
        PLAYERHOOK_ON_BEFORE_ACHI_COMPLETE,
        PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT,
        PLAYERHOOK_CAN_PLAYER_USE_GROUP_CHAT,
        PLAYERHOOK_CAN_PLAYER_USE_GUILD_CHAT,
        PLAYERHOOK_CAN_PLAYER_USE_CHANNEL_CHAT,
        PLAYERHOOK_ON_GIVE_EXP,
        PLAYERHOOK_ON_BEFORE_TELEPORT,
        PLAYERHOOK_NOT_AVOID_SATISFY
    }) {}

    bool ShouldBypassDungeonAccess(Player* player) const
    {
        if (!sPlayerbotAIConfig.ignoreDungeonAccessRequirements)
            return false;

        PlayerbotAI* botAI = PlayerbotsMgr::instance().GetPlayerbotAI(player);
        return botAI && !botAI->IsRealPlayer();
    }

    bool OnPlayerNotAvoidSatisfy(Player* player, DungeonProgressionRequirements const* /*ar*/, uint32 /*target_map*/,
                                 bool /*report*/) override
    {
        // Return false here to let Player::Satisfy bypass missing dungeon requirements.
        return !ShouldBypassDungeonAccess(player);
    }

    void OnPlayerLogin(Player* player) override
    {
        if (!player->GetSession()->IsBot())
        {
            PlayerbotsMgr::instance().AddPlayerbotData(player, false);
            sRandomPlayerbotMgr.OnPlayerLogin(player);

            // Before modifying the following messages, please make sure it does not violate the AGPLv3.0 license
            // especially if you are distributing a repack or hosting a public server
            // e.g. you can replace the URL with your own repository,
            // but it should be publicly accessible and include all modifications you've made
            if (sPlayerbotAIConfig.enabled)
            {
                ChatHandler(player->GetSession()).SendSysMessage(
                    "|cff00ff00This server runs with |cff00ccffmod-playerbots|r "
                    "|cffcccccchttps://github.com/mod-playerbots/mod-playerbots|r");
            }

            if (sPlayerbotAIConfig.enabled || sPlayerbotAIConfig.randomBotAutologin)
            {
                std::string maxAllowedBotCount = std::to_string(sRandomPlayerbotMgr.GetMaxAllowedBotCount());

                ChatHandler(player->GetSession()).SendSysMessage(
                    "|cff00ff00Playerbots:|r The server is configured with " + maxAllowedBotCount + " bots.");
            }
        }
    }

    bool OnPlayerBeforeTeleport(Player* /*player*/, uint32 /*mapid*/, float /*x*/, float /*y*/, float /*z*/,
                                float /*orientation*/, uint32 /*options*/, Unit* /*target*/) override
    {
        /* for now commmented out until proven its actually required
        * havent seen any proof CleanVisibilityReferences() is needed

        // If the player is not safe to touch, do nothing
        if (!player)
            return true;

        // If same map or not in world do nothing
        if (!player->IsInWorld() || player->GetMapId() == mapid)
            return true;

        // If real player do nothing
        PlayerbotAI* ai = GET_PLAYERBOT_AI(player);
        if (!ai || ai->IsRealPlayer())
            return true;

        // Cross-map bot teleport: defer visibility reference cleanup.
        // CleanVisibilityReferences() erases this bot's GUID from other objects' visibility containers.
        // This is intentionally done via the event queue (instead of directly here) because erasing
        // from other players' visibility maps inside the teleport call stack can hit unsafe re-entrancy
        // or iterator invalidation while visibility updates are in progress
        ObjectGuid guid = player->GetGUID();
        player->m_Events.AddEventAtOffset(
            [guid, mapid]()
            {
                // do nothing, if the player is not safe to touch
                Player* p = ObjectAccessor::FindPlayer(guid);
                if (!p || !p->IsInWorld() || p->IsDuringRemoveFromWorld())
                    return;

                // do nothing if we are already on the target map
                if (p->GetMapId() == mapid)
                    return;

                p->GetObjectVisibilityContainer().CleanVisibilityReferences();
            },
            Milliseconds(0));

        */

        return true;
    }

    void OnPlayerAfterUpdate(Player* player, uint32 diff) override
    {
        PlayerbotAI* const botAI = PlayerbotsMgr::instance().GetPlayerbotAI(player);

        if (botAI != nullptr)
        {
            if (!ShouldDeferBotAIToZoneParallelUpdate(player, botAI))
                botAI->UpdateAI(diff);
        }

        if (PlayerbotMgr* playerbotMgr = GET_PLAYERBOT_MGR(player))
        {
            playerbotMgr->UpdateAI(diff);
        }
    }

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*lang*/, std::string& msg, Player* receiver) override
    {
        if (type != CHAT_MSG_WHISPER)
        {
            return true;
        }

        PlayerbotAI* const botAI = PlayerbotsMgr::instance().GetPlayerbotAI(receiver);

        if (botAI == nullptr)
        {
            return true;
        }

        botAI->HandleCommand(type, msg, player);

        // hotfix; otherwise the server will crash when whispering logout
        // https://github.com/mod-playerbots/mod-playerbots/pull/1838
        // TODO: find the root cause and solve it. (does not happen in party chat)
        if (msg == "logout")
            return false;

        return true;
    }

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*lang*/, std::string& msg, Group* group) override
    {
        for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
        {
            Player* const member = itr->GetSource();

            if (member == nullptr)
                continue;

            PlayerbotAI* const botAI = PlayerbotsMgr::instance().GetPlayerbotAI(member);

            if (botAI == nullptr)
                continue;

            botAI->HandleCommand(type, msg, player);
        }

        return true;
    }

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*lang*/, std::string& msg, Guild* /*guild*/) override
    {
        if (type != CHAT_MSG_GUILD)
            return true;

        PlayerbotMgr* playerbotMgr = PlayerbotsMgr::instance().GetPlayerbotMgr(player);

        if (playerbotMgr == nullptr)
            return true;

        for (PlayerBotMap::const_iterator it = playerbotMgr->GetPlayerBotsBegin(); it != playerbotMgr->GetPlayerBotsEnd(); ++it)
        {
            Player* const bot = it->second;

            if (bot == nullptr)
                continue;

            if (bot->GetGuildId() != player->GetGuildId())
                continue;

            PlayerbotsMgr::instance().GetPlayerbotAI(bot)->HandleCommand(type, msg, player);
        }

        return true;
    }

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*lang*/, std::string& msg, Channel* channel) override
    {
        PlayerbotMgr* const playerbotMgr = PlayerbotsMgr::instance().GetPlayerbotMgr(player);

        if (playerbotMgr != nullptr && channel->GetFlags() & 0x18)
            playerbotMgr->HandleCommand(type, msg);

        sRandomPlayerbotMgr.HandleCommand(type, msg, player);

        return true;
    }

    bool OnPlayerBeforeAchievementComplete(Player* player, AchievementEntry const* achievement) override
    {
        if ((sRandomPlayerbotMgr.IsRandomBot(player) || sRandomPlayerbotMgr.IsAddclassBot(player)) &&
            (achievement->flags & (ACHIEVEMENT_FLAG_REALM_FIRST_REACH | ACHIEVEMENT_FLAG_REALM_FIRST_KILL)))
        {
            return false;
        }

        return true;
    }

    void OnPlayerGiveXP(Player* player, uint32& amount, Unit* /*victim*/, uint8 /*xpSource*/) override
    {
        // early return
        if (sPlayerbotAIConfig.randomBotXPRate == 1.0 || !player)
            return;

        // no XP multiplier, when player is no bot.
        if (!player->GetSession()->IsBot() || !sRandomPlayerbotMgr.IsRandomBot(player))
            return;

        // no XP multiplier, when bot is in a group with a real player.
        if (Group* group = player->GetGroup())
        {
            for (GroupReference* gref = group->GetFirstMember(); gref; gref = gref->next())
            {
                Player* member = gref->GetSource();
                if (!member)
                    continue;

                if (!member->GetSession()->IsBot())
                    return;
            }
        }

        // otherwise apply bot XP multiplier.
        amount = static_cast<uint32>(std::round(static_cast<float>(amount) * sPlayerbotAIConfig.randomBotXPRate));
    }
};

class PlayerbotsMiscScript : public MiscScript
{
public:
    PlayerbotsMiscScript() : MiscScript("PlayerbotsMiscScript", {MISCHOOK_ON_DESTRUCT_PLAYER}) {}

    void OnDestructPlayer(Player* player) override
    {
        PlayerbotAI* botAI = PlayerbotsMgr::instance().GetPlayerbotAI(player);

        if (botAI != nullptr)
            delete botAI;

        if (PlayerbotMgr* playerbotMgr = GET_PLAYERBOT_MGR(player))
            delete playerbotMgr;
    }
};

class PlayerbotsServerScript : public ServerScript
{
public:
    PlayerbotsServerScript() : ServerScript("PlayerbotsServerScript", {
        SERVERHOOK_CAN_PACKET_RECEIVE
    }) {}

    void OnPacketReceived(WorldSession* session, WorldPacket const& packet) override
    {
        if (Player* player = session->GetPlayer())
            if (PlayerbotMgr* playerbotMgr = GET_PLAYERBOT_MGR(player))
                playerbotMgr->HandleMasterIncomingPacket(packet);
    }
};

class PlayerbotsGlobalScript : public GlobalScript
{
public:
    PlayerbotsGlobalScript() : GlobalScript("PlayerbotsGlobalScript", { GLOBALHOOK_ON_INITIALIZE_LOCKED_DUNGEONS }) {}

    void OnInitializeLockedDungeons(Player* player, uint8& /*level*/, uint32& lockData,
                                    lfg::LFGDungeonData const* /*dungeon*/) override
    {
        if (!sPlayerbotAIConfig.ignoreDungeonAccessRequirements)
            return;

        PlayerbotAI* botAI = PlayerbotsMgr::instance().GetPlayerbotAI(player);
        if (!botAI || botAI->IsRealPlayer())
            return;

        switch (lockData)
        {
            case lfg::LFG_LOCKSTATUS_TOO_LOW_LEVEL:
            case lfg::LFG_LOCKSTATUS_TOO_HIGH_LEVEL:
            case lfg::LFG_LOCKSTATUS_TOO_LOW_GEAR_SCORE:
            case lfg::LFG_LOCKSTATUS_TOO_HIGH_GEAR_SCORE:
            case lfg::LFG_LOCKSTATUS_ATTUNEMENT_TOO_LOW_LEVEL:
            case lfg::LFG_LOCKSTATUS_ATTUNEMENT_TOO_HIGH_LEVEL:
            case lfg::LFG_LOCKSTATUS_QUEST_NOT_COMPLETED:
            case lfg::LFG_LOCKSTATUS_MISSING_ITEM:
            case lfg::LFG_LOCKSTATUS_MISSING_ACHIEVEMENT:
                lockData = 0;
                break;
            default:
                break;
        }
    }
};

class PlayerbotsWorldScript : public WorldScript
{
public:
    PlayerbotsWorldScript() : WorldScript("PlayerbotsWorldScript", {
        WORLDHOOK_ON_BEFORE_WORLD_INITIALIZED,
        WORLDHOOK_ON_UPDATE
    }) {}

    void OnBeforeWorldInitialized() override
    {
        // Before modifying the following messages, please make sure it does not violate the AGPLv3.0 license
        // especially if you are distributing a repack or hosting a public server
        // e.g. you can replace the URL with your own repository,
        // but it should be publicly accessible and include all modifications you've made
        LOG_INFO("server.loading", "╔══════════════════════════════════════════════════════════╗");
        LOG_INFO("server.loading", "║                                                          ║");
        LOG_INFO("server.loading", "║              AzerothCore Playerbots Module               ║");
        LOG_INFO("server.loading", "║                                                          ║");
        LOG_INFO("server.loading", "╟──────────────────────────────────────────────────────────╢");
        LOG_INFO("server.loading", "║     mod-playerbots is a community-driven open-source     ║");
        LOG_INFO("server.loading", "║  project based on AzerothCore, licensed under AGPLv3.0   ║");
        LOG_INFO("server.loading", "╟──────────────────────────────────────────────────────────╢");
        LOG_INFO("server.loading", "║      https://github.com/mod-playerbots/mod-playerbots    ║");
        LOG_INFO("server.loading", "╚══════════════════════════════════════════════════════════╝");

        uint32 oldMSTime = getMSTime();

        LOG_INFO("server.loading", " ");
        LOG_INFO("server.loading", "Load Playerbots Config...");

        sPlayerbotAIConfig.Initialize();

        LOG_INFO("server.loading", ">> Loaded playerbots config in {} ms", GetMSTimeDiffToNow(oldMSTime));
        LOG_INFO("server.loading", " ");

        PlayerbotSpellRepository::Instance().Initialize();

        LOG_INFO("server.loading", "Playerbots World Thread Processor initialized");
    }

    void OnUpdate(uint32 diff) override
    {
        PlayerbotWorldThreadProcessor::instance().Update(diff);
        PlayerbotAI::UpdateAsyncActivityCache(diff);
        PlayerbotAI::UpdateAsyncNearbyPlayerCache(diff);
        BGTactics::UpdateAsyncAVStrategyCache(diff);
        sRandomPlayerbotMgr.UpdateAI(diff);  // World thread only
        UpdateZoneParallelBotAI(diff);
    }
};

class PlayerbotsScript : public PlayerbotScript
{
public:
    PlayerbotsScript() : PlayerbotScript("PlayerbotsScript") {}

    bool OnPlayerbotCheckLFGQueue(lfg::Lfg5Guids const& guidsList) override
    {
        bool nonBotFound = false;

        for (ObjectGuid const& guid : guidsList.guids)
        {
            Player* player = ObjectAccessor::FindPlayer(guid);

            if (guid.IsGroup() || (player && !PlayerbotsMgr::instance().GetPlayerbotAI(player)))
            {
                nonBotFound = true;
                break;
            }
        }

        return nonBotFound;
    }

    void OnPlayerbotCheckKillTask(Player* player, Unit* victim) override
    {
        if (player)
            GuildTaskMgr::instance().CheckKillTask(player, victim);
    }

    void OnPlayerbotCheckPetitionAccount(Player* player, bool& found) override
    {
        if (!found)
            return;

        if (PlayerbotsMgr::instance().GetPlayerbotAI(player) != nullptr)
            found = false;
    }

    bool OnPlayerbotCheckUpdatesToSend(Player* player) override
    {
        PlayerbotAI* botAI = PlayerbotsMgr::instance().GetPlayerbotAI(player);

        if (botAI == nullptr)
            return true;

        return botAI->IsRealPlayer();
    }

    void OnPlayerbotPacketSent(Player* player, WorldPacket const* packet) override
    {
        if (player == nullptr || packet == nullptr)
            return;

        uint16 opcode = packet->GetOpcode();
        if (player->GetSession() && player->GetSession()->IsBot() && IsBotOutgoingPacketHandled(opcode))
        {
            PlayerbotAI* botAI = PlayerbotsMgr::instance().GetPlayerbotAI(player);
            if (botAI != nullptr)
                botAI->HandleBotOutgoingPacket(*packet);
        }

        if (!IsMasterOutgoingPacketHandled(opcode))
            return;

        if (PlayerbotMgr* playerbotMgr = GET_PLAYERBOT_MGR(player))
            playerbotMgr->HandleMasterOutgoingPacket(*packet);
    }

    void OnPlayerbotUpdate(uint32 /*diff*/) override
    {
        sRandomPlayerbotMgr.UpdateSessions();  // Per-bot updates only
    }

    void OnPlayerbotUpdateSessions(Player* player) override
    {
        if (player)
            if (PlayerbotMgr* playerbotMgr = GET_PLAYERBOT_MGR(player))
                playerbotMgr->UpdateSessions();
    }

    void OnPlayerbotLogout(Player* player) override
    {
        if (PlayerbotMgr* playerbotMgr = GET_PLAYERBOT_MGR(player))
        {
            PlayerbotAI* botAI = PlayerbotsMgr::instance().GetPlayerbotAI(player);

            if (botAI == nullptr || botAI->IsRealPlayer())
            {
                playerbotMgr->LogoutAllBots();
            }
        }

        sRandomPlayerbotMgr.OnPlayerLogout(player);
    }

    void OnPlayerbotLogoutBots() override
    {
        LOG_INFO("playerbots", "Logging out all bots...");
        sRandomPlayerbotMgr.LogoutAllBots();
    }
};

class PlayerBotsBGScript : public BGScript
{
public:
    PlayerBotsBGScript() : BGScript("PlayerBotsBGScript") {}

    void OnBattlegroundStart(Battleground* bg) override
    {
        BGStrategyData data;

        // A battleground created from the random queue keeps BATTLEGROUND_RB as its real type id and
        // carries the rolled map in the random type id, so without resolving it every random-queue
        // match would fall through to the default and run with the plain default strategy pair.
        BattlegroundTypeId bgType = bg->GetBgTypeID();
        if (bgType == BATTLEGROUND_RB)
            bgType = bg->GetBgTypeID(true);

        switch (bgType)
        {
            case BATTLEGROUND_WS:
                data.allianceStrategy = urand(0, WS_STRATEGY_MAX - 1);
                data.hordeStrategy = urand(0, WS_STRATEGY_MAX - 1);
                break;
            case BATTLEGROUND_AB:
                data.allianceStrategy = urand(0, AB_STRATEGY_MAX - 1);
                data.hordeStrategy = urand(0, AB_STRATEGY_MAX - 1);
                break;
            case BATTLEGROUND_AV:
                data.allianceStrategy = AV_STRATEGY_ALLIANCE_CONTROL_TEMPO;
                data.hordeStrategy = urand(AV_STRATEGY_BALANCED, AV_STRATEGY_DEFENSIVE);
                break;
            case BATTLEGROUND_EY:
                data.allianceStrategy = urand(0, EY_STRATEGY_MAX - 1);
                data.hordeStrategy = urand(0, EY_STRATEGY_MAX - 1);
                break;
            default:
                break;
        }

        bgStrategies[bg->GetInstanceID()] = data;
    }

    void OnBattlegroundEnd(Battleground* bg, TeamId /*winnerTeam*/) override
    {
        bgStrategies.erase(bg->GetInstanceID());
        // Instance ids are reused, so per-instance Alliance tactic state that outlives the match
        // would be read back as if it belonged to the next one.
        BGTactics::OnBattlegroundEnd(bg->GetInstanceID());
    }
};

// Workaround for missing InitEnabledHooksIfNeeded for new BattlefieldScript in ScriptMgr
class PlayerbotsBattlefieldScript : public BattlefieldScript
{
public:
    PlayerbotsBattlefieldScript() : BattlefieldScript("PlayerbotsBattlefieldScript") { }
};

void AddPlayerbotsSecureLoginScripts();

void AddSC_TempestKeepBotScripts();

void AddPlayerbotsScripts()
{
    new PlayerbotsBattlefieldScript();
    new PlayerbotsDatabaseScript();
    new PlayerbotsPlayerScript();
    new PlayerbotsGlobalScript();
    new PlayerbotsMiscScript();
    new PlayerbotsServerScript();
    new PlayerbotsWorldScript();
    new PlayerbotsScript();
    new PlayerBotsBGScript();
    AddPlayerbotsSecureLoginScripts();
    AddPlayerbotsCommandscripts();
    PlayerBotsGuildValidationScript();
    AddSC_TempestKeepBotScripts();
}
