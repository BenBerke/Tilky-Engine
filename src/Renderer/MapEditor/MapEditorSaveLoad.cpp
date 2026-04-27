#include "MapEditorInternal.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <SDL3/SDL_log.h>

using json = nlohmann::json;

namespace MapEditorInternal {
    bool SaveAndQuit() {
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

        for (int i = 0; i < static_cast<int>(objects.size()); i++) {
            json jsonObj = {
                {"id", i},
                {"type", static_cast<int>(objects[i].type)},
                {"position", {objects[i].position.x, objects[i].position.y}},
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
                {"backSector", wall.backSector}
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

            json jsonObj = {
                {"id", i},
                {"corners", cornerArray},
                {"ceilingHeight", sector.ceilingHeight},
                {"floorHeight", sector.floorHeight},
                {"ceilingTextureIndex", sector.ceilingTextureIndex},
                {"floorTextureIndex", sector.floorTextureIndex}
            };

            levelData["sectors"].push_back(jsonObj);
        }

        std::ofstream file("../Assets/Levels/test_level.json");

        if (!file.is_open()) {
            SDL_Log("Failed to open test_level.json");
            return false;
        }

        file << levelData.dump(4);
        file.close();

        SDL_Log("Level created successfully");
        return true;
    }
}

namespace MapEditor {
    bool LoadLevel(const std::string& level) {
        using namespace MapEditorInternal;

        walls.clear();
        sectors.clear();
        objects.clear();
        placedLines.clear();
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

                object.type = static_cast<ObjectType>(objectJson.value("type", 0));

                object.position = {
                    objectJson["position"][0].get<float>(),
                    objectJson["position"][1].get<float>(),
                };

                objects.push_back(object);

                if (object.type == PLAYER) {
                    playerPlaced = true;
                    playerStartPos = object.position;
                }
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

                Wall wall(
                    start,
                    end,
                    {255.0f, 255.0f, 255.0f, 255.0f},
                    frontSector,
                    backSector,
                    textureIndex
                );

                walls.push_back(wall);
                placedLines.push_back({start, end});

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

                Sector sector = {
                    corners,
                    {},
                    sectorJson.value("ceilingHeight", 40.0f),
                    sectorJson.value("floorHeight", 0.0f),
                    {255.0f, 255.0f, 255.0f},
                    {255.0f, 255.0f, 255.0f},
                    sectorJson.value("ceilingTextureIndex", 1),
                    sectorJson.value("floorTextureIndex", 1)
                };

                sector.triangles = Triangulate(sector.vertices);

                sectors.push_back(sector);
            }
        }

        SDL_Log("Level loaded successfully");
        return true;
    }
}