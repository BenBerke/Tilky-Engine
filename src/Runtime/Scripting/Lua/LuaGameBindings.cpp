
// Created by berke on 6/22/2026.
//

#include "Headers/Objects/Prefab.hpp"
#include "Headers/Runtime/EventSystem.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaScriptRuntime.hpp"
#include "Headers/Map/MapQueries.hpp"
#include "Headers/Objects/LuaWrappers.hpp"
#include "Headers/Runtime/Gameplay/GameFunctions.hpp"
#include <sol/state.hpp>

#include "Headers/Map/LevelManager.hpp"
#include "Headers/Map/LevelSerialization.hpp"
#include "Headers/Runtime/LevelSystem.hpp"
#include "../../../../Headers/Runtime/Scripting/Lua/LuaScripting.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaBindingMetadata.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaRayHit.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

namespace {
    using namespace LuaBindingMetadata;

    struct RayTypeInfo {
        const char* name;
        RayType type;
    };

    constexpr RayTypeInfo RAY_TYPES[] = {
        {"General", RayType::GENERAL},
        {"Interact", RayType::INTERACT},
        {"LineOfSight", RayType::LINE_OF_SIGHT},
        {"Bullet", RayType::BULLET},
        {"Custom", RayType::CUSTOM},
    };

    void RegisterGameMetadata() {
        RegisterType(GlobalTable("Game", "Global game/level table.", {
            Prop("levelName", "string", true, "The current level's name, as passed to LoadLevel."),
        }, {
            Method("LoadLevel", {Param("levelName", "string")}, {},
                   "Switches to the level `levelName` at the end of this frame. The level file can be in any "
                   "folder under Assets; levelName is its file name without .bson. "
                   "Raises an error if there is no such level, or more than one."),
            Method(
                "Raycast",
                {
                    Param("origin", "Vector3"), Param("direction", "Vector3"), Param("length", "number"),
                    Param("ignoredEntityID", "integer"), Param("requireCollider", "boolean"), Param("rayType", "RayType?"),
                },
                "table",
                "Returns nil on miss, else a table with type/typeID/position/distance/"
                "entityID/wallID/sectorID and (whichever applies) entity/wall/sector. "
                "A rayType other than RayType.General (the default) fires the Receive Ray events of what it hits and of "
                "every linedef it passes, with the ignored entity as the instigator."
            ),
            Method("Signal", {Param("name", "string"), Param("instigator", "Entity?")}, {},
                   "Fires every Lua Signal event named `name`, on any wall, sector, entity, linedef or the level. "
                   "`instigator` is what their tag filters check and what their dispatches get."),
            Method("SetMessage", {Param("text", "string"), Param("color", "Vector3?"), Param("duration", "number?")}, {},
                   "Shows `text` in the level's message box (World Settings > Message Box), like a Show Message "
                   "dispatch. `color` is 0..255 per channel, white by default; `duration` is in seconds, 3 by default."),
            Method("LineOfSight", {Param("from", "Vector3"), Param("to", "Vector3")}, "boolean",
                   "true when nothing solid is between `from` and `to`. Walls, floors and ceilings block it; "
                   "entities don't. Much cheaper than Raycast for sight checks."),
            Method("CreateEntity", {Param("isUIEntityOrEntity", "boolean|Entity?")}, "Entity?",
                   "Adds a new Entity to the level and returns it. With nothing or a boolean it is empty, named \"Entity\", "
                   "with a Transform at the origin (a UITransform when true). Given an Entity it is a copy of it, scripts "
                   "and all - pass an Entity field holding a prefab to spawn that prefab."),
            Method("FindEntity", {Param("name", "string")}, "Entity?", "First Entity with this exact name, or nil."),
            Method("FindEntities", {Param("name", "string")}, "Entity[]", "Every Entity with this exact name."),
            Method("FindEntitiesWithTag", {Param("tag", "string")}, "Entity[]", "Every Entity that has this tag."),
            Method("GetEntity", {Param("id", "integer")}, "Entity?", "The Entity with this ID (e.g. a Raycast hit's entityID), or nil."),
            Method("GetEntities", {}, "Entity[]", "Every Entity in the level."),
            Method("GetSectorAt", {Param("position", "Vector2|Vector3")}, "Sector?",
                   "The innermost sector containing `position` (x, z; height ignored), or nil if it is outside the map."),
            Method("GetVariable", {Param("name", "string")}, "number",
                   "The value of an event variable: one of this level's (World Settings) or a global one (Project Settings). "
                   "Raises an error if there is no variable with that name."),
            Method("SetVariable", {Param("name", "string"), Param("value", "number")}, {},
                   "Sets an event variable, as a Change Variable dispatch would. Level variables reset when a level loads; "
                   "global ones keep their value until play stops. Raises an error if there is no variable with that name."),

            Method("SetSkyMode", {Param("mode", "SkyMode")}, {}, "How the sky image wraps around the camera, e.g. SkyMode.Cylinder."),
            Method("GetSkyMode", {}, "SkyMode"),
            Method("SetSkyTexture", {Param("path", "string")}, {},
                   "The sky image for Equirectangular and Cylinder, relative to Assets. \"\" clears it."),
            Method("GetSkyTexture", {}, "string"),
            Method("SetSkyCubemapFace", {Param("face", "SkyFace"), Param("path", "string")}, {},
                   "One Cubemap face image, relative to Assets, e.g. SkyFace.Up. \"\" clears it."),
            Method("GetSkyCubemapFace", {Param("face", "SkyFace")}, "string"),
            Method("SetSkyRotation", {Param("degrees", "number")}, {}, "Turns the sky, in the same direction as camera yaw."),
            Method("GetSkyRotation", {}, "number"),
            Method("SetSkyRotationSpeed", {Param("degreesPerSecond", "number")}, {}, "Keeps turning the sky while the game runs."),
            Method("GetSkyRotationSpeed", {}, "number"),
            Method("SetSkyTint", {Param("color", "Vector3")}, {}, "Multiplies the sky image. 0..255 per channel."),
            Method("GetSkyTint", {}, "Vector3"),
            Method("SetSkyHorizonOffset", {Param("degrees", "number")}, {}, "Moves the horizon up (positive) or down."),
            Method("GetSkyHorizonOffset", {}, "number"),
            Method("SetSkyFallbackColor", {Param("color", "Vector3")}, {},
                   "Shown when the sky has no usable image. 0..255 per channel."),
            Method("GetSkyFallbackColor", {}, "Vector3"),
        }));

        std::vector<EnumValueDoc> modes;
        for (const SkyModeInfo& mode : SKY_MODES) modes.push_back({mode.name, static_cast<int>(mode.mode), {}});
        RegisterType(Enum("SkyMode", "Sky projection modes for Game.SetSkyMode.", std::move(modes)));

        std::vector<EnumValueDoc> rayTypes;
        for (const RayTypeInfo& rayType : RAY_TYPES) rayTypes.push_back({rayType.name, static_cast<int>(rayType.type), {}});
        RegisterType(Enum("RayType",
                          "What a ray is for, for Game.Raycast and Camera:Raycast. Any but General fires Receive Ray events.",
                          std::move(rayTypes)));

        std::vector<EnumValueDoc> faces;
        for (const SkyFaceInfo& face : SKY_FACES) faces.push_back({face.name, static_cast<int>(face.face), {}});
        RegisterType(Enum("SkyFace", "Cubemap faces for Game.SetSkyCubemapFace. Front is +Z, Right is -X, Up is +Y.",
                          std::move(faces)));
    }

    SkyMode ToSkyMode(const int value) {
        const std::optional<SkyMode> mode = SkyModeFromInt(value);
        if (!mode) throw sol::error("Game.SetSkyMode expects a SkyMode value, e.g. SkyMode.Cylinder");
        return *mode;
    }

    SkyFace ToSkyFace(const int value, const char *function) {
        if (!IsValidSkyFace(value)) throw sol::error(std::string(function) + " expects a SkyFace value, e.g. SkyFace.Up");
        return static_cast<SkyFace>(value);
    }

    SkySettings &CurrentSky() {
        return LevelManager::CurrentLevel().sky;
    }
}

static const char *RayHitTypeToString(const RayHitType type) {
    switch (type) {
        case RayHitType::Entity: return "Entity";
        case RayHitType::Wall: return "Wall";
        case RayHitType::SectorFloor: return "SectorFloor";
        case RayHitType::SectorCeiling: return "SectorCeiling";
        default: return "None";
    }
}

void LuaScriptSystem::RegisterGameBindings(sol::state &lua) {
    RegisterGameMetadata();

    const sol::object existing = lua["Game"];
    sol::table game;

    if (existing.get_type() == sol::type::table) game = existing.as<sol::table>();
    else {
        if (existing.get_type() != sol::type::nil)
            spdlog::warn("Replacing Lua global 'Game' because it is not a table");

        game = lua.create_named_table("Game");
    }

    // Only queues the switch; RuntimeSession does it once this frame is over.
    // The file is checked here so a wrong name is an error at the call.
    game.set_function("LoadLevel", [](const sol::object& value) {
        if (value.get_type() != sol::type::string)
            throw sol::error("Game.LoadLevel expects a level name (string)");

        const std::string levelName = LevelSerialization::CleanLevelName(value.as<std::string>());

        if (levelName.empty()) throw sol::error("Game.LoadLevel expects a level name, got an empty string");

        std::string errorMessage;
        if (LevelSerialization::FindLevelPath(levelName, &errorMessage).empty())
            throw sol::error("Game.LoadLevel: " + errorMessage);

        LevelSystem::RequestLevelLoad(levelName);
    });

    // Game is a plain table, so levelName is served from its metatable: it
    // always reads the current level, and assigning to it is an error.
    sol::table gameMeta = lua.create_table();

    gameMeta[sol::meta_function::index] = [](const sol::table&, const sol::object& key, const sol::this_state state) -> sol::object {
        if (key.is<std::string>() && key.as<std::string>() == "levelName")
            return sol::make_object(state, LevelManager::CurrentLevel().name);

        return sol::make_object(state, sol::nil);
    };

    gameMeta[sol::meta_function::new_index] = [](sol::table self, const sol::object& key, const sol::object& value) {
        if (key.is<std::string>() && key.as<std::string>() == "levelName")
            throw sol::error("Game.levelName is read-only");

        self.raw_set(key, value);
    };

    game[sol::metatable_key] = gameMeta;

    // Game.CreateEntity(), Game.CreateEntity(isUIEntity), or
    // Game.CreateEntity(entity): a copy of an entity - a prefab instance
    // when `entity` came from an Entity field holding a prefab.
    game.set_function("CreateEntity", [](sol::this_state state, const sol::object argument) -> sol::object {
        Level& level = LevelManager::CurrentLevel();

        if (argument.is<ScriptEntity>()) {
            const ScriptEntity source = argument.as<ScriptEntity>();
            Entity* original = source.GetEntity();
            if (original == nullptr) return sol::make_object(state, sol::nil);

            ID newID = INVALID_ID;
            if (const std::string prefab = Prefab::ScriptCopyReference(source.level); !prefab.empty())
                newID = Prefab::Instantiate(level, *source.level, prefab);
            else if (source.level == &level) {
                Entity copy = *original; // CreateEntity can grow the entity list
                newID = level.CreateEntity(copy);
            }

            if (newID == INVALID_ID) return sol::make_object(state, sol::nil);

            LuaScriptRuntime::QueueEntityScriptsStart(newID);
            return sol::make_object(state, ScriptEntity{&level, newID});
        }

        const bool isUIEntity = argument.is<bool>() && argument.as<bool>();
        return sol::make_object(state, ScriptEntity{&level, level.CreateEntity(isUIEntity)});
    });

    game.set_function("FindEntity", [](sol::this_state state, const std::string& name) -> sol::object {
        Level& level = LevelManager::CurrentLevel();

        for (const Entity& candidate : level.entities)
            if (candidate.name == name) return sol::make_object(state, ScriptEntity{&level, candidate.id});

        return sol::make_object(state, sol::nil);
    });

    game.set_function("FindEntities", [](const std::string& name) -> sol::as_table_t<std::vector<ScriptEntity>> {
        Level& level = LevelManager::CurrentLevel();
        std::vector<ScriptEntity> result;

        for (const Entity& candidate : level.entities)
            if (candidate.name == name) result.push_back({&level, candidate.id});

        return sol::as_table(std::move(result));
    });

    game.set_function("FindEntitiesWithTag", [](const std::string& tag) -> sol::as_table_t<std::vector<ScriptEntity>> {
        Level& level = LevelManager::CurrentLevel();
        std::vector<ScriptEntity> result;

        for (const Entity& candidate : level.entities) {
            const ScriptEntity entity{&level, candidate.id};
            if (entity.HasTag(tag)) result.push_back(entity);
        }

        return sol::as_table(std::move(result));
    });

    // Event variables, level or global, found by name. Never creates one.
    const auto variableValue = [](const std::string& name, const char* function) -> float& {
        Level& level = LevelManager::CurrentLevel();
        const std::optional<EventVariableID> id = level.FindEventVariable(name);
        float* value = id ? level.GetEventVariableValue(*id) : nullptr;

        if (!value) throw sol::error(std::string(function) + ": no event variable named \"" + name + "\"");
        return *value;
    };

    game.set_function("GetVariable", [variableValue](const std::string& name) {
        return variableValue(name, "Game.GetVariable");
    });

    game.set_function("SetVariable", [variableValue](const std::string& name, const float value) {
        variableValue(name, "Game.SetVariable") = value;
    });

    game.set_function("GetEntity", [](sol::this_state state, const ID id) -> sol::object {
        Level& level = LevelManager::CurrentLevel();

        if (level.GetEntity(id) == nullptr) return sol::make_object(state, sol::nil);

        return sol::make_object(state, ScriptEntity{&level, id});
    });

    game.set_function("GetEntities", []() -> sol::as_table_t<std::vector<ScriptEntity>> {
        Level& level = LevelManager::CurrentLevel();
        std::vector<ScriptEntity> result;
        result.reserve(level.entities.size());

        for (const Entity& candidate : level.entities) result.push_back({&level, candidate.id});

        return sol::as_table(std::move(result));
    });

    // Same lookup that decides which sector an entity is in.
    const auto sectorAt = [](const sol::this_state state, const Vector2 point) -> sol::object {
        Level& level = LevelManager::CurrentLevel();

        const int index = MapQueries::FindSectorContainingPoint(level.sectors, point);
        if (index < 0) return sol::make_object(state, sol::nil);

        return sol::make_object(state, ScriptSector{&level, level.sectors[index].id});
    };

    game.set_function("GetSectorAt", sol::overload(
        [sectorAt](const sol::this_state state, const Vector2& position) { return sectorAt(state, position); },
        [sectorAt](const sol::this_state state, const Vector3& position) {
            return sectorAt(state, {position.x, position.z});
        }
    ));

    // ---- Sky (Level::sky) ----
    sol::table skyMode = lua.create_named_table("SkyMode");
    for (const SkyModeInfo& mode : SKY_MODES) skyMode[mode.name] = static_cast<int>(mode.mode);

    sol::table skyFace = lua.create_named_table("SkyFace");
    for (const SkyFaceInfo& face : SKY_FACES) skyFace[face.name] = static_cast<int>(face.face);

    game.set_function("SetSkyMode", [](const int mode) { CurrentSky().mode = ToSkyMode(mode); });
    game.set_function("GetSkyMode", [] { return static_cast<int>(CurrentSky().mode); });

    game.set_function("SetSkyTexture", [](const std::string& path) { CurrentSky().texture = path; });
    game.set_function("GetSkyTexture", [] { return CurrentSky().texture; });

    game.set_function("SetSkyCubemapFace", [](const int face, const std::string& path) {
        CurrentSky().Face(ToSkyFace(face, "Game.SetSkyCubemapFace")) = path;
    });
    game.set_function("GetSkyCubemapFace", [](const int face) {
        return CurrentSky().Face(ToSkyFace(face, "Game.GetSkyCubemapFace"));
    });

    game.set_function("SetSkyRotation", [](const float degrees) { CurrentSky().rotation = degrees; });
    game.set_function("GetSkyRotation", [] { return CurrentSky().rotation; });

    game.set_function("SetSkyRotationSpeed", [](const float degreesPerSecond) { CurrentSky().rotationSpeed = degreesPerSecond; });
    game.set_function("GetSkyRotationSpeed", [] { return CurrentSky().rotationSpeed; });

    game.set_function("SetSkyTint", [](const Vector3& color) { CurrentSky().tint = color; });
    game.set_function("GetSkyTint", [] { return CurrentSky().tint; });

    game.set_function("SetSkyHorizonOffset", [](const float degrees) { CurrentSky().horizonOffset = degrees; });
    game.set_function("GetSkyHorizonOffset", [] { return CurrentSky().horizonOffset; });

    game.set_function("SetSkyFallbackColor", [](const Vector3& color) { CurrentSky().fallbackColor = color; });
    game.set_function("GetSkyFallbackColor", [] { return CurrentSky().fallbackColor; });

    sol::table rayType = lua.create_named_table("RayType");
    for (const RayTypeInfo& info : RAY_TYPES) rayType[info.name] = static_cast<int>(info.type);

    game.set_function("Raycast",
                      [](sol::this_state state,
                         const Vector3 &pos,
                         const Vector3 &dir,
                         const float length,
                         const ID ignoredID,
                         const bool requireCollider,
                         const sol::optional<int> type) -> sol::object {
                          Level &level = LevelManager::CurrentLevel();

                          return LuaRayHit::ToLua(state, level, GameFunctions::Raycast(
                              level,
                              pos,
                              dir,
                              length,
                              ignoredID,
                              requireCollider,
                              LuaRayHit::ToRayType(type, "Game.Raycast")
                          ));
                      }
    );

    game.set_function("Signal", [](const std::string& name, const sol::optional<ScriptEntity> instigator) {
        Level& level = LevelManager::CurrentLevel();
        EventSystem::Signal(level, name, instigator ? instigator->ownerID : INVALID_ID);
    });

    game.set_function("SetMessage", [](const std::string& text, const sol::optional<Vector3> color,
                                        const sol::optional<float> duration) {
        const float seconds = duration.value_or(3.0f);
        if (!(seconds > 0.0f)) throw sol::error("Game.SetMessage expects a duration above 0, got " + std::to_string(seconds));

        EventSystem::SetMessage(LevelManager::CurrentLevel(), text, color.value_or(Vector3{255.0f, 255.0f, 255.0f}), seconds);
    });

    game.set_function("LineOfSight", [](const Vector3& from, const Vector3& to) {
        return GameFunctions::LineOfSight(LevelManager::CurrentLevel(), from, to);
    });
}

RayType LuaRayHit::ToRayType(const sol::optional<int>& value, const char* function) {
    if (!value) return RayType::GENERAL;

    if (*value < 0 || *value >= static_cast<int>(RayType::COUNT))
        throw sol::error(std::string(function) + " expects a RayType value, e.g. RayType.Interact");

    return static_cast<RayType>(*value);
}

sol::object LuaRayHit::ToLua(sol::state_view lua, Level &level, const std::optional<RayHit> &hit) {
    if (!hit.has_value()) return sol::make_object(lua, sol::nil);

    sol::table result = lua.create_table();

    result["type"] = RayHitTypeToString(hit->type);
    result["typeID"] = static_cast<int>(hit->type);
    result["position"] = hit->position;
    result["distance"] = hit->distance;

    result["entityID"] = hit->entity != nullptr
                             ? hit->entity->id
                             : INVALID_ENTITY_ID;

    result["wallID"] = hit->wall != nullptr
                           ? hit->wall->id
                           : INVALID_ID;

    result["sectorID"] = hit->sector != nullptr
                             ? hit->sector->id
                             : INVALID_ID;

    if (hit->entity != nullptr) {
        result["entity"] = ScriptEntity{
            .level = &level,
            .ownerID = hit->entity->id
        };
    }
    else result["entity"] = sol::nil;

    if (hit->wall != nullptr) {
        result["wall"] = ScriptWall{
            .level = &level,
            .wallID = hit->wall->id
        };
    }
    else result["wall"] = sol::nil;

    if (hit->sector != nullptr) {
        result["sector"] = ScriptSector {
            .level = &level,
            .sectorID = hit->sector->id
        };
    }
    else result["sector"] = sol::nil;

    return sol::make_object(lua, result);
}