#include "Chat.h"
#include "CommandScript.h"
#include "InvasionScheduler.h"
#include "InvasionRuntimeManager.h"
#include "InvasionSpawnManager.h"
#include "LivingWorldInvasions.h"
#include "MovementController.h"
#include "RuntimeEntityGroup.h"
#include "LwiCreatureTemplateManager.h"
#include "CreatureAbilityManager.h"
#include "RuntimeSignalManager.h"

#include "ConfigValueCache.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "Field.h"
#include "QueryResult.h"
#include "Log.h"
#include "Player.h"
#include "WorldSession.h"
#include "ScriptMgr.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_set>
#include <vector>

using namespace Acore::ChatCommands;

namespace
{
std::unordered_set<uint64> routeTestGroupIds;

struct RouteRecordingSession
{
    uint32 OwnerGuidLow = 0;
    uint32 PathId = 0;
    std::string PathName;
    uint16 MapId = 0;
    uint32 NextNodeId = 1;
    uint16 NextNodeOrder = 10;
    std::vector<uint32> NodeIds;
};

RouteRecordingSession routeRecordingSession;
bool routeRecordingActive = false;

bool IsSafeRouteRecordName(std::string const& name)
{
    if (name.empty() || name.size() > 120)
        return false;

    return std::all_of(name.begin(), name.end(), [](unsigned char c)
    {
        return std::isalnum(c) || c == '_' || c == '-';
    });
}

Player* GetCommandPlayer(ChatHandler* handler)
{
    return handler && handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
}

enum class LwiConfig
{
    Enabled,
    PlayerbotsEnabled,
    Debug,
    SchedulerEnabled,
    SchedulerCheckIntervalSeconds,
    SchedulerInitialDelayMinSeconds,
    SchedulerInitialDelayMaxSeconds,
    SchedulerNextDelayMinSeconds,
    SchedulerNextDelayMaxSeconds,
    SchedulerMaxActiveGlobal,
    SchedulerDefaultMaxActivePerMap,
    SchedulerDefaultMaxActivePerResponseOrigin,
    Count
};

class LwiConfigData final : public ConfigValueCache<LwiConfig>
{
public:
    LwiConfigData() : ConfigValueCache(LwiConfig::Count) { }

    void BuildConfigCache() override
    {
        SetConfigValue<bool>(LwiConfig::Enabled, "LWI.Enable", true);
        SetConfigValue<bool>(LwiConfig::PlayerbotsEnabled, "LWI.Playerbots.Enable", false);
        SetConfigValue<bool>(LwiConfig::Debug, "LWI.Debug", false);
        SetConfigValue<bool>(LwiConfig::SchedulerEnabled, "LWI.Scheduler.Enable", true);
        SetConfigValue<uint32>(LwiConfig::SchedulerCheckIntervalSeconds, "LWI.Scheduler.CheckIntervalSeconds", 5);
        SetConfigValue<uint32>(LwiConfig::SchedulerInitialDelayMinSeconds, "LWI.Scheduler.InitialDelayMinSeconds", 20);
        SetConfigValue<uint32>(LwiConfig::SchedulerInitialDelayMaxSeconds, "LWI.Scheduler.InitialDelayMaxSeconds", 40);
        SetConfigValue<uint32>(LwiConfig::SchedulerNextDelayMinSeconds, "LWI.Scheduler.NextDelayMinSeconds", 30);
        SetConfigValue<uint32>(LwiConfig::SchedulerNextDelayMaxSeconds, "LWI.Scheduler.NextDelayMaxSeconds", 60);
        SetConfigValue<uint32>(LwiConfig::SchedulerMaxActiveGlobal, "LWI.Scheduler.MaxActiveGlobal", 3);
        SetConfigValue<uint32>(LwiConfig::SchedulerDefaultMaxActivePerMap, "LWI.Scheduler.DefaultMaxActivePerMap", 2);
        SetConfigValue<uint32>(LwiConfig::SchedulerDefaultMaxActivePerResponseOrigin, "LWI.Scheduler.DefaultMaxActivePerResponseOrigin", 1);
    }
};

LwiConfigData lwiConfig;

class LivingWorldInvasionsWorldScript final : public WorldScript
{
public:
    LivingWorldInvasionsWorldScript() : WorldScript("LivingWorldInvasionsWorldScript", {
        WORLDHOOK_ON_AFTER_CONFIG_LOAD,
		WORLDHOOK_ON_STARTUP,
        WORLDHOOK_ON_UPDATE
    }) { }

    void OnAfterConfigLoad(bool reload) override
    {
        lwiConfig.Initialize(reload);

        if (!lwiConfig.GetConfigValue<bool>(LwiConfig::Enabled))
        {
            LOG_INFO("server.loading", "Living World Invasions is disabled.");
            sInvasionRuntimeMgr.Reset();
            sInvasionScheduler.Reset();
            return;
        }

        // On initial world startup this hook runs before ObjectMgr loads
        // creature_template. Materialize LWI-owned derived templates now so the
        // normal AzerothCore creature-template loader sees them on this startup.
        //
        // A config reload occurs after ObjectMgr is already populated; do not
        // rebuild generated creature entries in that case. Template-definition
        // changes therefore intentionally require a worldserver restart.
        if (!reload)
        {
            if (!sLwiCreatureTemplateMgr.MaterializeStartupTemplates())
            {
                LOG_ERROR("server.loading",
                    "Living World Invasions failed to materialize derived creature templates.");
            }
        }

        sInvasionMgr.LoadDefinitions();

        lwi::SchedulerSettings settings;
        settings.Enabled = lwiConfig.GetConfigValue<bool>(LwiConfig::SchedulerEnabled);
        settings.Debug = lwiConfig.GetConfigValue<bool>(LwiConfig::Debug);
        settings.CheckIntervalSeconds = lwiConfig.GetConfigValue<uint32>(LwiConfig::SchedulerCheckIntervalSeconds);
        settings.InitialDelayMinSeconds = lwiConfig.GetConfigValue<uint32>(LwiConfig::SchedulerInitialDelayMinSeconds);
        settings.InitialDelayMaxSeconds = lwiConfig.GetConfigValue<uint32>(LwiConfig::SchedulerInitialDelayMaxSeconds);
        settings.NextDelayMinSeconds = lwiConfig.GetConfigValue<uint32>(LwiConfig::SchedulerNextDelayMinSeconds);
        settings.NextDelayMaxSeconds = lwiConfig.GetConfigValue<uint32>(LwiConfig::SchedulerNextDelayMaxSeconds);
        settings.MaxActiveGlobal = lwiConfig.GetConfigValue<uint32>(LwiConfig::SchedulerMaxActiveGlobal);
        settings.DefaultMaxActivePerMap = lwiConfig.GetConfigValue<uint32>(LwiConfig::SchedulerDefaultMaxActivePerMap);
        settings.DefaultMaxActivePerResponseOrigin = lwiConfig.GetConfigValue<uint32>(LwiConfig::SchedulerDefaultMaxActivePerResponseOrigin);

        sInvasionScheduler.Configure(settings);
        
        LOG_INFO("server.loading", "Living World Invasions configured. Playerbots integration requested: {}.",
            lwiConfig.GetConfigValue<bool>(LwiConfig::PlayerbotsEnabled) ? "yes" : "no");
    }
	
	void OnStartup() override
	{
	    if (!lwiConfig.GetConfigValue<bool>(LwiConfig::Enabled))
	    {
	        return;
	    }

	    sInvasionScheduler.Initialize();
    sInvasionRuntimeMgr.Initialize();
	}

    void OnUpdate(uint32 diff) override
    {
        sInvasionRuntimeMgr.Update(diff);

        for (auto itr = routeTestGroupIds.begin(); itr != routeTestGroupIds.end();)
        {
            uint64 const runtimeGroupId = *itr;
            if (sMovementController.IsGroupMoving(runtimeGroupId))
            {
                ++itr;
                continue;
            }

            sRuntimeEntityGroupMgr.RemoveGroup(runtimeGroupId);
            itr = routeTestGroupIds.erase(itr);
        }

        sInvasionScheduler.Update(diff);
    }
};

class LivingWorldInvasionsCommandScript final : public CommandScript
{
public:
    LivingWorldInvasionsCommandScript()
        : CommandScript("LivingWorldInvasionsCommandScript")
    {
    }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable abortCommandTable =
        {
            {
                "confirm",
                HandleAbortConfirmCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::Yes
            },
            {
                "",
                HandleAbortCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::Yes
            }
        };

        static ChatCommandTable routeRecordCancelCommandTable =
        {
            {
                "confirm",
                HandleRouteRecordCancelConfirmCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::No
            },
            {
                "",
                HandleRouteRecordCancelCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::No
            }
        };

        static ChatCommandTable routeRecordCommandTable =
        {
            {
                "start",
                HandleRouteRecordStartCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::No
            },
            {
                "add",
                HandleRouteRecordAddCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::No
            },
            {
                "undo",
                HandleRouteRecordUndoCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::No
            },
            {
                "status",
                HandleRouteRecordStatusCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::No
            },
            {
                "finish",
                HandleRouteRecordFinishCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::No
            },
            {
                "cancel",
                routeRecordCancelCommandTable
            }
        };

        static ChatCommandTable routeNodeCommandTable =
        {
            {
                "add",
                HandleRouteNodeAddCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::No
            }
        };

        static ChatCommandTable routeSegmentCommandTable =
        {
            {
                "add",
                HandleRouteSegmentAddCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::No
            }
        };

        static ChatCommandTable routeCommandTable =
        {
            {
                "node",
                routeNodeCommandTable
            },
            {
                "segment",
                routeSegmentCommandTable
            },
            {
                "test",
                HandleRouteTestCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::No
            },
            {
                "record",
                routeRecordCommandTable
            }
        };

        static ChatCommandTable lwiCommandTable =
        {
            {
                "status",
                HandleStatusCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::Yes
            },
            {
                "signals",
                HandleSignalsCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::Yes
            },
            {
                "start",
                HandleStartCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::Yes
            },
            {
                "stop",
                HandleStopCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::Yes
            },
            {
                "enable",
                HandleEnableCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::Yes
            },
            {
                "disable",
                HandleDisableCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::Yes
            },
            {
                "reload",
                HandleReloadCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::Yes
            },
            {
                "version",
                HandleVersionCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::Yes
            },
            {
                "abort",
                abortCommandTable
            },
            {
                "route",
                routeCommandTable
            },
            {
                "trigger",
                HandleTriggerCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::Yes
            }
        };

        static ChatCommandTable commandTable =
        {
            {
                "lwi",
                lwiCommandTable
            }
        };

        return commandTable;
    }

private:
    static bool HandleStatusCommand(ChatHandler* handler)
    {
        handler->SendSysMessage(
            sInvasionScheduler.BuildStatusReport() +
            sInvasionRuntimeMgr.BuildStatusReport());

        return true;
    }

    static bool HandleSignalsCommand(ChatHandler* handler)
    {
        handler->SendSysMessage(sRuntimeSignalMgr.BuildStatusReport());
        return true;
    }


    static bool HandleStartCommand(ChatHandler* handler)
    {
        sInvasionScheduler.Resume();
        handler->SendSysMessage("Living World Invasions scheduler started.");
        return true;
    }

    static bool HandleStopCommand(ChatHandler* handler)
    {
        sInvasionScheduler.Drain();

        uint32 const active = sInvasionRuntimeMgr.GetActiveRuntimeCount();
        handler->PSendSysMessage(
            "Living World Invasions scheduler stopped. {} active runtime(s) will continue to completion.",
            active);
        return true;
    }

    static bool HandleEnableCommand(ChatHandler* handler, uint32 invasionId)
    {
        QueryResult result = WorldDatabase.Query(
            "SELECT `name`, `enabled` FROM `lwi_invasion` WHERE `id` = {}",
            invasionId);

        if (!result)
        {
            handler->PSendSysMessage(
                "Living World Invasions invasion {} does not exist.",
                invasionId);
            return false;
        }

        Field* fields = result->Fetch();
        std::string const name = fields[0].Get<std::string>();
        bool const enabled = fields[1].Get<uint8>() != 0;

        if (enabled)
        {
            handler->PSendSysMessage(
                "Living World Invasions invasion {} ({}) is already enabled.",
                invasionId,
                name);
            return true;
        }

        WorldDatabase.Execute(
            "UPDATE `lwi_invasion` SET `enabled` = 1 WHERE `id` = {}",
            invasionId);

        handler->PSendSysMessage(
            "Living World Invasions invasion {} ({}) enabled in the database. Use .lwi reload to apply the change.",
            invasionId,
            name);
        return true;
    }

    static bool HandleDisableCommand(ChatHandler* handler, uint32 invasionId)
    {
        QueryResult result = WorldDatabase.Query(
            "SELECT `name`, `enabled` FROM `lwi_invasion` WHERE `id` = {}",
            invasionId);

        if (!result)
        {
            handler->PSendSysMessage(
                "Living World Invasions invasion {} does not exist.",
                invasionId);
            return false;
        }

        Field* fields = result->Fetch();
        std::string const name = fields[0].Get<std::string>();
        bool const enabled = fields[1].Get<uint8>() != 0;

        if (!enabled)
        {
            handler->PSendSysMessage(
                "Living World Invasions invasion {} ({}) is already disabled.",
                invasionId,
                name);
            return true;
        }

        WorldDatabase.Execute(
            "UPDATE `lwi_invasion` SET `enabled` = 0 WHERE `id` = {}",
            invasionId);

        handler->PSendSysMessage(
            "Living World Invasions invasion {} ({}) disabled in the database. "
            "Any currently active runtime is unaffected. Use .lwi stop/.lwi abort as needed, then .lwi reload to apply the definition change.",
            invasionId,
            name);
        return true;
    }

    static bool HandleReloadCommand(ChatHandler* handler)
    {
        if (!lwiConfig.GetConfigValue<bool>(LwiConfig::Enabled))
        {
            handler->SendSysMessage("Living World Invasions is disabled by configuration.");
            return true;
        }

        uint32 const active = sInvasionRuntimeMgr.GetActiveRuntimeCount();
        if (active != 0)
        {
            handler->PSendSysMessage(
                "Living World Invasions cannot reload while {} runtime(s) are active. Use .lwi stop and wait for them to finish, or .lwi abort confirm for an emergency stop.",
                active);
            return true;
        }

        if (sInvasionScheduler.GetControlState() == lwi::SchedulerControlState::Running)
        {
            handler->SendSysMessage("Living World Invasions scheduler is still running. Use .lwi stop before .lwi reload.");
            return true;
        }

        LOG_INFO("server.loading", "[LWI] Definition reload requested.");

        // There are no active runtimes at this point, so it is safe to drop
        // all transient runtime state before rebuilding definition caches.
        sInvasionRuntimeMgr.Reset();
        sInvasionSpawnMgr.Reset();
        sInvasionScheduler.Reset();

        sInvasionMgr.LoadDefinitions();

        // Rebuild scheduler/runtime state from the newly loaded definitions.
        // Initialize() returns the scheduler to Running when it is enabled in config.
        sInvasionScheduler.Initialize();
        sInvasionRuntimeMgr.Initialize();

        handler->PSendSysMessage(
            "Living World Invasions reloaded: {} invasion(s), {} stage(s), {} action(s), {} spawn group(s), {} movement path(s), {} route node(s), {} route segment(s), {} dialogue(s), {} signal(s). Scheduler restarted.",
            sInvasionMgr.GetDefinitionCount(),
            sInvasionMgr.GetStageCount(),
            sInvasionMgr.GetActionCount(),
            sInvasionMgr.GetSpawnGroupCount(),
            sInvasionMgr.GetMovementPathCount(),
            sInvasionMgr.GetRouteNodeCount(),
            sInvasionMgr.GetRouteSegmentCount(),
            sInvasionMgr.GetDialogueCount(),
            sInvasionMgr.GetRuntimeSignalCount());

        LOG_INFO("server.loading", "[LWI] Definition reload completed successfully.");
        return true;
    }

    static bool HandleVersionCommand(ChatHandler* handler)
    {
        char const* schedulerState = "unknown";
        switch (sInvasionScheduler.GetControlState())
        {
            case lwi::SchedulerControlState::Running:
                schedulerState = "running";
                break;
            case lwi::SchedulerControlState::Paused:
                schedulerState = "paused";
                break;
            case lwi::SchedulerControlState::Draining:
                schedulerState = "draining";
                break;
        }

        handler->SendSysMessage("Living World Invasions");
        handler->SendSysMessage("Version: 0.2.1-dev");
        handler->PSendSysMessage("Scheduler: {}", schedulerState);
        handler->PSendSysMessage("Debug: {}", lwiConfig.GetConfigValue<bool>(LwiConfig::Debug) ? "enabled" : "disabled");
        handler->PSendSysMessage("Active runtimes: {}", sInvasionRuntimeMgr.GetActiveRuntimeCount());
        handler->SendSysMessage("Loaded definitions:");
        handler->PSendSysMessage("  Response origins: {}", sInvasionMgr.GetResponseOriginCount());
        handler->PSendSysMessage("  Invasions: {}", sInvasionMgr.GetDefinitionCount());
        handler->PSendSysMessage("  Stages: {}", sInvasionMgr.GetStageCount());
        handler->PSendSysMessage("  Actions: {}", sInvasionMgr.GetActionCount());
        handler->PSendSysMessage("  Spawn groups: {}", sInvasionMgr.GetSpawnGroupCount());
        handler->PSendSysMessage("  Spawn members: {}", sInvasionMgr.GetSpawnMemberCount());
        handler->PSendSysMessage("  Dynamic creature templates: {}", sLwiCreatureTemplateMgr.GetMappedTemplateCount());
        handler->PSendSysMessage("  Creature abilities: {}", sCreatureAbilityMgr.GetAbilityCount());
        handler->PSendSysMessage("  Movement paths: {}", sInvasionMgr.GetMovementPathCount());
        handler->PSendSysMessage("  Movement nodes: {}", sInvasionMgr.GetMovementNodeCount());
        handler->PSendSysMessage("  Movement profiles: {}", sInvasionMgr.GetMovementProfileCount());
        handler->PSendSysMessage("  Route nodes: {}", sInvasionMgr.GetRouteNodeCount());
        handler->PSendSysMessage("  Route segments: {}", sInvasionMgr.GetRouteSegmentCount());
        handler->PSendSysMessage("  Dialogues: {}", sInvasionMgr.GetDialogueCount());
        handler->PSendSysMessage("  Runtime signals: {}", sInvasionMgr.GetRuntimeSignalCount());
        return true;
    }

    static bool HandleAbortCommand(ChatHandler* handler)
    {
        uint32 const active = sInvasionRuntimeMgr.GetActiveRuntimeCount();

        handler->PSendSysMessage(
            "WARNING: This will immediately terminate {} active Living World Invasions runtime(s) and clean up their entities.",
            active);
        handler->SendSysMessage("Use .lwi abort confirm to continue.");
        return true;
    }

    static bool HandleAbortConfirmCommand(ChatHandler* handler)
    {
        sInvasionScheduler.Drain();

        uint32 const active = sInvasionRuntimeMgr.GetActiveRuntimeCount();
        sInvasionRuntimeMgr.AbortAll();

        handler->PSendSysMessage(
            "Living World Invasions emergency abort completed. {} runtime(s) were targeted. Scheduler remains stopped.",
            active);
        return true;
    }

    static bool HandleRouteRecordStartCommand(ChatHandler* handler, uint32 pathId, std::string pathName)
    {
        if (!lwiConfig.GetConfigValue<bool>(LwiConfig::Debug))
        {
            handler->SendSysMessage(
                "Living World Invasions debug commands are disabled. Set LWI.Debug = 1 to use .lwi route record.");
            return false;
        }

        Player* player = GetCommandPlayer(handler);
        if (!player)
            return false;

        if (routeRecordingActive)
        {
            handler->PSendSysMessage(
                "A route recording is already active for movement path {} ({}). Finish or cancel it before starting another recording.",
                routeRecordingSession.PathId,
                routeRecordingSession.PathName);
            return false;
        }

        if (!IsSafeRouteRecordName(pathName))
        {
            handler->SendSysMessage(
                "Route recording name must be 1-120 characters and may contain only letters, numbers, underscores, and hyphens.");
            return false;
        }

        if (WorldDatabase.Query("SELECT 1 FROM `lwi_movement_path` WHERE `id` = {} LIMIT 1", pathId))
        {
            handler->PSendSysMessage(
                "Movement path {} already exists. Choose an unused path ID; the recorder will never overwrite an existing path.",
                pathId);
            return false;
        }

        QueryResult nextIdResult = WorldDatabase.Query(
            "SELECT COALESCE(MAX(`id`), 0) + 1 FROM `lwi_movement_node`");
        uint32 nextNodeId = 1;
        if (nextIdResult)
            nextNodeId = nextIdResult->Fetch()[0].Get<uint32>();

        WorldDatabase.Execute(
            "INSERT INTO `lwi_movement_path` (`id`, `name`, `enabled`, `comment`) "
            "VALUES ({}, '{}', 1, 'Recorded in-game with the LWI route recorder')",
            pathId,
            pathName);

        routeRecordingSession = {};
        routeRecordingSession.OwnerGuidLow = player->GetGUID().GetCounter();
        routeRecordingSession.PathId = pathId;
        routeRecordingSession.PathName = pathName;
        routeRecordingSession.MapId = player->GetMapId();
        routeRecordingSession.NextNodeId = nextNodeId;
        routeRecordingSession.NextNodeOrder = 10;
        routeRecordingActive = true;

        handler->PSendSysMessage(
            "Started recording movement path {} ({}) on map {}. Move to the first point and use .lwi route record add.",
            pathId,
            pathName,
            player->GetMapId());
        return true;
    }

    static bool HandleRouteRecordAddCommand(ChatHandler* handler)
    {
        if (!routeRecordingActive)
        {
            handler->SendSysMessage(
                "No route recording is active. Use .lwi route record start <pathId> <name> first.");
            return false;
        }

        Player* player = GetCommandPlayer(handler);
        if (!player)
            return false;

        if (player->GetGUID().GetCounter() != routeRecordingSession.OwnerGuidLow)
        {
            handler->PSendSysMessage(
                "Movement path {} ({}) is currently being recorded by another GM.",
                routeRecordingSession.PathId,
                routeRecordingSession.PathName);
            return false;
        }

        if (player->GetMapId() != routeRecordingSession.MapId)
        {
            handler->PSendSysMessage(
                "This recording started on map {}, but you are now on map {}. A single movement path must remain on one map.",
                routeRecordingSession.MapId,
                player->GetMapId());
            return false;
        }

        if (routeRecordingSession.NextNodeOrder > 65520)
        {
            handler->SendSysMessage(
                "This path has reached the maximum supported node order. Finish the current recording and begin another route segment.");
            return false;
        }

        uint32 const nodeId = routeRecordingSession.NextNodeId++;
        uint16 const nodeOrder = routeRecordingSession.NextNodeOrder;
        routeRecordingSession.NextNodeOrder = static_cast<uint16>(routeRecordingSession.NextNodeOrder + 10);

        WorldDatabase.Execute(
            "INSERT INTO `lwi_movement_node` "
            "(`id`, `path_id`, `node_order`, `map_id`, `x`, `y`, `z`, `orientation`, `wait_ms`, `profile_override_id`, `enabled`, `comment`) "
            "VALUES ({}, {}, {}, {}, {}, {}, {}, {}, 0, 0, 1, '{} route node {}')",
            nodeId,
            routeRecordingSession.PathId,
            nodeOrder,
            player->GetMapId(),
            player->GetPositionX(),
            player->GetPositionY(),
            player->GetPositionZ(),
            player->GetOrientation(),
            routeRecordingSession.PathName,
            nodeOrder);

        routeRecordingSession.NodeIds.push_back(nodeId);

        handler->PSendSysMessage(
            "Recorded node {} (order {}) for path {} at X {:.3f} Y {:.3f} Z {:.3f} O {:.3f}. Total nodes: {}.",
            nodeId,
            nodeOrder,
            routeRecordingSession.PathId,
            player->GetPositionX(),
            player->GetPositionY(),
            player->GetPositionZ(),
            player->GetOrientation(),
            routeRecordingSession.NodeIds.size());
        return true;
    }

    static bool HandleRouteRecordUndoCommand(ChatHandler* handler)
    {
        if (!routeRecordingActive)
        {
            handler->SendSysMessage("No route recording is active.");
            return false;
        }

        Player* player = GetCommandPlayer(handler);
        if (!player || player->GetGUID().GetCounter() != routeRecordingSession.OwnerGuidLow)
        {
            handler->SendSysMessage("Only the GM who started the route recording may modify it.");
            return false;
        }

        if (routeRecordingSession.NodeIds.empty())
        {
            handler->SendSysMessage("The current recording has no nodes to undo.");
            return false;
        }

        uint32 const nodeId = routeRecordingSession.NodeIds.back();
        routeRecordingSession.NodeIds.pop_back();
        routeRecordingSession.NextNodeOrder = static_cast<uint16>(routeRecordingSession.NextNodeOrder - 10);

        WorldDatabase.Execute(
            "DELETE FROM `lwi_movement_node` WHERE `id` = {} AND `path_id` = {}",
            nodeId,
            routeRecordingSession.PathId);

        handler->PSendSysMessage(
            "Removed the last recorded node ({}) from path {}. Remaining nodes: {}.",
            nodeId,
            routeRecordingSession.PathId,
            routeRecordingSession.NodeIds.size());
        return true;
    }

    static bool HandleRouteRecordStatusCommand(ChatHandler* handler)
    {
        if (!routeRecordingActive)
        {
            handler->SendSysMessage("No route recording is active.");
            return true;
        }

        handler->PSendSysMessage(
            "Route recording: path {} ({}) | map {} | nodes {} | next order {}.",
            routeRecordingSession.PathId,
            routeRecordingSession.PathName,
            routeRecordingSession.MapId,
            routeRecordingSession.NodeIds.size(),
            routeRecordingSession.NextNodeOrder);
        return true;
    }

    static bool HandleRouteRecordFinishCommand(ChatHandler* handler)
    {
        if (!routeRecordingActive)
        {
            handler->SendSysMessage("No route recording is active.");
            return false;
        }

        Player* player = GetCommandPlayer(handler);
        if (!player || player->GetGUID().GetCounter() != routeRecordingSession.OwnerGuidLow)
        {
            handler->SendSysMessage("Only the GM who started the route recording may finish it.");
            return false;
        }

        if (routeRecordingSession.NodeIds.size() < 2)
        {
            handler->PSendSysMessage(
                "Path {} currently has only {} node(s). Record at least two nodes before finishing, or use .lwi route record cancel confirm.",
                routeRecordingSession.PathId,
                routeRecordingSession.NodeIds.size());
            return false;
        }

        uint32 const pathId = routeRecordingSession.PathId;
        std::string const pathName = routeRecordingSession.PathName;
        std::size_t const nodeCount = routeRecordingSession.NodeIds.size();

        routeRecordingSession = {};
        routeRecordingActive = false;

        handler->PSendSysMessage(
            "Finished recording movement path {} ({}) with {} node(s). The data is saved in lwi_movement_path/lwi_movement_node. Use .lwi reload before testing it through a route segment.",
            pathId,
            pathName,
            nodeCount);
        return true;
    }

    static bool HandleRouteRecordCancelCommand(ChatHandler* handler)
    {
        if (!routeRecordingActive)
        {
            handler->SendSysMessage("No route recording is active.");
            return true;
        }

        Player* player = GetCommandPlayer(handler);
        if (!player || player->GetGUID().GetCounter() != routeRecordingSession.OwnerGuidLow)
        {
            handler->SendSysMessage("Only the GM who started the route recording may cancel it.");
            return false;
        }

        handler->PSendSysMessage(
            "WARNING: This will delete unfinished movement path {} ({}) and its {} recorded node(s). Use .lwi route record cancel confirm to continue.",
            routeRecordingSession.PathId,
            routeRecordingSession.PathName,
            routeRecordingSession.NodeIds.size());
        return true;
    }

    static bool HandleRouteRecordCancelConfirmCommand(ChatHandler* handler)
    {
        if (!routeRecordingActive)
        {
            handler->SendSysMessage("No route recording is active.");
            return true;
        }

        Player* player = GetCommandPlayer(handler);
        if (!player || player->GetGUID().GetCounter() != routeRecordingSession.OwnerGuidLow)
        {
            handler->SendSysMessage("Only the GM who started the route recording may cancel it.");
            return false;
        }

        uint32 const pathId = routeRecordingSession.PathId;
        std::string const pathName = routeRecordingSession.PathName;

        WorldDatabase.Execute(
            "DELETE FROM `lwi_movement_node` WHERE `path_id` = {}",
            pathId);
        WorldDatabase.Execute(
            "DELETE FROM `lwi_movement_path` WHERE `id` = {}",
            pathId);

        routeRecordingSession = {};
        routeRecordingActive = false;

        handler->PSendSysMessage(
            "Canceled route recording and deleted unfinished movement path {} ({}).",
            pathId,
            pathName);
        return true;
    }

    static bool HandleRouteNodeAddCommand(ChatHandler* handler, uint32 nodeId, std::string nodeName)
    {
        if (!lwiConfig.GetConfigValue<bool>(LwiConfig::Debug))
        {
            handler->SendSysMessage(
                "Living World Invasions debug commands are disabled. Set LWI.Debug = 1 to use .lwi route node add.");
            return false;
        }

        Player* player = GetCommandPlayer(handler);
        if (!player)
            return false;

        if (!IsSafeRouteRecordName(nodeName))
        {
            handler->SendSysMessage(
                "Route node name must be 1-120 characters and may contain only letters, numbers, underscores, and hyphens.");
            return false;
        }

        if (WorldDatabase.Query("SELECT 1 FROM `lwi_route_node` WHERE `id` = {} LIMIT 1", nodeId))
        {
            handler->PSendSysMessage(
                "Route node ID {} already exists. Choose an unused node ID; this command will never overwrite an existing route node.",
                nodeId);
            return false;
        }

        if (WorldDatabase.Query("SELECT 1 FROM `lwi_route_node` WHERE `name` = '{}' LIMIT 1", nodeName))
        {
            handler->PSendSysMessage(
                "Route node name {} already exists. Choose a unique route node name.",
                nodeName);
            return false;
        }

        WorldDatabase.Execute(
            "INSERT INTO `lwi_route_node` "
            "(`id`, `name`, `map_id`, `x`, `y`, `z`, `orientation`, `arrival_radius`, `enabled`, `comment`) "
            "VALUES ({}, '{}', {}, {}, {}, {}, {}, 5.0, 1, 'Created in-game with .lwi route node add')",
            nodeId,
            nodeName,
            player->GetMapId(),
            player->GetPositionX(),
            player->GetPositionY(),
            player->GetPositionZ(),
            player->GetOrientation());

        handler->PSendSysMessage(
            "Created route node {} ({}) on map {} at X {:.3f} Y {:.3f} Z {:.3f} O {:.3f}. Use .lwi reload before using it in loaded route definitions.",
            nodeId,
            nodeName,
            player->GetMapId(),
            player->GetPositionX(),
            player->GetPositionY(),
            player->GetPositionZ(),
            player->GetOrientation());
        return true;
    }

    static bool HandleRouteSegmentAddCommand(
        ChatHandler* handler,
        uint32 segmentId,
        std::string segmentName,
        uint32 startNodeId,
        uint32 endNodeId,
        uint32 movementPathId)
    {
        if (!lwiConfig.GetConfigValue<bool>(LwiConfig::Debug))
        {
            handler->SendSysMessage(
                "Living World Invasions debug commands are disabled. Set LWI.Debug = 1 to use .lwi route segment add.");
            return false;
        }

        if (!GetCommandPlayer(handler))
            return false;

        if (!IsSafeRouteRecordName(segmentName))
        {
            handler->SendSysMessage(
                "Route segment name must be 1-120 characters and may contain only letters, numbers, underscores, and hyphens.");
            return false;
        }

        if (startNodeId == endNodeId)
        {
            handler->SendSysMessage("A route segment must connect two different route nodes.");
            return false;
        }

        if (WorldDatabase.Query("SELECT 1 FROM `lwi_route_segment` WHERE `id` = {} LIMIT 1", segmentId))
        {
            handler->PSendSysMessage(
                "Route segment ID {} already exists. Choose an unused segment ID; this command will never overwrite an existing route segment.",
                segmentId);
            return false;
        }

        if (WorldDatabase.Query("SELECT 1 FROM `lwi_route_segment` WHERE `name` = '{}' LIMIT 1", segmentName))
        {
            handler->PSendSysMessage(
                "Route segment name {} already exists. Choose a unique route segment name.",
                segmentName);
            return false;
        }

        QueryResult startNodeResult = WorldDatabase.Query(
            "SELECT `name`, `map_id` FROM `lwi_route_node` WHERE `id` = {} AND `enabled` = 1 LIMIT 1",
            startNodeId);
        if (!startNodeResult)
        {
            handler->PSendSysMessage(
                "Start route node {} does not exist or is disabled.",
                startNodeId);
            return false;
        }

        QueryResult endNodeResult = WorldDatabase.Query(
            "SELECT `name`, `map_id` FROM `lwi_route_node` WHERE `id` = {} AND `enabled` = 1 LIMIT 1",
            endNodeId);
        if (!endNodeResult)
        {
            handler->PSendSysMessage(
                "End route node {} does not exist or is disabled.",
                endNodeId);
            return false;
        }

        Field* startFields = startNodeResult->Fetch();
        Field* endFields = endNodeResult->Fetch();
        std::string const startNodeName = startFields[0].Get<std::string>();
        uint16 const startMapId = startFields[1].Get<uint16>();
        std::string const endNodeName = endFields[0].Get<std::string>();
        uint16 const endMapId = endFields[1].Get<uint16>();

        if (startMapId != endMapId)
        {
            handler->PSendSysMessage(
                "Route nodes {} ({}, map {}) and {} ({}, map {}) are on different maps. A movement-path segment must remain on one map.",
                startNodeId,
                startNodeName,
                startMapId,
                endNodeId,
                endNodeName,
                endMapId);
            return false;
        }

        QueryResult pathResult = WorldDatabase.Query(
            "SELECT `name` FROM `lwi_movement_path` WHERE `id` = {} AND `enabled` = 1 LIMIT 1",
            movementPathId);
        if (!pathResult)
        {
            handler->PSendSysMessage(
                "Movement path {} does not exist or is disabled.",
                movementPathId);
            return false;
        }

        QueryResult pathMapResult = WorldDatabase.Query(
            "SELECT MIN(`map_id`), MAX(`map_id`), COUNT(*) FROM `lwi_movement_node` "
            "WHERE `path_id` = {} AND `enabled` = 1",
            movementPathId);
        if (!pathMapResult)
        {
            handler->PSendSysMessage(
                "Movement path {} has no enabled movement nodes.",
                movementPathId);
            return false;
        }

        Field* pathMapFields = pathMapResult->Fetch();
        uint64 const pathNodeCount = pathMapFields[2].Get<uint64>();
        if (pathNodeCount < 2)
        {
            handler->PSendSysMessage(
                "Movement path {} has only {} enabled node(s). A route segment requires at least two movement nodes.",
                movementPathId,
                pathNodeCount);
            return false;
        }

        uint16 const pathMinMapId = pathMapFields[0].Get<uint16>();
        uint16 const pathMaxMapId = pathMapFields[1].Get<uint16>();
        if (pathMinMapId != pathMaxMapId || pathMinMapId != startMapId)
        {
            handler->PSendSysMessage(
                "Movement path {} is on map {}-{}, but route nodes {} and {} are on map {}. Segment not created.",
                movementPathId,
                pathMinMapId,
                pathMaxMapId,
                startNodeId,
                endNodeId,
                startMapId);
            return false;
        }

        std::string const movementPathName = pathResult->Fetch()[0].Get<std::string>();

        WorldDatabase.Execute(
            "INSERT INTO `lwi_route_segment` "
            "(`id`, `name`, `start_node_id`, `end_node_id`, `movement_path_id`, `enabled`, `comment`) "
            "VALUES ({}, '{}', {}, {}, {}, 1, 'Created in-game with .lwi route segment add')",
            segmentId,
            segmentName,
            startNodeId,
            endNodeId,
            movementPathId);

        handler->PSendSysMessage(
            "Created route segment {} ({}) from node {} ({}) to node {} ({}) using movement path {} ({}). Use .lwi reload before testing it.",
            segmentId,
            segmentName,
            startNodeId,
            startNodeName,
            endNodeId,
            endNodeName,
            movementPathId,
            movementPathName);
        return true;
    }

    static bool HandleRouteTestCommand(ChatHandler* handler, uint32 routeSegmentId, uint32 fromNodeId)
    {
        if (!lwiConfig.GetConfigValue<bool>(LwiConfig::Debug))
        {
            handler->SendSysMessage(
                "Living World Invasions debug commands are disabled. Set LWI.Debug = 1 to use .lwi route test.");
            return false;
        }

        Creature* creature = handler->getSelectedCreature();
        if (!creature)
        {
            handler->SendSysMessage(
                "Select a creature to use as the route-test traveler, then use .lwi route test <segmentId> <fromNodeId>.");
            return false;
        }

        lwi::RouteSegmentDefinition const* segment = sInvasionMgr.GetRouteSegment(routeSegmentId);
        if (!segment)
        {
            handler->PSendSysMessage(
                "Living World Invasions route segment {} does not exist or is disabled.",
                routeSegmentId);
            return false;
        }

        if (fromNodeId != segment->StartNodeId && fromNodeId != segment->EndNodeId)
        {
            handler->PSendSysMessage(
                "Route segment {} ({}) connects route nodes {} and {}; {} is not an endpoint.",
                segment->Id,
                segment->Name,
                segment->StartNodeId,
                segment->EndNodeId,
                fromNodeId);
            return false;
        }

        lwi::RouteNodeDefinition const* fromNode = sInvasionMgr.GetRouteNode(fromNodeId);
        uint32 const destinationNodeId = fromNodeId == segment->StartNodeId
            ? segment->EndNodeId
            : segment->StartNodeId;
        lwi::RouteNodeDefinition const* destinationNode = sInvasionMgr.GetRouteNode(destinationNodeId);

        if (!fromNode || !destinationNode)
        {
            handler->SendSysMessage(
                "Living World Invasions route test could not resolve the route endpoint definitions.");
            return false;
        }

        if (creature->GetMapId() != fromNode->MapId)
        {
            handler->PSendSysMessage(
                "Selected creature is on map {}, but route node {} ({}) is on map {}.",
                creature->GetMapId(),
                fromNode->Id,
                fromNode->Name,
                fromNode->MapId);
            return false;
        }

        for (uint64 const runtimeGroupId : routeTestGroupIds)
        {
            lwi::RuntimeEntityGroup const* existingGroup = sRuntimeEntityGroupMgr.GetGroup(runtimeGroupId);
            if (!existingGroup)
            {
                continue;
            }

            for (lwi::RuntimeEntity const& entity : existingGroup->Entities)
            {
                if (entity.Guid == creature->GetGUID())
                {
                    handler->PSendSysMessage(
                        "Selected creature is already being used by active route test group #{}.",
                        runtimeGroupId);
                    return false;
                }
            }
        }

        lwi::RuntimeEntityGroup& testGroup = sRuntimeEntityGroupMgr.CreateGroup(0, 0);

        lwi::RuntimeEntity entity;
        entity.EntityType = static_cast<uint8>(lwi::EntityProviderType::Creature);
        entity.MapId = creature->GetMapId();
        entity.Entry = creature->GetEntry();
        entity.Guid = creature->GetGUID();
        testGroup.Entities.push_back(entity);

        uint64 const runtimeGroupId = testGroup.Id;
        routeTestGroupIds.insert(runtimeGroupId);

        if (!sMovementController.StartRouteSegment(
                runtimeGroupId,
                routeSegmentId,
                fromNodeId))
        {
            routeTestGroupIds.erase(runtimeGroupId);
            sRuntimeEntityGroupMgr.RemoveGroup(runtimeGroupId);

            handler->PSendSysMessage(
                "Living World Invasions failed to start route segment {} ({}) for the selected creature.",
                segment->Id,
                segment->Name);
            return false;
        }

        handler->PSendSysMessage(
            "Route test group #{} started segment {} ({}) from {} to {} using selected creature {} (entry {}).",
            runtimeGroupId,
            segment->Id,
            segment->Name,
            fromNode->Name,
            destinationNode->Name,
            creature->GetName(),
            creature->GetEntry());
        return true;
    }

    static bool HandleTriggerCommand(ChatHandler* handler, uint32 invasionId)
    {
        if (!lwiConfig.GetConfigValue<bool>(LwiConfig::Debug))
        {
            handler->SendSysMessage("Living World Invasions debug commands are disabled. Set LWI.Debug = 1 to use .lwi trigger.");
            return false;
        }

        if (!sInvasionMgr.GetDefinition(invasionId))
        {
            handler->PSendSysMessage("Living World Invasions invasion {} does not exist or is disabled.", invasionId);
            return false;
        }

        if (sInvasionScheduler.GetControlState() != lwi::SchedulerControlState::Running)
        {
            handler->SendSysMessage("Living World Invasions scheduler is stopped. Use .lwi start before triggering an invasion.");
            return false;
        }

        if (!sInvasionScheduler.TriggerInvasion(invasionId))
        {
            handler->PSendSysMessage("Living World Invasions could not trigger invasion {}. It may already be active or the scheduler may be unavailable.", invasionId);
            return false;
        }

        handler->PSendSysMessage("Living World Invasions manually triggered invasion {}.", invasionId);
        return true;
    }
};

}

void AddLivingWorldInvasionsScripts()
{
    new LivingWorldInvasionsWorldScript();
    new LivingWorldInvasionsCommandScript();
}
