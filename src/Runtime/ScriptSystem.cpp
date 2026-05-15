//
// Created by berke on 5/15/2026.
//

#include "Headers/Objects/EntityTypes.hpp"

#include <sol/sol.hpp>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <string>
#include <vector>

#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Objects/Wrappers.hpp"
#include "Headers/Objects/Components.hpp"

namespace {
    struct ScriptInstance {
        EntityID ownerID = static_cast<EntityID>(-1);
        std::string scriptFile;

        sol::environment environment;
        sol::protected_function startFunction;
        sol::protected_function updateFunction;
    };

    sol::state lua;
    std::vector<ScriptInstance> scriptInstances;

    void RegisterBindings() {
        lua.new_usertype<Vector2>(
            "Vector2",
            sol::constructors<Vector2(), Vector2(float, float)>(),
            "x", &Vector2::x,
            "y", &Vector2::y
        );

        lua.new_usertype<ScriptTransform>(
            "Transform",
            "x", sol::property(&ScriptTransform::GetX, &ScriptTransform::SetX),
            "y", sol::property(&ScriptTransform::GetY, &ScriptTransform::SetY),
            "position", sol::property(&ScriptTransform::GetPosition, &ScriptTransform::SetPosition)
        );

        lua.new_usertype<ScriptEntity>(
            "Entity",

            "GetID", &ScriptEntity::GetID,
            "GetTransform", &ScriptEntity::GetTransform,
            "HasTransform", &ScriptEntity::HasTransform
        );

        lua.set_function("Log", [](const std::string& message) {
            spdlog::info("[Lua] {}", message);
        });
    }

    std::filesystem::path GetScriptPath(const std::string& scriptFile) {
        return ProjectManager::GetProjectFolder() / "Assets" / "Scripts" / scriptFile;
    }

    namespace fs = std::filesystem;

    namespace {
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
    }
}

namespace ScriptSystem {
    bool Initialize() {
        try {
            lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string);
            RegisterBindings();

            spdlog::info("Lua scripting initialized");
            return true;
        }
        catch (std::exception& e) {
            spdlog::critical("Failed to initialize Lua scripting {}", e.what());
            return false;
        }
    }

    void Start(Level& level) {
        scriptInstances.clear();

        for (const ComponentScript& script : level.scripts.components) {
            if (!script.enabled) {
                continue;
            }

            const std::string cleanFileName = CleanScriptFileName(script.fileName);

            if (cleanFileName.empty()) {
                spdlog::warn("Skipping script component with empty file name on entity {}", script.ownerID);
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
            instance.environment = sol::environment(lua, sol::create, lua.globals());

            instance.environment["owner"] = ScriptEntity {
                &level,
                script.ownerID
            };

            sol::load_result loadedScript = lua.load_file(path.string());

            if (!loadedScript.valid()) {
                sol::error error = loadedScript;
                spdlog::error("Failed to load Lua script '{}': {}", path.string(), error.what());
                continue;
            }

            sol::protected_function scriptFunction = loadedScript;
            sol::protected_function_result result = scriptFunction(instance.environment);

            if (!result.valid()) {
                sol::error error = result;
                spdlog::error("Failed to run Lua script '{}': {}", path.string(), error.what());
                continue;
            }

            instance.startFunction = instance.environment["Start"];
            instance.updateFunction = instance.environment["Update"];

            if (instance.startFunction.valid()) {
                sol::protected_function_result startResult = instance.startFunction();

                if (!startResult.valid()) {
                    sol::error error = startResult;
                    spdlog::error("Lua Start error in '{}': {}", path.string(), error.what());
                }
            }

            scriptInstances.push_back(std::move(instance));

            spdlog::info("Started Lua script: {}", path.string());
        }
    }

    void Update(Level& level, const float deltaTime) {
        for (ScriptInstance& instance : scriptInstances) {
            ComponentScript* script = level.scripts.Get(instance.ownerID);

            if (script == nullptr || !script->enabled || !instance.updateFunction.valid()) continue;

            sol::protected_function_result result = instance.updateFunction(script);

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
