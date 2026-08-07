#include "LivingWorldInvasions.h"

#include "DatabaseEnv.h"
#include "Field.h"
#include "Log.h"
#include "QueryResult.h"

namespace lwi
{
InvasionMgr& InvasionMgr::Instance()
{
    static InvasionMgr instance;
    return instance;
}

void InvasionMgr::Clear()
{
    _responseOrigins.clear();
    _definitions.clear();
    _stagesByInvasion.clear();
    _actionsByStage.clear();
    _spawnGroups.clear();
    _spawnMembersByGroup.clear();
}

void InvasionMgr::LoadDefinitions()
{
    Clear();

    if (QueryResult result = WorldDatabase.Query(
        "SELECT `id`, `name`, `map_id`, `team`, `max_active_default`, `enabled` "
        "FROM `lwi_response_origin` WHERE `enabled` = 1 ORDER BY `id`"))
    {
        do
        {
            Field* fields = result->Fetch();

            ResponseOriginDefinition origin;
            origin.Id = fields[0].Get<uint32>();
            origin.Name = fields[1].Get<std::string>();
            origin.MapId = fields[2].Get<uint16>();
            origin.Team = fields[3].Get<uint8>();
            origin.MaxActiveDefault = fields[4].Get<uint32>();
            origin.Enabled = fields[5].Get<bool>();

            auto [iterator, inserted] = _responseOrigins.emplace(origin.Id, std::move(origin));
            if (!inserted)
            {
                LOG_ERROR("server.loading", "Living World Invasions: duplicate response origin id {} ignored.", iterator->first);
            }
        } while (result->NextRow());
    }

    LOG_INFO("server.loading", "Living World Invasions: loaded {} enabled response origin definition(s).", _responseOrigins.size());

    if (QueryResult result = WorldDatabase.Query(
        "SELECT `id`, `name`, `map_id`, `zone_id`, `team`, `response_origin_id`, "
        "`recommended_min_level`, `recommended_max_level`, `selection_weight`, "
        "`minimum_cooldown_seconds`, `maximum_cooldown_seconds`, `allow_random_start`, `enabled` "
        "FROM `lwi_invasion` WHERE `enabled` = 1 ORDER BY `id`"))
    {
        do
        {
            Field* fields = result->Fetch();

            InvasionDefinition definition;
            definition.Id = fields[0].Get<uint32>();
            definition.Name = fields[1].Get<std::string>();
            definition.MapId = fields[2].Get<uint16>();
            definition.ZoneId = fields[3].Get<uint32>();
            definition.Team = fields[4].Get<uint8>();
            definition.ResponseOriginId = fields[5].Get<uint32>();
            definition.RecommendedMinLevel = fields[6].Get<uint8>();
            definition.RecommendedMaxLevel = fields[7].Get<uint8>();
            definition.SelectionWeight = fields[8].Get<uint32>();
            definition.MinimumCooldownSeconds = fields[9].Get<uint32>();
            definition.MaximumCooldownSeconds = fields[10].Get<uint32>();
            definition.AllowRandomStart = fields[11].Get<bool>();
            definition.Enabled = fields[12].Get<bool>();

            ResponseOriginDefinition const* origin = GetResponseOrigin(definition.ResponseOriginId);
            if (!origin)
            {
                LOG_ERROR("server.loading",
                    "Living World Invasions: invasion {} ({}) references missing or disabled response origin {} and was ignored.",
                    definition.Id, definition.Name, definition.ResponseOriginId);
                continue;
            }

            if (origin->MapId != definition.MapId)
            {
                LOG_ERROR("server.loading",
                    "Living World Invasions: invasion {} ({}) uses map {}, but response origin {} ({}) uses map {}; invasion ignored.",
                    definition.Id, definition.Name, definition.MapId, origin->Id, origin->Name, origin->MapId);
                continue;
            }

            if (definition.RecommendedMinLevel > definition.RecommendedMaxLevel)
            {
                LOG_ERROR("server.loading", "Living World Invasions: invasion {} ({}) has an invalid level range and was ignored.",
                    definition.Id, definition.Name);
                continue;
            }

            if (definition.MinimumCooldownSeconds > definition.MaximumCooldownSeconds)
            {
                LOG_ERROR("server.loading", "Living World Invasions: invasion {} ({}) has minimum cooldown greater than maximum cooldown and was ignored.",
                    definition.Id, definition.Name);
                continue;
            }

            if (definition.SelectionWeight == 0)
            {
                LOG_ERROR("server.loading", "Living World Invasions: invasion {} ({}) has selection weight 0 and was ignored.",
                    definition.Id, definition.Name);
                continue;
            }

            auto [iterator, inserted] = _definitions.emplace(definition.Id, std::move(definition));
            if (!inserted)
            {
                LOG_ERROR("server.loading", "Living World Invasions: duplicate invasion id {} ignored.", iterator->first);
            }
        } while (result->NextRow());
    }

    LOG_INFO("server.loading", "Living World Invasions: loaded {} enabled invasion definition(s).", _definitions.size());

    std::size_t stageCount = 0;
    if (QueryResult result = WorldDatabase.Query(
        "SELECT `id`, `invasion_id`, `stage_order`, `name`, `duration_seconds`, `completion_type`, `enabled` "
        "FROM `lwi_invasion_stage` WHERE `enabled` = 1 ORDER BY `invasion_id`, `stage_order`"))
    {
        do
        {
            Field* fields = result->Fetch();

            InvasionStageDefinition stage;
            stage.Id = fields[0].Get<uint32>();
            stage.InvasionId = fields[1].Get<uint32>();
            stage.StageOrder = fields[2].Get<uint16>();
            stage.Name = fields[3].Get<std::string>();
            stage.DurationSeconds = fields[4].Get<uint32>();
            stage.CompletionType = fields[5].Get<uint8>();
            stage.Enabled = fields[6].Get<bool>();

            if (!GetDefinition(stage.InvasionId))
            {
                LOG_ERROR("server.loading",
                    "Living World Invasions: stage {} ({}) references missing or disabled invasion {} and was ignored.",
                    stage.Id, stage.Name, stage.InvasionId);
                continue;
            }

            if (stage.DurationSeconds == 0)
            {
                LOG_ERROR("server.loading",
                    "Living World Invasions: stage {} ({}) has duration 0 and was ignored.",
                    stage.Id, stage.Name);
                continue;
            }

            auto& stages = _stagesByInvasion[stage.InvasionId];
            bool duplicateOrder = false;
            for (InvasionStageDefinition const& existing : stages)
            {
                if (existing.StageOrder == stage.StageOrder || existing.Id == stage.Id)
                {
                    duplicateOrder = true;
                    break;
                }
            }

            if (duplicateOrder)
            {
                LOG_ERROR("server.loading",
                    "Living World Invasions: duplicate stage id/order for invasion {} ignored (stage {}).",
                    stage.InvasionId, stage.Id);
                continue;
            }

            stages.push_back(std::move(stage));
            ++stageCount;
        } while (result->NextRow());
    }

    LOG_INFO("server.loading", "Living World Invasions: loaded {} enabled stage definition(s) for {} invasion(s).",
        stageCount, _stagesByInvasion.size());

    if (QueryResult result = WorldDatabase.Query("SELECT `id`, `stage_id`, `action_order`, `action_type`, `target_id`, `delay_seconds`, `enabled` FROM `lwi_stage_action` WHERE `enabled` = 1 ORDER BY `stage_id`, `action_order`"))
    {
        do
        {
            Field* fields = result->Fetch();
            StageActionDefinition action;
            action.Id = fields[0].Get<uint32>();
            action.StageId = fields[1].Get<uint32>();
            action.ActionOrder = fields[2].Get<uint16>();
            action.ActionType = fields[3].Get<uint8>();
            action.TargetId = fields[4].Get<uint32>();
            action.DelaySeconds = fields[5].Get<uint32>();
            action.Enabled = fields[6].Get<bool>();
            _actionsByStage[action.StageId].push_back(std::move(action));
        } while (result->NextRow());
    }

    if (QueryResult result = WorldDatabase.Query("SELECT `id`, `name`, `map_id`, `x`, `y`, `z`, `orientation`, `spawn_radius`, `enabled` FROM `lwi_spawn_group` WHERE `enabled` = 1"))
    {
        do
        {
            Field* fields = result->Fetch();
            SpawnGroupDefinition group;
            group.Id = fields[0].Get<uint32>();
            group.Name = fields[1].Get<std::string>();
            group.MapId = fields[2].Get<uint16>();
            group.X = fields[3].Get<float>();
            group.Y = fields[4].Get<float>();
            group.Z = fields[5].Get<float>();
            group.Orientation = fields[6].Get<float>();
            group.SpawnRadius = fields[7].Get<float>();
            group.Enabled = fields[8].Get<bool>();
            _spawnGroups.emplace(group.Id, std::move(group));
        } while (result->NextRow());
    }

    if (QueryResult result = WorldDatabase.Query("SELECT `id`, `spawn_group_id`, `creature_entry`, `count`, `level_override` FROM `lwi_spawn_member` ORDER BY `spawn_group_id`"))
    {
        do
        {
            Field* fields = result->Fetch();
            SpawnMemberDefinition member;
            member.Id = fields[0].Get<uint32>();
            member.SpawnGroupId = fields[1].Get<uint32>();
            member.CreatureEntry = fields[2].Get<uint32>();
            member.Count = fields[3].Get<uint16>();
            member.LevelOverride = fields[4].Get<uint16>();
            _spawnMembersByGroup[member.SpawnGroupId].push_back(std::move(member));
        } while (result->NextRow());
    }
}

ResponseOriginDefinition const* InvasionMgr::GetResponseOrigin(uint32 responseOriginId) const
{
    auto const iterator = _responseOrigins.find(responseOriginId);
    return iterator != _responseOrigins.end() ? &iterator->second : nullptr;
}

InvasionDefinition const* InvasionMgr::GetDefinition(uint32 invasionId) const
{
    auto const iterator = _definitions.find(invasionId);
    return iterator != _definitions.end() ? &iterator->second : nullptr;
}

std::unordered_map<uint32, ResponseOriginDefinition> const& InvasionMgr::GetResponseOrigins() const
{
    return _responseOrigins;
}

std::unordered_map<uint32, InvasionDefinition> const& InvasionMgr::GetDefinitions() const
{
    return _definitions;
}

std::vector<InvasionStageDefinition> const* InvasionMgr::GetStages(uint32 invasionId) const
{
    auto const iterator = _stagesByInvasion.find(invasionId);
    return iterator != _stagesByInvasion.end() ? &iterator->second : nullptr;
}

std::size_t InvasionMgr::GetDefinitionCount() const
{
    return _definitions.size();
}
}


namespace lwi
{
std::vector<StageActionDefinition> const* InvasionMgr::GetActions(uint32 stageId) const
{
    auto it = _actionsByStage.find(stageId);
    return it != _actionsByStage.end() ? &it->second : nullptr;
}
SpawnGroupDefinition const* InvasionMgr::GetSpawnGroup(uint32 id) const
{
    auto it = _spawnGroups.find(id);
    return it != _spawnGroups.end() ? &it->second : nullptr;
}
std::vector<SpawnMemberDefinition> const* InvasionMgr::GetSpawnMembers(uint32 id) const
{
    auto it = _spawnMembersByGroup.find(id);
    return it != _spawnMembersByGroup.end() ? &it->second : nullptr;
}
}
