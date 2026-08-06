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
    }
};

LwiConfigData lwiConfig;

class LivingWorldInvasionsWorldScript final : public WorldScript
{
public:
    LivingWorldInvasionsWorldScript() : WorldScript("LivingWorldInvasionsWorldScript", {
        WORLDHOOK_ON_BEFORE_CONFIG_LOAD,
        WORLDHOOK_ON_STARTUP
    }) { }

    void OnBeforeConfigLoad(bool reload) override
    {
        lwiConfig.Initialize(reload);
    }

	void OnStartup() override
	{
	    LOG_INFO("server.loading",
	        "Living World Invasions: startup hook reached.");

	    if (!lwiConfig.GetConfigValue<bool>(LwiConfig::Enabled))
	    {
	        LOG_INFO("server.loading",
	            "Living World Invasions is disabled.");
	        return;
	    }

	    sInvasionMgr.LoadDefinitions();

	    LOG_INFO("server.loading",
	        "Living World Invasions initialized. "
	        "Playerbots integration requested: {}.",
	        lwiConfig.GetConfigValue<bool>(
	            LwiConfig::PlayerbotsEnabled) ? "yes" : "no");
	}
};
}

void AddLivingWorldInvasionsScripts()
{
    new LivingWorldInvasionsWorldScript();
}
