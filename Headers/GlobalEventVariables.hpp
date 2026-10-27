//
// Created by berke on 10/27/2026.
//

#ifndef TILKY_ENGINE_GLOBALEVENTVARIABLES_HPP
#define TILKY_ENGINE_GLOBALEVENTVARIABLES_HPP

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

#include <nlohmann/json_fwd.hpp>

#include "Objects/Types.hpp"

// Project-wide event variables, saved in ProjectSettings.json. Their IDs
// carry GLOBAL_EVENT_VARIABLE_BIT; a level's own variables live in
// WorldSettings. Values keep between levels and reset when a play session
// starts.
namespace GlobalEventVariables
{
    struct Definition {
        std::string name;
        float defaultValue = 0.0f;
    };

    using DefinitionMap = std::unordered_map<EventVariableID, Definition>;

    std::optional<EventVariableID> Find(const std::string& name);
    // nullopt if `id` isn't registered (e.g. deleted while a level's
    // condition still used it).
    std::optional<std::string> GetName(EventVariableID id);

    // Fails (returns nullopt / false, registry left untouched) if the name is
    // empty or already used by another global variable. Clashes with the
    // loaded level's variables are checked by Level::IsEventVariableNameTaken,
    // since only the caller knows which levels are loaded.
    std::optional<EventVariableID> Create(const std::string& name, float defaultValue = 0.0f);
    bool Rename(EventVariableID id, const std::string& newName);
    bool SetDefaultValue(EventVariableID id, float defaultValue);
    bool Delete(EventVariableID id);

    const DefinitionMap& GetAll();

    // Live values, read by Condition::Check. Empty until ResetValues.
    std::unordered_map<EventVariableID, float>& Values();
    // Every value back to its default; called when a play session starts.
    void ResetValues();

    // Adds "eventVariables" and "nextEventVariableId" to a ProjectSettings.json object.
    void WriteToProjectSettings(nlohmann::json& settings);
    // A missing file or missing "eventVariables" means a project without
    // global variables: the registry is cleared and this returns true.
    // Broken entries are skipped with a warning.
    bool LoadFromProjectSettings(const std::filesystem::path& settingsPath);
    void Clear();
}

#endif //TILKY_ENGINE_GLOBALEVENTVARIABLES_HPP
