//
// Created by berke on 5/2/2026.
//

#include "Headers/Map/LevelManager.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include <nlohmann/json.hpp>

#include "Headers/Objects/Wall.hpp"
#include "Headers/Objects/Sector.hpp"
#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Math/Geometry/Geometry.hpp"
#include "config.h"

constexpr EntityID INVALID_ENTITY_ID = static_cast<EntityID>(-1);

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
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

    std::string CleanLevelName(const std::string& levelName) {
        if (levelName.ends_with(".json")) {
            return levelName.substr(0, levelName.size() - 5);
        }

        if (levelName.ends_with(".bson")) {
            return levelName.substr(0, levelName.size() - 5);
        }

        return levelName;
    }

    fs::path BuildLevelPath(const std::string& levelName) {
        const std::string cleanName = CleanLevelName(levelName);
        return ProjectManager::GetLevelsPath() / (cleanName + ".bson");
    }

    void RebuildNextEntityID(Level& level) {
        EntityID highestID = 0;

        for (const Entity& entity : level.entities) {
            highestID = std::max(highestID, entity.id);
        }

        level.nextEntityID = std::max(level.nextEntityID, highestID + 1);
    }

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

        const int distanceModel = listenerJson.value(
            "distanceModel",
            static_cast<int>(AL_INVERSE_DISTANCE_CLAMPED)
        );

        settings.distanceModel = ValidateDistanceModel(distanceModel);

        settings.masterGain = std::max(0.0f, settings.masterGain);
        settings.dopplerFactor = std::max(0.0f, settings.dopplerFactor);
        settings.speedOfSound = std::max(1.0f, settings.speedOfSound);
    }

    void LoadEntities(const json& levelData, Level& level) {
        if (!levelData.contains("entities")) {
            return;
        }

        level.entities.clear();

        EntityID highestEntityID = 0;

        for (const json& entityJson : levelData["entities"]) {
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
                sound.fileName = fs::path(
                    soundJson.get<std::string>()
                ).stem().string();
            }

            if (!sound.fileName.empty()) {
                level.sounds.push_back(sound);
            }
        }
    }

    void LoadWalls(const json& levelData, Level& level) {
        if (!levelData.contains("walls")) {
            return;
        }

        level.walls.clear();

        for (const json& wallJson : levelData["walls"]) {
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
        }
    }

    void LoadSectors(const json& levelData, Level& level) {
        if (!levelData.contains("sectors")) {
            return;
        }

        level.sectors.clear();

        for (const json& sectorJson : levelData["sectors"]) {
            Sector sector;

            std::vector<Vector2> corners;

            for (const json& cornerJson : sectorJson["corners"]) {
                corners.emplace_back(
                    cornerJson["x"].get<float>(),
                    cornerJson["y"].get<float>()
                );
            }

            sector.vertices = corners;

            sector.triangles = Geometry::Triangulate(sector.vertices);

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
                const json& ceilingTextureArray =
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

            level.sectors.push_back(sector);
        }
    }

    void LoadComponents(const json& levelData, Level& level) {
        if (!levelData.contains("components")) {
            return;
        }

        const json& componentsJson = levelData["components"];

        level.transforms.Clear();
        level.sprites.Clear();
        level.decals.Clear();
        level.audioSources.Clear();
        level.scripts.Clear();
        level.playerControllers.Clear();
        level.cameras.Clear();
        level.colliders.Clear();
        level.rigidbodies.Clear();

        level.ui_transforms.Clear();
        level.ui_sprites.Clear();
        level.ui_texts.Clear();

        if (componentsJson.contains("transforms")) {
            for (const json& transformJson : componentsJson["transforms"]) {
                const EntityID ownerID =
                    transformJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) {
                    continue;
                }

                Entity* entity = level.GetEntity(ownerID);
                if (entity == nullptr) {
                    continue;
                }

                ComponentTransform& c = level.transforms.Add(ownerID);
                entity->componentsMask.set(CMP_TRANSFORM);

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

        if (componentsJson.contains("sprites")) {
            for (const json& spriteJson : componentsJson["sprites"]) {
                const EntityID ownerID =
                    spriteJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) {
                    continue;
                }

                Entity* entity = level.GetEntity(ownerID);
                if (entity == nullptr) {
                    continue;
                }

                ComponentSprite& c = level.sprites.Add(ownerID);
                entity->componentsMask.set(CMP_SPRITE);

                c.textureIndex = spriteJson.value("textureIndex", -1);
            }
        }

        if (componentsJson.contains("decals")) {
            for (const json& decalJson : componentsJson["decals"]) {
                const EntityID ownerID =
                    decalJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) {
                    continue;
                }

                Entity* entity = level.GetEntity(ownerID);
                if (entity == nullptr) {
                    continue;
                }

                ComponentDecal& c = level.decals.Add(ownerID);
                entity->componentsMask.set(CMP_DECAL);

                c.wallIndex = decalJson.value("wallIndex", -1);
                c.verticalPos = decalJson.value("verticalPos", 0.0f);
                c.horizontalPos = decalJson.value("horizontalPos", -1.0f);
                c.wallNormalOffset = decalJson.value("wallNormalOffset", 0.0f);
                c.wallT = decalJson.value("wallT", 0.5f);
                c.baseHeight = decalJson.value("baseHeight", 0.0f);
                c.absHeight = decalJson.value("absHeight", false);
            }
        }

        if (componentsJson.contains("audioSources")) {
            for (const json& audioSourceJson : componentsJson["audioSources"]) {
                const EntityID ownerID =
                    audioSourceJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) {
                    continue;
                }

                Entity* entity = level.GetEntity(ownerID);
                if (entity == nullptr) {
                    continue;
                }

                ComponentAudioSource& c = level.audioSources.Add(ownerID);
                entity->componentsMask.set(CMP_AUDIO_SOURCE);

                c.soundIndex = audioSourceJson.value("soundIndex", -1);
                c.pitch = audioSourceJson.value("pitch", 1.0f);
                c.gain = audioSourceJson.value("gain", 1.0f);
                c.looping = audioSourceJson.value("looping", false);
                c.playOnStart = audioSourceJson.value("playOnStart", true);

                c.referenceDistance =
                    audioSourceJson.value("referenceDistance", 1.0f);
                c.maxDistance =
                    audioSourceJson.value("maxDistance", 10000.0f);
                c.rollOffFactor =
                    audioSourceJson.value("rollOffFactor", 1.0f);

                c.innerConeAngle =
                    audioSourceJson.value("innerConeAngle", 360.0f);
                c.outerConeAngle =
                    audioSourceJson.value("outerConeAngle", 360.0f);
                c.outerGain =
                    audioSourceJson.value("outerGain", 0.0f);

                c.name = "entity_" + std::to_string(ownerID) + "_audio";
            }
        }

        if (componentsJson.contains("scripts")) {
            for (const json& scriptJson : componentsJson["scripts"]) {
                const EntityID ownerID =
                    scriptJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) {
                    continue;
                }

                Entity* entity = level.GetEntity(ownerID);
                if (entity == nullptr) {
                    continue;
                }

                ComponentScript& c = level.scripts.Add(ownerID);
                entity->componentsMask.set(CMP_SCRIPT);

                const std::string loadedName =
                    scriptJson.value("fileName", "Test");

                c.enabled = scriptJson.value("enabled", true);
                c.fileName = fs::path(loadedName).stem().string();
            }
        }

        if (componentsJson.contains("uiTransforms")) {
            for (const json& transformJson : componentsJson["uiTransforms"]) {
                const EntityID ownerID =
                    transformJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) {
                    continue;
                }

                Entity* entity = level.GetEntity(ownerID);
                if (entity == nullptr) {
                    continue;
                }

                ComponentUITransform& c = level.ui_transforms.Add(ownerID);
                entity->componentsMask.set(CMP_UI_TRANSFORM);

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

                if (transformJson.contains("position")) {
                    c.position = {
                        transformJson["position"][0].get<float>(),
                        transformJson["position"][1].get<float>()
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

        if (componentsJson.contains("uiSprites")) {
            for (const json& spriteJson : componentsJson["uiSprites"]) {
                const EntityID ownerID =
                    spriteJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) {
                    continue;
                }

                Entity* entity = level.GetEntity(ownerID);
                if (entity == nullptr) {
                    continue;
                }

                ComponentUISprite& c = level.ui_sprites.Add(ownerID);
                entity->componentsMask.set(CMP_UI_SPRITE);

                c.textureIndex = spriteJson.value("textureIndex", -1);
            }
        }

        if (componentsJson.contains("uiTexts")) {
            for (const json& textJson : componentsJson["uiTexts"]) {
                const EntityID ownerID =
                    textJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) {
                    continue;
                }

                Entity* entity = level.GetEntity(ownerID);
                if (entity == nullptr) {
                    continue;
                }

                ComponentUIText& c = level.ui_texts.Add(ownerID);
                entity->componentsMask.set(CMP_UI_TEXT);

                c.text = textJson.value("text", "");
            }
        }

        if (componentsJson.contains("playerControllers")) {
            for (const json& controllerJson : componentsJson["playerControllers"]) {
                const EntityID ownerID =
                    controllerJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) {
                    continue;
                }

                Entity* entity = level.GetEntity(ownerID);
                if (entity == nullptr) {
                    continue;
                }

                ComponentPlayerController& c =
                    level.playerControllers.Add(ownerID);

                entity->componentsMask.set(CMP_PLAYER_CONTROLLER);

                c.isActive = controllerJson.value("isActive", true);
                c.speed = controllerJson.value("speed", 46.0f);
                c.runningSpeed = controllerJson.value("runningSpeed", 90.0f);
                c.eyeHeight = controllerJson.value("eyeHeight", 12.0f);
                c.stepSize = controllerJson.value("stepSize", 8.0f);
                c.friction = controllerJson.value("friction", 0.8f);
                c.sensitivityX = controllerJson.value("sensitivityX", 0.5f);
                c.sensitivityY = controllerJson.value("sensitivityY", 0.5f);
                c.noClip = controllerJson.value("noClip", false);
            }
        }

        if (componentsJson.contains("cameras")) {
            for (const json& cameraJson : componentsJson["cameras"]) {
                const EntityID ownerID =
                    cameraJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) {
                    continue;
                }

                Entity* entity = level.GetEntity(ownerID);
                if (entity == nullptr) {
                    continue;
                }

                ComponentCamera& c = level.cameras.Add(ownerID);
                entity->componentsMask.set(CMP_CAMERA);

                c.isActive = cameraJson.value("isActive", true);
                c.yaw = cameraJson.value("yaw", 0.0f);
                c.pitch = cameraJson.value("pitch", 0.0f);

                c.fov = cameraJson.value("fov", 90.0f);
                c.aspectRatio =
                    cameraJson.value("aspectRatio", 1680.0f / 960.0f);
                c.nearPlane = cameraJson.value("nearPlane", 0.1f);
                c.farPlane = cameraJson.value("farPlane", 10000.0f);
            }
        }

        if (componentsJson.contains("colliders")) {
            for (const json& colliderJson : componentsJson["colliders"]) {
                const EntityID ownerID =
                    colliderJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) {
                    continue;
                }

                Entity* entity = level.GetEntity(ownerID);
                if (entity == nullptr) {
                    continue;
                }

                ComponentCollider& c = level.colliders.Add(ownerID);
                entity->componentsMask.set(CMP_COLLIDER);

                c.isActive = colliderJson.value("isActive", true);
                c.isTrigger = colliderJson.value("isTrigger", false);
                c.type = colliderJson.value("type", COLLIDERTYPE_SPHERE);

                if (colliderJson.contains("scale")) {
                    c.scale = {
                        colliderJson["scale"][0].get<float>(),
                        colliderJson["scale"][1].get<float>(),
                        colliderJson["scale"][2].get<float>()
                    };
                }
            }
        }

        if (componentsJson.contains("rigidbodies")) {
            for (const json& rigidBodyJson : componentsJson["rigidbodies"]) {
                const EntityID ownerID =
                    rigidBodyJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) {
                    continue;
                }

                Entity* entity = level.GetEntity(ownerID);
                if (entity == nullptr) {
                    continue;
                }

                ComponentRigidbody& c = level.rigidbodies.Add(ownerID);
                entity->componentsMask.set(CMP_RIGIDBODY);

                c.isStatic = rigidBodyJson.value("isStatic", true);
                c.mass = rigidBodyJson.value("mass", 1.0f);
            }
        }
    }
}

namespace LevelManager {
    std::vector<Level> loadedLevels;
    int currentLevelIndex = -1;

    bool HasCurrentLevel() {
        return currentLevelIndex >= 0 &&
               currentLevelIndex < static_cast<int>(loadedLevels.size());
    }

    Level& CurrentLevel() {
        return loadedLevels[currentLevelIndex];
    }

    void ClearLoadedLevels() {
        loadedLevels.clear();
        currentLevelIndex = -1;
    }

    bool LoadLevelFromFile(const fs::path& levelFile) {
        if (!fs::exists(levelFile)) {
            std::cerr << "Level file does not exist: "
                      << levelFile.string()
                      << "\n";
            return false;
        }

        std::ifstream file(levelFile, std::ios::binary);

        if (!file.is_open()) {
            std::cerr << "Could not open BSON level file: "
                      << levelFile.string()
                      << "\n";
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
            std::cerr << "Failed to parse BSON level file "
                      << levelFile.string()
                      << ": "
                      << e.what()
                      << "\n";
            return false;
        }

        const std::string cleanName =
            CleanLevelName(levelFile.stem().string());

        Level loadedLevel;

        loadedLevel.id = levelData.value("id", 0);
        loadedLevel.name = levelData.value("name", cleanName);
        loadedLevel.nextEntityID = levelData.value("nextEntityID", 1);

        try {
            LoadEntities(levelData, loadedLevel);
            LoadComponents(levelData, loadedLevel);
            LoadWalls(levelData, loadedLevel);
            LoadSectors(levelData, loadedLevel);
            LoadTextures(levelData, loadedLevel);
            LoadSounds(levelData, loadedLevel);
            LoadLevelStats(levelData, loadedLevel);
        }
        catch (const nlohmann::json::exception& e) {
            std::cerr << "Level BSON schema error while loading "
                      << levelFile.string()
                      << ": "
                      << e.what()
                      << "\n";
            return false;
        }
        catch (const std::exception& e) {
            std::cerr << "Unexpected error while loading level "
                      << levelFile.string()
                      << ": "
                      << e.what()
                      << "\n";
            return false;
        }

        RebuildNextEntityID(loadedLevel);

        ClearLoadedLevels();

        loadedLevels.push_back(std::move(loadedLevel));
        currentLevelIndex = 0;

        return true;
    }

    bool LoadLevelByName(const std::string& levelName) {
        return LoadLevelFromFile(BuildLevelPath(levelName));
    }

    bool LoadFirstProjectLevel() {
        const fs::path levelsPath = ProjectManager::GetLevelsPath();

        if (!fs::exists(levelsPath) || !fs::is_directory(levelsPath)) {
            std::cerr << "Project levels folder does not exist: "
                      << levelsPath.string()
                      << "\n";
            return false;
        }

        std::vector<fs::path> levelFiles;

        for (const fs::directory_entry& entry : fs::directory_iterator(levelsPath)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            if (entry.path().extension() == ".bson") {
                levelFiles.push_back(entry.path());
            }
        }

        if (levelFiles.empty()) {
            std::cerr << "No .bson level files found in: "
                      << levelsPath.string()
                      << "\n";
            return false;
        }

        std::ranges::sort(levelFiles);

        return LoadLevelFromFile(levelFiles.front());
    }

    void TriangulateCurrentLevelSectors() {
        if (!HasCurrentLevel()) {
            return;
        }

        Level& level = CurrentLevel();

        for (Sector& sector : level.sectors) {
            sector.triangles.clear();

            if (sector.vertices.size() < 3) {
                continue;
            }

            sector.triangles = Geometry::Triangulate(sector.vertices);
        }
    }
}