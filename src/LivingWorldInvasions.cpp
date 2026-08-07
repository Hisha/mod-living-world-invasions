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
    _movementPaths.clear();
    _movementNodesByPath.clear();
    _movementProfiles.clear();
    _runtimeSignals.clear();
    _dialogues.clear();
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
        "SELECT `id`, `invasion_id`, `stage_order`, `name`, `duration_seconds`, `completion_type`, `completion_target_id`, `enabled` "
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
            stage.CompletionTargetId = fields[6].Get<uint32>();
            stage.Enabled = fields[7].Get<bool>();

            if (!GetDefinition(stage.InvasionId))
            {
                LOG_ERROR("server.loading",
                    "Living World Invasions: stage {} ({}) references missing or disabled invasion {} and was ignored.",
                    stage.Id, stage.Name, stage.InvasionId);
                continue;
            }

            if (stage.CompletionType > 1)
            {
                LOG_ERROR("server.loading",
                    "Living World Invasions: stage {} ({}) uses unsupported completion type {} and was ignored.",
                    stage.Id, stage.Name, stage.CompletionType);
                continue;
            }

            if (stage.CompletionType == 0 && stage.DurationSeconds == 0)
            {
                LOG_ERROR("server.loading",
                    "Living World Invasions: timer stage {} ({}) has duration 0 and was ignored.",
                    stage.Id, stage.Name);
                continue;
            }

            if (stage.CompletionType == 1 && stage.CompletionTargetId == 0)
            {
                LOG_ERROR("server.loading",
                    "Living World Invasions: signal stage {} ({}) has no completion target signal and was ignored.",
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

    if (QueryResult result = WorldDatabase.Query("SELECT `id`, `stage_id`, `action_order`, `action_type`, `target_id`, `parameter1`, `parameter2`, `parameter3`, `delay_seconds`, `enabled` FROM `lwi_stage_action` WHERE `enabled` = 1 ORDER BY `stage_id`, `action_order`"))
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
            action.Parameter1 = fields[5].Get<uint32>();
            action.Parameter2 = fields[6].Get<uint32>();
            action.Parameter3 = fields[7].Get<uint32>();
            action.DelaySeconds = fields[8].Get<uint32>();
            action.Enabled = fields[9].Get<bool>();
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

    if (QueryResult result = WorldDatabase.Query("SELECT `id`, `spawn_group_id`, `entity_type`, `entity_entry`, `count`, `level_override`, `comment` FROM `lwi_spawn_member` ORDER BY `spawn_group_id`, `id`"))
    {
        do
        {
            Field* fields = result->Fetch();
            SpawnMemberDefinition member;
            member.Id = fields[0].Get<uint32>();
            member.SpawnGroupId = fields[1].Get<uint32>();
            member.EntityType = fields[2].Get<uint8>();
            member.EntityEntry = fields[3].Get<uint32>();
            member.Count = fields[4].Get<uint16>();
            member.LevelOverride = fields[5].Get<uint16>();
            member.Comment = fields[6].Get<std::string>();
            _spawnMembersByGroup[member.SpawnGroupId].push_back(std::move(member));
        } while (result->NextRow());
    }

    if (QueryResult result = WorldDatabase.Query(
        "SELECT `id`, `name`, `enabled`, `comment` "
        "FROM `lwi_runtime_signal` WHERE `enabled` = 1 ORDER BY `id`"))
    {
        do
        {
            Field* fields = result->Fetch();
            RuntimeSignalDefinition signal;
            signal.Id = fields[0].Get<uint32>();
            signal.Name = fields[1].Get<std::string>();
            signal.Enabled = fields[2].Get<bool>();
            if (!fields[3].IsNull())
                signal.Comment = fields[3].Get<std::string>();

            auto [iterator, inserted] = _runtimeSignals.emplace(signal.Id, std::move(signal));
            if (!inserted)
            {
                LOG_ERROR("server.loading",
                    "[LWI Signal] Duplicate runtime signal definition id {} ignored.", iterator->first);
            }
        } while (result->NextRow());
    }

    LOG_INFO("server.loading", "[LWI Signal] Loaded {} runtime signal definition(s).", _runtimeSignals.size());

    if (QueryResult result = WorldDatabase.Query(
        "SELECT `id`, `name`, `text`, `chat_type`, `language`, `enabled`, `comment` "
        "FROM `lwi_dialogue` WHERE `enabled` = 1 ORDER BY `id`"))
    {
        do
        {
            Field* fields = result->Fetch();
            DialogueDefinition dialogue;
            dialogue.Id = fields[0].Get<uint32>();
            dialogue.Name = fields[1].Get<std::string>();
            dialogue.Text = fields[2].Get<std::string>();
            dialogue.ChatType = fields[3].Get<uint8>();
            dialogue.Language = fields[4].Get<uint8>();
            dialogue.Enabled = fields[5].Get<bool>();
            if (!fields[6].IsNull())
                dialogue.Comment = fields[6].Get<std::string>();

            if (dialogue.Text.empty())
            {
                LOG_ERROR("server.loading",
                    "[LWI Dialogue] Dialogue {} ({}) has empty text and was ignored.",
                    dialogue.Id, dialogue.Name);
                continue;
            }

            if (dialogue.ChatType > 1)
            {
                LOG_ERROR("server.loading",
                    "[LWI Dialogue] Dialogue {} ({}) uses unsupported chat type {} and was ignored.",
                    dialogue.Id, dialogue.Name, dialogue.ChatType);
                continue;
            }

            auto [iterator, inserted] = _dialogues.emplace(dialogue.Id, std::move(dialogue));
            if (!inserted)
            {
                LOG_ERROR("server.loading",
                    "[LWI Dialogue] Duplicate dialogue definition id {} ignored.", iterator->first);
            }
        } while (result->NextRow());
    }

    LOG_INFO("server.loading", "[LWI Dialogue] Loaded {} dialogue definition(s).", _dialogues.size());

    if (QueryResult result = WorldDatabase.Query(
        "SELECT `id`, `name`, `enabled`, `comment` "
        "FROM `lwi_movement_path` WHERE `enabled` = 1 ORDER BY `id`"))
    {
        do
        {
            Field* fields = result->Fetch();
            MovementPathDefinition path;
            path.Id = fields[0].Get<uint32>();
            path.Name = fields[1].Get<std::string>();
            path.Enabled = fields[2].Get<bool>();
            if (!fields[3].IsNull())
                path.Comment = fields[3].Get<std::string>();
            _movementPaths.emplace(path.Id, std::move(path));
        } while (result->NextRow());
    }

    std::size_t movementNodeCount = 0;
    if (QueryResult result = WorldDatabase.Query(
        "SELECT `id`, `path_id`, `node_order`, `map_id`, `x`, `y`, `z`, `orientation`, `wait_ms`, "
        "`profile_override_id`, `enabled`, `comment` "
        "FROM `lwi_movement_node` WHERE `enabled` = 1 ORDER BY `path_id`, `node_order`"))
    {
        do
        {
            Field* fields = result->Fetch();
            MovementNodeDefinition node;
            node.Id = fields[0].Get<uint32>();
            node.PathId = fields[1].Get<uint32>();
            node.NodeOrder = fields[2].Get<uint16>();
            node.MapId = fields[3].Get<uint16>();
            node.X = fields[4].Get<float>();
            node.Y = fields[5].Get<float>();
            node.Z = fields[6].Get<float>();
            node.Orientation = fields[7].Get<float>();
            node.WaitMs = fields[8].Get<uint32>();
            node.ProfileOverrideId = fields[9].Get<uint32>();
            node.Enabled = fields[10].Get<bool>();
            if (!fields[11].IsNull())
                node.Comment = fields[11].Get<std::string>();

            if (_movementPaths.find(node.PathId) == _movementPaths.end())
            {
                LOG_ERROR("server.loading",
                    "[LWI Movement] Node {} references missing or disabled movement path {}; node ignored.",
                    node.Id, node.PathId);
                continue;
            }

            _movementNodesByPath[node.PathId].push_back(std::move(node));
            ++movementNodeCount;
        } while (result->NextRow());
    }

    if (QueryResult result = WorldDatabase.Query(
        "SELECT `id`, `name`, `default_mode`, `walk_speed_multiplier`, `run_speed_multiplier`, "
        "`stealth_enabled`, `enabled`, `comment` "
        "FROM `lwi_movement_profile` WHERE `enabled` = 1 ORDER BY `id`"))
    {
        do
        {
            Field* fields = result->Fetch();
            MovementProfileDefinition profile;
            profile.Id = fields[0].Get<uint32>();
            profile.Name = fields[1].Get<std::string>();
            profile.DefaultMode = fields[2].Get<uint8>();
            profile.WalkSpeedMultiplier = fields[3].Get<float>();
            profile.RunSpeedMultiplier = fields[4].Get<float>();
            profile.StealthEnabled = fields[5].Get<bool>();
            profile.Enabled = fields[6].Get<bool>();
            if (!fields[7].IsNull())
                profile.Comment = fields[7].Get<std::string>();
            _movementProfiles.emplace(profile.Id, std::move(profile));
        } while (result->NextRow());
    }

    LOG_INFO("server.loading", "[LWI Movement] Loaded {} movement path(s).", _movementPaths.size());
    LOG_INFO("server.loading", "[LWI Movement] Loaded {} movement node(s).", movementNodeCount);
    LOG_INFO("server.loading", "[LWI Movement] Loaded {} movement profile(s).", _movementProfiles.size());
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

MovementPathDefinition const* InvasionMgr::GetMovementPath(uint32 id) const
{
    auto it = _movementPaths.find(id);
    return it != _movementPaths.end() ? &it->second : nullptr;
}

std::vector<MovementNodeDefinition> const* InvasionMgr::GetMovementNodes(uint32 pathId) const
{
    auto it = _movementNodesByPath.find(pathId);
    return it != _movementNodesByPath.end() ? &it->second : nullptr;
}

MovementProfileDefinition const* InvasionMgr::GetMovementProfile(uint32 id) const
{
    auto it = _movementProfiles.find(id);
    return it != _movementProfiles.end() ? &it->second : nullptr;
}

RuntimeSignalDefinition const* InvasionMgr::GetRuntimeSignal(uint32 id) const
{
    auto it = _runtimeSignals.find(id);
    return it != _runtimeSignals.end() ? &it->second : nullptr;
}

DialogueDefinition const* InvasionMgr::GetDialogue(uint32 id) const
{
    auto it = _dialogues.find(id);
    return it != _dialogues.end() ? &it->second : nullptr;
}
}
