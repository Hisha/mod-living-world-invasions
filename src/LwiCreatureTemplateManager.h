#ifndef MOD_LIVING_WORLD_INVASIONS_CREATURE_TEMPLATE_MANAGER_H
#define MOD_LIVING_WORLD_INVASIONS_CREATURE_TEMPLATE_MANAGER_H

#include "Define.h"

#include <unordered_map>

namespace lwi
{
class LwiCreatureTemplateManager
{
public:
    static LwiCreatureTemplateManager& Instance();

    // Must run before AzerothCore loads creature_template into ObjectMgr.
    bool MaterializeStartupTemplates();

    [[nodiscard]] uint32 ResolveEntry(uint32 lwiTemplateId) const;
    [[nodiscard]] uint32 GetMappedTemplateCount() const;

private:
    LwiCreatureTemplateManager() = default;

    bool RetireInactiveMappings();
    bool MaterializeEnabledDefinitions();
    uint32 AllocateEntry() const;
    bool MaterializeDefinition(uint32 lwiTemplateId, uint32 allocatedEntry);
    void LoadMappings();

    std::unordered_map<uint32, uint32> _entryByLwiTemplate;
};
}

#define sLwiCreatureTemplateMgr lwi::LwiCreatureTemplateManager::Instance()

#endif
