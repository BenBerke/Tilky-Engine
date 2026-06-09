//
// Created by berke on 5/15/2026.
//

#include "Headers/Runtime/ScriptSystem.hpp"

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

//todo add more functionality

namespace {
    SDL_Scancode GetScancodeFromString(const std::string& key) {
        static const std::unordered_map<std::string, SDL_Scancode> keys = {
            {"A", SDL_SCANCODE_A},
            {"B", SDL_SCANCODE_B},
            {"C", SDL_SCANCODE_C},
            {"D", SDL_SCANCODE_D},
            {"E", SDL_SCANCODE_E},
            {"F", SDL_SCANCODE_F},
            {"G", SDL_SCANCODE_G},
            {"H", SDL_SCANCODE_H},
            {"I", SDL_SCANCODE_I},
            {"J", SDL_SCANCODE_J},
            {"K", SDL_SCANCODE_K},
            {"L", SDL_SCANCODE_L},
            {"M", SDL_SCANCODE_M},
            {"N", SDL_SCANCODE_N},
            {"O", SDL_SCANCODE_O},
            {"P", SDL_SCANCODE_P},
            {"Q", SDL_SCANCODE_Q},
            {"R", SDL_SCANCODE_R},
            {"S", SDL_SCANCODE_S},
            {"T", SDL_SCANCODE_T},
            {"U", SDL_SCANCODE_U},
            {"V", SDL_SCANCODE_V},
            {"W", SDL_SCANCODE_W},
            {"X", SDL_SCANCODE_X},
            {"Y", SDL_SCANCODE_Y},
            {"Z", SDL_SCANCODE_Z},

            {"Space", SDL_SCANCODE_SPACE},
            {"Escape", SDL_SCANCODE_ESCAPE},
            {"Enter", SDL_SCANCODE_RETURN},
            {"Tab", SDL_SCANCODE_TAB},
            {"Backspace", SDL_SCANCODE_BACKSPACE},

            {"Left", SDL_SCANCODE_LEFT},
            {"Right", SDL_SCANCODE_RIGHT},
            {"Up", SDL_SCANCODE_UP},
            {"Down", SDL_SCANCODE_DOWN},

            {"LShift", SDL_SCANCODE_LSHIFT},
            {"RShift", SDL_SCANCODE_RSHIFT},
            {"LCtrl", SDL_SCANCODE_LCTRL},
            {"RCtrl", SDL_SCANCODE_RCTRL},
            {"LAlt", SDL_SCANCODE_LALT},
            {"RAlt", SDL_SCANCODE_RALT},

            {"1", SDL_SCANCODE_1},
            {"2", SDL_SCANCODE_2},
            {"3", SDL_SCANCODE_3},
            {"4", SDL_SCANCODE_4},
            {"5", SDL_SCANCODE_5},
            {"6", SDL_SCANCODE_6},
            {"7", SDL_SCANCODE_7},
            {"8", SDL_SCANCODE_8},
            {"9", SDL_SCANCODE_9},
            {"0", SDL_SCANCODE_0},
        };

        const auto it = keys.find(key);

        if (it != keys.end()) {
            return it->second;
        }

        return SDL_SCANCODE_UNKNOWN;
    }
}

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

    void RegisterInputManager() {
        sol::table inputManager = lua.create_table();

        inputManager.set_function("GetKeyDown", [](const std::string& key) -> bool {
            const SDL_Scancode scancode = GetScancodeFromString(key);

            if (scancode == SDL_SCANCODE_UNKNOWN) return false;

            return InputManager::GetKeyDown(scancode);
        });

        inputManager.set_function("GetKey", [](const std::string& key) -> bool {
            const SDL_Scancode scancode = GetScancodeFromString(key);

            if (scancode == SDL_SCANCODE_UNKNOWN) return false;

            return InputManager::GetKey(scancode);
        });

        inputManager.set_function("GetKeyUp", [](const std::string& key) -> bool {
            const SDL_Scancode scancode = GetScancodeFromString(key);

            if (scancode == SDL_SCANCODE_UNKNOWN) return false;

            return InputManager::GetKeyUp(scancode);
        });

        inputManager.set_function("GetMouseButtonDown", [](const int button) -> bool {
            return InputManager::GetMouseButtonDown(button);
        });

        inputManager.set_function("GetMouseButton", [](const int button) -> bool {
            return InputManager::GetMouseButton(button);
        });

        inputManager.set_function("GetMouseButtonUp", [](const int button) -> bool {
            return InputManager::GetMouseButtonUp(button);
        });

        inputManager.set_function("GetMousePosition", []() -> sol::table {
            const Vector2 pos = InputManager::GetMousePosition();

            sol::table result = lua.create_table();
            result["x"] = pos.x;
            result["y"] = pos.y;

            return result;
        });

        inputManager["MouseLeft"] = SDL_BUTTON_LEFT;
        inputManager["MouseMiddle"] = SDL_BUTTON_MIDDLE;
        inputManager["MouseRight"] = SDL_BUTTON_RIGHT;

        lua["Input"] = inputManager;
    }

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

        math.set_function("Vector2DistanceSquared", [](const Vector2& a, const Vector2& b) -> float {
            return Vector2Math::DistanceSquared(a, b);
        });

        math.set_function("Vector2Dot", [](const Vector2& a, const Vector2& b) -> float {
           return Vector2Math::Dot(a, b);
        });

        math.set_function("Vector3Dot", [](const Vector3& a, const Vector3& b) -> float {
           return Vector3Math::Dot(a, b);
        });

        math.set_function("Vector3Distance", [](const Vector3& a, const Vector3& b) -> float {
            return Vector3Math::Distance(a, b);
        });

        math.set_function("Vector3DistanceSquared", [](const Vector3& a, const Vector3& b) -> float {
            return Vector3Math::DistanceSquared(a, b);
        });

        math.set_function("Vector3Cross", [](const Vector3& a, const Vector3& b) -> Vector3 {
            return Vector3Math::Cross(a, b);
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
            }),
            "lengthSquared", sol::property([](const Vector2 &self) {
                return Vector2Math::LengthSquared(self);
            }),
            "normalized", sol::property([](const Vector2 &self) {
                return Vector2Math::Normalized(self);
            })
        );

        lua.new_usertype<Vector3>(
            "Vector3",
            sol::constructors<Vector3(), Vector3(float, float, float)>(),
            "x", &Vector3::x,
            "y", &Vector3::y,
             "z", &Vector3::z,
             "length", sol::property([](const Vector3 &self) {
                 return Vector3Math::Length(self);
             }),
             "lengthSquared", sol::property([](const Vector3 &self) {
                 return Vector3Math::LengthSquared(self);
             }),
             "normalized", sol::property([](const Vector3 &self) {
                 return Vector3Math::Normalized(self);
             })
        );

        lua.new_usertype<Vector4>(
            "Vector4",
            sol::constructors<Vector4(), Vector4(float, float, float)>(),
            "x", &Vector4::x,
            "y", &Vector4::y,
             "z", &Vector4::z,
             "w", &Vector4::w
        );

        lua.new_usertype<ScriptTransform>(
            "Transform",

            "position", sol::property(
                &ScriptTransform::GetPosition,
                &ScriptTransform::SetPosition
            ),

            "floor", sol::property(
                &ScriptTransform::GetFloor,
                &ScriptTransform::SetFloor
            ),

            "scale", sol::property(
                &ScriptTransform::GetScale,
                &ScriptTransform::SetScale
                )
        );

        lua.new_usertype<ScriptSprite>(
            "Sprite",

            "textureIndex", sol::property(
                &ScriptSprite::GetTextureIndex,
                &ScriptSprite::SetTextureIndex
            )
        );

        lua.new_usertype<ScriptDecal>(
            "Decal",

            "wallIndex", sol::property(
                &ScriptDecal::GetWallIndex,
                &ScriptDecal::SetWallIndex
            ),

            "verticalPos", sol::property(
                &ScriptDecal::GetVerticalPos,
                &ScriptDecal::SetVerticalPos
            ),

            "horizontalPos", sol::property(
                &ScriptDecal::GetHorizontalPos,
                &ScriptDecal::SetHorizontalPos
            ),

            "wallNormalOffset", sol::property(
                &ScriptDecal::GetWallNormalOffset,
                &ScriptDecal::SetWallNormalOffset
            ),

            "wallT", sol::property(
                &ScriptDecal::GetWallT,
                &ScriptDecal::SetWallT
            ),

            "baseHeight", sol::property(
                &ScriptDecal::GetBaseHeight,
                &ScriptDecal::SetBaseHeight
            ),

            "absHeight", sol::property(
                &ScriptDecal::GetAbsHeight,
                &ScriptDecal::SetAbsHeight
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
            ),

            "hasSprite", sol::property(&ScriptEntity::HasSprite),

            "sprite", sol::property(
                [](const ScriptEntity &entity, const sol::this_state state) -> sol::object {
                    const sol::state_view lua(state);

                    if (entity.level == nullptr) {
                        return sol::nil;
                    }

                    if (entity.level->sprites.Get(entity.ownerID) == nullptr) {
                        return sol::nil;
                    }

                    return sol::make_object(
                        lua,
                        ScriptSprite{
                            entity.level,
                            entity.ownerID
                        }
                    );
                }
            ),

            "hasDecal", sol::property(&ScriptEntity::HasDecal),

            "decal", sol::property(
                [](const ScriptEntity &entity, const sol::this_state state) -> sol::object {
                    const sol::state_view lua(state);

                    if (entity.level == nullptr) {
                        return sol::nil;
                    }

                    if (entity.level->decals.Get(entity.ownerID) == nullptr) {
                        return sol::nil;
                    }

                    return sol::make_object(
                        lua,
                        ScriptDecal{
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
            RegisterInputManager();

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
