#include "EditorInternal.hpp"

#include <vector>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "Headers/Project/ProjectManager.hpp"

#include "Headers/Map/LevelManager.hpp"

#include "Headers/Objects/Level.hpp"
#include "Headers/Objects/Wall.hpp"
#include "Headers/Objects/Sector.hpp"
#include "config.h"
#include "Headers/Editor/EditorTextureCache.hpp"
#include "Headers/Math/Geometry/Geometry.hpp"
#include "Headers/Objects/Loadables.hpp"

constexpr EntityID INVALID_ENTITY_ID = static_cast<EntityID>(-1);

using json = nlohmann::json;

namespace Editor {
    bool ExportProjectAsGame(const std::filesystem::path& exportFolder) {
        namespace fs = std::filesystem;

        try {
            fs::create_directories(exportFolder);

            const fs::path runtimeExe =
                ProjectManager::GetEngineBasePath() / "Tilky_GameRuntime.exe";

            const fs::path outputExe =
                exportFolder / "MyGame.exe";

            fs::copy_file(
                runtimeExe,
                outputExe,
                fs::copy_options::overwrite_existing
            );

            fs::copy(
                ProjectManager::GetProjectFolder(),
                exportFolder / "Project",
                fs::copy_options::recursive |
                fs::copy_options::overwrite_existing
            );

            fs::copy(
                ProjectManager::GetEngineBasePath() / "EngineAssets",
                exportFolder / "EngineAssets",
                fs::copy_options::recursive |
                fs::copy_options::overwrite_existing
            );

            fs::copy(
                ProjectManager::GetEngineBasePath() / "Shaders",
                exportFolder / "Shaders",
                fs::copy_options::recursive |
                fs::copy_options::overwrite_existing
            );

            fs::copy(
                ProjectManager::GetEngineBasePath() / "Fonts",
                exportFolder / "Fonts",
                fs::copy_options::recursive |
                fs::copy_options::overwrite_existing
            );

            spdlog::info("Exported game to {}", exportFolder.string());
            return true;
        }
        catch (const std::exception& e) {
            spdlog::error("Failed to export game: {}", e.what());
            return false;
        }
    }
}

namespace {
    //region backend

    ALenum ValidateDistanceModel(const int model) {
        switch (model) {
            case AL_INVERSE_DISTANCE:
            case AL_INVERSE_DISTANCE_CLAMPED:
            case AL_LINEAR_DISTANCE:
            case AL_LINEAR_DISTANCE_CLAMPED:
            case AL_EXPONENT_DISTANCE:
            case AL_EXPONENT_DISTANCE_CLAMPED:
            case AL_NONE:
                return static_cast<ALenum>(model);

            default:
                return AL_INVERSE_DISTANCE_CLAMPED;
        }
    }

    std::filesystem::path BuildLevelPath(const std::string& levelName) {
        std::string cleanName = levelName;

        if (cleanName.ends_with(".json")) {
            cleanName = cleanName.substr(0, cleanName.size() - 5);
        }

        if (cleanName.ends_with(".bson")) {
            cleanName = cleanName.substr(0, cleanName.size() - 5);
        }

        return ProjectManager::GetLevelsPath() / (cleanName + ".bson");
    }

    std::string CleanLevelName(const std::string& levelName) {
        if (levelName.ends_with(".json")) {
            return levelName.substr(0, levelName.size() - 5);
        }

        if (levelName.ends_with(".bson")) {
            return levelName.substr(0, levelName.size() - 5);
        }

        return levelName;
    }

    Level &GetOrCreateCurrentLevel() {
        if (!LevelManager::HasCurrentLevel()) {
            LevelManager::loadedLevels.emplace_back();
            LevelManager::currentLevelIndex = 0;
        }

        return LevelManager::CurrentLevel();
    }

    void RebuildNextEntityID(Level &level) {
        EntityID highestID = 0;

        for (const Entity &entity: level.entities) {
            highestID = std::max(highestID, entity.id);
        }

        level.nextEntityID = std::max(level.nextEntityID, highestID + 1);
    }

    //endregion

    //region Saving

    void SaveLevelStats(json& levelData, const Level& level) {
        const ListenerSettings& settings = level.listenerSettings;

        levelData["levelStats"] = {
            {"listenerSettings", {
                {"masterGain", settings.masterGain},
                {"dopplerFactor", settings.dopplerFactor},
                {"speedOfSound", settings.speedOfSound},
                {"distanceModel", static_cast<int>(settings.distanceModel)}
            }},
        };
    }

    void SaveComponents(json &levelData, const Level &level) {
        json componentsJson;

        componentsJson["transforms"] = json::array();
        componentsJson["sprites"] = json::array();
        componentsJson["decals"] = json::array();
        componentsJson["audioSources"] = json::array();
        componentsJson["scripts"] = json::array();
        componentsJson["uiTransforms"] = json::array();
        componentsJson["uiSprites"] = json::array();
        componentsJson["playerControllers"] = json::array();
        componentsJson["cameras"] = json::array();
        componentsJson["colliders"] = json::array();
        componentsJson["rigidbodies"] = json::array();

        for (const ComponentTransform &c: level.transforms.components) {
            componentsJson["transforms"].push_back({
                {"ownerID", c.ownerID},
                {"position", {c.position.x, c.position.y, c.position.z}},
                {"sectorIndex", c.sectorIndex},
                {"floor", c.floor},
                {"scale", {c.scale.x, c.scale.y}}
            });
        }

        for (const ComponentSprite &c: level.sprites.components) {
            componentsJson["sprites"].push_back({
                {"ownerID", c.ownerID},
                {"textureIndex", c.textureIndex}
            });
        }

        for (const ComponentDecal &c: level.decals.components) {
            componentsJson["decals"].push_back({
                {"ownerID", c.ownerID},
                {"wallIndex", c.wallIndex},
                {"verticalPos", c.verticalPos},
                {"horizontalPos", c.horizontalPos},
                {"wallNormalOffset", c.wallNormalOffset},
                {"wallT", c.wallT},
                {"baseHeight", c.baseHeight},
                {"absHeight", c.absHeight}
            });
        }

        for (const ComponentAudioSource &c: level.audioSources.components) {
            componentsJson["audioSources"].push_back({
                {"ownerID", c.ownerID},
                {"soundIndex", c.soundIndex},
                {"pitch", c.pitch},
                {"gain", c.gain},
                {"looping", c.looping},
                {"playOnStart", c.playOnStart},
                {"referenceDistance", c.referenceDistance},
                {"maxDistance", c.maxDistance},
                {"rollOffFactor", c.rollOffFactor},
                {"innerConeAngle", c.innerConeAngle},
                {"outerConeAngle", c.outerConeAngle},
                {"outerGain", c.outerGain}
            });
        }

        for (const ComponentScript &c: level.scripts.components) {
            componentsJson["scripts"].push_back({
                {"ownerID", c.ownerID},
                {"fileName", std::filesystem::path(c.fileName).stem().string()},
                {"enabled", c.enabled},
            });
        }

        for (const ComponentUITransform &c: level.ui_transforms.components) {
            componentsJson["uiTransforms"].push_back({
                {"ownerID", c.ownerID},
                {"anchorMin", {c.anchorMin.x, c.anchorMin.y}},
                {"anchorMax", {c.anchorMax.x, c.anchorMax.y}},
                {"pivot", {c.pivot.x, c.pivot.y}},
                {"position", {c.position.x, c.position.y}},
                {"scale", {c.scale.x, c.scale.y}},
                {"rotation", c.rotation}
            });
        }

        for (const ComponentUISprite &c: level.ui_sprites.components) {
            componentsJson["uiSprites"].push_back({
                {"ownerID", c.ownerID},
                {"textureIndex", c.textureIndex},
            });
        }

        for (const ComponentUIText &c: level.ui_texts.components) {
            componentsJson["uiTexts"].push_back({
                {"ownerID", c.ownerID},
                {"text", c.text},
            });
        }

        for (const ComponentPlayerController &c: level.playerControllers.components) {
            componentsJson["playerControllers"].push_back({
                {"ownerID", c.ownerID},
                {"isActive", c.isActive},
                {"velocity", {c.velocity.x, c.velocity.y, c.velocity.z}},
                {"speed", c.speed},
                {"runningSpeed", c.runningSpeed},
                {"eyeHeight", c.eyeHeight},
                {"stepSize", c.stepSize},
                {"friction", c.friction},
                {"sensitivityX", c.sensitivityX},
                {"sensitivityY", c.sensitivityY},
            });
        }

        for (const ComponentCamera &c : level.cameras.components) {
            componentsJson["cameras"].push_back({
                {"ownerID", c.ownerID},
                {"isActive", c.isActive},
                {"yaw", c.yaw},
                {"pitch", c.pitch},
                {"forward", {c.forward.x, c.forward.y, c.forward.z}},
                {"target", {c.target.x, c.target.y, c.target.z}},
                {"fov", c.fov},
                {"aspectRatio", c.aspectRatio},
                {"nearPlane", c.nearPlane},
                {"farPlane", c.farPlane}
            });
        }

        for (const ComponentCollider &c : level.colliders.components) {
            componentsJson["colliders"].push_back({
                {"ownerID", c.ownerID},
                {"isActive", c.isActive},
                {"isTrigger", c.isTrigger},
                    {"type", c.type},
                {"scale", {c.scale.x, c.scale.y, c.scale.z}}
            });
        }

        for (const ComponentRigidbody &c: level.rigidbodies.components) {
            componentsJson["rigidbodies"].push_back({
                {"ownerID", c.ownerID},
                {"isStatic", c.isStatic},
                {"mass", c.mass}
            });
        }

        levelData["components"] = componentsJson;
    }

    void SaveWalls(json &levelData, const Level &level) {
        levelData["walls"] = json::array();

        for (int i = 0; i < static_cast<int>(level.walls.size()); ++i) {
            const Wall &wall = level.walls[i];

            json wallJson = {
                {"id", i},
                {"start", {wall.start.x, wall.start.y}},
                {"end", {wall.end.x, wall.end.y}},
                {"color", {wall.color.x, wall.color.y, wall.color.z, wall.color.w}},
                {"textureIndex", wall.textureIndex},
                {"frontSector", wall.frontSector},
                {"backSector", wall.backSector},
                {"floor", wall.floor}
            };

            levelData["walls"].push_back(wallJson);
        }
    }

    void SaveSectors(json &levelData, const Level &level) {
        levelData["sectors"] = json::array();

        for (int i = 0; i < static_cast<int>(level.sectors.size()); ++i) {
            const Sector &sector = level.sectors[i];

            json cornerArray = json::array();

            for (const Vector2 &point: sector.vertices) {
                cornerArray.push_back({
                    {"x", point.x},
                    {"y", point.y}
                });
            }

            json ceilingTextureArray = json::array();

            for (int j = 0; j < MAX_FLOOR_COUNT; ++j) {
                ceilingTextureArray.push_back(sector.ceilingTextureIndices[j]);
            }

            json sectorJson = {
                {"id", i},
                {"corners", cornerArray},
                {"ceilingHeight", sector.ceilingHeight},
                {"floorHeight", sector.floorHeight},
                {"ceilingColor", {sector.ceilingColor.x, sector.ceilingColor.y, sector.ceilingColor.z}},
                {"floorColor", {sector.floorColor.x, sector.floorColor.y, sector.floorColor.z}},
                {"floorTextureIndex", sector.floorTextureIndex},
                {"floorCount", sector.floorCount},
                {"ceilingTextureIndices", ceilingTextureArray}
            };

            levelData["sectors"].push_back(sectorJson);
        }
    }

    void SaveEntities(json &levelData, const Level &level) {
        levelData["entities"] = json::array();

        for (const Entity &entity: level.entities) {
            json entityJson;

            entityJson["id"] = entity.id;
            entityJson["name"] = entity.name;

            levelData["entities"].push_back(entityJson);
        }
    }

    void SaveTextures(json& levelData, const Level& level) {
        levelData["textures"] = json::array();

        for (const Texture& texture : level.textures) {
            levelData["textures"].push_back({
                {"fileName", texture.fileName}
            });
        }
    }

    void SaveSounds(json& levelData, const Level& level) {
        levelData["sounds"] = json::array();

        spdlog::info("Saving {} sound(s)", level.sounds.size());

        for (const Sound& sound : level.sounds) {
            spdlog::info("Saving sound: {}", sound.fileName);

            if (sound.fileName.empty()) {
                continue;
            }

            levelData["sounds"].push_back({
                {"fileName", sound.fileName}
            });
        }
    }
    //endregion

    //region Loading

    void LoadLevelStats(const json& levelData, Level& level) {
        if (!levelData.contains("levelStats") ||
            !levelData["levelStats"].is_object()) {
            return;
            }

        const json& levelStatsJson = levelData["levelStats"];

        if (!levelStatsJson.contains("listenerSettings") ||
            !levelStatsJson["listenerSettings"].is_object()) {
            return;
            }

        const json& listenerJson = levelStatsJson["listenerSettings"];

        ListenerSettings& settings = level.listenerSettings;

        settings.masterGain = listenerJson.value("masterGain", 1.0f);
        settings.dopplerFactor = listenerJson.value("dopplerFactor", 1.0f);
        settings.speedOfSound = listenerJson.value("speedOfSound", 343.3f);

        const int distanceModel =
            listenerJson.value(
                "distanceModel",
                static_cast<int>(AL_INVERSE_DISTANCE_CLAMPED)
            );

        settings.distanceModel = ValidateDistanceModel(distanceModel);

        settings.masterGain = std::max(0.0f, settings.masterGain);
        settings.dopplerFactor = std::max(0.0f, settings.dopplerFactor);
        settings.speedOfSound = std::max(1.0f, settings.speedOfSound);
    }

    void LoadComponents(const json &levelData, Level &level) {
        if (!levelData.contains("components")) {
            return;
        }

        const json &componentsJson = levelData["components"];

        level.transforms.Clear();
        level.sprites.Clear();
        level.decals.Clear();
        level.audioSources.Clear();
        level.playerControllers.Clear();
        level.cameras.Clear();
        level.colliders.Clear();
        level.rigidbodies.Clear();

        level.ui_transforms.Clear();
        level.ui_sprites.Clear();

        if (componentsJson.contains("transforms")) [[likely]] {
            for (const json &transformJson: componentsJson["transforms"]) {
                const EntityID ownerID =
                        transformJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) [[unlikely]] continue;


                ComponentTransform &c = level.transforms.Add(ownerID);
                level.GetEntity(ownerID)->componentsMask.set(CMP_TRANSFORM);

                if (transformJson.contains("position")) {
                    c.position = {
                        transformJson["position"][0].get<float>(),
                        transformJson["position"][1].get<float>(),
                          transformJson["position"][2].get<float>()
                    };
                }

                c.sectorIndex = transformJson.value("sectorIndex", -1);
                c.floor = transformJson.value("floor", 0);

                if (transformJson.contains("scale")) {
                    c.scale = {
                        transformJson["scale"][0].get<float>(),
                        transformJson["scale"][1].get<float>()
                    };
                }
            }
        }

        if (componentsJson.contains("sprites")) [[likely]] {
            for (const json &spriteJson: componentsJson["sprites"]) {

                const EntityID ownerID =spriteJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) [[unlikely]] continue;


                ComponentSprite &c = level.sprites.Add(ownerID);
                level.GetEntity(ownerID)->componentsMask.set(CMP_SPRITE);

                c.textureIndex = spriteJson.value("textureIndex", -1);
            }
        }

        if (componentsJson.contains("decals")) [[likely]] {
            for (const json &decalJson: componentsJson["decals"]) {
                const EntityID ownerID = decalJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) [[unlikely]] continue;


                ComponentDecal &c = level.decals.Add(ownerID);
                level.GetEntity(ownerID)->componentsMask.set(CMP_DECAL);

                c.wallIndex = decalJson.value("wallIndex", -1);

                c.verticalPos = decalJson.value("verticalPos", 0.0f);
                c.horizontalPos = decalJson.value("horizontalPos", -1.0f);
                c.wallNormalOffset = decalJson.value("wallNormalOffset", 0.0f);

                c.wallT = decalJson.value("wallT", 0.5f);
                c.baseHeight = decalJson.value("baseHeight", 0.0f);
                c.absHeight = decalJson.value("absHeight", false);
            }
        }

        if (componentsJson.contains("audioSources")) [[likely]] {
            for (const json &audioSourceJson : componentsJson["audioSources"]) {
                const EntityID ownerID = audioSourceJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) [[unlikely]] continue;

                ComponentAudioSource& c = level.audioSources.Add(ownerID);
                level.GetEntity(ownerID)->componentsMask.set(CMP_AUDIO_SOURCE);

                c.soundIndex = audioSourceJson.value("soundIndex", -1);
                c.pitch      = audioSourceJson.value("pitch", 1.0f);
                c.gain       = audioSourceJson.value("gain", 1.0f);
                c.looping    = audioSourceJson.value("looping", false);
                c.playOnStart = audioSourceJson.value("playOnStart", true);

                // Distance Attenuation
                c.referenceDistance = audioSourceJson.value("referenceDistance", 1.0f);
                c.maxDistance       = audioSourceJson.value("maxDistance", 10000.0f);
                c.rollOffFactor     = audioSourceJson.value("rollOffFactor", 1.0f);

                // Sound Cone
                c.innerConeAngle = audioSourceJson.value("innerConeAngle", 360.0f);
                c.outerConeAngle = audioSourceJson.value("outerConeAngle", 360.0f);
                c.outerGain      = audioSourceJson.value("outerGain", 0.0f);

                c.name = "entity_" + std::to_string(ownerID) + "_audio";
            }
        }

        if (componentsJson.contains("scripts")) [[likely]] {
            for (const json &scriptJson : componentsJson["scripts"]) {
                const EntityID ownerID = scriptJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) [[unlikely]] continue;

                ComponentScript& c = level.scripts.Add(ownerID);
                level.GetEntity(ownerID)->componentsMask.set(CMP_SCRIPT);

                const std::string loadedName = scriptJson.value("fileName", "Test");

                c.enabled = scriptJson.value("enabled", true);
                c.fileName = std::filesystem::path(loadedName).stem().string();
            }
        }

        if (componentsJson.contains("uiTransforms")) [[likely]] {
            for (const json &transformJson : componentsJson["uiTransforms"]) {
                const EntityID ownerID = transformJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) [[unlikely]] continue;

                ComponentUITransform& c = level.ui_transforms.Add(ownerID);
                level.GetEntity(ownerID)->componentsMask.set(CMP_UI_TRANSFORM);

                // New Fields
                if (transformJson.contains("anchorMin")) {
                    c.anchorMin = {
                        transformJson["anchorMin"][0].get<float>(),
                        transformJson["anchorMin"][1].get<float>()
                    };
                }

                if (transformJson.contains("anchorMax")) {
                    c.anchorMax = {
                        transformJson["anchorMax"][0].get<float>(),
                        transformJson["anchorMax"][1].get<float>()
                    };
                }

                if (transformJson.contains("pivot")) {
                    c.pivot = {
                        transformJson["pivot"][0].get<float>(),
                        transformJson["pivot"][1].get<float>()
                    };
                }

                // Existing Fields
                if (transformJson.contains("position")) {
                    c.position = {
                        transformJson["position"][0].get<float>(),
                        transformJson["position"][1].get<float>(),
                    };
                }

                if (transformJson.contains("scale")) {
                    c.scale = {
                        transformJson["scale"][0].get<float>(),
                        transformJson["scale"][1].get<float>()
                    };
                }

                if (transformJson.contains("rotation")) {
                    c.rotation = transformJson["rotation"].get<float>();
                }
            }
        }

        if (componentsJson.contains("uiSprites")) [[likely]] {
            for (const json &spriteJson : componentsJson["uiSprites"]) {
                const EntityID ownerID = spriteJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) [[unlikely]] continue;

                ComponentUISprite& c = level.ui_sprites.Add(ownerID);
                level.GetEntity(ownerID)->componentsMask.set(CMP_UI_SPRITE);

                c.textureIndex = spriteJson.value("textureIndex", -1);
            }
        }

        if (componentsJson.contains("uiTexts")) [[likely]] {
            for (const json &textJson : componentsJson["uiTexts"]) {
                const EntityID ownerID = textJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) [[unlikely]] continue;

                ComponentUIText& c = level.ui_texts.Add(ownerID);
                level.GetEntity(ownerID)->componentsMask.set(CMP_UI_TEXT);

                c.text = textJson.value("text", "");
            }
        }

        if (componentsJson.contains("playerControllers")) [[likely]] {
        for (const json &controllerJson : componentsJson["playerControllers"]) {
            const EntityID ownerID = controllerJson.value("ownerID", INVALID_ENTITY_ID);

            if (ownerID == INVALID_ENTITY_ID) [[unlikely]] continue;

            ComponentPlayerController& c = level.playerControllers.Add(ownerID);
            level.GetEntity(ownerID)->componentsMask.set(CMP_PLAYER_CONTROLLER);

            c.isActive = controllerJson.value("isActive", true);
            c.speed           = controllerJson.value("speed", 46.0f);
            c.runningSpeed    = controllerJson.value("runningSpeed", 90.0f);
            c.eyeHeight       = controllerJson.value("eyeHeight", 12.0f);
            c.stepSize        = controllerJson.value("stepSize", 8.0f);
            c.friction     = controllerJson.value("friction", 0.8f);
            c.sensitivityX = controllerJson.value("sensitivityX", 0.5f);
            c.sensitivityY = controllerJson.value("sensitivityY", 0.5f);
            c.noClip       = controllerJson.value("noClip", false);
        }
    }

        if (componentsJson.contains("cameras")) [[likely]] {
            for (const json &cameraJson: componentsJson["cameras"]) {
                const EntityID ownerID = cameraJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) [[unlikely]] continue;

                ComponentCamera &c = level.cameras.Add(ownerID);
                level.GetEntity(ownerID)->componentsMask.set(CMP_CAMERA);

                c.isActive = cameraJson.value("isActive", true);
                c.yaw = cameraJson.value("yaw", 0.0f);
                c.pitch = cameraJson.value("pitch", 0.0f);

                // if (cameraJson.contains("forward") && cameraJson["forward"].is_array() && cameraJson["forward"].size()
                //     == 3) {
                //     c.forward.x = cameraJson["forward"][0];
                //     c.forward.y = cameraJson["forward"][1];
                //     c.forward.z = cameraJson["forward"][2];
                // }
                //
                // if (cameraJson.contains("target") && cameraJson["target"].is_array() && cameraJson["target"].size() ==
                //     3) {
                //     c.target.x = cameraJson["target"][0];
                //     c.target.y = cameraJson["target"][1];
                //     c.target.z = cameraJson["target"][2];
                // }

                c.fov = cameraJson.value("fov", 90.0f);
                c.aspectRatio = cameraJson.value("aspectRatio", 1680.0f / 960.0f);
                c.nearPlane = cameraJson.value("nearPlane", 0.1f);
                c.farPlane = cameraJson.value("farPlane", 10000.0f);

                // Note: Matrices (view, projection) default to Matrix4::Identity() on
                // creation and should be left to calculate automatically via your update systems.
            }
        }

        if (componentsJson.contains("colliders")) [[likely]] {
            for (const json &colliderJson: componentsJson["colliders"]) {
                const EntityID ownerID = colliderJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) [[unlikely]] continue;

                ComponentCollider &c = level.colliders.Add(ownerID);
                level.GetEntity(ownerID)->componentsMask.set(CMP_COLLIDER);

                c.isActive = colliderJson.value("isActive", true);
                c.isTrigger = colliderJson.value("isTrigger", false);
                c.type = colliderJson.value("type", COLLIDERTYPE_SPHERE);
                if (colliderJson.contains("scale")) {
                    c.scale = {
                        colliderJson["scale"][0].get<float>(),
                        colliderJson["scale"][1].get<float>(),
                        colliderJson["scale"][2].get<float>(),
                    };
                }
            }
        }

        if (componentsJson.contains("rigidbodies")) [[likely]] {
            for (const json &rigidBodyJson : componentsJson["rigidbodies"]) {
                const EntityID ownerID = rigidBodyJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) [[unlikely]] continue;

                ComponentRigidbody &c = level.rigidbodies.Add(ownerID);
                level.GetEntity(ownerID)->componentsMask.set(CMP_RIGIDBODY);

                c.isStatic = rigidBodyJson.value("isStatic", true);
                c.mass = rigidBodyJson.value("mass", 1.0f);
            }
        }
    }

    void LoadWalls(const json &levelData, Level &level) {
        using namespace MapEditorInternal;

        if (!levelData.contains("walls")) [[unlikely]] return;


        for (const json &wallJson: levelData["walls"]) {
            Vector2 start = {
                wallJson["start"][0].get<float>(),
                wallJson["start"][1].get<float>()
            };

            Vector2 end = {
                wallJson["end"][0].get<float>(),
                wallJson["end"][1].get<float>()
            };

            Vector4 color = {
                255.0f,
                255.0f,
                255.0f,
                255.0f
            };

            if (wallJson.contains("color")) {
                color = {
                    wallJson["color"][0].get<float>(),
                    wallJson["color"][1].get<float>(),
                    wallJson["color"][2].get<float>(),
                    wallJson["color"][3].get<float>()
                };
            }

            const int frontSector = wallJson.value("frontSector", -1);
            const int backSector = wallJson.value("backSector", -1);
            const int textureIndex = wallJson.value("textureIndex", -1);
            const int floor = wallJson.value("floor", 0);

            Wall wall(
                start,
                end,
                color,
                frontSector,
                backSector,
                textureIndex,
                floor
            );

            level.walls.push_back(wall);

            if (!CornerExistsAt(start)) {
                placedCorners.push_back(start);
            }

            if (!CornerExistsAt(end)) {
                placedCorners.push_back(end);
            }
        }
    }

    void LoadSectors(const json &levelData, Level &level) {
        if (!levelData.contains("sectors")) {
            return;
        }

        for (const json &sectorJson: levelData["sectors"]) {
            Sector sector;

            std::vector<Vector2> corners;

            for (const json &cornerJson: sectorJson["corners"]) {
                corners.emplace_back(
                    cornerJson["x"].get<float>(),
                    cornerJson["y"].get<float>()
                );
            }

            sector.vertices = corners;
            sector.triangles = {};

            sector.ceilingHeight = sectorJson.value("ceilingHeight", 40.0f);
            sector.floorHeight = sectorJson.value("floorHeight", 0.0f);

            sector.ceilingColor = {255.0f, 255.0f, 255.0f};
            sector.floorColor = {255.0f, 255.0f, 255.0f};

            if (sectorJson.contains("ceilingColor")) {
                sector.ceilingColor = {
                    sectorJson["ceilingColor"][0].get<float>(),
                    sectorJson["ceilingColor"][1].get<float>(),
                    sectorJson["ceilingColor"][2].get<float>()
                };
            }

            if (sectorJson.contains("floorColor")) {
                sector.floorColor = {
                    sectorJson["floorColor"][0].get<float>(),
                    sectorJson["floorColor"][1].get<float>(),
                    sectorJson["floorColor"][2].get<float>()
                };
            }

            sector.floorCount = sectorJson.value("floorCount", 1);
            sector.floorCount = std::clamp(sector.floorCount, 1, MAX_FLOOR_COUNT);

            sector.floorTextureIndex = sectorJson.value("floorTextureIndex", -1);

            const int oldCeilingTexture =
                    sectorJson.value("ceilingTextureIndex", -1);

            sector.ceilingTextureIndices.fill(oldCeilingTexture);

            if (sectorJson.contains("ceilingTextureIndices")) {
                const json &ceilingTextureArray =
                        sectorJson["ceilingTextureIndices"];

                for (int i = 0;
                     i < std::min<int>(
                         static_cast<int>(ceilingTextureArray.size()),
                         MAX_FLOOR_COUNT
                     );
                     ++i) {
                    sector.ceilingTextureIndices[i] =
                            ceilingTextureArray[i].get<int>();
                }
            }

            sector.triangles = Geometry::Triangulate(sector.vertices);

            level.sectors.push_back(sector);
        }
    }

    void LoadEntities(const json &levelData, Level &level) {
        if (!levelData.contains("entities")) {
            return;
        }

        level.entities.clear();

        EntityID highestEntityID = 0;

        for (const json &entityJson: levelData["entities"]) {
            Entity entity;

            entity.id = entityJson.at("id").get<EntityID>();
            entity.name = entityJson.value("name", "Entity");
            entity.attachedLevelId = level.id;

            highestEntityID = std::max(highestEntityID, entity.id);

            level.entities.push_back(entity);
        }

        level.nextEntityID = highestEntityID + 1;
    }

    void LoadTextures(const json& levelData, Level& level) {
        level.textures.clear();

        if (!levelData.contains("textures")) {
            return;
        }

        for (const json& textureJson : levelData["textures"]) {
            Texture texture;

            if (textureJson.is_object()) {
                texture.fileName = textureJson.value("fileName", "");
            }

            if (!texture.fileName.empty()) {
                level.textures.push_back(texture);
            }
        }
    }

    void LoadSounds(const json& levelData, Level& level) {
        level.sounds.clear();

        if (!levelData.contains("sounds")) {
            return;
        }

        for (const json& soundJson : levelData["sounds"]) {
            Sound sound;

            if (soundJson.is_object()) {
                sound.fileName = soundJson.value("fileName", "");
            }
            else if (soundJson.is_string()) {
                sound.fileName = std::filesystem::path(
                    soundJson.get<std::string>()
                ).stem().string();
            }

            if (!sound.fileName.empty()) {
                level.sounds.push_back(sound);
            }
        }

        spdlog::info("Loaded {} sound(s) into level", level.sounds.size());
    }

    //endregion
}

namespace MapEditorInternal {
    void UpdateLevels() {
        const std::filesystem::path levelsPath = ProjectManager::GetLevelsPath();

        try {
            Editor::maps.clear();

            if (!std::filesystem::exists(levelsPath) ||
                !std::filesystem::is_directory(levelsPath)) {
                std::filesystem::create_directories(levelsPath);
                return;
            }

            for (const auto &entry: std::filesystem::directory_iterator(levelsPath)) {
                if (!std::filesystem::is_regular_file(entry)) {
                    continue;
                }

                if (entry.path().extension() != ".bson") {
                    continue;
                }

                Editor::maps.push_back(entry.path().stem().string());
            }
        } catch (const std::filesystem::filesystem_error &e) {
            spdlog::critical("Error loading levels {}", e.what());
        }
    }

    bool Save(const std::string &saveTo) {
        Level &level = GetOrCreateCurrentLevel();

        EditorTextureCache::RefreshLevelTexturesFromFolder();;
        Editor::RefreshLevelSoundsFromFolder();

        const std::string cleanName = CleanLevelName(saveTo);

        if (cleanName.empty()) {
            spdlog::warn("Can not save with an empty name");
            return false;
        }

        level.name = cleanName;

        json levelData;

        levelData["id"] = level.id;
        levelData["name"] = level.name;
        levelData["nextEntityID"] = level.nextEntityID;

        levelData["levelVars"] = {
            {"backgroundTextureIndex", Editor::backgroundTextureIndex}
        };

        SaveLevelStats(levelData, level);
        SaveEntities(levelData, level);
        SaveComponents(levelData, level);
        SaveWalls(levelData, level);
        SaveSectors(levelData, level);
        SaveTextures(levelData, level);
        SaveSounds(levelData, level);

        const std::filesystem::path path = BuildLevelPath(cleanName);

        std::filesystem::create_directories(ProjectManager::GetLevelsPath());

        const std::vector<std::uint8_t> bsonData = json::to_bson(levelData);

        std::ofstream file(path, std::ios::binary);

        if (!file.is_open()) {
            spdlog::critical("Failed to open BSON level for saving: {}", path.string());
            return false;
        }

        file.write(
            reinterpret_cast<const char*>(bsonData.data()),
            static_cast<std::streamsize>(bsonData.size())
        );

        file.close();

        spdlog::info("Level saved successfully {}", path.string());

        UpdateLevels();

        return true;
    }
}

namespace Editor {
    bool LoadLevel(const std::string &levelName) {
        using namespace MapEditorInternal;

        const std::string cleanName = CleanLevelName(levelName);
        const std::filesystem::path path = BuildLevelPath(cleanName);

        std::ifstream file(path, std::ios::binary);

        if (!file.is_open()) {
            spdlog::critical("Could not open BSON level file {}", path.string());
            return false;
        }

        std::vector<std::uint8_t> bsonData{
                std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()
        };

        file.close();

        json levelData;

        try {
            levelData = json::from_bson(bsonData);
        }
        catch (const std::exception& e) {
            spdlog::critical("Failed to parse level BSON: {}", e.what());
            return false;
        }

        Level loadedLevel;

        loadedLevel.id = levelData.value("id", 0);
        loadedLevel.name = levelData.value("name", cleanName);
        loadedLevel.nextEntityID = levelData.value("nextEntityID", 1);

        if (levelData.contains("levelVars") &&
            levelData["levelVars"].is_object()) {
            backgroundTextureIndex = levelData["levelVars"].value("backgroundTextureIndex", -1);
        }

        currentMap = cleanName;

        placedCorners.clear();
        sectorBeingCreated.clear();

        editingSector = false;
        selectedSector = -1;
        creatableSector = false;

        editingWall = false;
        selectedWall = -1;

        actions.clear();

        try {

            LoadEntities(levelData, loadedLevel);
            spdlog::info("Entity loaded");

            LoadComponents(levelData, loadedLevel);
            spdlog::info("Components loaded");

            LoadWalls(levelData, loadedLevel);
            spdlog::info("Walls loaded");

            LoadSectors(levelData, loadedLevel);
            spdlog::info("Sectors loaded");

            LoadTextures(levelData, loadedLevel);
            spdlog::info("Textures loaded");

            LoadSounds(levelData, loadedLevel);
            spdlog::info("Sounds loaded");

            LoadLevelStats(levelData, loadedLevel);
            spdlog::info("Level stats loaded");
        }
        catch (const nlohmann::json::exception& e) {
            spdlog::critical("Level JSON schema error while loading '{}': {}", path.string(), e.what());
            return false;
        }
        catch (const std::exception& e) {
            spdlog::critical("Unexpected error while loading level '{}': {}", path.string(), e.what());
            return false;
        }

        RebuildNextEntityID(loadedLevel);

        if (LevelManager::loadedLevels.empty()) {
            LevelManager::loadedLevels.push_back(std::move(loadedLevel));
            LevelManager::currentLevelIndex = 0;
        }
        else if (!LevelManager::HasCurrentLevel()) {
            LevelManager::loadedLevels.push_back(std::move(loadedLevel));
            LevelManager::currentLevelIndex =
                static_cast<int>(LevelManager::loadedLevels.size()) - 1;
        }
        else {
            LevelManager::loadedLevels[LevelManager::currentLevelIndex] =
                std::move(loadedLevel);
        }

        Level& activeLevel = LevelManager::CurrentLevel();

        EditorTextureCache::RefreshLevelTexturesFromFolder();
        RefreshLevelSoundsFromFolder();

        spdlog::info("Level loaded successfully {}", path.string());

        return true;
    }
}
