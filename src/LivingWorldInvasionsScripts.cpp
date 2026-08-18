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
#include "WaypointMgr.h"
#include "TemporarySummon.h"
#include "Map.h"
#include "ObjectAccessor.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <queue>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace Acore::ChatCommands;

namespace
{
std::unordered_set<uint64> routeTestGroupIds;

std::vector<ObjectGuid> routePathMarkerGuids;
constexpr uint32 RoutePathMarkerLifetimeMs = 600000;

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

constexpr float RoutePathBuildSpacingYards = 5.0f;
constexpr float RoutePathBuildEndpointSnapYards = 0.25f;

struct RoutePathBuildSession
{
    ObjectGuid OwnerGuid;
    uint16 MapId = 0;

    std::string StartName;
    std::string EndName;
    std::string PathName;
    std::string SegmentName;

    uint32 StartRouteNodeId = 0;
    uint32 EndRouteNodeId = 0;
    bool StartRouteNodeCreated = false;
    bool EndRouteNodeExists = false;

    uint32 PathId = 0;
    uint32 SegmentId = 0;
    uint32 NextMovementNodeId = 1;
    uint16 NextNodeOrder = 10;
    uint32 LastMovementNodeId = 0;

    bool Paused = false;
    uint32 NodeCount = 0;
    float TotalDistance = 0.0f;
    float DistanceSinceLastNode = 0.0f;

    bool HasLastSample = false;
    float LastSampleX = 0.0f;
    float LastSampleY = 0.0f;
    float LastSampleZ = 0.0f;

    float LastRecordedX = 0.0f;
    float LastRecordedY = 0.0f;
    float LastRecordedZ = 0.0f;
};

RoutePathBuildSession routePathBuildSession;
bool routePathBuildActive = false;

float RouteDistance3D(float x1, float y1, float z1, float x2, float y2, float z2)
{
    float const dx = x2 - x1;
    float const dy = y2 - y1;
    float const dz = z2 - z1;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool InsertAutoBuildMovementNode(float x, float y, float z, float orientation)
{
    if (!routePathBuildActive || routePathBuildSession.NextNodeOrder > 65520)
        return false;

    uint32 const nodeId = routePathBuildSession.NextMovementNodeId++;
    uint16 const nodeOrder = routePathBuildSession.NextNodeOrder;
    routePathBuildSession.NextNodeOrder = static_cast<uint16>(routePathBuildSession.NextNodeOrder + 10);

    WorldDatabase.Execute(
        "INSERT INTO `lwi_movement_node` "
        "(`id`, `path_id`, `node_order`, `map_id`, `x`, `y`, `z`, `orientation`, `wait_ms`, `profile_override_id`, `enabled`, `comment`) "
        "VALUES ({}, {}, {}, {}, {}, {}, {}, {}, 0, 0, 1, '{} auto-build node {}')",
        nodeId,
        routePathBuildSession.PathId,
        nodeOrder,
        routePathBuildSession.MapId,
        x, y, z, orientation,
        routePathBuildSession.PathName,
        nodeOrder);

    routePathBuildSession.LastMovementNodeId = nodeId;
    routePathBuildSession.LastRecordedX = x;
    routePathBuildSession.LastRecordedY = y;
    routePathBuildSession.LastRecordedZ = z;
    ++routePathBuildSession.NodeCount;
    return true;
}

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

void ClearMarkerList(Player* player, std::vector<ObjectGuid>& guids)
{
    if (player)
    {
        Map* map = player->GetMap();
        if (map)
        {
            for (ObjectGuid const& guid : guids)
            {
                if (Creature* marker = map->GetCreature(guid))
                {
                    marker->DespawnOrUnsummon();
                }
            }
        }
    }

    guids.clear();
}

bool TryParseRouteId(std::string const& token, uint32& id)
{
    if (token.empty())
        return false;

    uint32 value = 0;
    auto const result = std::from_chars(token.data(), token.data() + token.size(), value);
    if (result.ec != std::errc() || result.ptr != token.data() + token.size())
        return false;

    id = value;
    return true;
}

lwi::RouteNodeDefinition const* ResolveRouteNode(std::string const& token)
{
    uint32 id = 0;
    if (TryParseRouteId(token, id))
        return sInvasionMgr.GetRouteNode(id);

    return sInvasionMgr.GetRouteNode(token);
}

lwi::RouteSegmentDefinition const* ResolveRouteSegment(std::string const& token)
{
    uint32 id = 0;
    if (TryParseRouteId(token, id))
        return sInvasionMgr.GetRouteSegment(id);

    return sInvasionMgr.GetRouteSegment(token);
}

std::string SqlQuote(std::string const& value)
{
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (char const c : value)
    {
        if (c == '\\' || c == '\'')
            escaped.push_back('\\');
        escaped.push_back(c);
    }

    return "'" + escaped + "'";
}

std::string SqlNullableText(std::string const& value)
{
    return value.empty() ? "NULL" : SqlQuote(value);
}

std::string SafeExportFileComponent(std::string value)
{
    for (char& c : value)
    {
        unsigned char const uc = static_cast<unsigned char>(c);
        if (!std::isalnum(uc) && c != '_' && c != '-')
            c = '_';
    }
    return value;
}

bool BuildRouteExportJourney(uint32 fromNodeId, uint32 destinationNodeId, std::vector<uint32>& segmentIds)
{
    segmentIds.clear();
    if (fromNodeId == destinationNodeId)
        return true;

    struct PreviousStep
    {
        uint32 PreviousNodeId = 0;
        uint32 SegmentId = 0;
    };

    std::queue<uint32> pending;
    std::unordered_set<uint32> visited;
    std::unordered_map<uint32, PreviousStep> previous;

    visited.insert(fromNodeId);
    pending.push(fromNodeId);

    while (!pending.empty())
    {
        uint32 const nodeId = pending.front();
        pending.pop();

        for (auto const& [segmentId, segment] : sInvasionMgr.GetRouteSegments())
        {
            uint32 nextNodeId = 0;
            if (segment.StartNodeId == nodeId)
                nextNodeId = segment.EndNodeId;
            else if (segment.EndNodeId == nodeId)
                nextNodeId = segment.StartNodeId;
            else
                continue;

            if (!visited.insert(nextNodeId).second)
                continue;

            previous[nextNodeId] = PreviousStep{ nodeId, segmentId };
            if (nextNodeId == destinationNodeId)
            {
                uint32 cursor = destinationNodeId;
                while (cursor != fromNodeId)
                {
                    auto const itr = previous.find(cursor);
                    if (itr == previous.end())
                        return false;
                    segmentIds.push_back(itr->second.SegmentId);
                    cursor = itr->second.PreviousNodeId;
                }
                std::reverse(segmentIds.begin(), segmentIds.end());
                return true;
            }

            pending.push(nextNodeId);
        }
    }

    return false;
}

bool WriteRouteExport(std::vector<uint32> const& requestedSegmentIds, std::string const& exportName, std::string& outputPath, std::string& error)
{
    std::vector<uint32> segmentIds = requestedSegmentIds;
    std::sort(segmentIds.begin(), segmentIds.end());
    segmentIds.erase(std::unique(segmentIds.begin(), segmentIds.end()), segmentIds.end());

    if (segmentIds.empty())
    {
        error = "No route segments were selected for export.";
        return false;
    }

    std::vector<lwi::RouteSegmentDefinition const*> segments;
    std::unordered_set<uint32> routeNodeIds;
    std::unordered_set<uint32> pathIds;

    for (uint32 const segmentId : segmentIds)
    {
        lwi::RouteSegmentDefinition const* segment = sInvasionMgr.GetRouteSegment(segmentId);
        if (!segment)
        {
            error = "Route segment " + std::to_string(segmentId) + " does not exist or is disabled.";
            return false;
        }

        segments.push_back(segment);
        routeNodeIds.insert(segment->StartNodeId);
        routeNodeIds.insert(segment->EndNodeId);
        pathIds.insert(segment->MovementPathId);
    }

    std::filesystem::path exportDirectory = std::filesystem::current_path() / "lwi_exports";
    std::error_code ec;
    std::filesystem::create_directories(exportDirectory, ec);
    if (ec)
    {
        error = "Could not create export directory: " + ec.message();
        return false;
    }

    std::filesystem::path filePath = exportDirectory / (SafeExportFileComponent(exportName) + ".sql");
    std::ofstream out(filePath, std::ios::trunc);
    if (!out)
    {
        error = "Could not open export file for writing: " + filePath.string();
        return false;
    }

    out << "-- Living World Invasions route export\n";
    out << "-- Generated by the in-game LWI route exporter.\n";
    out << "-- Contains route nodes, movement paths/nodes/actions, and route segments.\n\n";

    out << "-- Remove exported segment definitions first so movement data can be refreshed safely.\n";
    out << "DELETE FROM `lwi_route_segment` WHERE `id` IN (";
    for (std::size_t i = 0; i < segmentIds.size(); ++i)
    {
        if (i) out << ", ";
        out << segmentIds[i];
    }
    out << ");\n\n";

    std::vector<uint32> sortedPathIds(pathIds.begin(), pathIds.end());
    std::sort(sortedPathIds.begin(), sortedPathIds.end());
    out << "DELETE a FROM `lwi_movement_node_action` a JOIN `lwi_movement_node` n ON n.`id` = a.`node_id` WHERE n.`path_id` IN (";
    for (std::size_t i = 0; i < sortedPathIds.size(); ++i)
    {
        if (i) out << ", ";
        out << sortedPathIds[i];
    }
    out << ");\n";
    out << "DELETE FROM `lwi_movement_node` WHERE `path_id` IN (";
    for (std::size_t i = 0; i < sortedPathIds.size(); ++i)
    {
        if (i) out << ", ";
        out << sortedPathIds[i];
    }
    out << ");\n\n";

    std::vector<uint32> sortedRouteNodeIds(routeNodeIds.begin(), routeNodeIds.end());
    std::sort(sortedRouteNodeIds.begin(), sortedRouteNodeIds.end());
    out << "-- Route nodes\n";
    for (uint32 const nodeId : sortedRouteNodeIds)
    {
        lwi::RouteNodeDefinition const* node = sInvasionMgr.GetRouteNode(nodeId);
        if (!node)
        {
            error = "Could not resolve route node " + std::to_string(nodeId) + " while exporting.";
            return false;
        }

        out << "INSERT INTO `lwi_route_node` (`id`,`name`,`map_id`,`x`,`y`,`z`,`orientation`,`arrival_radius`,`enabled`,`comment`) VALUES ("
            << node->Id << "," << SqlQuote(node->Name) << "," << node->MapId << ","
            << std::fixed << std::setprecision(6) << node->X << "," << node->Y << "," << node->Z << "," << node->Orientation << ","
            << node->ArrivalRadius << "," << (node->Enabled ? 1 : 0) << "," << SqlNullableText(node->Comment) << ") "
            << "ON DUPLICATE KEY UPDATE `name`=VALUES(`name`),`map_id`=VALUES(`map_id`),`x`=VALUES(`x`),`y`=VALUES(`y`),`z`=VALUES(`z`),`orientation`=VALUES(`orientation`),`arrival_radius`=VALUES(`arrival_radius`),`enabled`=VALUES(`enabled`),`comment`=VALUES(`comment`);\n";
    }
    out << "\n-- Movement paths and nodes\n";

    for (uint32 const pathId : sortedPathIds)
    {
        lwi::MovementPathDefinition const* path = sInvasionMgr.GetMovementPath(pathId);
        std::vector<lwi::MovementNodeDefinition> const* nodes = sInvasionMgr.GetMovementNodes(pathId);
        if (!path || !nodes || nodes->empty())
        {
            error = "Movement path " + std::to_string(pathId) + " is missing or has no enabled nodes.";
            return false;
        }

        out << "INSERT INTO `lwi_movement_path` (`id`,`name`,`enabled`,`comment`) VALUES ("
            << path->Id << "," << SqlQuote(path->Name) << "," << (path->Enabled ? 1 : 0) << "," << SqlNullableText(path->Comment) << ") "
            << "ON DUPLICATE KEY UPDATE `name`=VALUES(`name`),`enabled`=VALUES(`enabled`),`comment`=VALUES(`comment`);\n";

        for (lwi::MovementNodeDefinition const& node : *nodes)
        {
            out << "INSERT INTO `lwi_movement_node` (`id`,`path_id`,`node_order`,`map_id`,`x`,`y`,`z`,`orientation`,`wait_ms`,`profile_override_id`,`enabled`,`comment`) VALUES ("
                << node.Id << "," << node.PathId << "," << node.NodeOrder << "," << node.MapId << ","
                << std::fixed << std::setprecision(6) << node.X << "," << node.Y << "," << node.Z << "," << node.Orientation << ","
                << node.WaitMs << "," << node.ProfileOverrideId << "," << (node.Enabled ? 1 : 0) << "," << SqlNullableText(node.Comment) << ");\n";

            if (std::vector<lwi::MovementNodeActionDefinition> const* actions = sInvasionMgr.GetMovementNodeActions(node.Id))
            {
                for (lwi::MovementNodeActionDefinition const& action : *actions)
                {
                    out << "INSERT INTO `lwi_movement_node_action` (`id`,`node_id`,`action_order`,`action_type`,`target_id`,`parameter1`,`parameter2`,`parameter3`,`enabled`,`comment`) VALUES ("
                        << action.Id << "," << action.NodeId << "," << action.ActionOrder << "," << static_cast<uint32>(action.ActionType) << ","
                        << action.TargetId << "," << action.Parameter1 << "," << action.Parameter2 << "," << action.Parameter3 << ","
                        << (action.Enabled ? 1 : 0) << "," << SqlNullableText(action.Comment) << ") "
                        << "ON DUPLICATE KEY UPDATE `node_id`=VALUES(`node_id`),`action_order`=VALUES(`action_order`),`action_type`=VALUES(`action_type`),`target_id`=VALUES(`target_id`),`parameter1`=VALUES(`parameter1`),`parameter2`=VALUES(`parameter2`),`parameter3`=VALUES(`parameter3`),`enabled`=VALUES(`enabled`),`comment`=VALUES(`comment`);\n";
                }
            }
        }
        out << "\n";
    }

    out << "-- Route segments\n";
    for (lwi::RouteSegmentDefinition const* segment : segments)
    {
        out << "INSERT INTO `lwi_route_segment` (`id`,`name`,`start_node_id`,`end_node_id`,`movement_path_id`,`enabled`,`comment`) VALUES ("
            << segment->Id << "," << SqlQuote(segment->Name) << "," << segment->StartNodeId << "," << segment->EndNodeId << ","
            << segment->MovementPathId << "," << (segment->Enabled ? 1 : 0) << "," << SqlNullableText(segment->Comment) << ") "
            << "ON DUPLICATE KEY UPDATE `name`=VALUES(`name`),`start_node_id`=VALUES(`start_node_id`),`end_node_id`=VALUES(`end_node_id`),`movement_path_id`=VALUES(`movement_path_id`),`enabled`=VALUES(`enabled`),`comment`=VALUES(`comment`);\n";
    }

    out.flush();
    if (!out)
    {
        error = "An error occurred while writing export file: " + filePath.string();
        return false;
    }

    outputPath = std::filesystem::absolute(filePath).string();
    return true;
}

Creature* SpawnRoutePathMarker(WorldObject* summoner, float x, float y, float z, float orientation, float scale)
{
    if (!summoner)
        return nullptr;

    TempSummon* marker = summoner->SummonCreature(
        VISUAL_WAYPOINT,
        x, y, z, orientation,
        TEMPSUMMON_TIMED_DESPAWN,
        RoutePathMarkerLifetimeMs);

    if (!marker)
        return nullptr;

    marker->SetObjectScale(scale);
    return marker;
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

        if (routePathBuildActive && !routePathBuildSession.Paused)
        {
            Player* builder = ObjectAccessor::FindConnectedPlayer(routePathBuildSession.OwnerGuid);

            if (builder && builder->IsInWorld() && builder->GetMapId() == routePathBuildSession.MapId)
            {
                float const currentX = builder->GetPositionX();
                float const currentY = builder->GetPositionY();
                float const currentZ = builder->GetPositionZ();

                if (!routePathBuildSession.HasLastSample)
                {
                    routePathBuildSession.HasLastSample = true;
                    routePathBuildSession.LastSampleX = currentX;
                    routePathBuildSession.LastSampleY = currentY;
                    routePathBuildSession.LastSampleZ = currentZ;
                }
                else
                {
                    float segmentStartX = routePathBuildSession.LastSampleX;
                    float segmentStartY = routePathBuildSession.LastSampleY;
                    float segmentStartZ = routePathBuildSession.LastSampleZ;
                    float remainingSegment = RouteDistance3D(
                        segmentStartX, segmentStartY, segmentStartZ,
                        currentX, currentY, currentZ);

                    routePathBuildSession.TotalDistance += remainingSegment;

                    while (remainingSegment > 0.0001f &&
                           routePathBuildSession.DistanceSinceLastNode + remainingSegment >= RoutePathBuildSpacingYards)
                    {
                        float const needed = RoutePathBuildSpacingYards - routePathBuildSession.DistanceSinceLastNode;
                        float const t = needed / remainingSegment;

                        float const pointX = segmentStartX + (currentX - segmentStartX) * t;
                        float const pointY = segmentStartY + (currentY - segmentStartY) * t;
                        float const pointZ = segmentStartZ + (currentZ - segmentStartZ) * t;

                        float orientation = builder->GetOrientation();
                        float const headingDx = currentX - segmentStartX;
                        float const headingDy = currentY - segmentStartY;
                        if (std::fabs(headingDx) > 0.0001f || std::fabs(headingDy) > 0.0001f)
                            orientation = std::atan2(headingDy, headingDx);

                        if (!InsertAutoBuildMovementNode(pointX, pointY, pointZ, orientation))
                        {
                            routePathBuildSession.Paused = true;
                            LOG_ERROR("server.loading",
                                "[LWI Route Build] Auto-build path {} reached the supported node-order limit and was paused.",
                                routePathBuildSession.PathId);
                            break;
                        }

                        segmentStartX = pointX;
                        segmentStartY = pointY;
                        segmentStartZ = pointZ;
                        remainingSegment = RouteDistance3D(
                            segmentStartX, segmentStartY, segmentStartZ,
                            currentX, currentY, currentZ);
                        routePathBuildSession.DistanceSinceLastNode = 0.0f;
                    }

                    if (!routePathBuildSession.Paused)
                        routePathBuildSession.DistanceSinceLastNode += remainingSegment;

                    routePathBuildSession.LastSampleX = currentX;
                    routePathBuildSession.LastSampleY = currentY;
                    routePathBuildSession.LastSampleZ = currentZ;
                }
            }
        }

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

        static ChatCommandTable routePathCancelCommandTable =
        {
            {
                "confirm",
                HandleRoutePathBuildCancelConfirmCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::No
            },
            {
                "",
                HandleRoutePathBuildCancelCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::No
            }
        };

        static ChatCommandTable routeNetworkResetCommandTable =
        {
            {
                "confirm",
                HandleRouteNetworkResetConfirmCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::No
            },
            {
                "",
                HandleRouteNetworkResetCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::No
            }
        };

        static ChatCommandTable routeNetworkCommandTable =
        {
            {
                "reset",
                routeNetworkResetCommandTable
            }
        };

        static ChatCommandTable routePathCommandTable =
        {
            {
                "build",
                HandleRoutePathBuildCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::No
            },
            {
                "complete",
                HandleRoutePathBuildCompleteCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::No
            },
            {
                "status",
                HandleRoutePathBuildStatusCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::No
            },
            {
                "pause",
                HandleRoutePathBuildPauseCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::No
            },
            {
                "resume",
                HandleRoutePathBuildResumeCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::No
            },
            {
                "cancel",
                routePathCancelCommandTable
            },
            {
                "show",
                HandleRoutePathShowCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::No
            },
            {
                "hide",
                HandleRoutePathHideCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::No
            },
            {
                "nearest",
                HandleRoutePathNearestCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::No
            }
        };

        static ChatCommandTable routeExportCommandTable =
        {
            {
                "segment",
                HandleRouteExportSegmentCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::No
            },
            {
                "journey",
                HandleRouteExportJourneyCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::No
            },
            {
                "network",
                HandleRouteExportNetworkCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::No
            }
        };

        static ChatCommandTable routeCommandTable =
        {
            {
                "network",
                routeNetworkCommandTable
            },
            {
                "export",
                routeExportCommandTable
            },
            {
                "path",
                routePathCommandTable
            },
            {
                "node",
                routeNodeCommandTable
            },
            {
                "segment",
                routeSegmentCommandTable
            },
            {
                "travel",
                HandleRouteTravelCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::No
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

    static bool HandleRoutePathBuildCommand(ChatHandler* handler, std::string startName, std::string endName)
    {
        if (!lwiConfig.GetConfigValue<bool>(LwiConfig::Debug))
        {
            handler->SendSysMessage("Living World Invasions debug commands are disabled. Set LWI.Debug = 1 to build routes.");
            return false;
        }

        Player* player = GetCommandPlayer(handler);
        if (!player)
            return false;

        if (routePathBuildActive)
        {
            handler->PSendSysMessage(
                "A route path build is already active: {} -> {} (path {}). Complete or cancel it first.",
                routePathBuildSession.StartName,
                routePathBuildSession.EndName,
                routePathBuildSession.PathId);
            return false;
        }

        if (routeRecordingActive)
        {
            handler->SendSysMessage("The manual route recorder is active. Finish or cancel it before starting an automatic path build.");
            return false;
        }

        if (!IsSafeRouteRecordName(startName) || !IsSafeRouteRecordName(endName) || startName == endName)
        {
            handler->SendSysMessage("Start/end names must be different, 1-120 characters, and contain only letters, numbers, underscores, or hyphens.");
            return false;
        }

        std::string const pathName = startName + "_" + endName;
        std::string const segmentName = startName + "_" + endName;
        if (pathName.size() > 120)
        {
            handler->SendSysMessage("The combined start/end names are too long for an LWI path name (maximum 120 characters).");
            return false;
        }

        if (WorldDatabase.Query("SELECT 1 FROM `lwi_route_segment` WHERE `name` = '{}' LIMIT 1", segmentName))
        {
            handler->PSendSysMessage("Route segment {} already exists. Reset/remove it before rebuilding this same connection.", segmentName);
            return false;
        }

        uint32 startRouteNodeId = 0;
        bool startCreated = false;
        if (QueryResult result = WorldDatabase.Query(
            "SELECT `id`, `map_id`, `x`, `y`, `z` FROM `lwi_route_node` WHERE `name` = '{}' LIMIT 1",
            startName))
        {
            Field* fields = result->Fetch();
            startRouteNodeId = fields[0].Get<uint32>();
            uint16 const mapId = fields[1].Get<uint16>();
            if (mapId != player->GetMapId())
            {
                handler->PSendSysMessage("Existing start route node {} is on map {}, but you are on map {}.", startName, mapId, player->GetMapId());
                return false;
            }

            float const distance = RouteDistance3D(
                player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(),
                fields[2].Get<float>(), fields[3].Get<float>(), fields[4].Get<float>());
            if (distance > 25.0f)
            {
                handler->PSendSysMessage("You are {:.1f} yd from existing start node {}. Stand at that route node before starting the build.", distance, startName);
                return false;
            }
        }
        else
        {
            QueryResult idResult = WorldDatabase.Query("SELECT COALESCE(MAX(`id`), 0) + 10 FROM `lwi_route_node`");
            startRouteNodeId = idResult ? idResult->Fetch()[0].Get<uint32>() : 10;
            WorldDatabase.Execute(
                "INSERT INTO `lwi_route_node` (`id`, `name`, `map_id`, `x`, `y`, `z`, `orientation`, `arrival_radius`, `enabled`, `comment`) "
                "VALUES ({}, '{}', {}, {}, {}, {}, {}, 5.0, 1, 'Created by automatic LWI route path builder')",
                startRouteNodeId,
                startName,
                player->GetMapId(),
                player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), player->GetOrientation());
            startCreated = true;
        }

        uint32 endRouteNodeId = 0;
        bool endExists = false;
        if (QueryResult result = WorldDatabase.Query(
            "SELECT `id`, `map_id` FROM `lwi_route_node` WHERE `name` = '{}' LIMIT 1",
            endName))
        {
            Field* fields = result->Fetch();
            endRouteNodeId = fields[0].Get<uint32>();
            if (fields[1].Get<uint16>() != player->GetMapId())
            {
                if (startCreated)
                    WorldDatabase.Execute("DELETE FROM `lwi_route_node` WHERE `id` = {}", startRouteNodeId);
                handler->PSendSysMessage("Existing end route node {} is on a different map. This builder currently records continuous paths on one map.", endName);
                return false;
            }
            endExists = true;
        }
        else
        {
            QueryResult idResult = WorldDatabase.Query("SELECT COALESCE(MAX(`id`), 0) + 10 FROM `lwi_route_node`");
            endRouteNodeId = idResult ? idResult->Fetch()[0].Get<uint32>() : 10;
            if (endRouteNodeId == startRouteNodeId)
                endRouteNodeId += 10;
        }

        if (WorldDatabase.Query(
            "SELECT 1 FROM `lwi_route_segment` "
            "WHERE (`start_node_id` = {} AND `end_node_id` = {}) "
            "OR (`start_node_id` = {} AND `end_node_id` = {}) LIMIT 1",
            startRouteNodeId, endRouteNodeId, endRouteNodeId, startRouteNodeId))
        {
            if (startCreated)
                WorldDatabase.Execute("DELETE FROM `lwi_route_node` WHERE `id` = {}", startRouteNodeId);
            handler->PSendSysMessage("A shared route segment already connects {} and {}. Reset/remove that segment before rebuilding it.", startName, endName);
            return false;
        }

        QueryResult pathIdResult = WorldDatabase.Query(
            "SELECT GREATEST("
            "COALESCE((SELECT MAX(`id`) FROM `lwi_movement_path`), 0), "
            "COALESCE((SELECT MAX(`id`) FROM `lwi_route_segment`), 0)) + 10");
        uint32 const pathId = pathIdResult ? pathIdResult->Fetch()[0].Get<uint32>() : 10;
        uint32 const segmentId = pathId;

        QueryResult movementNodeIdResult = WorldDatabase.Query("SELECT COALESCE(MAX(`id`), 0) + 1 FROM `lwi_movement_node`");
        uint32 const nextMovementNodeId = movementNodeIdResult ? movementNodeIdResult->Fetch()[0].Get<uint32>() : 1;

        WorldDatabase.Execute(
            "INSERT INTO `lwi_movement_path` (`id`, `name`, `enabled`, `comment`) "
            "VALUES ({}, '{}', 1, 'AUTO BUILD IN PROGRESS: {} -> {}')",
            pathId, pathName, startName, endName);

        routePathBuildSession = {};
        routePathBuildSession.OwnerGuid = player->GetGUID();
        routePathBuildSession.MapId = player->GetMapId();
        routePathBuildSession.StartName = startName;
        routePathBuildSession.EndName = endName;
        routePathBuildSession.PathName = pathName;
        routePathBuildSession.SegmentName = segmentName;
        routePathBuildSession.StartRouteNodeId = startRouteNodeId;
        routePathBuildSession.EndRouteNodeId = endRouteNodeId;
        routePathBuildSession.StartRouteNodeCreated = startCreated;
        routePathBuildSession.EndRouteNodeExists = endExists;
        routePathBuildSession.PathId = pathId;
        routePathBuildSession.SegmentId = segmentId;
        routePathBuildSession.NextMovementNodeId = nextMovementNodeId;
        routePathBuildSession.NextNodeOrder = 10;
        routePathBuildSession.HasLastSample = true;
        routePathBuildSession.LastSampleX = player->GetPositionX();
        routePathBuildSession.LastSampleY = player->GetPositionY();
        routePathBuildSession.LastSampleZ = player->GetPositionZ();
        routePathBuildActive = true;

        if (!InsertAutoBuildMovementNode(
                player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), player->GetOrientation()))
        {
            routePathBuildActive = false;
            WorldDatabase.Execute("DELETE FROM `lwi_movement_path` WHERE `id` = {}", pathId);
            if (startCreated)
                WorldDatabase.Execute("DELETE FROM `lwi_route_node` WHERE `id` = {}", startRouteNodeId);
            handler->SendSysMessage("Failed to create the first automatic route node.");
            return false;
        }

        handler->PSendSysMessage(
            "Automatic LWI route build started: {} (node {}) -> {} (node {}) | path/segment ID {} | recording every {:.1f} yd. Ride/walk the exact route, then use .lwi route path complete at the endpoint.",
            startName, startRouteNodeId, endName, endRouteNodeId, pathId, RoutePathBuildSpacingYards);
        handler->SendSysMessage("Points are written to the world database as you travel; .lwi route path status shows progress.");
        return true;
    }

    static bool HandleRoutePathBuildCompleteCommand(ChatHandler* handler)
    {
        if (!routePathBuildActive)
        {
            handler->SendSysMessage("No automatic route path build is active.");
            return false;
        }

        Player* player = GetCommandPlayer(handler);
        if (!player || player->GetGUID() != routePathBuildSession.OwnerGuid)
        {
            handler->SendSysMessage("Only the GM who started the automatic route build may complete it.");
            return false;
        }

        if (player->GetMapId() != routePathBuildSession.MapId)
        {
            handler->SendSysMessage("You changed maps during this build. Return to the build map or cancel the build.");
            return false;
        }

        float const finalX = player->GetPositionX();
        float const finalY = player->GetPositionY();
        float const finalZ = player->GetPositionZ();
        float const finalO = player->GetOrientation();

        if (routePathBuildSession.EndRouteNodeExists)
        {
            if (QueryResult result = WorldDatabase.Query(
                "SELECT `x`, `y`, `z` FROM `lwi_route_node` WHERE `id` = {} LIMIT 1",
                routePathBuildSession.EndRouteNodeId))
            {
                Field* fields = result->Fetch();
                float const endDistance = RouteDistance3D(
                    finalX, finalY, finalZ,
                    fields[0].Get<float>(), fields[1].Get<float>(), fields[2].Get<float>());
                if (endDistance > 25.0f)
                {
                    handler->PSendSysMessage(
                        "You are {:.1f} yd from existing end node {}. Stand at that route node before completing the build.",
                        endDistance,
                        routePathBuildSession.EndName);
                    return false;
                }
            }
        }
        float const finalGap = RouteDistance3D(
            routePathBuildSession.LastRecordedX,
            routePathBuildSession.LastRecordedY,
            routePathBuildSession.LastRecordedZ,
            finalX, finalY, finalZ);

        if (finalGap > RoutePathBuildEndpointSnapYards)
        {
            if (!InsertAutoBuildMovementNode(finalX, finalY, finalZ, finalO))
            {
                handler->SendSysMessage("Could not record the exact final endpoint; the build remains active.");
                return false;
            }
        }
        else if (routePathBuildSession.LastMovementNodeId != 0)
        {
            WorldDatabase.Execute(
                "UPDATE `lwi_movement_node` SET `x` = {}, `y` = {}, `z` = {}, `orientation` = {} WHERE `id` = {}",
                finalX, finalY, finalZ, finalO,
                routePathBuildSession.LastMovementNodeId);
            routePathBuildSession.LastRecordedX = finalX;
            routePathBuildSession.LastRecordedY = finalY;
            routePathBuildSession.LastRecordedZ = finalZ;
        }

        if (routePathBuildSession.NodeCount < 2)
        {
            handler->SendSysMessage("The automatic build contains fewer than two movement nodes. Travel farther before completing it.");
            return false;
        }

        if (!routePathBuildSession.EndRouteNodeExists)
        {
            WorldDatabase.Execute(
                "INSERT INTO `lwi_route_node` (`id`, `name`, `map_id`, `x`, `y`, `z`, `orientation`, `arrival_radius`, `enabled`, `comment`) "
                "VALUES ({}, '{}', {}, {}, {}, {}, {}, 5.0, 1, 'Created by automatic LWI route path builder')",
                routePathBuildSession.EndRouteNodeId,
                routePathBuildSession.EndName,
                routePathBuildSession.MapId,
                finalX, finalY, finalZ, finalO);
        }

        WorldDatabase.Execute(
            "INSERT INTO `lwi_route_segment` (`id`, `name`, `start_node_id`, `end_node_id`, `movement_path_id`, `enabled`, `comment`) "
            "VALUES ({}, '{}', {}, {}, {}, 1, 'Automatically recorded shared route segment')",
            routePathBuildSession.SegmentId,
            routePathBuildSession.SegmentName,
            routePathBuildSession.StartRouteNodeId,
            routePathBuildSession.EndRouteNodeId,
            routePathBuildSession.PathId);

        WorldDatabase.Execute(
            "UPDATE `lwi_movement_path` SET `comment` = 'Automatically recorded at {:.1f} yd spacing: {} -> {}' WHERE `id` = {}",
            RoutePathBuildSpacingYards,
            routePathBuildSession.StartName,
            routePathBuildSession.EndName,
            routePathBuildSession.PathId);

        uint32 const pathId = routePathBuildSession.PathId;
        uint32 const segmentId = routePathBuildSession.SegmentId;
        uint32 const startNodeId = routePathBuildSession.StartRouteNodeId;
        uint32 const endNodeId = routePathBuildSession.EndRouteNodeId;
        uint32 const nodes = routePathBuildSession.NodeCount;
        float const distance = routePathBuildSession.TotalDistance;
        std::string const startName = routePathBuildSession.StartName;
        std::string const endName = routePathBuildSession.EndName;

        routePathBuildSession = {};
        routePathBuildActive = false;

        handler->PSendSysMessage(
            "Completed automatic route {} -> {}: path/segment {} with {} movement nodes over approximately {:.1f} yd.",
            startName, endName, pathId, nodes, distance);
        handler->PSendSysMessage(
            "Route segment {} is saved: start node {} -> end node {}. Run .lwi reload before testing it.",
            segmentId, startNodeId, endNodeId);
        handler->PSendSysMessage(
            "Forward test: .lwi route test {} {} | Reverse test: .lwi route test {} {}",
            segmentId, startNodeId, segmentId, endNodeId);
        return true;
    }

    static bool HandleRoutePathBuildStatusCommand(ChatHandler* handler)
    {
        if (!routePathBuildActive)
        {
            handler->SendSysMessage("No automatic route path build is active.");
            return true;
        }

        Player* player = GetCommandPlayer(handler);
        float currentGap = routePathBuildSession.DistanceSinceLastNode;
        if (player && player->GetGUID() == routePathBuildSession.OwnerGuid && player->GetMapId() == routePathBuildSession.MapId)
        {
            currentGap = RouteDistance3D(
                routePathBuildSession.LastRecordedX,
                routePathBuildSession.LastRecordedY,
                routePathBuildSession.LastRecordedZ,
                player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());
        }

        handler->PSendSysMessage(
            "Route build {} -> {} | path {} | nodes {} | traveled ~{:.1f} yd | {:.1f} yd from last saved point | {}.",
            routePathBuildSession.StartName,
            routePathBuildSession.EndName,
            routePathBuildSession.PathId,
            routePathBuildSession.NodeCount,
            routePathBuildSession.TotalDistance,
            currentGap,
            routePathBuildSession.Paused ? "PAUSED" : "RECORDING");
        return true;
    }

    static bool HandleRoutePathBuildPauseCommand(ChatHandler* handler)
    {
        if (!routePathBuildActive)
        {
            handler->SendSysMessage("No automatic route path build is active.");
            return false;
        }
        Player* player = GetCommandPlayer(handler);
        if (!player || player->GetGUID() != routePathBuildSession.OwnerGuid)
            return false;

        routePathBuildSession.Paused = true;
        handler->SendSysMessage("Automatic route path build paused. Movement while paused will not be recorded.");
        return true;
    }

    static bool HandleRoutePathBuildResumeCommand(ChatHandler* handler)
    {
        if (!routePathBuildActive)
        {
            handler->SendSysMessage("No automatic route path build is active.");
            return false;
        }
        Player* player = GetCommandPlayer(handler);
        if (!player || player->GetGUID() != routePathBuildSession.OwnerGuid)
            return false;
        if (player->GetMapId() != routePathBuildSession.MapId)
        {
            handler->SendSysMessage("Return to the route build map before resuming.");
            return false;
        }

        routePathBuildSession.Paused = false;
        routePathBuildSession.HasLastSample = true;
        routePathBuildSession.LastSampleX = player->GetPositionX();
        routePathBuildSession.LastSampleY = player->GetPositionY();
        routePathBuildSession.LastSampleZ = player->GetPositionZ();
        routePathBuildSession.DistanceSinceLastNode = RouteDistance3D(
            routePathBuildSession.LastRecordedX,
            routePathBuildSession.LastRecordedY,
            routePathBuildSession.LastRecordedZ,
            player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());
        handler->SendSysMessage("Automatic route path build resumed.");
        return true;
    }

    static bool HandleRoutePathBuildCancelCommand(ChatHandler* handler)
    {
        if (!routePathBuildActive)
        {
            handler->SendSysMessage("No automatic route path build is active.");
            return true;
        }
        handler->PSendSysMessage(
            "WARNING: canceling will delete unfinished path {} ({} -> {}) and its {} recorded movement nodes. Use .lwi route path cancel confirm.",
            routePathBuildSession.PathId,
            routePathBuildSession.StartName,
            routePathBuildSession.EndName,
            routePathBuildSession.NodeCount);
        return true;
    }

    static bool HandleRoutePathBuildCancelConfirmCommand(ChatHandler* handler)
    {
        if (!routePathBuildActive)
        {
            handler->SendSysMessage("No automatic route path build is active.");
            return true;
        }
        Player* player = GetCommandPlayer(handler);
        if (!player || player->GetGUID() != routePathBuildSession.OwnerGuid)
            return false;

        uint32 const pathId = routePathBuildSession.PathId;
        bool const deleteStartNode = routePathBuildSession.StartRouteNodeCreated;
        uint32 const startNodeId = routePathBuildSession.StartRouteNodeId;

        WorldDatabase.Execute(
            "DELETE FROM `lwi_movement_node_action` WHERE `movement_node_id` IN (SELECT `id` FROM `lwi_movement_node` WHERE `path_id` = {})",
            pathId);
        WorldDatabase.Execute("DELETE FROM `lwi_movement_node` WHERE `path_id` = {}", pathId);
        WorldDatabase.Execute("DELETE FROM `lwi_movement_path` WHERE `id` = {}", pathId);
        if (deleteStartNode)
            WorldDatabase.Execute("DELETE FROM `lwi_route_node` WHERE `id` = {}", startNodeId);

        routePathBuildSession = {};
        routePathBuildActive = false;
        handler->PSendSysMessage("Canceled and deleted unfinished automatic route path {}.", pathId);
        return true;
    }

    static bool HandleRouteNetworkResetCommand(ChatHandler* handler)
    {
        if (!lwiConfig.GetConfigValue<bool>(LwiConfig::Debug))
        {
            handler->SendSysMessage("Living World Invasions debug commands are disabled. Set LWI.Debug = 1 first.");
            return false;
        }

        QueryResult countResult = WorldDatabase.Query("SELECT COUNT(*) FROM `lwi_route_segment`");
        uint32 const segmentCount = countResult ? countResult->Fetch()[0].Get<uint32>() : 0;
        handler->PSendSysMessage(
            "WARNING: this will delete all {} shared LWI route segment(s), their route nodes, and movement paths referenced by those segments. Non-route invasion movement paths are preserved.",
            segmentCount);
        handler->SendSysMessage("Use .lwi route network reset confirm to continue.");
        return true;
    }

    static bool HandleRouteNetworkResetConfirmCommand(ChatHandler* handler)
    {
        if (routePathBuildActive || routeRecordingActive)
        {
            handler->SendSysMessage("Finish/cancel any active route build or manual recording before resetting the route network.");
            return false;
        }

        std::vector<uint32> pathIds;
        if (QueryResult result = WorldDatabase.Query("SELECT DISTINCT `movement_path_id` FROM `lwi_route_segment` ORDER BY `movement_path_id`"))
        {
            do
            {
                pathIds.push_back(result->Fetch()[0].Get<uint32>());
            } while (result->NextRow());
        }

        WorldDatabase.Execute("DELETE FROM `lwi_route_segment`");
        WorldDatabase.Execute("DELETE FROM `lwi_route_node`");

        for (uint32 const pathId : pathIds)
        {
            WorldDatabase.Execute(
                "DELETE FROM `lwi_movement_node_action` WHERE `movement_node_id` IN (SELECT `id` FROM `lwi_movement_node` WHERE `path_id` = {})",
                pathId);
            WorldDatabase.Execute("DELETE FROM `lwi_movement_node` WHERE `path_id` = {}", pathId);
            WorldDatabase.Execute("DELETE FROM `lwi_movement_path` WHERE `id` = {}", pathId);
        }

        handler->PSendSysMessage(
            "Shared LWI route network reset complete. Deleted {} route-linked movement path(s). Existing non-route invasion movement paths were preserved. Run .lwi reload.",
            pathIds.size());
        return true;
    }

    static bool HandleRoutePathShowCommand(ChatHandler* handler, uint32 pathId)
    {
        if (!lwiConfig.GetConfigValue<bool>(LwiConfig::Debug))
        {
            handler->SendSysMessage("Living World Invasions debug commands are disabled. Set LWI.Debug = 1 to use .lwi route path show.");
            return false;
        }

        Player* player = GetCommandPlayer(handler);
        if (!player)
            return false;

        auto const* nodes = sInvasionMgr.GetMovementNodes(pathId);
        if (!nodes || nodes->empty())
        {
            handler->PSendSysMessage("Movement path {} does not exist, is disabled, or has no loaded nodes. Use .lwi reload after editing route data.", pathId);
            return false;
        }

        ClearMarkerList(player, routePathMarkerGuids);

        uint32 shown = 0;
        for (lwi::MovementNodeDefinition const& node : *nodes)
        {
            if (Creature* marker = SpawnRoutePathMarker(player, node.X, node.Y, node.Z, node.Orientation, 0.5f))
            {
                routePathMarkerGuids.push_back(marker->GetGUID());
                ++shown;
            }
        }

        handler->PSendSysMessage(
            "Showing {} marker(s) for LWI movement path {}. Use .lwi route path nearest <pathId> to identify the closest authored node and .lwi route path hide to remove the markers.",
            shown, pathId);
        return true;
    }

    static bool HandleRoutePathHideCommand(ChatHandler* handler)
    {
        Player* player = GetCommandPlayer(handler);
        ClearMarkerList(player, routePathMarkerGuids);
        handler->SendSysMessage("LWI route path markers cleared.");
        return true;
    }

    static bool HandleRoutePathNearestCommand(ChatHandler* handler, uint32 pathId)
    {
        Player* player = GetCommandPlayer(handler);
        if (!player)
            return false;

        auto const* nodes = sInvasionMgr.GetMovementNodes(pathId);
        if (!nodes || nodes->empty())
        {
            handler->PSendSysMessage("Movement path {} does not exist, is disabled, or has no loaded nodes.", pathId);
            return false;
        }

        lwi::MovementNodeDefinition const* nearest = nullptr;
        float nearestDistance = 0.0f;
        for (lwi::MovementNodeDefinition const& node : *nodes)
        {
            float const dx = player->GetPositionX() - node.X;
            float const dy = player->GetPositionY() - node.Y;
            float const dz = player->GetPositionZ() - node.Z;
            float const distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (!nearest || distance < nearestDistance)
            {
                nearest = &node;
                nearestDistance = distance;
            }
        }

        handler->PSendSysMessage(
            "Nearest node on path {}: node order {} (definition id {}) at X {:.3f} Y {:.3f} Z {:.3f} O {:.3f}; distance {:.2f} yd.",
            pathId,
            nearest->NodeOrder,
            nearest->Id,
            nearest->X,
            nearest->Y,
            nearest->Z,
            nearest->Orientation,
            nearestDistance);
        return true;
    }

    static bool HandleRouteExportNetworkCommand(ChatHandler* handler)
    {
        if (!lwiConfig.GetConfigValue<bool>(LwiConfig::Debug))
        {
            handler->SendSysMessage(
                "Living World Invasions debug commands are disabled. Set LWI.Debug = 1 to use .lwi route export.");
            return false;
        }

        std::vector<uint32> segmentIds;
        std::unordered_set<uint32> routeNodeIds;
        std::unordered_set<uint32> movementPathIds;
        segmentIds.reserve(sInvasionMgr.GetRouteSegments().size());
        for (auto const& [segmentId, segment] : sInvasionMgr.GetRouteSegments())
        {
            segmentIds.push_back(segmentId);
            routeNodeIds.insert(segment.StartNodeId);
            routeNodeIds.insert(segment.EndNodeId);
            movementPathIds.insert(segment.MovementPathId);
        }

        if (segmentIds.empty())
        {
            handler->SendSysMessage("No enabled route segments are loaded; nothing to export.");
            return false;
        }

        std::string outputPath;
        std::string error;
        if (!WriteRouteExport(segmentIds, "801_routes", outputPath, error))
        {
            handler->PSendSysMessage("Route network export failed: {}", error);
            return false;
        }

        handler->PSendSysMessage(
            "Exported complete LWI route network: {} segment(s), {} route node(s), {} movement path(s). File: {}",
            segmentIds.size(),
            routeNodeIds.size(),
            movementPathIds.size(),
            outputPath);
        handler->SendSysMessage(
            "Copy this file into data/sql/db-world/prebuilt/801_routes.sql when you are ready to publish the current canonical route network.");
        return true;
    }

    static bool HandleRouteExportSegmentCommand(ChatHandler* handler, std::string segmentToken)
    {
        if (!lwiConfig.GetConfigValue<bool>(LwiConfig::Debug))
        {
            handler->SendSysMessage(
                "Living World Invasions debug commands are disabled. Set LWI.Debug = 1 to use .lwi route export.");
            return false;
        }

        lwi::RouteSegmentDefinition const* segment = ResolveRouteSegment(segmentToken);
        if (!segment)
        {
            handler->PSendSysMessage(
                "Living World Invasions route segment '{}' does not exist or is disabled. Use either its numeric ID or exact name.",
                segmentToken);
            return false;
        }

        std::string outputPath;
        std::string error;
        std::string const exportName = "lwi_route_segment_" + std::to_string(segment->Id) + "_" + segment->Name;
        if (!WriteRouteExport({ segment->Id }, exportName, outputPath, error))
        {
            handler->PSendSysMessage("Route export failed: {}", error);
            return false;
        }

        handler->PSendSysMessage(
            "Exported route segment {} ({}) to {}",
            segment->Id,
            segment->Name,
            outputPath);
        return true;
    }

    static bool HandleRouteExportJourneyCommand(ChatHandler* handler, std::string fromToken, std::string destinationToken)
    {
        if (!lwiConfig.GetConfigValue<bool>(LwiConfig::Debug))
        {
            handler->SendSysMessage(
                "Living World Invasions debug commands are disabled. Set LWI.Debug = 1 to use .lwi route export.");
            return false;
        }

        lwi::RouteNodeDefinition const* fromNode = ResolveRouteNode(fromToken);
        lwi::RouteNodeDefinition const* destinationNode = ResolveRouteNode(destinationToken);
        if (!fromNode || !destinationNode)
        {
            handler->PSendSysMessage(
                "Could not resolve route journey endpoints '{}' and '{}'. Use numeric IDs or exact route-node names.",
                fromToken,
                destinationToken);
            return false;
        }

        if (fromNode->Id == destinationNode->Id)
        {
            handler->SendSysMessage("Route export journey requires two different route nodes.");
            return false;
        }

        std::vector<uint32> segmentIds;
        if (!BuildRouteExportJourney(fromNode->Id, destinationNode->Id, segmentIds) || segmentIds.empty())
        {
            handler->PSendSysMessage(
                "No connected route journey exists from {} ({}) to {} ({}).",
                fromNode->Name, fromNode->Id, destinationNode->Name, destinationNode->Id);
            return false;
        }

        std::string outputPath;
        std::string error;
        std::string const exportName = "lwi_route_journey_" + fromNode->Name + "_to_" + destinationNode->Name;
        if (!WriteRouteExport(segmentIds, exportName, outputPath, error))
        {
            handler->PSendSysMessage("Route journey export failed: {}", error);
            return false;
        }

        handler->PSendSysMessage(
            "Exported route journey {} ({}) -> {} ({}) across {} segment(s) to {}",
            fromNode->Name, fromNode->Id, destinationNode->Name, destinationNode->Id, segmentIds.size(), outputPath);
        return true;
    }

    static bool HandleRouteTravelCommand(ChatHandler* handler, std::string fromToken, std::string destinationToken)
    {
        if (!lwiConfig.GetConfigValue<bool>(LwiConfig::Debug))
        {
            handler->SendSysMessage(
                "Living World Invasions debug commands are disabled. Set LWI.Debug = 1 to use .lwi route travel.");
            return false;
        }

        lwi::RouteNodeDefinition const* fromNode = ResolveRouteNode(fromToken);
        if (!fromNode)
        {
            handler->PSendSysMessage(
                "Living World Invasions route node '{}' does not exist or is disabled. Use either its numeric ID or exact name.",
                fromToken);
            return false;
        }

        lwi::RouteNodeDefinition const* destinationNode = ResolveRouteNode(destinationToken);
        if (!destinationNode)
        {
            handler->PSendSysMessage(
                "Living World Invasions route node '{}' does not exist or is disabled. Use either its numeric ID or exact name.",
                destinationToken);
            return false;
        }

        uint32 const fromNodeId = fromNode->Id;
        uint32 const destinationNodeId = destinationNode->Id;
        if (fromNodeId == destinationNodeId)
        {
            handler->SendSysMessage("Route travel requires two different route nodes.");
            return false;
        }

        Creature* creature = handler->getSelectedCreature();
        if (!creature)
        {
            handler->SendSysMessage(
                "Select a creature to use as the route traveler, then use .lwi route travel <fromNodeId|name> <destinationNodeId|name>.");
            return false;
        }

        if (creature->GetMapId() != fromNode->MapId)
        {
            handler->PSendSysMessage(
                "Selected creature is on map {}, but starting route node {} ({}) is on map {}.",
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
                continue;

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

        if (!sMovementController.StartRouteJourney(
                runtimeGroupId,
                fromNodeId,
                destinationNodeId))
        {
            routeTestGroupIds.erase(runtimeGroupId);
            sRuntimeEntityGroupMgr.RemoveGroup(runtimeGroupId);

            handler->PSendSysMessage(
                "Living World Invasions could not find/start a connected route from {} ({}) to {} ({}).",
                fromNode->Name,
                fromNode->Id,
                destinationNode->Name,
                destinationNode->Id);
            return false;
        }

        handler->PSendSysMessage(
            "Route travel group #{} started from {} ({}) to {} ({}) using selected creature {} (entry {}).",
            runtimeGroupId,
            fromNode->Name,
            fromNode->Id,
            destinationNode->Name,
            destinationNode->Id,
            creature->GetName(),
            creature->GetEntry());
        return true;
    }

    static bool HandleRouteTestCommand(ChatHandler* handler, std::string segmentToken, std::string fromNodeToken)
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
                "Select a creature to use as the route-test traveler, then use .lwi route test <segmentId|name> <fromNodeId|name>.");
            return false;
        }

        lwi::RouteSegmentDefinition const* segment = ResolveRouteSegment(segmentToken);
        if (!segment)
        {
            handler->PSendSysMessage(
                "Living World Invasions route segment '{}' does not exist or is disabled. Use either its numeric ID or exact name.",
                segmentToken);
            return false;
        }

        lwi::RouteNodeDefinition const* requestedFromNode = ResolveRouteNode(fromNodeToken);
        if (!requestedFromNode)
        {
            handler->PSendSysMessage(
                "Living World Invasions route node '{}' does not exist or is disabled. Use either its numeric ID or exact name.",
                fromNodeToken);
            return false;
        }

        uint32 const routeSegmentId = segment->Id;
        uint32 const fromNodeId = requestedFromNode->Id;

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
