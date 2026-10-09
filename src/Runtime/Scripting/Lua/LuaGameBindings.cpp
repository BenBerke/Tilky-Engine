
// Created by berke on 6/22/2026.
//

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
                    Param("ignoredEntityID", "integer"), Param("requireCollider", "boolean"),
                },
                "table",
                "Returns nil on miss, else a table with type/typeID/position/distance/"
                "entityID/wallID/sectorID and (whichever applies) entity/wall/sector."
            ),
            Method("CreateEntity", {Param("isUIEntity", "boolean?")}, "Entity",
                   "Adds a new empty Entity named \"Entity\" to the level and returns it. It starts with a Transform "
                   "at the origin, or a UITransform when `isUIEntity` is true."),
            Method("FindEntity", {Param("name", "string")}, "Entity?", "First Entity with this exact name, or nil."),
            Method("FindEntities", {Param("name", "string")}, "Entity[]", "Every Entity with this exact name."),
            Method("FindEntitiesWithTag", {Param("tag", "string")}, "Entity[]", "Every Entity that has this tag."),
            Method("GetEntity", {Param("id", "integer")}, "Entity?", "The Entity with this ID (e.g. a Raycast hit's entityID), or nil."),
            Method("GetEntities", {}, "Entity[]", "Every Entity in the level."),
            Method("GetSectorAt", {Param("position", "Vector2|Vector3")}, "Sector?",
                   "The innermost sector containing `position` (x, z; height ignored), or nil if it is outside the map."),

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

    game.set_function("CreateEntity", [](const sol::optional<bool> isUIEntity) -> ScriptEntity {
        Level& level = LevelManager::CurrentLevel();

        return {&level, level.CreateEntity(isUIEntity.value_or(false))};
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

    game.set_function("Raycast",
                      [](sol::this_state state,
                         const Vector3 &pos,
                         const Vector3 &dir,
                         const float length,
                         const ID ignoredID,
                         const bool requireCollider) -> sol::object {
                          Level &level = LevelManager::CurrentLevel();

                          return LuaRayHit::ToLua(state, level, GameFunctions::Raycast(
                              level,
                              pos,
                              dir,
                              length,
                              ignoredID,
                              requireCollider
                          ));
                      }
    );
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