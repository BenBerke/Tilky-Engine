#include "Headers/Map/LevelSerialization.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

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
        if (outExtraData == nullptr) {
            return;
        }

        *outExtraData = LevelSerialization::LevelExtraData{};

        if (!levelData.contains("levelVars") ||
            !levelData["levelVars"].is_object()) {
            return;
        }

        outExtraData->backgroundTextureIndex =
            levelData["levelVars"].value("backgroundTextureIndex", -1);
    }

    void SaveExtraData(json& levelData, const LevelSerialization::LevelExtraData* extraData) {
        if (extraData == nullptr) {
            return;
        }

        levelData["levelVars"] = {
            {"backgroundTextureIndex", extraData->backgroundTextureIndex}
        };
    }

    void LoadLevelStats(const json &levelData, Level &level) {
        if (!levelData.contains("levelStats") ||
            !levelData["levelStats"].is_object()) {
            return;
        }

    const json& levelStatsJson = levelData["levelStats"];

    if (levelStatsJson.contains("listenerSettings") &&
        levelStatsJson["listenerSettings"].is_object()) {
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

        if (levelStatsJson.contains("worldSettings") &&
            levelStatsJson["worldSettings"].is_object()) {
            const json &worldSettingsJson = levelStatsJson["worldSettings"];
            WorldSettings &worldSettings = level.worldSettings;

            worldSettings.gravity = worldSettingsJson.value("gravity", 9.8f);
        }
    }

    void SaveLevelStats(json &levelData, const Level &level) {
        const ListenerSettings &listenerSettings = level.listenerSettings;
        const WorldSettings &worldSettings = level.worldSettings;

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
            if (sound.fileName.empty()) {
                continue;
            }

            levelData["sounds"].push_back({
                {"fileName", sound.fileName}
            });
        }
    }


    void LoadWalls(const json &levelData, Level &level) {
        level.walls.clear();

        if (!levelData.contains("walls")) {
            return;
        }

        ID highestWallID = 0;
        std::unordered_set<ID> seenWallIDs;

        for (int i = 0; i < static_cast<int>(levelData["walls"].size()); ++i) {
            const json &wallJson = levelData["walls"][i];

            Vector2 start = {
                wallJson["start"][0].get<float>(),
                wallJson["start"][1].get<float>()
            };

            Vector2 end = {
                wallJson["end"][0].get<float>(),
                wallJson["end"][1].get<float>()
            };

            Vector4 color = {255.0f, 255.0f, 255.0f, 255.0f};

            if (wallJson.contains("color")) {
                color = {
                    wallJson["color"][0].get<float>(),
                    wallJson["color"][1].get<float>(),
                    wallJson["color"][2].get<float>(),
                    wallJson["color"][3].get<float>()
                };
            }

            Wall wall(
                start,
                end,
                color,
                LoadIDField(wallJson, "frontSector", INVALID_ID),
                LoadIDField(wallJson, "backSector", INVALID_ID),
                wallJson.value("textureIndex", -1)
            );

            wall.id = LoadIDField(wallJson, "id", static_cast<ID>(i));

            if (wall.id == INVALID_ID) wall.id = static_cast<ID>(i);

            // Guard against a duplicate id. A mixed-format save - some entries with
            // an explicit "id", some without (partial save, manual edit, objects
            // copy-pasted in from another level file) - can make the index-based
            // fallback above collide with a legitimately saved id. If that happens
            // silently, RebuildWallIDLookup()'s map can only ever resolve ONE of the
            // two walls sharing that id; the other stays in level.walls but becomes
            // invisible to GetWallByID/DeleteWall. Surface it and reassign instead
            // of colliding quietly.
            if (seenWallIDs.contains(wall.id)) {
                const ID reassigned = highestWallID + 1;
                spdlog::warn(
                    "LoadWalls: duplicate wall id {} at array index {} - reassigning to {}",
                    wall.id, i, reassigned
                );
                wall.id = reassigned;
            }

            seenWallIDs.insert(wall.id);
            highestWallID = std::max(highestWallID, wall.id);

            level.walls.push_back(wall);
        }

        level.nextWallID = std::max(level.nextWallID, highestWallID + 1);
    }

    void SaveWalls(json &levelData, const Level &level) {
        levelData["walls"] = json::array();

        for (const Wall &wall: level.walls) {
            levelData["walls"].push_back({
                {"id", wall.id},
                {"start", {wall.start.x, wall.start.y}},
                {"end", {wall.end.x, wall.end.y}},
                {"color", {wall.color.x, wall.color.y, wall.color.z, wall.color.w}},
                {"textureIndex", wall.textureIndex},
                {"frontSector", wall.frontSector},
                {"backSector", wall.backSector},
            });
        }
    }

    void LoadSectors(const json &levelData, Level &level) {
        level.sectors.clear();

        if (!levelData.contains("sectors")) return;

        ID highestSectorID = 0;
        std::unordered_set<ID> seenSectorIDs;

        for (int i = 0; i < static_cast<int>(levelData["sectors"].size()); ++i) {
            const json &sectorJson = levelData["sectors"][i];

            Sector sector;
            std::vector<Vector2> corners;

            sector.id = LoadIDField(sectorJson, "id", static_cast<ID>(i));

            if (sector.id == INVALID_ID) sector.id = static_cast<ID>(i);

            // Same guard as LoadWalls() above - see the comment there.
            if (seenSectorIDs.contains(sector.id)) {
                const ID reassigned = highestSectorID + 1;
                spdlog::warn(
                    "LoadSectors: duplicate sector id {} at array index {} - reassigning to {}",
                    sector.id, i, reassigned
                );
                sector.id = reassigned;
            }

            seenSectorIDs.insert(sector.id);
            highestSectorID = std::max(highestSectorID, sector.id);

            for (const json &cornerJson: sectorJson["corners"]) {
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

            sector.lightValue = sectorJson.value("lightValue", 255.0f);

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

            sector.floorTextureIndex = sectorJson.value("floorTextureIndex", -1);

            // New format.
            sector.ceilingTextureIndex = sectorJson.value("ceilingTextureIndex", -1);

            // Backward compatibility for old saves that used ceilingTextureIndices.
            if (sector.ceilingTextureIndex == -1 && sectorJson.contains("ceilingTextureIndices")) {
                const json &oldCeilingTextureArray = sectorJson["ceilingTextureIndices"];

                if (!oldCeilingTextureArray.empty()) {
                    sector.ceilingTextureIndex = oldCeilingTextureArray[0].get<int>();
                }
            }

            level.sectors.push_back(sector);
        }

        level.nextSectorID = std::max(level.nextSectorID, highestSectorID + 1);
    }

    void SaveSectors(json &levelData, const Level &level) {
        levelData["sectors"] = json::array();

        for (const Sector &sector: level.sectors) {
            json cornerArray = json::array();

            for (const Vector2 &point: sector.vertices) {
                cornerArray.push_back({
                    {"x", point.x},
                    {"y", point.y}
                });
            }

            levelData["sectors"].push_back({
                {"id", sector.id},
                {"corners", cornerArray},
                {"ceilingHeight", sector.ceilingHeight},
                {"floorHeight", sector.floorHeight},
                {
                    "ceilingColor", {
                        sector.ceilingColor.x,
                        sector.ceilingColor.y,
                        sector.ceilingColor.z
                    }
                },
                {
                    "floorColor", {
                        sector.floorColor.x,
                        sector.floorColor.y,
                        sector.floorColor.z
                    }
                },
                {"floorTextureIndex", sector.floorTextureIndex},
                {"ceilingTextureIndex", sector.ceilingTextureIndex},
                {"lightValue", sector.lightValue},
            });
        }
    }

    void LoadComponents(const json &levelData, Level &level) {
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

        if (!levelData.contains("components")) return;

        const json &componentsJson = levelData["components"];

        if (componentsJson.contains("transforms")) {
            for (const json &transformJson: componentsJson["transforms"]) {
                const ID ownerID = transformJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) continue;

                Entity *entity = level.GetEntity(ownerID);
                if (entity == nullptr) continue;

                ComponentTransform &c = level.transforms.Add(ownerID);
                entity->componentsMask.set(CMP_TRANSFORM);

                if (transformJson.contains("position")) {
                    c.position = {
                        transformJson["position"][0].get<float>(),
                        transformJson["position"][1].get<float>(),
                        transformJson["position"][2].get<float>()
                    };
                }

                c.sectorIndex = transformJson.value("sectorIndex", -1);

                if (transformJson.contains("scale")) {
                    c.scale = {
                        transformJson["scale"][0].get<float>(),
                        transformJson["scale"][1].get<float>(),
                        transformJson["scale"][2].get<float>()
                    };
                }
            }
        }

        if (componentsJson.contains("sprites")) {
            for (const json &spriteJson: componentsJson["sprites"]) {
                const ID ownerID = spriteJson.at("ownerID").get<ID>();

                Entity *entity = level.GetEntity(ownerID);
                if (entity == nullptr) continue;

                ComponentSprite &c = level.sprites.Add(ownerID);
                entity->componentsMask.set(CMP_SPRITE);

                const json &textureIndicesJson = spriteJson.at("textureIndices");

                for (size_t i = 0; i < c.textureIndices.size(); i++)
                    c.textureIndices[i] = textureIndicesJson.at(i).get<int>();

                const int sideCountValue = spriteJson.at("sideCount").get<int>();

                if (sideCountValue < SIDECOUNT_SINGLE || sideCountValue > SIDECOUNT_90) continue;

                c.sideCount = static_cast<SideCount>(sideCountValue);
            }
        }

        if (componentsJson.contains("decals")) {
            for (const json &decalJson: componentsJson["decals"]) {
                const ID ownerID = decalJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) continue;

                Entity *entity = level.GetEntity(ownerID);
                if (entity == nullptr) continue;

                ComponentDecal &c = level.decals.Add(ownerID);
                entity->componentsMask.set(CMP_DECAL);

                c.wallIndex = decalJson.value("wallIndex", -1);
                c.type = decalJson.value("type", WALL);
                c.verticalPos = decalJson.value("verticalPos", 0.0f);
                c.horizontalPos = decalJson.value("horizontalPos", -1.0f);
                c.wallNormalOffset = decalJson.value("wallNormalOffset", 0.0f);
                c.wallT = decalJson.value("wallT", 0.5f);
                c.baseHeight = decalJson.value("baseHeight", 0.0f);
                c.absHeight = decalJson.value("absHeight", false);
            }
        }

        if (componentsJson.contains("audioSources")) {
            for (const json &audioSourceJson: componentsJson["audioSources"]) {
                const ID ownerID = audioSourceJson.value("ownerID", INVALID_ENTITY_ID);

                if (ownerID == INVALID_ENTITY_ID) continue;

                Entity *entity = level.GetEntity(ownerID);
                if (entity == nullptr) continue;

                ComponentAudioSource &c = level.audioSources.Add(ownerID);
                entity->componentsMask.set(CMP_AUDIO_SOURCE);

                c.soundIndex = audioSourceJson.value("soundIndex", -1);
                c.pitch = audioSourceJson.value("pitch", 1.0f);
                c.gain = audioSourceJson.value("gain", 1.0f);
                c.looping = audioSourceJson.value("looping", false);
                c.playOnStart = audioSourceJson.value("playOnStart", true);

                c.referenceDistance = audioSourceJson.value("referenceDistance", 1.0f);
                c.maxDistance = audioSourceJson.value("maxDistance", 10000.0f);
                c.rollOffFactor = audioSourceJson.value("rollOffFactor", 1.0f);

                c.innerConeAngle = audioSourceJson.value("innerConeAngle", 360.0f);
                c.outerConeAngle = audioSourceJson.value("outerConeAngle", 360.0f);
                c.outerGain = audioSourceJson.value("outerGain", 0.0f);

                c.name = "entity_" + std::to_string(ownerID) + "_audio";
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

                c.textureIndex = spriteJson.value("textureIndex", -1);
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
                c.aspectRatio =
                        cameraJson.value("aspectRatio", 1680.0f / 960.0f);
                c.nearPlane = cameraJson.value("nearPlane", 0.1f);
                c.farPlane = cameraJson.value("farPlane", 10000.0f);
            }
        }

        if (componentsJson.contains("colliders")) {
            for (const json &colliderJson: componentsJson["colliders"]) {
                const ID ownerID =
                        colliderJson.value("ownerID", INVALID_ENTITY_ID);

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

                if (ownerID == INVALID_ENTITY_ID) {
                    continue;
                }

                Entity *entity = level.GetEntity(ownerID);
                if (entity == nullptr) {
                    continue;
                }

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
        componentsJson["decals"] = json::array();
        componentsJson["audioSources"] = json::array();
        componentsJson["scripts"] = json::array();
        componentsJson["uiTransforms"] = json::array();
        componentsJson["uiSprites"] = json::array();
        componentsJson["uiTexts"] = json::array();
        componentsJson["playerControllers"] = json::array();
        componentsJson["cameras"] = json::array();
        componentsJson["colliders"] = json::array();
        componentsJson["rigidbodies"] = json::array();

        for (const ComponentTransform &c: level.transforms.components) {
            componentsJson["transforms"].push_back({
                {"ownerID", c.ownerID},
                {"position", {c.position.x, c.position.y, c.position.z}},
                {"sectorIndex", c.sectorIndex},
                {"scale", {c.scale.x, c.scale.y, c.scale.z}}
            });
        }

        for (const ComponentSprite &c: level.sprites.components) {
            componentsJson["sprites"].push_back({
                {"ownerID", c.ownerID},
                {"textureIndices", c.textureIndices},
                {"sideCount", static_cast<int>(c.sideCount)}
            });
        }

        for (const ComponentDecal &c: level.decals.components) {
            componentsJson["decals"].push_back({
                {"ownerID", c.ownerID},
                {"wallIndex", c.wallIndex},
                {"type", c.type},
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
                {"fileName", fs::path(c.fileName).stem().string()},
                {"enabled", c.enabled},
                {"publicValues", ScriptPublicValuesToJson(c.publicValues)},
                {"schemaHash", c.schemaHash}
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
                {"textureIndex", c.textureIndex}
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
                {"farPlane", c.farPlane}
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
        RebuildNextEntityID(loadedLevel);
        MapQueries::RebuildSectorRuntimeLinks(loadedLevel);

        outLevel = std::move(loadedLevel);

        return true;
    }

    bool SaveLevelToFile(
        const fs::path& levelFile,
        const Level& level,
        const LevelExtraData* extraData,
        std::string* errorMessage
    ) {
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

        try {
            fs::create_directories(levelFile.parent_path());
        }
        catch (const std::exception& e) {
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
            SetError(
                errorMessage,
                "Failed to open BSON level for saving: " + levelFile.string()
            );
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
