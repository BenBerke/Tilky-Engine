//
// Created by berke on 5/15/2026.
//

#include "../../../Headers/Runtime/Scripting/ScriptSystem.hpp"

#include "Headers/Objects/EntityTypes.hpp"

#include <sol/sol.hpp>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <string>
#include <vector>

#include "Headers/Engine/GameTime.hpp"
#include "Headers/Project/ProjectManager.hpp"

#include "Headers/Objects/Wrappers.hpp"

//todo add more functionality

namespace {
    struct ScriptInstance {
        ID ownerID = static_cast<ID>(-1);
        std::string scriptFile;

        sol::environment environment;
        sol::protected_function startFunction;
        sol::protected_function updateFunction;
    };

    sol::state lua;
    std::vector<ScriptInstance> scriptInstances;

    void ResetLuaScriptRegistry() {
        lua["Public"] = lua.create_table();
    }

    void RegisterBindings() {
        ScriptSystem::RegisterMathBindings(lua);
        ScriptSystem::RegisterVectorBindings(lua);
        ScriptSystem::RegisterComponentBindings(lua);
        ScriptSystem::RegisterEntityBindings(lua);
        ScriptSystem::RegisterInputBindings(lua);
        ScriptSystem::RegisterEditorFunctionBindings(lua);
    }

    namespace fs = std::filesystem;

    namespace {
        std::string CleanScriptFileName(const std::string &fileName) {
            if (fileName.empty()) {
                return "";
            }

            return fs::path(fileName).stem().string();
        }

        fs::path GetScriptPathFromFileName(const std::string &fileName) {
            const std::string cleanName = CleanScriptFileName(fileName);

            return ProjectManager::GetScriptsPath() / (cleanName + ".lua");
        }
    }
}

namespace ScriptSystem {
    bool Initialize() {
        try {
            lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string);

            RegisterBindings();

            spdlog::info("Lua scripting initialized");
            return true;
        } catch (std::exception &e) {
            spdlog::critical("Failed to initialize Lua scripting {}", e.what());
            return false;
        }
    }

    void Start(Level &level) {
        scriptInstances.clear();

        ResetLuaScriptRegistry();

        sol::table scripts = lua["Public"];

        // PASS 1:
        // Load every script file and run its top-level code.
        // This lets scripts register shared variables/functions into Scripts.
        for (const ComponentScript &script: level.scripts.components) {
            // if (!script.enabled) continue; The script should load even if its disabled

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
                spdlog::error("Lua script does not exist: {}", path.string());
                continue;
            }

            ScriptInstance instance;
            instance.ownerID = script.ownerID;
            instance.scriptFile = cleanFileName;

            instance.environment = sol::environment(
                lua,
                sol::create,
                lua.globals()
            );

            ScriptEntity ownerEntity{&level, script.ownerID};

            instance.environment["Owner"] = ownerEntity;

            // Every script sees the exact same shared table.
            instance.environment["Public"] = scripts;

            sol::load_result loadedScript = lua.load_file(path.string());

            if (!loadedScript.valid()) {
                sol::error error = loadedScript;
                spdlog::error(
                    "Failed to load Lua script '{}': {}",
                    path.string(),
                    error.what()
                );
                continue;
            }

            sol::protected_function scriptFunction = loadedScript;

            sol::set_environment(instance.environment, scriptFunction);

            sol::protected_function_result result = scriptFunction();

            if (!result.valid()) {
                sol::error error = result;
                spdlog::error(
                    "Failed to run Lua script '{}': {}",
                    path.string(),
                    error.what()
                );
                continue;
            }

            instance.startFunction = instance.environment["Start"];
            instance.updateFunction = instance.environment["Update"];

            scriptInstances.push_back(std::move(instance));

            //spdlog::info("Loaded Lua script: {}", path.string());
        }

        // Call Start only after every script has had a chance to register into Scripts.
        for (ScriptInstance &instance: scriptInstances) {
            if (!instance.startFunction.valid()) continue;

            sol::protected_function_result startResult = instance.startFunction();

            if (!startResult.valid()) {
                sol::error error = startResult;
                spdlog::error(
                    "Lua Start error in '{}': {}",
                    instance.scriptFile,
                    error.what()
                );
            }
        }
    }

    void Update(Level &level) {
        for (ScriptInstance &instance: scriptInstances) {
            ComponentScript *script = level.scripts.Get(instance.ownerID);

            if (script == nullptr || !script->enabled || !instance.updateFunction.valid()) continue;

            sol::protected_function_result result = instance.updateFunction(GameTime::deltaTime);

            if (!result.valid()) {
                sol::error error = result;
                spdlog::error("Lua update error in {}: {}", instance.scriptFile, error.what());
            }
        }
    }

    void Shutdown() {
        scriptInstances.clear();
        lua = sol::state{};

        spdlog::info("Lua scripting shutdown");
    }
}
