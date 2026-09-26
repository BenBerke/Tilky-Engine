
// Created by berke on 6/22/2026.
//

#include "Headers/Objects/LuaWrappers.hpp"
#include "Headers/Runtime/Gameplay/GameFunctions.hpp"
#include <sol/state.hpp>

#include "Headers/Editor/Editor.hpp"
#include "../../../../Headers/Runtime/Scripting/Lua/LuaScripting.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaBindingMetadata.hpp"

#include <optional>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

namespace {
    using namespace LuaBindingMetadata;

    void RegisterGameMetadata() {
        RegisterType(GlobalTable("Game", "Global game/level table.", {}, {
            Method("LoadLevel", {Param("levelName", "string")}),
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
            Method("FindEntity", {Param("name", "string")}, "Entity?", "First Entity with this exact name, or nil."),
            Method("FindEntities", {Param("name", "string")}, "Entity[]", "Every Entity with this exact name."),
            Method("FindEntitiesWithTag", {Param("tag", "string")}, "Entity[]", "Every Entity that has this tag."),
            Method("GetEntity", {Param("id", "integer")}, "Entity?", "The Entity with this ID (e.g. a Raycast hit's entityID), or nil."),
            Method("GetEntities", {}, "Entity[]", "Every Entity in the level."),
        }));
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

    game.set_function("LoadLevel", [](const std::string& levelName)->void {
       Editor::LoadLevel(levelName);
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

    game.set_function("Raycast",
                      [](sol::this_state state,
                         const Vector3 &pos,
                         const Vector3 &dir,
                         const float length,
                         const ID ignoredID,
                         const bool requireCollider) -> sol::object {
                          sol::state_view lua(state);
                          Level &level = LevelManager::CurrentLevel();

                          const std::optional<RayHit> hit = GameFunctions::Raycast(
                              level,
                              pos,
                              dir,
                              length,
                              ignoredID,
                              requireCollider
                          );

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
    );
}