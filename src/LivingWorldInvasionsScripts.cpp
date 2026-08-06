#include "InvasionScheduler.h"
#include "LivingWorldInvasions.h"

#include "ConfigValueCache.h"
#include "Log.h"
#include "ScriptMgr.h"

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
    SchedulerTestActiveDurationSeconds,
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
        SetConfigValue<uint32>(LwiConfig::SchedulerTestActiveDurationSeconds, "LWI.Scheduler.TestActiveDurationSeconds", 45);
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
        WORLDHOOK_ON_UPDATE
    }) { }

    void OnAfterConfigLoad(bool reload) override
    {
        lwiConfig.Initialize(reload);

        if (!lwiConfig.GetConfigValue<bool>(LwiConfig::Enabled))
        {
            LOG_INFO("server.loading", "Living World Invasions is disabled.");
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
        settings.TestActiveDurationSeconds = lwiConfig.GetConfigValue<uint32>(LwiConfig::SchedulerTestActiveDurationSeconds);
        settings.MaxActiveGlobal = lwiConfig.GetConfigValue<uint32>(LwiConfig::SchedulerMaxActiveGlobal);
        settings.DefaultMaxActivePerMap = lwiConfig.GetConfigValue<uint32>(LwiConfig::SchedulerDefaultMaxActivePerMap);
        settings.DefaultMaxActivePerResponseOrigin = lwiConfig.GetConfigValue<uint32>(LwiConfig::SchedulerDefaultMaxActivePerResponseOrigin);

        sInvasionScheduler.Configure(settings);
        sInvasionScheduler.Initialize();

        LOG_INFO("server.loading", "Living World Invasions initialized. Playerbots integration requested: {}.",
            lwiConfig.GetConfigValue<bool>(LwiConfig::PlayerbotsEnabled) ? "yes" : "no");
    }

    void OnUpdate(uint32 diff) override
    {
        sInvasionScheduler.Update(diff);
    }
};
}

void AddLivingWorldInvasionsScripts()
{
    new LivingWorldInvasionsWorldScript();
}
