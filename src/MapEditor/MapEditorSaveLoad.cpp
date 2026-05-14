#include "MapEditorInternal.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <SDL3/SDL_log.h>
#include <spdlog/spdlog.h>

#include "Headers/Project/ProjectManager.hpp"

#include "Headers/Map/LevelManager.hpp"

#include "Headers/Objects/Level.hpp"
#include "Headers/Objects/Wall.hpp"
#include "Headers/Objects/Sector.hpp"
#include "config.h"
#include "Headers/Objects/Loadables.hpp"

constexpr EntityID INVALID_ENTITY_ID = static_cast<EntityID>(-1);

using json = nlohmann::json;

namespace {
    //region backend
    std::filesystem::path BuildLevelPath(const std::string &levelName) {
        std::string cleanName = levelName;

        if (cleanName.ends_with(".json")) {
            cleanName = cleanName.substr(0, cleanName.size() - 5);
        }

        return ProjectManager::GetLevelsPath() / (cleanName + ".json");
    }

    std::string CleanLevelName(const std::string &levelName) {
        if (levelName.ends_with(".json")) {
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
    void SaveComponents(json &levelData, const Level &level) {
        json componentsJson;

        componentsJson["transforms"] = json::array();
        componentsJson["sprites"] = json::array();
        componentsJson["decals"] = json::array();
        componentsJson["playerSpawns"] = json::array();
        componentsJson["audioSources"] = json::array();

        for (const ComponentTransform &t: level.transforms.components) {
            componentsJson["transforms"].push_back({
                {"ownerID", t.ownerID},
                {"position", {t.position.x, t.position.y}},
                {"sectorIndex", t.sectorIndex},
                {"floor", t.floor},
                {"scale", {t.scale.x, t.scale.y}}
            });
        }

        for (const ComponentSprite &s: level.sprites.components) {
            componentsJson["sprites"].push_back({
                {"ownerID", s.ownerID},
                {"textureIndex", s.textureIndex}
            });
        }

        for (const ComponentDecal &d: level.decals.components) {
            componentsJson["decals"].push_back({
                {"ownerID", d.ownerID},
                {"wallIndex", d.wallIndex},

                {"verticalPos", d.verticalPos},
                {"horizontalPos", d.horizontalPos},
                {"wallNormalOffset", d.wallNormalOffset},

                {"wallT", d.wallT},
                {"baseHeight", d.baseHeight},
                {"absHeight", d.absHeight}
            });
        }

        for (const ComponentPlayerSpawn &p: level.playerSpawns.components) {
            componentsJson["playerSpawns"].push_back({
                {"ownerID", p.ownerID}
            });
        }

        for (const ComponentAudioSource &a : level.audioSources.components) {
            componentsJson["audioSources"].push_back({
                {"ownerID", a.ownerID},
                {"soundIndex", a.soundIndex},
                {"pitch", a.pitch},
                {"gain", a.gain},
                {"looping", a.looping},
                {"playOnStart", a.playOnStart},

                // Distance Attenuation
                {"referenceDistance", a.referenceDistance},
                {"maxDistance", a.maxDistance},
                {"rollOffFactor", a.rollOffFactor},

                // Sound Cone
                {"innerConeAngle", a.innerConeAngle},
                {"outerConeAngle", a.outerConeAngle},
                {"outerGain", a.outerGain}
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

    void LoadComponents(const json &levelData, Level &level) {
        if (!levelData.contains("components")) {
            return;
        }

        const json &componentsJson = levelData["components"];

        level.transforms.Clear();
        level.sprites.Clear();
        level.decals.Clear();
        level.playerSpawns.Clear();
        level.audioSources.Clear();

        if (componentsJson.contains("transforms")) {
            for (const json &transformJson: componentsJson["transforms"]) {
                const EntityID ownerID =
                        transformJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) {
                    continue;
                }

                ComponentTransform &t = level.transforms.Add(ownerID);

                if (transformJson.contains("position")) {
                    t.position = {
                        transformJson["position"][0].get<float>(),
                        transformJson["position"][1].get<float>()
                    };
                }

                t.sectorIndex = transformJson.value("sectorIndex", -1);
                t.floor = transformJson.value("floor", 0);

                if (transformJson.contains("scale")) {
                    t.scale = {
                        transformJson["scale"][0].get<float>(),
                        transformJson["scale"][1].get<float>()
                    };
                }
            }
        }

        if (componentsJson.contains("sprites")) {
            for (const json &spriteJson: componentsJson["sprites"]) {
                const EntityID ownerID =
                        spriteJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) {
                    continue;
                }

                ComponentSprite &s = level.sprites.Add(ownerID);
                s.textureIndex = spriteJson.value("textureIndex", -1);
            }
        }

        if (componentsJson.contains("decals")) {
            for (const json &decalJson: componentsJson["decals"]) {
                const EntityID ownerID =
                        decalJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) {
                    continue;
                }

                ComponentDecal &d = level.decals.Add(ownerID);

                d.wallIndex = decalJson.value("wallIndex", -1);

                d.verticalPos = decalJson.value("verticalPos", 0.0f);
                d.horizontalPos = decalJson.value("horizontalPos", -1.0f);
                d.wallNormalOffset = decalJson.value("wallNormalOffset", 0.0f);

                d.wallT = decalJson.value("wallT", 0.5f);
                d.baseHeight = decalJson.value("baseHeight", 0.0f);
                d.absHeight = decalJson.value("absHeight", false);
            }
        }

        if (componentsJson.contains("playerSpawns")) {
            for (const json &playerSpawnJson: componentsJson["playerSpawns"]) {
                const EntityID ownerID =
                        playerSpawnJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) {
                    continue;
                }

                level.playerSpawns.Add(ownerID);
            }
        }

        if (componentsJson.contains("audioSources")) {
            for (const json &audioSourceJson : componentsJson["audioSources"]) {
                const EntityID ownerID =
                    audioSourceJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) {
                    continue;
                }

                ComponentAudioSource& a = level.audioSources.Add(ownerID);

                a.soundIndex = audioSourceJson.value("soundIndex", -1);
                a.pitch      = audioSourceJson.value("pitch", 1.0f);
                a.gain       = audioSourceJson.value("gain", 1.0f);
                a.looping    = audioSourceJson.value("looping", false);
                a.playOnStart = audioSourceJson.value("playOnStart", true);

                // Distance Attenuation
                a.referenceDistance = audioSourceJson.value("referenceDistance", 1.0f);
                a.maxDistance       = audioSourceJson.value("maxDistance", 10000.0f);
                a.rollOffFactor     = audioSourceJson.value("rollOffFactor", 1.0f);

                // Sound Cone
                a.innerConeAngle = audioSourceJson.value("innerConeAngle", 360.0f);
                a.outerConeAngle = audioSourceJson.value("outerConeAngle", 360.0f);
                a.outerGain      = audioSourceJson.value("outerGain", 0.0f);

                a.name = "entity_" + std::to_string(ownerID) + "_audio";
            }
        }
    }

    void LoadWalls(const json &levelData, Level &level) {
        using namespace MapEditorInternal;

        if (!levelData.contains("walls")) {
            return;
        }

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

            sector.triangles = MapEditor::Triangulate(sector.vertices);

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

    void UpdatePlayerSpawnFromLoadedLevel(Level& level) {
        using namespace MapEditorInternal;

        playerPlaced = false;
        MapEditor::playerStartPos = {0.0f, 0.0f};

        for (const ComponentPlayerSpawn& spawn : level.playerSpawns.components) {
            ComponentTransform* transform =
                level.transforms.Get(spawn.ownerID);

            if (transform == nullptr) {
                continue;
            }

            playerPlaced = true;
            MapEditor::playerStartPos = transform->position;
            return;
        }

        Entity& playerEntity = level.CreateEntity();

        auto* transform =
            playerEntity.GetComponent<ComponentTransform>();

        if (transform == nullptr) {
            transform = playerEntity.AddComponent<ComponentTransform>();
        }

        transform->position = {0.0f, 0.0f};
        transform->floor = 0;
        transform->scale = {32.0f, 32.0f};

        level.playerSpawns.Add(playerEntity.id);

        playerPlaced = true;
        MapEditor::playerStartPos = transform->position;
    }
}

namespace MapEditorInternal {
    void UpdateLevels() {
        const std::filesystem::path levelsPath = ProjectManager::GetLevelsPath();

        try {
            MapEditor::maps.clear();

            if (!std::filesystem::exists(levelsPath) ||
                !std::filesystem::is_directory(levelsPath)) {
                std::filesystem::create_directories(levelsPath);
                return;
            }

            for (const auto &entry: std::filesystem::directory_iterator(levelsPath)) {
                if (!std::filesystem::is_regular_file(entry)) {
                    continue;
                }

                if (entry.path().extension() != ".json") {
                    continue;
                }

                MapEditor::maps.push_back(entry.path().stem().string());
            }
        } catch (const std::filesystem::filesystem_error &e) {
            spdlog::critical("Error loading levels {}", e.what());
        }
    }

    bool Save(const std::string &saveTo) {
        Level &level = GetOrCreateCurrentLevel();

        MapEditor::RefreshLevelTexturesFromFolder();
        MapEditor::RefreshLevelSoundsFromFolder();

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
            {"backgroundTextureIndex", MapEditor::backgroundTextureIndex}
        };

        SaveEntities(levelData, level);
        SaveComponents(levelData, level);
        SaveWalls(levelData, level);
        SaveSectors(levelData, level);
        SaveTextures(levelData, level);
        SaveSounds(levelData, level);

        const std::filesystem::path path = BuildLevelPath(cleanName);

        std::filesystem::create_directories(ProjectManager::GetLevelsPath());

        std::ofstream file(path);

        if (!file.is_open()) {
            spdlog::critical("Failed to open level for saving: {}", path.string().c_str());
            return false;
        }

        file << levelData.dump(4);
        file.close();

        spdlog::info("Level saved successfully {}", path.string());

        UpdateLevels();

        return true;
    }
}

namespace MapEditor {
    bool LoadLevel(const std::string &levelName) {
        using namespace MapEditorInternal;

        const std::string cleanName = CleanLevelName(levelName);
        const std::filesystem::path path = BuildLevelPath(cleanName);

        std::ifstream file(path);

        if (!file.is_open()) {
            spdlog::critical("Could not open level file {}", path.string());
            return false;
        }

        json levelData;

        try {
            file >> levelData;
        } catch (const std::exception &e) {
            spdlog::critical("Failed to parse level JSON: {}", e.what());
            return false;
        }

        file.close();

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
            spdlog::info("Entities loaded");

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

        UpdatePlayerSpawnFromLoadedLevel(activeLevel);

        RefreshLevelTexturesFromFolder();
        RefreshLevelSoundsFromFolder();

        spdlog::info("Level loaded successfully {}", path.string());

        return true;
    }
}
