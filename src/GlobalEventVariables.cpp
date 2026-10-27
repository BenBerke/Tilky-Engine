//
// Created by berke on 10/27/2026.
//

#include "Headers/GlobalEventVariables.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace
{
    GlobalEventVariables::DefinitionMap definitions;
    std::unordered_map<std::string, EventVariableID> idsByName;
    std::unordered_map<EventVariableID, float> values;

    // Without GLOBAL_EVENT_VARIABLE_BIT. Deleted IDs are never reused.
    uint32_t nextId = 1;

    // Highest counter whose ID stays below INVALID_ID once the bit is set.
    constexpr uint32_t MAX_ID = GLOBAL_EVENT_VARIABLE_BIT - 2u;
}

std::optional<EventVariableID> GlobalEventVariables::Find(const std::string& name) {
    const auto iterator = idsByName.find(name);

    if (iterator == idsByName.end()) return std::nullopt;

    return iterator->second;
}

std::optional<std::string> GlobalEventVariables::GetName(const EventVariableID id) {
    const auto iterator = definitions.find(id);

    if (iterator == definitions.end()) return std::nullopt;

    return iterator->second.name;
}

std::optional<EventVariableID> GlobalEventVariables::Create(const std::string& name, const float defaultValue) {
    if (name.empty() || idsByName.contains(name)) return std::nullopt;

    if (nextId > MAX_ID) {
        spdlog::error("Maximum number of global event variables reached");
        return std::nullopt;
    }

    const EventVariableID id = GLOBAL_EVENT_VARIABLE_BIT | nextId++;
    definitions[id] = {name, defaultValue};
    idsByName[name] = id;
    values[id] = defaultValue;

    return id;
}

bool GlobalEventVariables::Rename(const EventVariableID id, const std::string& newName) {
    const auto iterator = definitions.find(id);
    if (iterator == definitions.end() || newName.empty()) return false;
    if (iterator->second.name == newName) return true;
    if (idsByName.contains(newName)) return false;

    idsByName.erase(iterator->second.name);
    iterator->second.name = newName;
    idsByName[newName] = id;

    return true;
}

bool GlobalEventVariables::SetDefaultValue(const EventVariableID id, const float defaultValue) {
    const auto iterator = definitions.find(id);
    if (iterator == definitions.end()) return false;

    iterator->second.defaultValue = defaultValue;

    return true;
}

bool GlobalEventVariables::Delete(const EventVariableID id) {
    const auto iterator = definitions.find(id);
    if (iterator == definitions.end()) return false;

    idsByName.erase(iterator->second.name);
    definitions.erase(iterator);
    values.erase(id);

    return true;
}

const GlobalEventVariables::DefinitionMap& GlobalEventVariables::GetAll() {
    return definitions;
}

std::unordered_map<EventVariableID, float>& GlobalEventVariables::Values() {
    return values;
}

void GlobalEventVariables::ResetValues() {
    values.clear();
    for (const auto& [id, definition] : definitions) values[id] = definition.defaultValue;
}

void GlobalEventVariables::WriteToProjectSettings(nlohmann::json& settings) {
    nlohmann::json variables = nlohmann::json::array();

    for (const auto& [id, definition] : definitions) {
        variables.push_back({
            {"id", id},
            {"name", definition.name},
            {"value", definition.defaultValue}
        });
    }

    settings["eventVariables"] = std::move(variables);
    settings["nextEventVariableId"] = nextId;
}

bool GlobalEventVariables::LoadFromProjectSettings(const std::filesystem::path& settingsPath) {
    Clear();

    try {
        if (!std::filesystem::exists(settingsPath)) return true;

        std::ifstream file(settingsPath);

        if (!file.is_open()) {
            spdlog::error("Failed to open project settings: {}", settingsPath.string());
            return false;
        }

        nlohmann::json settings;
        file >> settings;

        if (!settings.contains("eventVariables")) return true;

        const nlohmann::json& variables = settings.at("eventVariables");

        if (!variables.is_array()) {
            spdlog::warn("Project settings: \"eventVariables\" is not an array, ignored");
            return true;
        }

        // Never hand out an ID at or below one already saved, even if
        // nextEventVariableId is missing or too low.
        uint32_t minimumNextId = 1;

        for (size_t i = 0; i < variables.size(); ++i) {
            try {
                const nlohmann::json& variable = variables[i];

                const auto id = variable.at("id").get<EventVariableID>();
                const auto name = variable.at("name").get<std::string>();
                const float value = variable.value("value", 0.0f);

                if (!IsGlobalEventVariable(id) || id == INVALID_ID)
                    throw std::runtime_error("id is not a global event variable id");
                if (name.empty()) throw std::runtime_error("empty name");
                if (definitions.contains(id)) throw std::runtime_error("duplicate id");
                if (idsByName.contains(name)) throw std::runtime_error("duplicate name \"" + name + "\"");

                definitions[id] = {name, value};
                idsByName[name] = id;
                minimumNextId = std::max(minimumNextId, (id & ~GLOBAL_EVENT_VARIABLE_BIT) + 1u);
            }
            catch (const std::exception& exception) {
                spdlog::warn("Project settings: skipped event variable {}: {}", i, exception.what());
            }
        }

        nextId = std::max(settings.value("nextEventVariableId", minimumNextId), minimumNextId);
        ResetValues();

        return true;
    }
    catch (const std::exception& error) {
        spdlog::error("Failed to load global event variables: {}", error.what());
        Clear();
        return false;
    }
}

void GlobalEventVariables::Clear() {
    definitions.clear();
    idsByName.clear();
    values.clear();
    nextId = 1;
}
