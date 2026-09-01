#include "Headers/Map/LevelSerialization.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "Headers/Math/Geometry/Geometry.hpp"
#include "Headers/Objects/Loadables.hpp"
#include "Headers/Objects/Sector.hpp"
#include "Headers/Objects/Wall.hpp"
#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Map/MapQueries.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaScripting.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
    const char *ScriptValueTypeToString(const ScriptValueType type) {
        switch (type) {
            case ScriptValueType::Int: return "Int";
            case ScriptValueType::Float: return "Float";
            case ScriptValueType::Bool: return "Bool";
            case ScriptValueType::String: return "String";
        }

        return "Unknown";
    }

    ScriptValueType ScriptValueTypeFromString(const std::string &type) {
        if (type == "Int") return ScriptValueType::Int;
        if (type == "Float") return ScriptValueType::Float;
        if (type == "Bool") return ScriptValueType::Bool;
        if (type == "String") return ScriptValueType::String;

        return ScriptValueType::String;
    }

    json ScriptValueToJson(const ScriptValue &value) {
        json valueJson;

        std::visit(
            [&]<typename T0>(const T0 &typedValue) {
                using T = std::decay_t<T0>;

                if constexpr (std::is_same_v<T, int>) {
                    valueJson["type"] = "Int";
                    valueJson["value"] = typedValue;
                } else if constexpr (std::is_same_v<T, float>) {
                    valueJson["type"] = "Float";
                    valueJson["value"] = typedValue;
                } else if constexpr (std::is_same_v<T, bool>) {
                    valueJson["type"] = "Bool";
                    valueJson["value"] = typedValue;
                } else if constexpr (std::is_same_v<T, std::string>) {
                    valueJson["type"] = "String";
                    valueJson["value"] = typedValue;
                }
            },
            value
        );

        return valueJson;
    }

    ScriptValue ScriptValueFromJson(const json &valueJson) {
        const std::string typeString = valueJson.value("type", "String");
        const ScriptValueType type = ScriptValueTypeFromString(typeString);

        switch (type) {
            case ScriptValueType::Int:
                return valueJson.value("value", 0);

            case ScriptValueType::Float:
                return valueJson.value("value", 0.0f);

            case ScriptValueType::Bool:
                return valueJson.value("value", false);

            case ScriptValueType::String:
                return valueJson.value("value", std::string{});
        }

        return std::string{};
    }

    json ScriptPublicValuesToJson(const std::unordered_map<std::string, ScriptValue> &publicValues) {
        json publicValuesJson = json::object();

        for (const auto &[name, value]: publicValues)
            publicValuesJson[name] = ScriptValueToJson(value);

        return publicValuesJson;
    }

    std::unordered_map<std::string, ScriptValue> ScriptPublicValuesFromJson(const json &publicValuesJson) {
        std::unordered_map<std::string, ScriptValue> publicValues;

        if (!publicValuesJson.is_object()) return publicValues;

        for (const auto &[name, valueJson]: publicValuesJson.items())
            publicValues[name] = ScriptValueFromJson(valueJson);

        return publicValues;
    }
}

namespace {
    ID LoadIDField(const json& object, const char* key, const ID fallback = INVALID_ID) {
        if (!object.contains(key)) return fallback;

        const json& value = object[key];

        if (value.is_number_integer()) {
            const std::int64_t signedValue = value.get<std::int64_t>();

            if (signedValue < 0)  return INVALID_ID;

            if (signedValue > static_cast<std::int64_t>(std::numeric_limits<ID>::max())) return fallback;

            return static_cast<ID>(signedValue);
        }

        if (value.is_number_unsigned()) {
            const std::uint64_t unsignedValue = value.get<std::uint64_t>();

            if (unsignedValue > static_cast<std::uint64_t>(std::numeric_limits<ID>::max())) return fallback;

            return static_cast<ID>(unsignedValue);
        }

        return fallback;
    }

    void SetError(std::string* errorMessage, const std::string& message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
    }

    ALenum ValidateDistanceModel(const int model) {
        switch (model) {
            case AL_INVERSE_DISTANCE:
            case AL_INVERSE_DISTANCE_CLAMPED:
            case AL_LINEAR_DISTANCE:
            case AL_LINEAR_DISTANCE_CLAMPED:
            case AL_EXPONENT_DISTANCE:
            case AL_EXPONENT_DISTANCE_CLAMPED:
            case AL_NONE: return static_cast<ALenum>(model);
            default: return AL_INVERSE_DISTANCE_CLAMPED;
        }
    }

    void RebuildNextEntityID(Level& level) {
        ID highestID = 0;

        for (const Entity& entity : level.entities) {
            highestID = std::max(highestID, entity.id);
        }

        level.nextEntityID = std::max(level.nextEntityID, highestID + 1);
    }

    void LoadExtraData(const json& levelData, LevelSerialization::LevelExtraData* outExtraData) {
        if (outExtraData == nullptr) return;

        *outExtraData = LevelSerialization::LevelExtraData{};

        if (!levelData.contains("levelVars") || !levelData["levelVars"].is_object()) return;

        outExtraData->backgroundTextureFileName = levelData["levelVars"].value("backgroundTextureFileName", std::string());
    }

    void SaveExtraData(json &levelData, const LevelSerialization::LevelExtraData *extraData) {
        if (extraData == nullptr) return;

        levelData["levelVars"] = {
            {"backgroundTextureFileName", extraData->backgroundTextureFileName}
        };
    }

    void LoadLevelStats(const json &levelData, Level &level) {
        if (!levelData.contains("levelStats") ||
            !levelData["levelStats"].is_object()) {
            return;
        }

        const json &levelStatsJson = levelData["levelStats"];

        if (levelStatsJson.contains("listenerSettings") && levelStatsJson["listenerSettings"].is_object()) {
            const json &listenerJson = levelStatsJson["listenerSettings"];
            ListenerSettings &listenerSettings = level.listenerSettings;

            listenerSettings.masterGain = listenerJson.value("masterGain", 1.0f);
            listenerSettings.dopplerFactor = listenerJson.value("dopplerFactor", 1.0f);
            listenerSettings.speedOfSound = listenerJson.value("speedOfSound", 343.3f);

            const int distanceModel = listenerJson.value(
                "distanceModel",
                static_cast<int>(AL_INVERSE_DISTANCE_CLAMPED)
            );

            listenerSettings.distanceModel = ValidateDistanceModel(distanceModel);

            listenerSettings.masterGain = std::max(0.0f, listenerSettings.masterGain);
            listenerSettings.dopplerFactor = std::max(0.0f, listenerSettings.dopplerFactor);
            listenerSettings.speedOfSound = std::max(1.0f, listenerSettings.speedOfSound);
        }

        if (levelStatsJson.contains("worldSettings") && levelStatsJson["worldSettings"].is_object()) {
            const json &worldSettingsJson = levelStatsJson["worldSettings"];
            WorldSettings &worldSettings = level.worldSettings;

            worldSettings.gravity = worldSettingsJson.value("gravity", 9.8f);
        }

        if (levelStatsJson.contains("rendererSettings") && levelStatsJson["rendererSettings"].is_object()) {
            const json &rendererSettingsJson = levelStatsJson["rendererSettings"];
            RendererSettings &rendererSettings = level.rendererSettings;

            rendererSettings.textureSetting = rendererSettingsJson.value("textureSetting", PIXEL_ART_SHIMMERY);
        }
    }

    void SaveLevelStats(json &levelData, const Level &level) {
        const ListenerSettings &listenerSettings = level.listenerSettings;
        const WorldSettings &worldSettings = level.worldSettings;
        const RendererSettings &rendererSettings = level.rendererSettings;

        levelData["levelStats"] = {
            {
                "listenerSettings", {
                    {"masterGain", listenerSettings.masterGain},
                    {"dopplerFactor", listenerSettings.dopplerFactor},
                    {"speedOfSound", listenerSettings.speedOfSound},
                    {"distanceModel", static_cast<int>(listenerSettings.distanceModel)}
                }
            },
            {
                "worldSettings", {
                    {"gravity", worldSettings.gravity}
                }
            },
            {
                "rendererSettings", {
                    {"textureSetting", rendererSettings.textureSetting}
                }
            }
        };
    }

    void LoadEntities(const json& levelData, Level& level) {
        level.entities.clear();

        if (!levelData.contains("entities")) {
            return;
        }

        ID highestEntityID = 0;

        for (const json& entityJson : levelData["entities"]) {
            Entity entity;

            entity.id = entityJson.at("id").get<ID>();
            entity.name = entityJson.value("name", "Entity");
            entity.attachedLevelId = level.id;

            highestEntityID = std::max(highestEntityID, entity.id);
            level.entities.push_back(entity);
        }

        level.nextEntityID = highestEntityID + 1;
    }

    void SaveEntities(json& levelData, const Level& level) {
        levelData["entities"] = json::array();

        for (const Entity& entity : level.entities) {
            levelData["entities"].push_back({
                {"id", entity.id},
                {"name", entity.name}
            });
        }
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

    void SaveTextures(json& levelData, const Level& level) {
        levelData["textures"] = json::array();

        for (const Texture& texture : level.textures) {
            if (texture.fileName.empty()) {
                continue;
            }

            levelData["textures"].push_back({
                {"fileName", texture.fileName}
            });
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
                sound.fileName = fs::path(soundJson.get<std::string>()
                ).stem().string();
            }

            if (!sound.fileName.empty()) level.sounds.push_back(sound);
        }
    }

    void SaveSounds(json& levelData, const Level& level) {
        levelData["sounds"] = json::array();

        for (const Sound& sound : level.sounds) {
            if (sound.fileName.empty()) continue;

            levelData["sounds"].push_back({{"fileName", sound.fileName}});
        }
    }

    void LoadWalls(const json &levelData, Level &level) {
        level.walls.clear();

        if (!levelData.contains("walls")) return;

        ID highestWallID = 0;
        std::unordered_set<ID> seenWallIDs;

        for (int i = 0; i < static_cast<int>(levelData["walls"].size()); ++i) {
            const json &wallJson = levelData["walls"][i];

            const Vector2 start = {
                wallJson.at("start").at(0).get<float>(),
                wallJson.at("start").at(1).get<float>()
            };

            const Vector2 end = {
                wallJson.at("end").at(0).get<float>(),
                wallJson.at("end").at(1).get<float>()
            };

            const uint_fast32_t color = static_cast<uint_fast32_t>(
                wallJson.value("color", std::uint32_t{0xFFFFFFFFu})
            );

            Wall wall(
                start,
                end,
                color,
                LoadIDField(wallJson, "frontSector", INVALID_ID),
                LoadIDField(wallJson, "backSector", INVALID_ID),
                wallJson.value("textureFileName", std::string{})
            );

            if (wallJson.contains("textureOffset")) {
                wall.textureOffset = {
                    wallJson.at("textureOffset").at(0).get<float>(),
                    wallJson.at("textureOffset").at(1).get<float>()
                };
            } else wall.textureOffset = {0.0f, 0.0f};

            wall.id = LoadIDField(
                wallJson,
                "id",
                static_cast<ID>(i)
            );

            if (wall.id == INVALID_ID) wall.id = static_cast<ID>(i);

            if (seenWallIDs.contains(wall.id)) {
                const ID reassigned = highestWallID + 1;

                spdlog::warn(
                    "LoadWalls: duplicate wall id {} at array index {} - reassigning to {}",
                    wall.id,
                    i,
                    reassigned
                );

                wall.id = reassigned;
            }

            seenWallIDs.insert(wall.id);
            highestWallID = std::max(highestWallID, wall.id);

            level.walls.push_back(std::move(wall));
        }

        level.nextWallID = std::max(
            level.nextWallID,
            highestWallID + 1
        );
    }

    void SaveWalls(json &levelData, const Level &level) {
        levelData["walls"] = json::array();

        for (const Wall &wall: level.walls) {
            levelData["walls"].push_back({
                {"id", wall.id},
                {
                    "start", {
                        wall.start.x,
                        wall.start.y
                    }
                },
                {
                    "end", {
                        wall.end.x,
                        wall.end.y
                    }
                },
                {"color", static_cast<std::uint32_t>(wall.color)},
                {"textureFileName", wall.textureFileName},
                {
                    "textureOffset", {
                        wall.textureOffset.x,
                        wall.textureOffset.y
                    }
                },
                {"frontSector", wall.frontSector},
                {"backSector", wall.backSector},
            });
        }
    }

    void LoadSectors(const json &levelData, Level &level) {
        level.sectors.clear();

        if (!levelData.contains("sectors") ||
            !levelData.at("sectors").is_array()) {
            return;
        }

        ID highestSectorID = 0;
        std::unordered_set<ID> seenSectorIDs;

        const auto loadVector3 = [](const json &parentJson,
                                    const char *field,
                                    const Vector3 &defaultValue) -> Vector3 {
            if (!parentJson.contains(field)) return defaultValue;

            const json &vectorJson = parentJson.at(field);

            if (!vectorJson.is_array() || vectorJson.size() != 3) {
                return defaultValue;
            }

            return {
                vectorJson[0].get<float>(),
                vectorJson[1].get<float>(),
                vectorJson[2].get<float>()
            };
        };

        const auto loadSurface = [](const json &surfaceJson,
                                    const float defaultHeight) -> SectorSurface {
            SectorSurface surface;

            surface.height = surfaceJson.value("height", defaultHeight);
            surface.color = static_cast<uint_fast32_t>(
                surfaceJson.value("color", std::uint32_t{0xFFFFFFFFu})
            );
            surface.texture = surfaceJson.value("texture", std::string{});

            const int slopeDirection = surfaceJson.value(
                "slopeDirection",
                static_cast<int>(PLUS_X)
            );

            if (slopeDirection >= static_cast<int>(PLUS_X) &&
                slopeDirection <= static_cast<int>(MINUS_Z)) {
                surface.slopeDirection =
                        static_cast<SlopeDirection>(slopeDirection);
            }

            surface.slopeStrength =
                    surfaceJson.value("slopeStrength", 0.0f);

            return surface;
        };

        const auto loadLoop = [](const json &loopJson) {
            std::vector<Vector2> loop;

            if (!loopJson.is_array()) return loop;

            loop.reserve(loopJson.size());

            for (const json &pointJson: loopJson) {
                if (!pointJson.is_object() ||
                    !pointJson.contains("x") ||
                    !pointJson.contains("y")) {
                    continue;
                }

                loop.emplace_back(
                    pointJson.at("x").get<float>(),
                    pointJson.at("y").get<float>()
                );
            }

            return loop;
        };

        const json &sectorArray = levelData.at("sectors");
        level.sectors.reserve(sectorArray.size());

        for (int sectorIndex = 0;
             sectorIndex < static_cast<int>(sectorArray.size());
             ++sectorIndex) {
            const json &sectorJson = sectorArray[sectorIndex];

            if (!sectorJson.is_object()) {
                spdlog::warn(
                    "LoadSectors: sector at array index {} is not an object",
                    sectorIndex
                );
                continue;
            }

            Sector sector;
            sector.vertices.clear();
            sector.innerLoops.clear();
            sector.triangles.clear();
            sector.floors.clear();

            sector.id = LoadIDField(
                sectorJson,
                "id",
                static_cast<ID>(sectorIndex)
            );

            if (sector.id == INVALID_ID) {
                sector.id = static_cast<ID>(sectorIndex);
            }

            if (seenSectorIDs.contains(sector.id)) {
                ID reassignedID = highestSectorID + 1;

                while (seenSectorIDs.contains(reassignedID) ||
                       reassignedID == INVALID_ID) {
                    ++reassignedID;
                }

                spdlog::warn(
                    "LoadSectors: duplicate sector id {} at array index {} "
                    "- reassigning to {}",
                    sector.id,
                    sectorIndex,
                    reassignedID
                );

                sector.id = reassignedID;
            }

            seenSectorIDs.insert(sector.id);
            highestSectorID = std::max(highestSectorID, sector.id);

            // Outer boundary
            if (sectorJson.contains("corners")) {
                sector.vertices = loadLoop(sectorJson.at("corners"));
            }

            // Hole boundaries
            if (sectorJson.contains("innerLoops") &&
                sectorJson.at("innerLoops").is_array()) {
                const json &innerLoopArray = sectorJson.at("innerLoops");
                sector.innerLoops.reserve(innerLoopArray.size());

                for (int loopIndex = 0;
                     loopIndex < static_cast<int>(innerLoopArray.size());
                     ++loopIndex) {
                    std::vector<Vector2> loop =
                            loadLoop(innerLoopArray[loopIndex]);

                    if (loop.size() < 3) {
                        spdlog::warn(
                            "LoadSectors: sector {} inner loop {} has fewer "
                            "than three valid vertices - skipping",
                            sector.id,
                            loopIndex
                        );
                        continue;
                    }

                    sector.innerLoops.push_back(std::move(loop));
                }
            }

            if (sectorJson.contains("floors") &&
                sectorJson.at("floors").is_array()) {
                const json &floorArray = sectorJson.at("floors");
                sector.floors.reserve(floorArray.size());

                for (int floorIndex = 0;
                     floorIndex < static_cast<int>(floorArray.size());
                     ++floorIndex) {
                    const json &floorJson = floorArray[floorIndex];

                    if (!floorJson.is_object() ||
                        !floorJson.contains("floor") ||
                        !floorJson.at("floor").is_object() ||
                        !floorJson.contains("ceiling") ||
                        !floorJson.at("ceiling").is_object()) {
                        spdlog::warn(
                            "LoadSectors: sector {} floor {} is missing "
                            "floor or ceiling data",
                            sector.id,
                            floorIndex
                        );
                        continue;
                    }

                    SectorFloor sectorFloor;
                    sectorFloor.floor =
                            loadSurface(floorJson.at("floor"), 0.0f);
                    sectorFloor.ceiling =
                            loadSurface(floorJson.at("ceiling"), 40.0f);

                    if (sectorFloor.floor.height >=
                        sectorFloor.ceiling.height) {
                        spdlog::warn(
                            "LoadSectors: sector {} floor {} has invalid "
                            "heights [{}, {}] - skipping",
                            sector.id,
                            floorIndex,
                            sectorFloor.floor.height,
                            sectorFloor.ceiling.height
                        );
                        continue;
                    }

                    sector.floors.push_back(std::move(sectorFloor));
                }
            }

            std::ranges::sort(
                sector.floors,
                {},
                [](const SectorFloor &floor) {
                    return floor.floor.height;
                }
            );

            for (int floorIndex = 1;
                 floorIndex < static_cast<int>(sector.floors.size());
                 ++floorIndex) {
                const SectorFloor &previous =
                        sector.floors[floorIndex - 1];
                const SectorFloor &current =
                        sector.floors[floorIndex];

                if (previous.ceiling.height > current.floor.height) {
                    spdlog::warn(
                        "LoadSectors: sector {} floors {} and {} overlap",
                        sector.id,
                        floorIndex - 1,
                        floorIndex
                    );
                }
            }

            if (sector.floors.empty()) {
                spdlog::warn(
                    "LoadSectors: sector {} has no valid floors - "
                    "inserting one default floor",
                    sector.id
                );

                sector.floors.push_back({
                    {
                        0.0f,
                        std::numeric_limits<uint_fast32_t>::max(),
                        {}
                    },
                    {
                        40.0f,
                        std::numeric_limits<uint_fast32_t>::max(),
                        {}
                    }
                });
            }

            sector.light = loadVector3(
                sectorJson,
                "light",
                {255.0f, 255.0f, 255.0f}
            );

            // Triangles are derived rather than serialized.
            sector.triangles = Geometry::Triangulate(
                sector.vertices,
                sector.innerLoops
            );

            level.sectors.push_back(std::move(sector));
        }

        level.nextSectorID = std::max(
            level.nextSectorID,
            highestSectorID + 1
        );
    }

    void SaveSectors(json &levelData, const Level &level) {
        const auto saveLoop = [](const std::vector<Vector2> &loop) {
            json loopJson = json::array();

            for (const Vector2 &point: loop) {
                loopJson.push_back({
                    {"x", point.x},
                    {"y", point.y}
                });
            }

            return loopJson;
        };

        const auto saveSurface = [](const SectorSurface &surface) {
            return json{
                {"height", surface.height},
                {
                    "color",
                    static_cast<std::uint32_t>(surface.color)
                },
                {"texture", surface.texture},
                {
                    "slopeDirection",
                    static_cast<int>(surface.slopeDirection)
                },
                {"slopeStrength", surface.slopeStrength}
            };
        };

        json sectorArray = json::array();

        for (const Sector &sector: level.sectors) {
            json innerLoopArray = json::array();

            for (const std::vector<Vector2> &innerLoop:
                 sector.innerLoops) {
                innerLoopArray.push_back(saveLoop(innerLoop));
            }

            json floorArray = json::array();

            for (const SectorFloor &sectorFloor: sector.floors) {
                floorArray.push_back({
                    {"floor", saveSurface(sectorFloor.floor)},
                    {"ceiling", saveSurface(sectorFloor.ceiling)}
                });
            }

            sectorArray.push_back({
                {"id", sector.id},
                {"corners", saveLoop(sector.vertices)},
                {"innerLoops", std::move(innerLoopArray)},
                {"floors", std::move(floorArray)},
                {
                    "light",
                    {
                        sector.light.x,
                        sector.light.y,
                        sector.light.z
                    }
                }
            });
        }

        levelData["sectors"] = std::move(sectorArray);
    }

    void LoadComponents(const json &levelData, Level &level) {
        level.transforms.Clear();
        level.sprites.Clear();
        level.audioSources.Clear();
        level.scripts.Clear();
        level.playerControllers.Clear();
        level.cameras.Clear();
        level.colliders.Clear();
        level.rigidbodies.Clear();

        level.ui_transforms.Clear();
        level.ui_sprites.Clear();
        level.ui_texts.Clear();

        if (!levelData.contains("components")) return;

        const json &componentsJson = levelData["components"];

        if (componentsJson.contains("transforms")) {
            for (const json& transformJson : componentsJson["transforms"]) {
                const ID ownerID = transformJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) continue;

                Entity* entity = level.GetEntity(ownerID);
                if (entity == nullptr) continue;

                ComponentTransform& c = level.transforms.Add(ownerID);
                entity->componentsMask.set(CMP_TRANSFORM);

                c.position = {
                    transformJson["position"][0].get<float>(),
                    transformJson["position"][1].get<float>(),
                    transformJson["position"][2].get<float>()
                };

                c.rotation = Quaternion{
                    transformJson["rotation"][0].get<float>(),
                    transformJson["rotation"][1].get<float>(),
                    transformJson["rotation"][2].get<float>(),
                    transformJson["rotation"][3].get<float>()
                }.Normalized();

                c.sectorIndex = transformJson["sectorIndex"].get<int>();

                c.scale = {
                    transformJson["scale"][0].get<float>(),
                    transformJson["scale"][1].get<float>(),
                    transformJson["scale"][2].get<float>()
                };
            }
        }

        if (componentsJson.contains("sprites")) {
            for (const json& spriteJson : componentsJson["sprites"]) {
                const ID ownerID = spriteJson.at("ownerID").get<ID>();

                Entity* entity = level.GetEntity(ownerID);
                if (entity == nullptr) continue;

                const auto textureFileNames =
                    spriteJson.at("textureFileNames").get<std::array<std::string, 8>>();

                const int sideCountValue = spriteJson.at("sideCount").get<int>();

                if (sideCountValue != SIDECOUNT_SINGLE &&
                    sideCountValue != SIDECOUNT_90 &&
                    sideCountValue != SIDECOUNT_45) continue;

                ComponentSprite& c = level.sprites.Add(ownerID);

                c.textureFileNames = textureFileNames;
                c.sideCount = static_cast<SideCount>(sideCountValue);
                c.color = static_cast<uint_fast32_t>(
                    spriteJson.value("color", std::uint32_t{0xFFFFFFFFu})
                );
                c.isStatic = spriteJson.value("isStatic", false);

                entity->componentsMask.set(CMP_SPRITE);
            }
        }

        if (componentsJson.contains("audioSources")) {
            for (const json& audioSourceJson : componentsJson["audioSources"]) {
                const ID ownerID = audioSourceJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) continue;

                Entity* entity = level.GetEntity(ownerID);
                if (entity == nullptr) continue;

                ComponentAudioSource& c = level.audioSources.Add(ownerID);

                c.name = "entity_" + std::to_string(ownerID) + "_audio";
                c.soundFileName = audioSourceJson.value("soundFileName", std::string{});

                c.pitch = audioSourceJson.value("pitch", 1.0f);
                c.gain = audioSourceJson.value("gain", 1.0f);
                c.looping = audioSourceJson.value("looping", false);
                c.playOnStart = audioSourceJson.value("playOnStart", false);

                c.referenceDistance = audioSourceJson.value("referenceDistance",1.0f);

                c.maxDistance = audioSourceJson.value("maxDistance",10000.0f);
                c.rollOffFactor = audioSourceJson.value("rollOffFactor",1.0f);
                c.innerConeAngle = audioSourceJson.value("innerConeAngle",360.0f);
                c.outerConeAngle = audioSourceJson.value("outerConeAngle",360.0f);
                c.outerGain = audioSourceJson.value("outerGain", 0.0f);

                entity->componentsMask.set(CMP_AUDIO_SOURCE);
            }
        }

        if (componentsJson.contains("scripts")) {
            for (const json &scriptJson: componentsJson["scripts"]) {
                const ID ownerID = scriptJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) continue;

                Entity *entity = level.GetEntity(ownerID);

                if (entity == nullptr) continue;

                ComponentScript &c = level.scripts.Add(ownerID);
                entity->componentsMask.set(CMP_SCRIPT);

                const std::string loadedName = scriptJson.value("fileName", std::string{});

                c.ownerID = ownerID;
                c.enabled = scriptJson.value("enabled", true);
                c.fileName = fs::path(loadedName).stem().string();
                c.schemaHash = scriptJson.value("schemaHash", static_cast<std::uint64_t>(0));

                if (scriptJson.contains("publicValues"))
                    c.publicValues = ScriptPublicValuesFromJson(scriptJson["publicValues"]);
                else c.publicValues.clear();
            }
        }

        if (componentsJson.contains("uiTransforms")) {
            for (const json &transformJson: componentsJson["uiTransforms"]) {
                const ID ownerID = transformJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) continue;

                Entity *entity = level.GetEntity(ownerID);
                if (entity == nullptr) continue;

                ComponentUITransform &c = level.ui_transforms.Add(ownerID);
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

                c.rotation = transformJson.value("rotation", c.rotation);
            }
        }

        if (componentsJson.contains("uiSprites")) {
            for (const json &spriteJson: componentsJson["uiSprites"]) {
                const ID ownerID = spriteJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) continue;

                Entity *entity = level.GetEntity(ownerID);
                if (entity == nullptr) continue;

                ComponentUISprite &c = level.ui_sprites.Add(ownerID);
                entity->componentsMask.set(CMP_UI_SPRITE);

                c.texture = spriteJson.value("texture", "");
            }
        }

        if (componentsJson.contains("uiTexts")) {
            for (const json &textJson: componentsJson["uiTexts"]) {
                const ID ownerID = textJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) continue;

                Entity *entity = level.GetEntity(ownerID);
                if (entity == nullptr) continue;

                ComponentUIText &c = level.ui_texts.Add(ownerID);
                entity->componentsMask.set(CMP_UI_TEXT);

                c.text = textJson.value("text", "");
            }
        }

        if (componentsJson.contains("playerControllers")) {
            for (const json &controllerJson: componentsJson["playerControllers"]) {
                const ID ownerID = controllerJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) continue;

                Entity *entity = level.GetEntity(ownerID);
                if (entity == nullptr) continue;

                ComponentPlayerController &c = level.playerControllers.Add(ownerID);

                entity->componentsMask.set(CMP_PLAYER_CONTROLLER);

                c.isActive = controllerJson.value("isActive", true);
                c.speed = controllerJson.value("speed", 46.0f);
                c.runningSpeed = controllerJson.value("runningSpeed", 90.0f);
                c.jumpPower = controllerJson.value("jumpPower", 100.0f);
                c.eyeHeight = controllerJson.value("eyeHeight", 12.0f);
                c.friction = controllerJson.value("friction", 0.8f);
                c.sensitivityX = controllerJson.value("sensitivityX", 0.5f);
                c.sensitivityY = controllerJson.value("sensitivityY", 0.5f);
                c.maxPitch = controllerJson.value("maxPitch", 89.0f);
                c.minPitch = controllerJson.value("minPitch", -89.0f);
                c.maxYaw = controllerJson.value("maxYaw", 360.0f);
                c.minYaw = controllerJson.value("minYaw", .0f);
                c.noClip = controllerJson.value("noClip", false);
                c.jumpBufferMs = controllerJson.value("jumpBufferMs", 100.0f);

                if (controllerJson.contains("velocity")) {
                    c.velocity = {
                        controllerJson["velocity"][0].get<float>(),
                        controllerJson["velocity"][1].get<float>(),
                        controllerJson["velocity"][2].get<float>()
                    };
                }
            }
        }

        if (componentsJson.contains("cameras")) {
            for (const json &cameraJson: componentsJson["cameras"]) {
                const ID ownerID =
                        cameraJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) continue;

                Entity *entity = level.GetEntity(ownerID);
                if (entity == nullptr) continue;

                ComponentCamera &c = level.cameras.Add(ownerID);
                entity->componentsMask.set(CMP_CAMERA);

                c.isActive = cameraJson.value("isActive", true);
                c.yaw = cameraJson.value("yaw", 0.0f);
                c.pitch = cameraJson.value("pitch", 0.0f);

                if (cameraJson.contains("forward")) {
                    c.forward = {
                        cameraJson["forward"][0].get<float>(),
                        cameraJson["forward"][1].get<float>(),
                        cameraJson["forward"][2].get<float>()
                    };
                }

                if (cameraJson.contains("target")) {
                    c.target = {
                        cameraJson["target"][0].get<float>(),
                        cameraJson["target"][1].get<float>(),
                        cameraJson["target"][2].get<float>()
                    };
                }

                c.fov = cameraJson.value("fov", 90.0f);
                c.aspectRatio = cameraJson.value("aspectRatio", 1680.0f / 960.0f);
                c.nearPlane = cameraJson.value("nearPlane", 0.1f);
                c.farPlane = cameraJson.value("farPlane", 10000.0f);
                c.smoothStep = cameraJson.value("smoothStep", false);
                c.smoothingStrength = cameraJson.value("smoothingStrength", 1.0f);
            }
        }

        if (componentsJson.contains("colliders")) {
            for (const json &colliderJson: componentsJson["colliders"]) {
                const ID ownerID = colliderJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) continue;

                Entity *entity = level.GetEntity(ownerID);
                if (entity == nullptr) continue;

                ComponentCollider &c = level.colliders.Add(ownerID);
                entity->componentsMask.set(CMP_COLLIDER);

                c.isActive = colliderJson.value("isActive", true);
                c.isTrigger = colliderJson.value("isTrigger", false);
                c.type = colliderJson.value("type", COLLIDERTYPE_SPHERE);
                c.stepSize = colliderJson.value("stepSize", c.stepSize);

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
            for (const json &rigidBodyJson: componentsJson["rigidbodies"]) {
                const ID ownerID =
                        rigidBodyJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) continue;

                Entity *entity = level.GetEntity(ownerID);
                if (entity == nullptr) continue;

                ComponentRigidbody &c = level.rigidbodies.Add(ownerID);
                entity->componentsMask.set(CMP_RIGIDBODY);

                c.isStatic = rigidBodyJson.value("isStatic", true);
                c.mass = rigidBodyJson.value("mass", 1.0f);
                c.gravityScale = rigidBodyJson.value("gravityScale", 1.0f);
                c.friction = rigidBodyJson.value("friction", 1.0f);
            }
        }
    }

    void SaveComponents(json &levelData, const Level &level) {
        json componentsJson;

        componentsJson["transforms"] = json::array();
        componentsJson["sprites"] = json::array();
        componentsJson["audioSources"] = json::array();
        componentsJson["scripts"] = json::array();
        componentsJson["uiTransforms"] = json::array();
        componentsJson["uiSprites"] = json::array();
        componentsJson["uiTexts"] = json::array();
        componentsJson["playerControllers"] = json::array();
        componentsJson["cameras"] = json::array();
        componentsJson["colliders"] = json::array();
        componentsJson["rigidbodies"] = json::array();

        for (const ComponentTransform& c : level.transforms.components) {
            componentsJson["transforms"].push_back({
                {"ownerID", c.ownerID},
                {"position", {c.position.x,c.position.y,c.position.z}},
                {"rotation", {c.rotation.x,c.rotation.y,c.rotation.z,c.rotation.w}},
                {"sectorIndex", c.sectorIndex},
                {"scale", {c.scale.x,c.scale.y,c.scale.z}}
            });
        }

        for (const ComponentScript& c : level.scripts.components) {
            componentsJson["scripts"].push_back({
                {"ownerID", c.ownerID},
                {"fileName", c.fileName},
                {"enabled", c.enabled},
                {"schemaHash", c.schemaHash},
                {"publicValues", ScriptPublicValuesToJson(c.publicValues)}
            });
        }

        for (const ComponentAudioSource& c : level.audioSources.components) {
            componentsJson["audioSources"].push_back({
                {"ownerID", c.ownerID},
                {"soundFileName", c.soundFileName},
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

        for (const ComponentSprite& c : level.sprites.components) {
            componentsJson["sprites"].push_back({
                {"ownerID", c.ownerID},
                {"textureFileNames", c.textureFileNames},
                {"sideCount", static_cast<int>(c.sideCount)},
                {"color", static_cast<std::uint32_t>(c.color)},
                {"isStatic", c.isStatic}
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
                {"texture", c.texture}
            });
        }

        for (const ComponentUIText &c: level.ui_texts.components) {
            componentsJson["uiTexts"].push_back({
                {"ownerID", c.ownerID},
                {"text", c.text}
            });
        }

        for (const ComponentPlayerController &c: level.playerControllers.components) {
            componentsJson["playerControllers"].push_back({
                {"ownerID", c.ownerID},
                {"isActive", c.isActive},
                {"velocity", {c.velocity.x, c.velocity.y, c.velocity.z}},
                {"speed", c.speed},
                {"jumpPower", c.jumpPower},
                {"runningSpeed", c.runningSpeed},
                {"eyeHeight", c.eyeHeight},
                {"friction", c.friction},
                {"sensitivityX", c.sensitivityX},
                {"sensitivityY", c.sensitivityY},
                {"minPitch", c.minPitch},
                {"maxPitch", c.maxPitch},
                {"minYaw", c.minYaw},
                {"maxYaw", c.maxYaw},
                {"noClip", c.noClip},
                {"jumpBufferMs", c.jumpBufferMs}
            });
        }

        for (const ComponentCamera &c: level.cameras.components) {
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
                {"farPlane", c.farPlane},
                {"smoothStep", c.smoothStep},
                {"smoothingStrength", c.smoothingStrength}
            });
        }

        for (const ComponentCollider &c: level.colliders.components) {
            componentsJson["colliders"].push_back({
                {"ownerID", c.ownerID},
                {"isActive", c.isActive},
                {"isTrigger", c.isTrigger},
                {"type", c.type},
                {"scale", {c.scale.x, c.scale.y, c.scale.z}},
                {"stepSize", c.stepSize}
            });
        }

        for (const ComponentRigidbody &c: level.rigidbodies.components) {
            componentsJson["rigidbodies"].push_back({
                {"ownerID", c.ownerID},
                {"isStatic", c.isStatic},
                {"mass", c.mass},
                {"gravityScale", c.gravityScale},
                {"friction", c.friction}
            });
        }

        levelData["components"] = componentsJson;
    }

#ifndef TILKY_STANDALONE
    void LoadMetadata(const json& levelData, Level& level) {
        if (levelData.contains("metadata")) {
            const json& metadatajson = levelData["metadata"];

            if (metadatajson.contains("editorCamPos") && metadatajson["editorCamPos"].is_array()) {
                level.editorCamPos.x = metadatajson["editorCamPos"][0];
                level.editorCamPos.y = metadatajson["editorCamPos"][1];
            }

            if (metadatajson.contains("runtimeCamPos") && metadatajson["runtimeCamPos"].is_array()) {
                level.runtimeCamPos.x = metadatajson["runtimeCamPos"][0];
                level.runtimeCamPos.y = metadatajson["runtimeCamPos"][1];
                level.runtimeCamPos.z = metadatajson["runtimeCamPos"][2];
            }

            if (metadatajson.contains("runtimeCamRot") && metadatajson["runtimeCamRot"].is_array()) {
                level.runtimeCamRot.x = metadatajson["runtimeCamRot"][0];
                level.runtimeCamRot.y = metadatajson["runtimeCamRot"][1];
            }
        }
    }

    void SaveMetadata(json &levelData, const Level &level) {
        json metadatajson;

        metadatajson["editorCamPos"] = {
            level.editorCamPos.x,
            level.editorCamPos.y,
        };

        metadatajson["runtimeCamPos"] = {
            level.runtimeCamPos.x,
            level.runtimeCamPos.y,
            level.runtimeCamPos.z,
        };

        metadatajson["runtimeCamRot"] = {
            level.runtimeCamRot.x,
            level.runtimeCamRot.y,
        };

        levelData["metadata"] = metadatajson;
    }
#endif
}

namespace LevelSerialization {
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

    bool LoadLevelFromFile(const fs::path& levelFile, Level& outLevel, LevelExtraData* outExtraData,std::string* errorMessage) {
        if (!fs::exists(levelFile)) {
            SetError(
                errorMessage,
                "Level file does not exist: " + levelFile.string()
            );
            return false;
        }

        std::ifstream file(levelFile, std::ios::binary);

        if (!file.is_open()) {
            SetError(
                errorMessage,
                "Could not open BSON level file: " + levelFile.string()
            );
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
            SetError(
                errorMessage,
                "Failed to parse BSON level file " + levelFile.string() +
                ": " + e.what()
            );
            return false;
        }

        const std::string cleanName = CleanLevelName(levelFile.stem().string());

        Level loadedLevel;
        loadedLevel.id = levelData.value("id", 0);
        loadedLevel.name = levelData.value("name", cleanName);
        loadedLevel.nextEntityID = levelData.value("nextEntityID", 1);

        try {
            LoadExtraData(levelData, outExtraData);

            LoadEntities(levelData, loadedLevel);
            LoadWalls(levelData, loadedLevel);
            LoadSectors(levelData, loadedLevel);
            LoadComponents(levelData, loadedLevel);
            LoadTextures(levelData, loadedLevel);
            LoadSounds(levelData, loadedLevel);
            LoadLevelStats(levelData, loadedLevel);

#ifndef TILKY_STANDALONE
            LoadMetadata(levelData, loadedLevel);
#endif
        }
        catch (const nlohmann::json::exception& e) {
            SetError(
                errorMessage,
                "Level BSON schema error while loading " +
                levelFile.string() + ": " + e.what()
            );
            return false;
        }
        catch (const std::exception& e) {
            SetError(
                errorMessage,
                "Unexpected error while loading level " +
                levelFile.string() + ": " + e.what()
            );
            return false;
        }

        RebuildNextEntityID(loadedLevel);
        MapQueries::RebuildSectorRuntimeLinks(loadedLevel);

        outLevel = std::move(loadedLevel);

        return true;
    }

    bool SaveLevelToFile(const fs::path& levelFile, const Level& level, const LevelExtraData* extraData, std::string* errorMessage) {
        json levelData;

        levelData["id"] = level.id;
        levelData["name"] = level.name;
        levelData["nextEntityID"] = level.nextEntityID;

        SaveExtraData(levelData, extraData);
        SaveLevelStats(levelData, level);
        SaveEntities(levelData, level);
        SaveComponents(levelData, level);
        SaveWalls(levelData, level);
        SaveSectors(levelData, level);
        SaveTextures(levelData, level);
        SaveSounds(levelData, level);

#ifndef TILKY_STANDALONE
        SaveMetadata(levelData, level);
#endif
        try {fs::create_directories(levelFile.parent_path());}
        catch (const json::exception& e) {
            SetError(
                errorMessage,
                "Failed to create level folder " +
                levelFile.parent_path().string() + ": " + e.what()
            );
            return false;
        }

        const std::vector<std::uint8_t> bsonData = json::to_bson(levelData);

        std::ofstream file(levelFile, std::ios::binary);

        if (!file.is_open()) {
            SetError(errorMessage,"Failed to open BSON level for saving: " + levelFile.string());
            return false;
        }

        file.write(
            reinterpret_cast<const char*>(bsonData.data()),
            static_cast<std::streamsize>(bsonData.size())
        );

        file.close();

        return true;
    }
}
