#include "Chat.h"
#include "CommandScript.h"
#include "InvasionScheduler.h"
#include "InvasionRuntimeManager.h"
#include "LivingWorldInvasions.h"
#include "RuntimeSignalManager.h"

#include "ConfigValueCache.h"
#include "Log.h"
#include "ScriptMgr.h"

using namespace Acore::ChatCommands;

namespace
{
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
                "abort",
                abortCommandTable
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
