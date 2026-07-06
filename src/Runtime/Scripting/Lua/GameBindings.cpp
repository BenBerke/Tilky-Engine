//
// Created by berke on 6/22/2026.
//

#include "Headers/Objects/LuaWrappers.hpp"
#include "Headers/Runtime/Gameplay/GameFunctions.hpp"
#include <sol/state.hpp>

#include "Headers/Editor/Editor.hpp"
#include "Headers/Runtime/Scripting/ScriptSystem.hpp"

static const char *RayHitTypeToString(const RayHitType type) {
    switch (type) {
        case RayHitType::Entity: return "Entity";
        case RayHitType::Wall: return "Wall";
        case RayHitType::SectorFloor: return "SectorFloor";
        case RayHitType::SectorCeiling: return "SectorCeiling";
        default: return "None";
    }
}

void ScriptSystem::RegisterGameBindings(sol::state &lua) {
    sol::table game;

    if (lua["Game"].valid()) game = lua["Game"];
    else game = lua.create_named_table("Game");

    game.set_function("LoadLevel", [](const std::string& levelName)->void {
       Editor::LoadLevel(levelName);
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