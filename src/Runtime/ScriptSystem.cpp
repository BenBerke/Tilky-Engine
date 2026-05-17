//
// Created by berke on 5/15/2026.
//

#include "Headers/Objects/EntityTypes.hpp"

#include <sol/sol.hpp>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <string>
#include <vector>

#include "Headers/Engine/GameTime.hpp"
#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Objects/Wrappers.hpp"
#include "Headers/Objects/Components.hpp"

#include "Headers/Math/Vector/Vector2Math.hpp"

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

    void RegisterMathLibrary() {
        sol::table tilky;

        if (lua["Tilky"].valid()) {
            tilky = lua["Tilky"];
        }
        else {
            tilky = lua.create_named_table("Tilky");
        }

        sol::table math = lua.create_table();


        math.set_function("DegToRad", [](const float value) -> float {
            return value * (180.0f / 3.14159265358979323846f);
        });

        math.set_function("RadToDeg", [](const float value) -> float {
            return value * (3.14159265358979323846f / 180.0f);
        });

        math.set_function("Clamp", [](const float value, const float minValue, const float maxValue) -> float {
            return std::clamp(value, minValue, maxValue);
        });

        math.set_function("Lerp", [](const float a, const float b, const float t) -> float {
            return (1-t) * a + t * b;
        });

        math.set_function("Vector2Distance", [](const Vector2& a, const Vector2& b) -> float {
            return Vector2Math::Distance(a, b);
        });

        tilky["Math"] = math;
    }

    void RegisterBindings() {
        lua.new_usertype<Vector2>(
            "Vector2",
            sol::constructors<Vector2(), Vector2(float, float)>(),
            "x", &Vector2::x,
            "y", &Vector2::y,

            // Properties
            "length", sol::property([](const Vector2 &self) {
                return Vector2Math::Length(self);
            })
        );

        lua.new_usertype<ScriptTransform>(
            "Transform",

            "position", sol::property(
                &ScriptTransform::GetPosition,
                &ScriptTransform::SetPosition
            ),

            "x", sol::property(
                &ScriptTransform::GetX,
                &ScriptTransform::SetX
            ),

            "y", sol::property(
                &ScriptTransform::GetY,
                &ScriptTransform::SetY
            )
        );

        lua.new_usertype<ScriptEntity>(
            "Entities",

            "id", sol::property(&ScriptEntity::GetID),

            "hasTransform", sol::property(&ScriptEntity::HasTransform),

            "transform", sol::property(
                [](const ScriptEntity& entity, const sol::this_state state) -> sol::object {
                    const sol::state_view lua(state);

                    if (entity.level == nullptr) {
                        return sol::nil;
                    }

                    if (entity.level->transforms.Get(entity.ownerID) == nullptr) {
                        return sol::nil;
                    }

                    return sol::make_object(
                        lua,
                        ScriptTransform {
                            entity.level,
                            entity.ownerID
                        }
                    );
                }
            )
        );
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
            RegisterMathLibrary();

            spdlog::info("Lua scripting initialized");
            return true;
        } catch (std::exception &e) {
            spdlog::critical("Failed to initialize Lua scripting {}", e.what());
            return false;
        }
    }

    void Start(Level &level) {
        scriptInstances.clear();

        for (const ComponentScript &script: level.scripts.components) {
            if (!script.enabled) {
                continue;
            }

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

            instance.environment["owner"] = ScriptEntity{
                &level,
                script.ownerID
            };

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

            // IMPORTANT:
            // This makes the Lua file use instance.environment as its _ENV.
            // Without this, owner is nil inside Start/Update.
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

            if (instance.startFunction.valid()) {
                sol::protected_function_result startResult =
                        instance.startFunction();

                if (!startResult.valid()) {
                    sol::error error = startResult;
                    spdlog::error(
                        "Lua Start error in '{}': {}",
                        path.string(),
                        error.what()
                    );
                }
            }

            scriptInstances.push_back(std::move(instance));

            spdlog::info("Started Lua script: {}", path.string());
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
