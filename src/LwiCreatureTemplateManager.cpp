#include "LwiCreatureTemplateManager.h"

#include "DatabaseEnv.h"
#include "Log.h"

#include <limits>
#include <sstream>
#include <unordered_set>

namespace lwi
{
namespace
{
// Keep generated entries well away from normal WotLK IDs, but DO NOT assume
// ownership of this range. Every candidate is checked against creature_template
// and the persistent LWI mapping table before use.
constexpr uint32 AllocationStart = 15000000;
constexpr uint32 AllocationEnd = 16777215;

void ExecuteSql(std::string const& sql)
{
    WorldDatabase.DirectExecute(sql.c_str());
}
}

LwiCreatureTemplateManager& LwiCreatureTemplateManager::Instance()
{
    static LwiCreatureTemplateManager instance;
    return instance;
}

bool LwiCreatureTemplateManager::MaterializeStartupTemplates()
{
    _entryByLwiTemplate.clear();

    LOG_INFO("server.loading",
        "[LWI Template] Reconciling dynamically allocated creature templates before ObjectMgr creature-template load.");

    if (!RetireInactiveMappings())
    {
        return false;
    }

    if (!MaterializeEnabledDefinitions())
    {
        return false;
    }

    LoadMappings();

    LOG_INFO("server.loading",
        "[LWI Template] Reconciliation complete. {} active LWI creature template mapping(s).",
        _entryByLwiTemplate.size());

    return true;
}

bool LwiCreatureTemplateManager::RetireInactiveMappings()
{
    QueryResult result = WorldDatabase.Query(
        "SELECT m.`lwi_template_id`, m.`allocated_entry` "
        "FROM `lwi_creature_template_map` m "
        "LEFT JOIN `lwi_creature_template` d ON d.`id` = m.`lwi_template_id` "
        "WHERE d.`id` IS NULL OR d.`enabled` = 0");

    if (!result)
    {
        return true;
    }

    do
    {
        Field* fields = result->Fetch();
        uint32 const lwiTemplateId = fields[0].Get<uint32>();
        uint32 const allocatedEntry = fields[1].Get<uint32>();

        std::ostringstream sql;
        sql << "DELETE FROM `creature_template_model` WHERE `CreatureID` = " << allocatedEntry;
        ExecuteSql(sql.str());

        sql.str("");
        sql.clear();
        sql << "DELETE FROM `creature_template` WHERE `entry` = " << allocatedEntry;
        ExecuteSql(sql.str());

        sql.str("");
        sql.clear();
        sql << "UPDATE `lwi_creature_template_map` SET `retired` = 1 "
            << "WHERE `lwi_template_id` = " << lwiTemplateId;
        ExecuteSql(sql.str());

        LOG_INFO("server.loading",
            "[LWI Template] Retired logical template {} and removed generated creature entry {}. "
            "The allocated ID remains reserved and will not be reused.",
            lwiTemplateId,
            allocatedEntry);
    } while (result->NextRow());

    return true;
}

bool LwiCreatureTemplateManager::MaterializeEnabledDefinitions()
{
    QueryResult result = WorldDatabase.Query(
        "SELECT `id`, `base_creature_entry` "
        "FROM `lwi_creature_template` "
        "WHERE `enabled` = 1 ORDER BY `id`");

    if (!result)
    {
        LOG_INFO("server.loading", "[LWI Template] No enabled derived creature templates are defined.");
        return true;
    }

    do
    {
        Field* fields = result->Fetch();
        uint32 const lwiTemplateId = fields[0].Get<uint32>();
        uint32 const baseEntry = fields[1].Get<uint32>();

        if (!WorldDatabase.Query(
            ("SELECT 1 FROM `creature_template` WHERE `entry` = " + std::to_string(baseEntry) + " LIMIT 1").c_str()))
        {
            LOG_ERROR("server.loading",
                "[LWI Template] Logical template {} references missing base creature entry {}; skipped.",
                lwiTemplateId,
                baseEntry);
            continue;
        }

        uint32 allocatedEntry = 0;

        if (QueryResult mapping = WorldDatabase.Query(
            ("SELECT `allocated_entry` FROM `lwi_creature_template_map` "
             "WHERE `lwi_template_id` = " + std::to_string(lwiTemplateId) + " LIMIT 1").c_str()))
        {
            allocatedEntry = mapping->Fetch()[0].Get<uint32>();
        }
        else
        {
            allocatedEntry = AllocateEntry();
            if (allocatedEntry == 0)
            {
                LOG_ERROR("server.loading",
                    "[LWI Template] Could not allocate a free creature_template entry for logical template {}.",
                    lwiTemplateId);
                return false;
            }

            std::ostringstream insertMap;
            insertMap
                << "INSERT INTO `lwi_creature_template_map` "
                << "(`lwi_template_id`,`allocated_entry`,`base_creature_entry`,`retired`) VALUES ("
                << lwiTemplateId << "," << allocatedEntry << "," << baseEntry << ",0)";
            ExecuteSql(insertMap.str());

            LOG_INFO("server.loading",
                "[LWI Template] Allocated creature entry {} to logical template {}.",
                allocatedEntry,
                lwiTemplateId);
        }

        // The mapping owns this entry. Rebuild it from the CURRENT base row on
        // each startup so upstream AzerothCore base-template changes are inherited.
        std::ostringstream cleanup;
        cleanup << "DELETE FROM `creature_template_model` WHERE `CreatureID` = " << allocatedEntry;
        ExecuteSql(cleanup.str());

        cleanup.str("");
        cleanup.clear();
        cleanup << "DELETE FROM `creature_template` WHERE `entry` = " << allocatedEntry;
        ExecuteSql(cleanup.str());

        if (!MaterializeDefinition(lwiTemplateId, allocatedEntry))
        {
            LOG_ERROR("server.loading",
                "[LWI Template] Failed to materialize logical template {} into creature entry {}.",
                lwiTemplateId,
                allocatedEntry);
            continue;
        }

        std::ostringstream updateMap;
        updateMap
            << "UPDATE `lwi_creature_template_map` m "
            << "JOIN `lwi_creature_template` d ON d.`id` = m.`lwi_template_id` "
            << "SET m.`base_creature_entry` = d.`base_creature_entry`, m.`retired` = 0 "
            << "WHERE m.`lwi_template_id` = " << lwiTemplateId;
        ExecuteSql(updateMap.str());

    } while (result->NextRow());

    return true;
}

uint32 LwiCreatureTemplateManager::AllocateEntry() const
{
    // Reserved mappings are included even if their generated row is currently
    // retired, preventing automatic ID recycling.
    QueryResult used = WorldDatabase.Query(
        "SELECT `id_value` FROM ("
        " SELECT `entry` AS `id_value` FROM `creature_template` WHERE `entry` >= 15000000"
        " UNION "
        " SELECT `allocated_entry` AS `id_value` FROM `lwi_creature_template_map` WHERE `allocated_entry` >= 15000000"
        ") ids ORDER BY `id_value`");

    uint32 candidate = AllocationStart;

    if (used)
    {
        do
        {
            uint32 const usedEntry = used->Fetch()[0].Get<uint32>();

            if (usedEntry < candidate)
            {
                continue;
            }

            if (usedEntry == candidate)
            {
                if (candidate == AllocationEnd)
                {
                    return 0;
                }

                ++candidate;
                continue;
            }

            // First gap.
            break;

        } while (used->NextRow());
    }

    return candidate <= AllocationEnd ? candidate : 0;
}

bool LwiCreatureTemplateManager::MaterializeDefinition(uint32 lwiTemplateId, uint32 allocatedEntry)
{
    // We deliberately inherit the physical/combat shell but strip world-service
    // identity and entry-specific scripting:
    //   - no gossip/vendor/quest/trainer/etc npcflags
    //   - no loot/pickpocket/skinning/gold
    //   - no SmartAI/ScriptName inheritance
    //   - no permanent movement path
    //
    // Level remains a spawn-time LWI concern via level_override.
    std::ostringstream sql;
    sql <<
        "INSERT INTO `creature_template` ("
        "`entry`,`difficulty_entry_1`,`difficulty_entry_2`,`difficulty_entry_3`,"
        "`KillCredit1`,`KillCredit2`,`name`,`subname`,`IconName`,`gossip_menu_id`,"
        "`minlevel`,`maxlevel`,`exp`,`faction`,`npcflag`,`speed_walk`,`speed_run`,"
        "`speed_swim`,`speed_flight`,`detection_range`,`rank`,`dmgschool`,"
        "`DamageModifier`,`BaseAttackTime`,`RangeAttackTime`,`BaseVariance`,`RangeVariance`,"
        "`unit_class`,`unit_flags`,`unit_flags2`,`dynamicflags`,`family`,`type`,`type_flags`,"
        "`lootid`,`pickpocketloot`,`skinloot`,`PetSpellDataId`,`VehicleId`,`mingold`,`maxgold`,"
        "`AIName`,`MovementType`,`HoverHeight`,`HealthModifier`,`ManaModifier`,`ArmorModifier`,"
        "`ExperienceModifier`,`RacialLeader`,`movementId`,`RegenHealth`,`CreatureImmunitiesId`,"
        "`flags_extra`,`ScriptName`,`VerifiedBuild`) "
        "SELECT "
        << allocatedEntry << ","
        "0,0,0,0,0,"
        "COALESCE(d.`name_override`, b.`name`),"
        "COALESCE(d.`subname_override`, b.`subname`),"
        "b.`IconName`,"
        "0,"
        "b.`minlevel`,b.`maxlevel`,b.`exp`,"
        "COALESCE(d.`faction_override`, b.`faction`),"
        "0,"
        "b.`speed_walk`,b.`speed_run`,b.`speed_swim`,b.`speed_flight`,b.`detection_range`,"
        "COALESCE(d.`rank_override`, b.`rank`),"
        "b.`dmgschool`,"
        "COALESCE(d.`damage_modifier_override`, b.`DamageModifier`),"
        "b.`BaseAttackTime`,b.`RangeAttackTime`,b.`BaseVariance`,b.`RangeVariance`,"
        "COALESCE(d.`unit_class_override`, b.`unit_class`),"
        "b.`unit_flags`,b.`unit_flags2`,b.`dynamicflags`,b.`family`,b.`type`,b.`type_flags`,"
        "0,0,0,0,0,0,0,"
        "'',"
        "0,"
        "b.`HoverHeight`,"
        "COALESCE(d.`health_modifier_override`, b.`HealthModifier`),"
        "COALESCE(d.`mana_modifier_override`, b.`ManaModifier`),"
        "COALESCE(d.`armor_modifier_override`, b.`ArmorModifier`),"
        "b.`ExperienceModifier`,"
        "0,0,b.`RegenHealth`,b.`CreatureImmunitiesId`,b.`flags_extra`,'',-1 "
        "FROM `lwi_creature_template` d "
        "JOIN `creature_template` b ON b.`entry` = d.`base_creature_entry` "
        "WHERE d.`id` = " << lwiTemplateId << " AND d.`enabled` = 1";

    ExecuteSql(sql.str());

    if (!WorldDatabase.Query(
        ("SELECT 1 FROM `creature_template` WHERE `entry` = " + std::to_string(allocatedEntry) + " LIMIT 1").c_str()))
    {
        return false;
    }

    std::ostringstream models;
    models <<
        "INSERT INTO `creature_template_model` "
        "(`CreatureID`,`Idx`,`CreatureDisplayID`,`DisplayScale`,`Probability`,`VerifiedBuild`) "
        "SELECT " << allocatedEntry << ",`Idx`,`CreatureDisplayID`,`DisplayScale`,`Probability`,-1 "
        "FROM `creature_template_model` "
        "WHERE `CreatureID` = (SELECT `base_creature_entry` FROM `lwi_creature_template` "
        "WHERE `id` = " << lwiTemplateId << ")";

    ExecuteSql(models.str());

    LOG_INFO("server.loading",
        "[LWI Template] Materialized logical template {} as creature entry {}.",
        lwiTemplateId,
        allocatedEntry);

    return true;
}

void LwiCreatureTemplateManager::LoadMappings()
{
    _entryByLwiTemplate.clear();

    QueryResult result = WorldDatabase.Query(
        "SELECT m.`lwi_template_id`, m.`allocated_entry` "
        "FROM `lwi_creature_template_map` m "
        "JOIN `lwi_creature_template` d ON d.`id` = m.`lwi_template_id` "
        "JOIN `creature_template` c ON c.`entry` = m.`allocated_entry` "
        "WHERE m.`retired` = 0 AND d.`enabled` = 1");

    if (!result)
    {
        return;
    }

    do
    {
        Field* fields = result->Fetch();
        _entryByLwiTemplate.emplace(
            fields[0].Get<uint32>(),
            fields[1].Get<uint32>());
    } while (result->NextRow());
}

uint32 LwiCreatureTemplateManager::ResolveEntry(uint32 lwiTemplateId) const
{
    auto const itr = _entryByLwiTemplate.find(lwiTemplateId);
    return itr != _entryByLwiTemplate.end() ? itr->second : 0;
}

uint32 LwiCreatureTemplateManager::GetMappedTemplateCount() const
{
    return static_cast<uint32>(_entryByLwiTemplate.size());
}
}
