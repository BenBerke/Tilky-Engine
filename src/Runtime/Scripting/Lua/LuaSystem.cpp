//
// Created by berke on 5/15/2026.
//

#include "../../../../Headers/Runtime/Scripting/Lua/LuaScripting.hpp"

#include "Headers/Engine/GameTime.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Objects/EntityTypes.hpp"
#include "Headers/Objects/LuaWrappers.hpp"
#include "Headers/Objects/ScriptPublicType.hpp"
#include "Headers/Project/ProjectManager.hpp"

#include <sol/sol.hpp>
#include <spdlog/spdlog.h>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace {
    namespace fs = std::filesystem;

    struct ScriptAsset {
        std::string fileName;
        fs::path path;

        std::vector<ScriptPublicField> publicFields;

        fs::file_time_type lastWriteTime {};
        std::uint64_t schemaHash = 0;
    };

    struct ScriptInstance {
        ID ownerID = INVALID_ENTITY_ID;
        std::string scriptFile;

        sol::environment environment;
        sol::table publicTable;

        sol::protected_function startFunction;
        sol::protected_function updateFunction;

        bool started = false;
    };

    struct ScriptGameTime {
    };

    sol::state lua;

    std::vector<ScriptInstance> scriptInstances;
    std::unordered_map<std::string, ScriptAsset> scriptAssets;

    // Entity ID -> indices into scriptInstances.
    std::unordered_map<ID, std::vector<std::size_t>> scriptInstancesByOwner;

    std::string CleanScriptFileName(const std::string& fileName) {
        if (fileName.empty()) {
            return "";
        }

        return fs::path(fileName).stem().string();
    }

    fs::path GetScriptPathFromFileName(const std::string& fileName) {
        const std::string cleanName = CleanScriptFileName(fileName);
        return ProjectManager::GetScriptsPath() / (cleanName + ".lua");
    }

    ScriptInstance* FindScriptInstance(const ID ownerID, const std::string& scriptFile) {
        const std::string cleanScriptFile = CleanScriptFileName(scriptFile);

        if (ownerID == INVALID_ENTITY_ID || cleanScriptFile.empty()) {
            return nullptr;
        }

        const auto ownerIt = scriptInstancesByOwner.find(ownerID);

        if (ownerIt == scriptInstancesByOwner.end()) {
            return nullptr;
        }

        for (const std::size_t instanceIndex : ownerIt->second) {
            if (instanceIndex >= scriptInstances.size()) {
                continue;
            }

            ScriptInstance& instance = scriptInstances[instanceIndex];

            if (instance.scriptFile == cleanScriptFile) {
                return &instance;
            }
        }

        return nullptr;
    }

    const char* ScriptValueTypeToString(const ScriptValueType type) {
        switch (type) {
            case ScriptValueType::Int:
                return "Int";

            case ScriptValueType::Float:
                return "Float";

            case ScriptValueType::Bool:
                return "Bool";

            case ScriptValueType::String:
                return "String";
        }

        return "Unknown";
    }

    bool IsReservedPublicName(const std::string& name) {
        return name == "Int" ||
               name == "Float" ||
               name == "Bool" ||
               name == "String";
    }

    bool IsScriptValueTypeValid(const ScriptValue& value, const ScriptValueType type) {
        switch (type) {
            case ScriptValueType::Int:
                return std::holds_alternative<int>(value);

            case ScriptValueType::Float:
                return std::holds_alternative<float>(value);

            case ScriptValueType::Bool:
                return std::holds_alternative<bool>(value);

            case ScriptValueType::String:
                return std::holds_alternative<std::string>(value);
        }

        return false;
    }

    void HashCombine(std::uint64_t& seed, const std::uint64_t value) {
        seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
    }

    void HashScriptValue(std::uint64_t& seed, const ScriptValue& value) {
        std::visit(
            [&seed](const auto& typedValue) {
                using T = std::decay_t<decltype(typedValue)>;

                if constexpr (std::is_same_v<T, int>) {
                    HashCombine(seed, std::hash<int>{}(typedValue));
                } else if constexpr (std::is_same_v<T, float>) {
                    HashCombine(seed, std::hash<float>{}(typedValue));
                } else if constexpr (std::is_same_v<T, bool>) {
                    HashCombine(seed, std::hash<bool>{}(typedValue));
                } else if constexpr (std::is_same_v<T, std::string>) {
                    HashCombine(seed, std::hash<std::string>{}(typedValue));
                }
            },
            value
        );
    }

    std::uint64_t HashPublicFields(const std::vector<ScriptPublicField>& fields) {
        std::uint64_t hash = 1469598103934665603ULL;

        for (const ScriptPublicField& field : fields) {
            HashCombine(hash, std::hash<std::string>{}(field.name));
            HashCombine(hash, static_cast<std::uint64_t>(field.type));
            HashScriptValue(hash, field.defaultValue);
        }

        return hash;
    }

    void AddPublicField(
        std::vector<ScriptPublicField>& fields,
        std::unordered_set<std::string>& registeredNames,
        const std::string& scriptName,
        std::string name,
        const ScriptValueType type,
        ScriptValue defaultValue
    ) {
        if (name.empty()) {
            spdlog::warn(
                "Lua script '{}' tried to declare a public variable with an empty name",
                scriptName
            );

            return;
        }

        if (IsReservedPublicName(name)) {
            spdlog::warn(
                "Lua script '{}' tried to declare reserved public variable name '{}'",
                scriptName,
                name
            );

            return;
        }

        if (!registeredNames.insert(name).second) {
            spdlog::warn(
                "Lua script '{}' declared duplicate public variable '{}'",
                scriptName,
                name
            );

            return;
        }

        ScriptPublicField field;
        field.name = std::move(name);
        field.type = type;
        field.defaultValue = std::move(defaultValue);
        field.displayName = field.name;

        fields.push_back(std::move(field));
    }

    void AddSchemaPublicDeclarationFunctions(
        sol::table publicApi,
        std::vector<ScriptPublicField>& fields,
        std::unordered_set<std::string>& registeredNames,
        const std::string& scriptName
    ) {
        publicApi.set_function(
            "Float",
            [&fields, &registeredNames, scriptName](const std::string& name, const float defaultValue) {
                AddPublicField(
                    fields,
                    registeredNames,
                    scriptName,
                    name,
                    ScriptValueType::Float,
                    ScriptValue {defaultValue}
                );
            }
        );

        publicApi.set_function(
            "Int",
            [&fields, &registeredNames, scriptName](const std::string& name, const int defaultValue) {
                AddPublicField(
                    fields,
                    registeredNames,
                    scriptName,
                    name,
                    ScriptValueType::Int,
                    ScriptValue {defaultValue}
                );
            }
        );

        publicApi.set_function(
            "Bool",
            [&fields, &registeredNames, scriptName](const std::string& name, const bool defaultValue) {
                AddPublicField(
                    fields,
                    registeredNames,
                    scriptName,
                    name,
                    ScriptValueType::Bool,
                    ScriptValue {defaultValue}
                );
            }
        );

        publicApi.set_function(
            "String",
            [&fields, &registeredNames, scriptName](const std::string& name, const std::string& defaultValue) {
                AddPublicField(
                    fields,
                    registeredNames,
                    scriptName,
                    name,
                    ScriptValueType::String,
                    ScriptValue {defaultValue}
                );
            }
        );
    }

    template<typename T>
    void SetPublicDefaultIfMissing(sol::table publicTable, const std::string& name, const T& defaultValue) {
        const sol::object existingValue = publicTable.get<sol::object>(name);

        if (existingValue.get_type() == sol::type::nil) {
            publicTable[name] = defaultValue;
        }
    }

    void AddRuntimePublicDeclarationFunctions(sol::table publicTable) {
        publicTable.set_function(
            "Float",
            [publicTable](const std::string& name, const float defaultValue) {
                SetPublicDefaultIfMissing(publicTable, name, defaultValue);
            }
        );

        publicTable.set_function(
            "Int",
            [publicTable](const std::string& name, const int defaultValue) {
                SetPublicDefaultIfMissing(publicTable, name, defaultValue);
            }
        );

        publicTable.set_function(
            "Bool",
            [publicTable](const std::string& name, const bool defaultValue) {
                SetPublicDefaultIfMissing(publicTable, name, defaultValue);
            }
        );

        publicTable.set_function(
            "String",
            [publicTable](const std::string& name, const std::string& defaultValue) {
                SetPublicDefaultIfMissing(publicTable, name, defaultValue);
            }
        );
    }

    std::vector<ScriptPublicField> ExtractPublicFields(
        const std::string& scriptName,
        const fs::path& path
    ) {
        std::vector<ScriptPublicField> fields;
        std::unordered_set<std::string> registeredNames;

        sol::environment environment(
            lua,
            sol::create,
            lua.globals()
        );

        sol::table publicApi = lua.create_table();

        AddSchemaPublicDeclarationFunctions(
            publicApi,
            fields,
            registeredNames,
            scriptName
        );

        environment["Public"] = publicApi;

        // Schema pass must not touch real game state.
        environment["Owner"] = sol::nil;

        // Prevent schema extraction from mutating the real shared Scripts table.
        environment["Scripts"] = lua.create_table();

        const sol::load_result loadedScript = lua.load_file(path.string());

        if (!loadedScript.valid()) {
            const sol::error error = loadedScript;

            spdlog::error(
                "Failed to load Lua script schema '{}': {}",
                path.string(),
                error.what()
            );

            return fields;
        }

        sol::protected_function scriptFunction = loadedScript;
        sol::set_environment(environment, scriptFunction);

        const sol::protected_function_result result = scriptFunction();

        if (!result.valid()) {
            const sol::error error = result;

            spdlog::error(
                "Failed to extract Lua script schema '{}': {}",
                path.string(),
                error.what()
            );
        }

        return fields;
    }

    ScriptAsset& LoadOrRefreshScriptAsset(
        const std::string& cleanFileName,
        const fs::path& path
    ) {
        const fs::file_time_type lastWriteTime = fs::last_write_time(path);

        const auto it = scriptAssets.find(cleanFileName);

        if (it != scriptAssets.end() && it->second.lastWriteTime == lastWriteTime) {
            return it->second;
        }

        ScriptAsset asset;
        asset.fileName = cleanFileName;
        asset.path = path;
        asset.lastWriteTime = lastWriteTime;
        asset.publicFields = ExtractPublicFields(cleanFileName, path);
        asset.schemaHash = HashPublicFields(asset.publicFields);

        scriptAssets[cleanFileName] = std::move(asset);

        return scriptAssets[cleanFileName];
    }

    void ReconcilePublicValues(ComponentScript& script, const ScriptAsset& asset) {
        for (const ScriptPublicField& field : asset.publicFields) {
            auto valueIt = script.publicValues.find(field.name);

            if (valueIt == script.publicValues.end()) {
                script.publicValues[field.name] = field.defaultValue;
                continue;
            }

            if (!IsScriptValueTypeValid(valueIt->second, field.type)) {
                spdlog::warn(
                    "Public variable '{}.{}' on entity {} had wrong type. Expected {}. Resetting to default.",
                    script.fileName,
                    field.name,
                    script.ownerID,
                    ScriptValueTypeToString(field.type)
                );

                valueIt->second = field.defaultValue;
            }
        }

        script.schemaHash = asset.schemaHash;

        // Do not auto-delete unknown public values here.
        // The editor should show orphaned values and let the user remove them manually.
    }

    void SetLuaValue(sol::table table, const std::string& name, const ScriptValue& value) {
        std::visit(
            [&table, &name](const auto& typedValue) {
                table[name] = typedValue;
            },
            value
        );
    }

    void ApplyPublicValuesToLua(
        sol::table publicTable,
        const std::unordered_map<std::string, ScriptValue>& publicValues
    ) {
        for (const auto& [name, value] : publicValues) {
            if (IsReservedPublicName(name)) {
                spdlog::warn(
                    "Skipping public variable with reserved name '{}'",
                    name
                );

                continue;
            }

            SetLuaValue(publicTable, name, value);
        }
    }

    bool LoadScriptIntoInstance(
        Level& level,
        ComponentScript& script,
        const std::string& cleanFileName,
        const fs::path& path,
        ScriptInstance& instance
    ) {
        instance.ownerID = script.ownerID;
        instance.scriptFile = cleanFileName;
        instance.started = false;

        instance.environment = sol::environment(
            lua,
            sol::create,
            lua.globals()
        );

        ScriptEntity ownerEntity {&level, script.ownerID};

        instance.environment["Owner"] = ownerEntity;
        instance.environment["Scripts"] = lua["Scripts"];

        instance.publicTable = lua.create_table();

        ApplyPublicValuesToLua(instance.publicTable, script.publicValues);
        AddRuntimePublicDeclarationFunctions(instance.publicTable);

        instance.environment["Public"] = instance.publicTable;

        const sol::load_result loadedScript = lua.load_file(path.string());

        if (!loadedScript.valid()) {
            const sol::error error = loadedScript;

            spdlog::error(
                "Failed to load Lua script '{}': {}",
                path.string(),
                error.what()
            );

            return false;
        }

        sol::protected_function scriptFunction = loadedScript;
        sol::set_environment(instance.environment, scriptFunction);

        const sol::protected_function_result result = scriptFunction();

        if (!result.valid()) {
            const sol::error error = result;

            spdlog::error(
                "Failed to run Lua script '{}': {}",
                path.string(),
                error.what()
            );

            return false;
        }

        instance.startFunction = instance.environment["Start"];
        instance.updateFunction = instance.environment["Update"];

        return true;
    }

    void CallStart(ScriptInstance& instance) {
        if (instance.started) {
            return;
        }

        instance.started = true;

        if (!instance.startFunction.valid()) {
            return;
        }

        const sol::protected_function_result startResult = instance.startFunction();

        if (!startResult.valid()) {
            const sol::error error = startResult;

            spdlog::error(
                "Lua Start error in '{}': {}",
                instance.scriptFile,
                error.what()
            );
        }
    }

    void RegisterGameTimeBindings(sol::state& luaState) {
        luaState.new_usertype<ScriptGameTime>(
            "ScriptGameTime",
            sol::no_constructor,

            "deltaTime",
            sol::property([] {
                return GameTime::deltaTime;
            })
        );

        luaState["GameTime"] = ScriptGameTime {};
    }

    void RegisterScriptComponentRefBindings(sol::state& luaState) {
        luaState.new_usertype<ScriptComponentRef>(
            "ScriptComponentRef",
            sol::no_constructor,

            "Public",
            sol::property([](const ScriptComponentRef& ref) -> sol::object {
                ScriptInstance* instance = FindScriptInstance(ref.ownerID, ref.scriptFile);

                if (instance == nullptr || !instance->publicTable.valid()) {
                    return sol::make_object(lua, sol::nil);
                }

                return sol::make_object(lua, instance->publicTable);
            }),

            "IsValid",
            [](const ScriptComponentRef& ref) {
                return FindScriptInstance(ref.ownerID, ref.scriptFile) != nullptr;
            },

            "isValid",
            [](const ScriptComponentRef& ref) {
                return FindScriptInstance(ref.ownerID, ref.scriptFile) != nullptr;
            }
        );
    }

    void RegisterBindings(LuaScriptSystem& scriptSystem) {
        scriptSystem.RegisterMathBindings(lua);
        scriptSystem.RegisterVectorBindings(lua);
        scriptSystem.RegisterComponentBindings(lua);
        scriptSystem.RegisterEntityBindings(lua);
        scriptSystem.RegisterInputBindings(lua);
        scriptSystem.RegisterEditorFunctionBindings(lua);
        scriptSystem.RegisterGameBindings(lua);
        scriptSystem.RegisterWallBindings(lua);
        scriptSystem.RegisterSectorBindings(lua);

        RegisterGameTimeBindings(lua);
        RegisterScriptComponentRefBindings(lua);
    }
}

bool LuaScriptSystem::Initialize() {
    try {
        lua.open_libraries(
            sol::lib::base,
            sol::lib::math,
            sol::lib::table,
            sol::lib::string
        );

        RegisterBindings(*this);

        lua["Scripts"] = lua.create_table();

        spdlog::info("Lua scripting initialized");
        return true;
    }
    catch (const std::exception &e) {
        spdlog::critical(
            "Failed to initialize Lua scripting {}",
            e.what()
        );

        return false;
    }
}

void LuaScriptSystem::Start(Level& level) {
    scriptInstances.clear();
    scriptInstancesByOwner.clear();

    // Shared table for cross-script utilities/state.
    lua["Scripts"] = lua.create_table();

    for (ComponentScript& script : level.scripts.components) {
        const std::string cleanFileName = CleanScriptFileName(script.fileName);

        if (cleanFileName.empty()) {
            spdlog::warn(
                "Skipping script component with empty file name on entity {}",
                script.ownerID
            );

            continue;
        }

        const fs::path path = GetScriptPathFromFileName(cleanFileName);

        if (!fs::exists(path)) {
            spdlog::error(
                "Lua script does not exist: {}",
                path.string()
            );

            continue;
        }

        ScriptAsset& asset = LoadOrRefreshScriptAsset(cleanFileName, path);

        ReconcilePublicValues(script, asset);

        ScriptInstance instance;

        if (!LoadScriptIntoInstance(level, script, cleanFileName, path, instance)) {
            continue;
        }

        const std::size_t instanceIndex = scriptInstances.size();

        scriptInstances.push_back(std::move(instance));
        scriptInstancesByOwner[script.ownerID].push_back(instanceIndex);
    }

    for (ScriptInstance& instance : scriptInstances) {
        ComponentScript* script = level.scripts.Get(instance.ownerID);

        if (script == nullptr || !script->enabled) {
            continue;
        }

        CallStart(instance);
    }
}

void LuaScriptSystem::Update(Level& level) {
    for (ScriptInstance& instance : scriptInstances) {
        ComponentScript* script = level.scripts.Get(instance.ownerID);

        if (script == nullptr || !script->enabled) continue;

        if (!instance.started) CallStart(instance);

        if (!instance.updateFunction.valid()) continue;


        const sol::protected_function_result result = instance.updateFunction();

        if (!result.valid()) {
            const sol::error error = result;

            spdlog::error(
                "Lua Update error in '{}': {}",
                instance.scriptFile,
                error.what()
            );
        }
    }
}

void LuaScriptSystem::Shutdown() {
    scriptInstances.clear();
    scriptInstancesByOwner.clear();
    scriptAssets.clear();

    lua = sol::state {};

    spdlog::info("Lua scripting shutdown");
}

const std::vector<ScriptPublicField>* LuaScriptSystem::GetPublicFieldsForScript(const std::string& fileName) {
    const std::string cleanFileName = CleanScriptFileName(fileName);

    if (cleanFileName.empty()) {
        return nullptr;
    }

    const fs::path path = GetScriptPathFromFileName(cleanFileName);

    if (!fs::exists(path)) {
        return nullptr;
    }

    ScriptAsset& asset = LoadOrRefreshScriptAsset(cleanFileName, path);
    return &asset.publicFields;
}

bool LuaScriptSystem::ReconcileScriptPublicValues(ComponentScript& script) {
    const std::string cleanFileName = CleanScriptFileName(script.fileName);

    if (cleanFileName.empty()) {
        return false;
    }

    const fs::path path = GetScriptPathFromFileName(cleanFileName);

    if (!fs::exists(path)) {
        return false;
    }

    ScriptAsset& asset = LoadOrRefreshScriptAsset(cleanFileName, path);
    ReconcilePublicValues(script, asset);

    return true;
}

void LuaScriptSystem::RefreshScriptAssets(Level& level) {
    scriptAssets.clear();

    for (ComponentScript& script : level.scripts.components) {
        ReconcileScriptPublicValues(script);
    }
}