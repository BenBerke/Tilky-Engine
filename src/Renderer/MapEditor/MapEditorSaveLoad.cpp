#include "MapEditorInternal.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <SDL3/SDL_log.h>

using json = nlohmann::json;

namespace MapEditorInternal {
    void UpdateLevels() {
        const std::string levelsPath = "../Assets/Levels";
        try {
            MapEditor::maps.clear();
            if (std::filesystem::exists(levelsPath) && std::filesystem::is_directory(levelsPath)) {
                for (const auto& entry : std::filesystem::directory_iterator(levelsPath)) {
                    if (std::filesystem::is_regular_file(entry)) {
                        MapEditor::maps.push_back(entry.path().filename().string());
                    }
                }
            }
        } catch (const std::filesystem::filesystem_error& e) {
            SDL_Log("Error loading levels: %s", e.what());
        }
    }

    bool Save(const std::string& saveTo) {
        if (!playerPlaced) {
            SDL_Log("Can not play without a player");
            return false;
        }

        json levelData;

        levelData["objects"] = json::array();
        levelData["walls"] = json::array();
        levelData["sectors"] = json::array();
        levelData["textures"] = json::array();

        for (const auto& pathInput : textureInputs) {
            if (pathInput[0] == '\0') {
                continue;
            }

            levelData["textures"].push_back(pathInput.data());
        }

        for (int i = 0; i < static_cast<int>(MapEditor::objects.size()); i++) {
            Object obj = MapEditor::objects[i];
            json jsonObj = {
                {"id", i},
                {"type", static_cast<int>(obj.type)},
                {"position", {obj.position.x, obj.position.y}},
                {"texture", obj.textureIndex},
                {"floor", obj.floor},
                    {"zOffset", obj.zOffset},
                {"wallOffset", obj.wallOffset},
                {"wallIndex", obj.wallIndex},
                {"decalBaseHeight", obj.decalBaseHeight},
            };

            levelData["objects"].push_back(jsonObj);
        }

        for (int i = 0; i < static_cast<int>(MapEditor::walls.size()); i++) {
            const Wall& wall = MapEditor::walls[i];

            json jsonObj = {
                {"id", i},
                {"start", {wall.start.x, wall.start.y}},
                {"end", {wall.end.x, wall.end.y}},
                {"textureIndex", wall.textureIndex},
                {"frontSector", wall.frontSector},
                {"backSector", wall.backSector},
                {"floor", wall.floor}
            };

            levelData["walls"].push_back(jsonObj);
        }

        for (int i = 0; i < static_cast<int>(MapEditor::sectors.size()); i++) {
            const Sector& sector = MapEditor::sectors[i];

            json cornerArray = json::array();

            for (const Vector2& point : sector.vertices) {
                cornerArray.push_back({
                    {"x", point.x},
                    {"y", point.y}
                });
            }

            json ceilingTextureArray = json::array();

            for (int j = 0; j < MAX_FLOOR_COUNT; ++j) ceilingTextureArray.push_back(sector.ceilingTextureIndices[j]);

            json jsonObj = {
                {"id", i},
                {"corners", cornerArray},
                {"ceilingHeight", sector.ceilingHeight},
                {"floorHeight", sector.floorHeight},
                {"floorTextureIndex", sector.floorTextureIndex},
                {"floorCount", sector.floorCount},
                {"ceilingTextureIndices", ceilingTextureArray},
            };

            levelData["sectors"].push_back(jsonObj);
        }

        const std::string level = "../Assets/Levels/" + saveTo + ".json";

        std::ofstream file(level);

        if (!file.is_open()) {
            SDL_Log("Failed to open level");
            return false;
        }

        file << levelData.dump(4);
        file.close();

        SDL_Log("Level created successfully");

        UpdateLevels();

        return true;
    }
}

namespace MapEditor {
    bool LoadLevel(const std::string& _level) {
        using namespace MapEditorInternal;

        currentMap = _level;
        const std::string level = "../Assets/Levels/" + _level + ".json";

        walls.clear();
        sectors.clear();
        objects.clear();
        placedCorners.clear();
        textureInputs.clear();

        playerPlaced = false;
        playerStartPos = {0.0f, 0.0f};

        std::ifstream file(level);

        if (!file.is_open()) {
            SDL_Log("Couldn't open level file");
            return false;
        }

        json levelData;

        try {
            file >> levelData;
        }
        catch (const std::exception& e) {
            SDL_Log("Failed to parse level JSON: %s", e.what());
            return false;
        }

        file.close();

        if (levelData.contains("textures")) {
            for (const json& textureJson : levelData["textures"]) {
                std::array<char, 256> input{};
                std::string path = textureJson.get<std::string>();

                std::snprintf(input.data(), input.size(), "%s", path.c_str());

                textureInputs.push_back(input);
            }
        }

        if (levelData.contains("objects")) {
            for (const json& objectJson : levelData["objects"]) {
                Object object;

                object.id = objectJson.value("id", -1);
                object.type = static_cast<ObjectType>(objectJson.value("type", 0));
                object.textureIndex = objectJson.value("texture", -1);
                object.floor = objectJson.value("floor", 0);
                object.zOffset = objectJson.value("zOffset", 0.0f);
                object.wallOffset = objectJson.value("wallOffset", 0.0f);
                object.wallIndex = objectJson.value("wallIndex", 0);
                object.decalBaseHeight = objectJson.value("decalBaseHeight", 0);


                if (playerPlaced && object.type == OBJ_PLAYER_SPAWN) continue;

                object.position = {
                    objectJson["position"][0].get<float>(),
                    objectJson["position"][1].get<float>(),
                };

                objects.push_back(object);

                if (object.type == OBJ_PLAYER_SPAWN) {
                    playerPlaced = true;
                    playerStartPos = object.position;
                }
            }
            if (!playerPlaced) {
                Object player = {-1, OBJ_PLAYER_SPAWN, {0, 0}};
                objects.push_back(player);
                playerPlaced = true;
            }
        }

        if (levelData.contains("walls")) {
            for (const json& wallJson : levelData["walls"]) {
                Vector2 start = {
                    wallJson["start"][0].get<float>(),
                    wallJson["start"][1].get<float>(),
                };

                Vector2 end = {
                    wallJson["end"][0].get<float>(),
                    wallJson["end"][1].get<float>(),
                };

                const int frontSector = wallJson.value("frontSector", -1);
                const int backSector = wallJson.value("backSector", -1);
                const int textureIndex = wallJson.value("textureIndex", 1);
                const int floor = wallJson.value("floor", 0);

                Wall wall(
                    start,
                    end,
                    {255.0f, 255.0f, 255.0f, 255.0f},
                    frontSector,
                    backSector,
                    textureIndex,
                    floor
                );

                walls.push_back(wall);

                if (!CornerExistsAt(start)) {
                    placedCorners.push_back(start);
                }

                if (!CornerExistsAt(end)) {
                    placedCorners.push_back(end);
                }
            }
        }

        if (levelData.contains("sectors")) {
            for (const json& sectorJson : levelData["sectors"]) {
                std::vector<Vector2> corners;

                for (const json& cornerJson : sectorJson["corners"]) {
                    corners.push_back({
                        cornerJson["x"].get<float>(),
                        cornerJson["y"].get<float>()
                    });
                }

                Sector sector;

                sector.vertices = corners;
                sector.triangles = {};

                sector.ceilingHeight = sectorJson.value("ceilingHeight", 40.0f);
                sector.floorHeight = sectorJson.value("floorHeight", 0.0f);

                sector.ceilingColor = {255.0f, 255.0f, 255.0f};
                sector.floorColor = {255.0f, 255.0f, 255.0f};

                sector.floorCount = sectorJson.value("floorCount", 2);
                sector.floorCount = std::clamp(sector.floorCount, 1, MAX_FLOOR_COUNT);

                sector.floorTextureIndex = sectorJson.value("floorTextureIndex", 1);

                // Backwards-compatible fallback for old maps.
                const int oldCeilingTexture = sectorJson.value("ceilingTextureIndex", 1);

                sector.ceilingTextureIndices.fill(oldCeilingTexture);

                if (sectorJson.contains("ceilingTextureIndices")) {
                    const json& ceilingTextureArray = sectorJson["ceilingTextureIndices"];

                    for (int i = 0; i < std::min<int>(
                        static_cast<int>(ceilingTextureArray.size()),
                        MAX_FLOOR_COUNT
                    ); ++i) {
                        sector.ceilingTextureIndices[i] = ceilingTextureArray[i].get<int>();
                    }
                }

                sector.triangles = Triangulate(sector.vertices);

                sectors.push_back(sector);
            }
        }

        //SDL_Log("Level loaded successfully");
        return true;
    }
}