#include "Headers/Map/LevelSerialization.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <unordered_set>
#include <vector>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "Headers/Math/Geometry/Geometry.hpp"
#include "Headers/Objects/Loadables.hpp"
#include "Headers/Objects/Prefab.hpp"
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
            case ScriptValueType::Vector2: return "Vector2";
            case ScriptValueType::Vector3: return "Vector3";
            case ScriptValueType::Vector4: return "Vector4";
            case ScriptValueType::Enum: return "Enum";
            case ScriptValueType::Entity: return "Entity";
            case ScriptValueType::Component: return "Component";
            case ScriptValueType::Behaviour: return "Behaviour";
            case ScriptValueType::Asset: return "Asset";
            case ScriptValueType::Wall: return "Wall";
            case ScriptValueType::Sector: return "Sector";
        }

        return "Unknown";
    }

    ScriptValueType ScriptValueTypeFromString(const std::string &type) {
        if (type == "Int") return ScriptValueType::Int;
        if (type == "Float") return ScriptValueType::Float;
        if (type == "Bool") return ScriptValueType::Bool;
        if (type == "String") return ScriptValueType::String;
        if (type == "Vector2") return ScriptValueType::Vector2;
        if (type == "Vector3") return ScriptValueType::Vector3;
        if (type == "Vector4") return ScriptValueType::Vector4;
        if (type == "Enum") return ScriptValueType::Enum;
        if (type == "Entity") return ScriptValueType::Entity;
        if (type == "Component") return ScriptValueType::Component;
        if (type == "Behaviour") return ScriptValueType::Behaviour;
        if (type == "Asset") return ScriptValueType::Asset;
        if (type == "Wall") return ScriptValueType::Wall;
        if (type == "Sector") return ScriptValueType::Sector;

        return ScriptValueType::String;
    }

    // Serializes one field's runtime ScriptValue. Reference kinds
    // (Entity/Component/Behaviour) store only stable IDs - never a name,
    // never a pointer - so they resolve correctly even after the target
    // entity/script is renamed, and safely fail to resolve (nil in Lua, "None"
    // in the inspector) once the target no longer exists.
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
                } else if constexpr (std::is_same_v<T, Vector2>) {
                    valueJson["type"] = "Vector2";
                    valueJson["value"] = {typedValue.x, typedValue.y};
                } else if constexpr (std::is_same_v<T, Vector3>) {
                    valueJson["type"] = "Vector3";
                    valueJson["value"] = {typedValue.x, typedValue.y, typedValue.z};
                } else if constexpr (std::is_same_v<T, Vector4>) {
                    valueJson["type"] = "Vector4";
                    valueJson["value"] = {typedValue.x, typedValue.y, typedValue.z, typedValue.w};
                } else if constexpr (std::is_same_v<T, EntityRefValue>) {
                    valueJson["type"] = "Entity";
                    valueJson["entityId"] = typedValue.entityId;
                    if (!typedValue.prefab.empty()) valueJson["prefab"] = typedValue.prefab;
                } else if constexpr (std::is_same_v<T, ComponentRefValue>) {
                    valueJson["type"] = "Component";
                    valueJson["entityId"] = typedValue.entityId;
                    valueJson["componentType"] = typedValue.componentType;
                    valueJson["instanceId"] = typedValue.instanceId;
                } else if constexpr (std::is_same_v<T, BehaviourRefValue>) {
                    valueJson["type"] = "Behaviour";
                    valueJson["entityId"] = typedValue.entityId;
                    valueJson["instanceId"] = typedValue.instanceId;
                } else if constexpr (std::is_same_v<T, AssetRefValue>) {
                    valueJson["type"] = "Asset";
                    valueJson["path"] = typedValue.path;
                } else if constexpr (std::is_same_v<T, WallRefValue>) {
                    valueJson["type"] = "Wall";
                    valueJson["wallId"] = typedValue.wallId;
                } else if constexpr (std::is_same_v<T, SectorRefValue>) {
                    valueJson["type"] = "Sector";
                    valueJson["sectorId"] = typedValue.sectorId;
                }
            },
            value
        );

        return valueJson;
    }

    // nlohmann::json's value(key, default) overload is for object keys, not
    // array indices, so Vector2/3/4 fields (stored as plain JSON arrays, same
    // convention as ComponentTransform::position elsewhere in this file) are
    // read back through this small bounds-checked helper instead.
    float JsonArrayFloat(const json &array, const std::size_t index) {
        if (!array.is_array() || index >= array.size()) return 0.0f;
        return array[index].get<float>();
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

            // Enum fields are always stored as a plain int - the option
            // name<->value table lives in the script's schema, not in
            // serialized data.
            case ScriptValueType::Enum:
                return valueJson.value("value", 0);

            case ScriptValueType::Vector2: {
                const json &v = valueJson["value"];
                return Vector2{JsonArrayFloat(v, 0), JsonArrayFloat(v, 1)};
            }

            case ScriptValueType::Vector3: {
                const json &v = valueJson["value"];
                return Vector3{JsonArrayFloat(v, 0), JsonArrayFloat(v, 1), JsonArrayFloat(v, 2)};
            }

            case ScriptValueType::Vector4: {
                const json &v = valueJson["value"];
                return Vector4{JsonArrayFloat(v, 0), JsonArrayFloat(v, 1), JsonArrayFloat(v, 2), JsonArrayFloat(v, 3)};
            }

            case ScriptValueType::Entity:
                return EntityRefValue{valueJson.value("entityId", INVALID_ID), valueJson.value("prefab", std::string{})};

            case ScriptValueType::Component: {
                // References saved before components had instance IDs name
                // only (entity, type); they load empty and need reassigning.
                if (!valueJson.contains("instanceId")) return ComponentRefValue{};
                return ComponentRefValue{
                    valueJson.value("entityId", INVALID_ID),
                    valueJson.value("componentType", -1),
                    valueJson.value("instanceId", INVALID_COMPONENT_INSTANCE_ID)
                };
            }

            case ScriptValueType::Behaviour:
                return BehaviourRefValue{
                    valueJson.value("entityId", INVALID_ID),
                    valueJson.value("instanceId", INVALID_SCRIPT_INSTANCE_ID)
                };

            case ScriptValueType::Asset:
                return AssetRefValue{valueJson.value("path", std::string{})};

            case ScriptValueType::Wall:
                return WallRefValue{valueJson.value("wallId", INVALID_ID)};

            case ScriptValueType::Sector:
                return SectorRefValue{valueJson.value("sectorId", INVALID_ID)};
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

    // The owner-agnostic part of one attached script. Shared by entity
    // scripts ("components"/"scripts", which add "ownerID") and sector
    // scripts ("sectors"[i]/"scripts", owned by the enclosing sector object).
    json ScriptAttachmentToJson(const ScriptAttachmentData &script) {
        return json{
            {"instanceID", script.instanceID},
            {"fileName", script.fileName},
            {"enabled", script.enabled},
            {"schemaHash", script.schemaHash},
            {"publicValues", ScriptPublicValuesToJson(script.publicValues)}
        };
    }

    // Loads everything ScriptAttachmentToJson writes except instanceID and
    // fileName, which the two callers treat differently.
    void ScriptAttachmentSettingsFromJson(const json &scriptJson, ScriptAttachmentData &script) {
        script.enabled = scriptJson.value("enabled", true);
        script.schemaHash = scriptJson.value("schemaHash", std::uint64_t{0});

        if (scriptJson.contains("publicValues"))
            script.publicValues = ScriptPublicValuesFromJson(scriptJson["publicValues"]);
        else script.publicValues.clear();
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

    // Loads the "tags"/"tagIds" parallel-array format shared by sectors,
    // walls and entities into outTags/outTagIds. Missing fields produce
    // empty vectors. A length mismatch discards both sides (logged) rather
    // than risk an index existing in one vector but not the other;
    // malformed individual entries (wrong JSON type) are skipped and
    // logged too, keeping whatever parsed cleanly.
    void LoadTagArrays(const json& objectJson, const char* kind, const ID objectId,
                        std::vector<std::string>& outTags, std::vector<uint16_t>& outTagIds) {
        outTags.clear();
        outTagIds.clear();

        if (!objectJson.contains("tags") && !objectJson.contains("tagIds")) return;

        std::vector<std::string> loadedTags;
        std::vector<uint16_t> loadedTagIds;
        bool malformedEntry = false;

        if (objectJson.contains("tags")) {
            if (objectJson.at("tags").is_array()) {
                for (const json& tagJson : objectJson.at("tags")) {
                    if (tagJson.is_string()) loadedTags.push_back(tagJson.get<std::string>());
                    else malformedEntry = true;
                }
            } else malformedEntry = true;
        }

        if (objectJson.contains("tagIds")) {
            if (objectJson.at("tagIds").is_array()) {
                for (const json& tagIdJson : objectJson.at("tagIds")) {
                    if (tagIdJson.is_number_unsigned() || tagIdJson.is_number_integer())
                        loadedTagIds.push_back(tagIdJson.get<uint16_t>());
                    else malformedEntry = true;
                }
            } else malformedEntry = true;
        }

        if (loadedTags.size() != loadedTagIds.size()) {
            spdlog::warn(
                "{} {}: tag name/id array length mismatch ({} vs {}) - tags discarded",
                kind, objectId, loadedTags.size(), loadedTagIds.size()
            );
            return;
        }

        if (malformedEntry)
            spdlog::warn("{} {}: malformed tag data - invalid entries were skipped", kind, objectId);

        outTags = std::move(loadedTags);
        outTagIds = std::move(loadedTagIds);
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
    }

    void SaveExtraData(json &levelData, const LevelSerialization::LevelExtraData *extraData) {
        if (extraData == nullptr) return;

        levelData["levelVars"] = json::object();
    }

    // Same [x, y, z] array form sector lights use.
    Vector3 LoadSkyColor(const json &skyJson, const char *field, const Vector3 &defaultValue) {
        if (!skyJson.contains(field)) return defaultValue;

        const json &vectorJson = skyJson.at(field);
        if (!vectorJson.is_array() || vectorJson.size() != 3) return defaultValue;

        return {vectorJson[0].get<float>(), vectorJson[1].get<float>(), vectorJson[2].get<float>()};
    }

    void LoadSky(const json &skyJson, SkySettings &sky) {
        sky = SkySettings{};

        if (const std::optional<SkyMode> mode = FindSkyModeByName(skyJson.value("mode", std::string())))
            sky.mode = *mode;

        sky.texture = skyJson.value("texture", std::string());

        if (skyJson.contains("cubemapFaces") && skyJson["cubemapFaces"].is_object()) {
            const json &facesJson = skyJson["cubemapFaces"];
            for (const SkyFaceInfo &face : SKY_FACES)
                sky.Face(face.face) = facesJson.value(face.name, std::string());
        }

        sky.rotation = skyJson.value("rotation", 0.0f);
        sky.rotationSpeed = skyJson.value("rotationSpeed", 0.0f);
        sky.horizonOffset = skyJson.value("horizonOffset", 0.0f);

        sky.tint = LoadSkyColor(skyJson, "tint", sky.tint);
        sky.fallbackColor = LoadSkyColor(skyJson, "fallbackColor", sky.fallbackColor);
    }

    json SaveSky(const SkySettings &sky) {
        json facesJson = json::object();
        for (const SkyFaceInfo &face : SKY_FACES) facesJson[face.name] = sky.Face(face.face);

        const SkyModeInfo *mode = FindSkyMode(sky.mode);

        return {
            {"mode", mode != nullptr ? mode->name : SKY_MODES[0].name},
            {"texture", sky.texture},
            {"cubemapFaces", facesJson},
            {"rotation", sky.rotation},
            {"rotationSpeed", sky.rotationSpeed},
            {"horizonOffset", sky.horizonOffset},
            {"tint", {sky.tint.x, sky.tint.y, sky.tint.z}},
            {"fallbackColor", {sky.fallbackColor.x, sky.fallbackColor.y, sky.fallbackColor.z}}
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
            listenerSettings.dopplerFactor = listenerJson.value("dopplerFactor", 0.0f);
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
            rendererSettings.wallContrast = std::clamp(
                rendererSettingsJson.value("wallContrast", RendererSettings{}.wallContrast),
                0.0f,
                MAX_WALL_CONTRAST
            );
        }

        if (levelStatsJson.contains("sky") && levelStatsJson["sky"].is_object()) LoadSky(levelStatsJson["sky"], level.sky);
        else level.sky = SkySettings{};
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
                    {"textureSetting", rendererSettings.textureSetting},
                    {"wallContrast", rendererSettings.wallContrast}
                }
            },
            {"sky", SaveSky(level.sky)}
        };
    }

    // An instance's link to its .prefab, saved on the entity as "prefab".
    json PrefabLinkToJson(const PrefabLink& link) {
        json components = json::array();
        for (const PrefabComponentLink& c : link.components)
            components.push_back({c.componentType, c.instanceID, c.prefabInstanceID});

        json removed = json::array();
        for (const PrefabRemovedComponent& r : link.removed)
            removed.push_back({r.componentType, r.prefabInstanceID});

        return {
            {"path", link.prefab},
            {"components", components},
            {"removed", removed},
            {"overrides", link.overrides}
        };
    }

    PrefabLink PrefabLinkFromJson(const json& linkJson) {
        PrefabLink link;
        link.prefab = linkJson.value("path", std::string{});

        if (linkJson.contains("components"))
            for (const json& c : linkJson["components"])
                link.components.push_back({c[0].get<int>(), c[1].get<std::uint64_t>(), c[2].get<std::uint64_t>()});

        if (linkJson.contains("removed"))
            for (const json& r : linkJson["removed"])
                link.removed.push_back({r[0].get<int>(), r[1].get<std::uint64_t>()});

        link.overrides = linkJson.value("overrides", std::vector<std::string>{});
        return link;
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
            entity.enabled = entityJson.value("enabled", true);
            entity.attachedLevelId = level.id;

            LoadTagArrays(entityJson, "Entity", entity.id, entity.tags, entity.tagIds);

            if (entityJson.contains("prefab")) entity.prefabLink = PrefabLinkFromJson(entityJson["prefab"]);

            highestEntityID = std::max(highestEntityID, entity.id);
            level.entities.push_back(entity);
        }

        level.nextEntityID = highestEntityID + 1;
    }

    void SaveEntities(json& levelData, const std::vector<const Entity*>& entities) {
        levelData["entities"] = json::array();

        for (const Entity* entity : entities) {
            json entityJson = {
                {"id", entity->id},
                {"name", entity->name},
                {"enabled", entity->enabled},
                {"tags", entity->tags},
                {"tagIds", entity->tagIds}
            };

            if (entity->prefabLink.IsInstance()) entityJson["prefab"] = PrefabLinkToJson(entity->prefabLink);

            levelData["entities"].push_back(std::move(entityJson));
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

    void SaveSounds(json &levelData, const Level &level) {
        levelData["sounds"] = json::array();

        for (const Sound &sound: level.sounds) {
            if (sound.fileName.empty()) continue;

            levelData["sounds"].push_back({{"fileName", sound.fileName}});
        }
    }

    void LoadWallSurface(const json &surfaceJson, WallSurface &surface) {
        if (!surfaceJson.is_object()) return;

        surface.texture = surfaceJson.value("texture", std::string{});

        if (surfaceJson.contains("textureOffset") &&
            surfaceJson.at("textureOffset").is_array() && surfaceJson.at("textureOffset").size() >= 2) {
            surface.textureOffset = {
                surfaceJson.at("textureOffset").at(0).get<float>(),
                surfaceJson.at("textureOffset").at(1).get<float>()
            };
        }

        if (surfaceJson.contains("textureScale") &&
            surfaceJson.at("textureScale").is_array() && surfaceJson.at("textureScale").size() >= 2) {
            surface.textureScale = {
                surfaceJson.at("textureScale").at(0).get<float>(),
                surfaceJson.at("textureScale").at(1).get<float>()
            };
        }

        surface.flipTextureX = surfaceJson.value("flipX", false);
        surface.flipTextureY = surfaceJson.value("flipY", false);

        const std::string anchorName = surfaceJson.value("anchor", std::string{WALL_TEXTURE_ANCHORS[0].name});

        if (const std::optional<WallTextureAnchor> anchor = FindWallTextureAnchorByName(anchorName)) surface.anchor = *anchor;
        else spdlog::warn("Unknown wall texture anchor '{}', using {}", anchorName, WALL_TEXTURE_ANCHORS[0].name);
    }

    void LoadWalls(const json &levelData, Level &level) {
        level.walls.clear();

        if (!levelData.contains("walls") || !levelData.at("walls").is_array()) return;

        ID highestWallID = 0;
        std::unordered_set<ID> seenWallIDs;

        const json &wallsJson = levelData.at("walls");

        for (int i = 0; i < static_cast<int>(wallsJson.size()); ++i) {
            const json &wallJson = wallsJson.at(i);

            const Vector2 start = {
                wallJson.at("start").at(0).get<float>(),
                wallJson.at("start").at(1).get<float>()
            };

            const Vector2 end = {
                wallJson.at("end").at(0).get<float>(),
                wallJson.at("end").at(1).get<float>()
            };

            Vector4 color = {1.0f, 1.0f, 1.0f,1.0f};

            if (wallJson.contains("color")) {
                const json &colorJson = wallJson.at("color");

                if (colorJson.is_array() && colorJson.size() >= 4) {
                    color = {
                        colorJson.at(0).get<float>(),
                        colorJson.at(1).get<float>(),
                        colorJson.at(2).get<float>(),
                        colorJson.at(3).get<float>()
                    };
                }
            }

            Wall wall(
                start,
                end,
                color,
                LoadIDField(wallJson, "frontSector", INVALID_ID),
                LoadIDField(wallJson, "backSector", INVALID_ID)
            );

            if (wallJson.contains("top")) LoadWallSurface(wallJson.at("top"), wall.top);
            if (wallJson.contains("bottom")) LoadWallSurface(wallJson.at("bottom"), wall.bottom);
            if (wallJson.contains("middle")) LoadWallSurface(wallJson.at("middle"), wall.middle);
            wall.middleHasCollision = wallJson.value("middleHasCollision", wall.middleHasCollision);
            wall.middleIgnoredByRaycast = wallJson.value("middleIgnoredByRaycast", wall.middleIgnoredByRaycast);
            wall.angle = wallJson.value("angle", 0.0f);

            wall.id = LoadIDField(wallJson,"id",static_cast<ID>(i));

            if (wall.id == INVALID_ID) wall.id = static_cast<ID>(i);

            if (seenWallIDs.contains(wall.id)) {
                const ID reassigned = highestWallID + 1;

                spdlog::warn(
                    "LoadWalls: duplicate wall id {} at array index {} - "
                    "reassigning to {}",
                    wall.id,
                    i,
                    reassigned
                );

                wall.id = reassigned;
            }

            seenWallIDs.insert(wall.id);
            highestWallID = std::max(highestWallID, wall.id);

            wall.name = wallJson.value("name", std::string{});

            LoadTagArrays(wallJson, "Wall", wall.id, wall.tags, wall.tagIds);

            level.walls.push_back(std::move(wall));
        }

        level.nextWallID = std::max(level.nextWallID,highestWallID + 1);
    }

    json SaveWallSurface(const WallSurface &surface) {
        const WallTextureAnchorInfo *anchor = FindWallTextureAnchor(surface.anchor);

        return {
            {"texture", surface.texture},
            {"textureOffset", {surface.textureOffset.x, surface.textureOffset.y}},
            {"textureScale", {surface.textureScale.x, surface.textureScale.y}},
            {"flipX", surface.flipTextureX},
            {"flipY", surface.flipTextureY},
            {"anchor", anchor != nullptr ? anchor->name : WALL_TEXTURE_ANCHORS[0].name}
        };
    }

    void SaveWalls(json &levelData, const Level &level) {
        levelData["walls"] = json::array();

        for (const Wall &wall: level.walls) {
            levelData["walls"].push_back({
                {"id", wall.id},
                {"name", wall.name},
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
                {
                    "color", {
                        wall.color.x,
                        wall.color.y,
                        wall.color.z,
                        wall.color.w
                    }
                },
                {"top", SaveWallSurface(wall.top)},
                {"bottom", SaveWallSurface(wall.bottom)},
                {"middle", SaveWallSurface(wall.middle)},
                {"middleHasCollision", wall.middleHasCollision},
                {"middleIgnoredByRaycast", wall.middleIgnoredByRaycast},
                {"frontSector", wall.frontSector},
                {"backSector", wall.backSector},
                {"tags", wall.tags},
                {"tagIds", wall.tagIds},
                {"angle", wall.angle}
            });
        }
    }

    // Loads a sector's optional "scripts" array. Levels saved before sector
    // scripts existed simply have no such key and load with none attached.
    // Unlike entity scripts, the file name is kept exactly as saved (a
    // project-relative path, no extension) rather than reduced to its stem.
    void LoadSectorScripts(const json &sectorJson, Sector &sector) {
        sector.scripts.clear();
        sector.nextScriptInstanceID = 1;

        if (!sectorJson.contains("scripts")) return;

        const json &scriptArray = sectorJson.at("scripts");

        if (!scriptArray.is_array()) {
            spdlog::warn("LoadSectors: sector {} 'scripts' is not an array - ignoring", sector.id);
            return;
        }

        std::unordered_set<ScriptInstanceID> usedInstanceIDs;

        for (const json &scriptJson: scriptArray) {
            if (!scriptJson.is_object()) {
                spdlog::warn("LoadSectors: sector {} has a script entry that is not an object - skipping", sector.id);
                continue;
            }

            SectorScript script;

            if (scriptJson.contains("fileName") && scriptJson.at("fileName").is_string())
                script.fileName = scriptJson.at("fileName").get<std::string>();

            // is_number_integer(), not is_number_unsigned(): BSON has no
            // unsigned types, so a saved (unsigned) ID comes back from
            // from_bson() as a plain signed integer.
            if (scriptJson.contains("instanceID") && scriptJson.at("instanceID").is_number_integer()) {
                const std::int64_t savedID = scriptJson.at("instanceID").get<std::int64_t>();
                if (savedID > 0) script.instanceID = static_cast<ScriptInstanceID>(savedID);
            }

            ScriptAttachmentSettingsFromJson(scriptJson, script);

            // Instance IDs must be unique within the sector; a missing or
            // duplicated one is replaced below, once every valid ID is known.
            if (script.instanceID != INVALID_SCRIPT_INSTANCE_ID && !usedInstanceIDs.insert(script.instanceID).second)
                script.instanceID = INVALID_SCRIPT_INSTANCE_ID;

            sector.scripts.push_back(std::move(script));
        }

        for (const SectorScript &script: sector.scripts)
            sector.nextScriptInstanceID = std::max(sector.nextScriptInstanceID, script.instanceID + 1);

        for (SectorScript &script: sector.scripts)
            if (script.instanceID == INVALID_SCRIPT_INSTANCE_ID) script.instanceID = sector.nextScriptInstanceID++;
    }

    void LoadSectors(const json &levelData, Level &level) {
        level.sectors.clear();

        if (!levelData.contains("sectors") || !levelData.at("sectors").is_array()) return;

        ID highestSectorID = 0;
        std::unordered_set<ID> seenSectorIDs;

        const auto loadVector2 = [](const json &parentJson,
                                    const char *field,
                                    const Vector2 &defaultValue) -> Vector2 {
            if (!parentJson.contains(field)) return defaultValue;

            const json &vectorJson = parentJson.at(field);

            if (!vectorJson.is_array() || vectorJson.size() != 2 ||
                !vectorJson[0].is_number() || !vectorJson[1].is_number()) {
                return defaultValue;
            }

            return {vectorJson[0].get<float>(),vectorJson[1].get<float>()};
        };

        const auto loadVector3 = [](const json &parentJson,
                                    const char *field,
                                    const Vector3 &defaultValue) -> Vector3 {
            if (!parentJson.contains(field)) return defaultValue;

            const json &vectorJson = parentJson.at(field);

            if (!vectorJson.is_array() || vectorJson.size() != 3) return defaultValue;

            return {
                vectorJson[0].get<float>(),
                vectorJson[1].get<float>(),
                vectorJson[2].get<float>()
            };
        };

        const auto loadSurface = [&loadVector2](const json &surfaceJson,
                                                const float defaultHeight) -> SectorSurface {
            SectorSurface surface;

            surface.height = surfaceJson.value("height", defaultHeight);
            surface.color = {
                surfaceJson["color"][0].get<float>(),
                surfaceJson["color"][1].get<float>(),
                surfaceJson["color"][2].get<float>(),
                surfaceJson["color"][3].get<float>(),
            };
            surface.texture = surfaceJson.value("texture", std::string{});

            const int slopeDirection = surfaceJson.value(
                "slopeDirection",
                static_cast<int>(PLUS_X)
            );

            if (slopeDirection >= static_cast<int>(PLUS_X) && slopeDirection <= static_cast<int>(MINUS_Z))
                surface.slopeDirection = static_cast<SlopeDirection>(slopeDirection);

            surface.slopeStrength = surfaceJson.value("slopeStrength", 0.0f);

            surface.textureOffset = loadVector2(
                surfaceJson,
                "textureOffset",
                {0.0f, 0.0f}
            );

            surface.textureScale = loadVector2(
                surfaceJson,
                "textureScale",
                {1.0f, 1.0f}
            );

            surface.flipTextureX = surfaceJson.value("flipTextureX", false);
            surface.flipTextureY = surfaceJson.value("flipTextureY", false);

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

        for (int sectorIndex = 0; sectorIndex < static_cast<int>(sectorArray.size()); ++sectorIndex) {
            const json &sectorJson = sectorArray[sectorIndex];

            if (!sectorJson.is_object()) {
                spdlog::warn("LoadSectors: sector at array index {} is not an object", sectorIndex);
                continue;
            }

            Sector sector;
            sector.vertices.clear();
            sector.innerLoops.clear();
            sector.triangles.clear();
            sector.floors.clear();

            sector.id = LoadIDField(sectorJson, "id", static_cast<ID>(sectorIndex));
            sector.parentID = LoadIDField(sectorJson, "parentID", static_cast<ID>(INVALID_ID));

            if (sector.id == INVALID_ID) sector.id = static_cast<ID>(sectorIndex);

            if (seenSectorIDs.contains(sector.id)) {
                ID reassignedID = highestSectorID + 1;

                while (seenSectorIDs.contains(reassignedID) || reassignedID == INVALID_ID) ++reassignedID;

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
            if (sectorJson.contains("corners")) sector.vertices = loadLoop(sectorJson.at("corners"));

            // Hole boundaries
            if (sectorJson.contains("innerLoops") && sectorJson.at("innerLoops").is_array()) {
                const json &innerLoopArray = sectorJson.at("innerLoops");
                sector.innerLoops.reserve(innerLoopArray.size());

                for (int loopIndex = 0; loopIndex < static_cast<int>(innerLoopArray.size()); ++loopIndex) {
                    std::vector<Vector2> loop = loadLoop(innerLoopArray[loopIndex]);

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

            if (sectorJson.contains("floors") && sectorJson.at("floors").is_array()) {
                const json &floorArray = sectorJson.at("floors");
                sector.floors.reserve(floorArray.size());

                for (int floorIndex = 0; floorIndex < static_cast<int>(floorArray.size()); ++floorIndex) {
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
                    sectorFloor.floor = loadSurface(floorJson.at("floor"), 0.0f);
                    sectorFloor.ceiling = loadSurface(floorJson.at("ceiling"), 40.0f);

                    if (sectorFloor.floor.height >= sectorFloor.ceiling.height) {
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

            for (int floorIndex = 1; floorIndex < static_cast<int>(sector.floors.size()); ++floorIndex) {
                const SectorFloor &previous = sector.floors[floorIndex - 1];
                const SectorFloor &current = sector.floors[floorIndex];

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
                        {1.0f, 1.0f, 1.0f, 1.0f},
                        {}
                    },
                    {
                        40.0f,
                        {1.0f, 1.0f, 1.0f, 1.0f},
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

            sector.name = sectorJson.at("name");

            if (sectorJson.contains("tags") || sectorJson.contains("tagIds")) {
                LoadTagArrays(sectorJson, "Sector", sector.id, sector.tags, sector.tagIds);
            } else if (sectorJson.contains("tag") && sectorJson.contains("tagId")) {
                // Legacy single-tag format (pre-multi-tag) - load as a
                // one-element vector rather than dropping the tag.
                sector.tags.clear();
                sector.tagIds.clear();

                const json &legacyTag = sectorJson.at("tag");
                const json &legacyTagId = sectorJson.at("tagId");

                if (legacyTag.is_string() &&
                    (legacyTagId.is_number_unsigned() || legacyTagId.is_number_integer())) {
                    sector.tags.push_back(legacyTag.get<std::string>());
                    sector.tagIds.push_back(legacyTagId.get<uint16_t>());
                } else {
                    spdlog::warn("Sector {}: malformed legacy tag data - skipping", sector.id);
                }
            } else {
                sector.tags.clear();
                sector.tagIds.clear();
            }

            LoadSectorScripts(sectorJson, sector);

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
                    {surface.color.x, surface.color.y, surface.color.z, surface.color.w},
                },
                {"texture", surface.texture},
                {
                    "slopeDirection",
                    static_cast<int>(surface.slopeDirection)
                },
                {"slopeStrength", surface.slopeStrength},
                {
                    "textureOffset",
                    {surface.textureOffset.x, surface.textureOffset.y}
                },
                {
                    "textureScale",
                    {surface.textureScale.x, surface.textureScale.y}
                },
                {"flipTextureX", surface.flipTextureX},
                {"flipTextureY", surface.flipTextureY}
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

            json sectorJson = {
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
                },
                {"name", sector.name},
                {"tags", sector.tags},
                {"tagIds", sector.tagIds},
                    {"parentID", sector.parentID}
            };

            // Only written when there is something to write, so levels
            // without sector scripts save exactly as they did before.
            if (!sector.scripts.empty()) {
                json scriptArray = json::array();

                for (const SectorScript &script: sector.scripts)
                    scriptArray.push_back(ScriptAttachmentToJson(script));

                sectorJson["scripts"] = std::move(scriptArray);
            }

            sectorArray.push_back(std::move(sectorJson));
        }

        levelData["sectors"] = std::move(sectorArray);
    }

    json Vec3Json(const Vector3& v) { return {v.x, v.y, v.z}; }
    Vector3 Vec3FromJson(const json& j) { return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()}; }
}

// ── Per-component JSON ───────────────────────────────────────────────────────
// Each component's own fields, keyed exactly as level.bson stores them, but
// without "ownerID"/"instanceID" - the level save adds those, and prefab
// syncing compares these objects field by field (see Prefab.cpp).
namespace LevelSerialization {
    json ComponentFieldsToJson(const ComponentTransform& c) {
        return {
            {"position", Vec3Json(c.position)},
            {"rotation", {c.rotation.x, c.rotation.y, c.rotation.z, c.rotation.w}},
            {"sectorIndex", c.sectorIndex},
            {"scale", Vec3Json(c.scale)}
        };
    }

    bool ComponentFieldsFromJson(const json& j, ComponentTransform& c) {
        c.position = Vec3FromJson(j["position"]);
        c.rotation = Quaternion{
            j["rotation"][0].get<float>(),
            j["rotation"][1].get<float>(),
            j["rotation"][2].get<float>(),
            j["rotation"][3].get<float>()
        }.Normalized();
        c.sectorIndex = j["sectorIndex"].get<int>();
        c.scale = Vec3FromJson(j["scale"]);
        return true;
    }

    json ComponentFieldsToJson(const ComponentSprite& c) {
        return {
            {"offset", Vec3Json(c.offset)},
            {"textureFileNames", c.textureFileNames},
            {"sideCount", static_cast<int>(c.sideCount)},
            {"color", {c.color.x, c.color.y, c.color.z, c.color.w}},
            {"isStatic", c.isStatic},
            {"isActive", c.isActive}
        };
    }

    bool ComponentFieldsFromJson(const json& j, ComponentSprite& c) {
        const auto textureFileNames = j.at("textureFileNames").get<std::array<std::string, 8>>();
        const int sideCountValue = j.at("sideCount").get<int>();

        if (sideCountValue != SIDECOUNT_SINGLE &&
            sideCountValue != SIDECOUNT_90 &&
            sideCountValue != SIDECOUNT_45) return false;

        c.textureFileNames = textureFileNames;
        c.sideCount = static_cast<SideCount>(sideCountValue);
        c.color = {
            j["color"][0].get<float>(),
            j["color"][1].get<float>(),
            j["color"][2].get<float>(),
            j["color"][3].get<float>()
        };
        c.isStatic = j.value("isStatic", false);
        c.isActive = j.value("isActive", true);
        if (j.contains("offset")) c.offset = Vec3FromJson(j["offset"]);
        return true;
    }

    json ComponentFieldsToJson(const ComponentAudioSource& c) {
        return {
            {"offset", Vec3Json(c.offset)},
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
        };
    }

    bool ComponentFieldsFromJson(const json& j, ComponentAudioSource& c) {
        c.soundFileName = j.value("soundFileName", std::string{});
        c.pitch = j.value("pitch", 1.0f);
        c.gain = j.value("gain", 1.0f);
        c.looping = j.value("looping", false);
        c.playOnStart = j.value("playOnStart", false);
        c.referenceDistance = j.value("referenceDistance", 1.0f);
        c.maxDistance = j.value("maxDistance", 10000.0f);
        c.rollOffFactor = j.value("rollOffFactor", 1.0f);
        c.innerConeAngle = j.value("innerConeAngle", 360.0f);
        c.outerConeAngle = j.value("outerConeAngle", 360.0f);
        c.outerGain = j.value("outerGain", 0.0f);
        if (j.contains("offset")) c.offset = Vec3FromJson(j["offset"]);
        return true;
    }

    json ComponentFieldsToJson(const ComponentScript& c) {
        json scriptJson = ScriptAttachmentToJson(c);
        scriptJson.erase("instanceID");
        return scriptJson;
    }

    bool ComponentFieldsFromJson(const json& j, ComponentScript& c) {
        ScriptAttachmentSettingsFromJson(j, c);

        // Keep the full saved path (Assets-relative, no extension) - NOT
        // just its stem. Reducing to the stem used to drop any subfolder
        // ("Scripts/Doors/Switch" -> "Switch"), so on the next load
        // LuaScriptSystem would look for "Switch.lua" directly under Assets
        // and silently fail to find every script organized into a subfolder.
        c.fileName = j.value("fileName", std::string{});
        return true;
    }

    json ComponentFieldsToJson(const ComponentUITransform& c) {
        return {
            {"anchorMin", {c.anchorMin.x, c.anchorMin.y}},
            {"anchorMax", {c.anchorMax.x, c.anchorMax.y}},
            {"pivot", {c.pivot.x, c.pivot.y}},
            {"position", {c.position.x, c.position.y}},
            {"scale", {c.scale.x, c.scale.y}},
            {"rotation", c.rotation}
        };
    }

    bool ComponentFieldsFromJson(const json& j, ComponentUITransform& c) {
        auto readVector2 = [&j](const char* key, Vector2& out) {
            if (j.contains(key)) out = {j[key][0].get<float>(), j[key][1].get<float>()};
        };

        readVector2("anchorMin", c.anchorMin);
        readVector2("anchorMax", c.anchorMax);
        readVector2("pivot", c.pivot);
        readVector2("position", c.position);
        readVector2("scale", c.scale);
        c.rotation = j.value("rotation", c.rotation);
        return true;
    }

    json ComponentFieldsToJson(const ComponentUISprite& c) {
        return {
            {"texture", c.texture},
            {"color", {c.color.x, c.color.y, c.color.z, c.color.w}},
            {"isActive", c.isActive}
        };
    }

    bool ComponentFieldsFromJson(const json& j, ComponentUISprite& c) {
        c.texture = j.value("texture", "");
        c.isActive = j.value("isActive", true);

        if (j.contains("color") && j["color"].is_array() && j["color"].size() == 4) {
            const json& colorJson = j["color"];
            c.color = {
                colorJson[0].get<float>(),
                colorJson[1].get<float>(),
                colorJson[2].get<float>(),
                colorJson[3].get<float>()
            };
        }
        return true;
    }

    json ComponentFieldsToJson(const ComponentUIText& c) {
        return {
            {"text", c.text},
            {"font", c.font},
            {"fontSize", c.fontSize},
            {"color", {c.color.x, c.color.y, c.color.z, c.color.w}}
        };
    }

    bool ComponentFieldsFromJson(const json& j, ComponentUIText& c) {
        c.text = j.value("text", "");
        c.font = j.value("font", "");
        c.fontSize = j.value("fontSize", DEFAULT_UI_FONT_SIZE);

        if (j.contains("color") && j["color"].is_array() && j["color"].size() == 4) {
            const json& colorJson = j["color"];
            c.color = {
                colorJson[0].get<float>(),
                colorJson[1].get<float>(),
                colorJson[2].get<float>(),
                colorJson[3].get<float>()
            };
        }
        return true;
    }

    json ComponentFieldsToJson(const ComponentPlayerController& c) {
        return {
            {"isActive", c.isActive},
            {"velocity", Vec3Json(c.velocity)},
            {"speed", c.speed},
            {"jumpPower", c.jumpPower},
            {"runningSpeed", c.runningSpeed},
            {"eyeHeight", c.eyeHeight},
            {"sensitivityX", c.sensitivityX},
            {"sensitivityY", c.sensitivityY},
            {"minPitch", c.minPitch},
            {"maxPitch", c.maxPitch},
            {"minYaw", c.minYaw},
            {"maxYaw", c.maxYaw},
            {"jumpBufferMs", c.jumpBufferMs},
            {"acceleration", c.acceleration},
            {"deceleration", c.deceleration},
            {"airControl", c.airControl}
        };
    }

    bool ComponentFieldsFromJson(const json& j, ComponentPlayerController& c) {
        c.isActive = j.value("isActive", true);
        c.speed = j.value("speed", 46.0f);
        c.runningSpeed = j.value("runningSpeed", 90.0f);
        c.jumpPower = j.value("jumpPower", 100.0f);
        c.eyeHeight = j.value("eyeHeight", 12.0f);
        c.sensitivityX = j.value("sensitivityX", 0.5f);
        c.sensitivityY = j.value("sensitivityY", 0.5f);
        c.maxPitch = j.value("maxPitch", 89.0f);
        c.minPitch = j.value("minPitch", -89.0f);
        c.maxYaw = j.value("maxYaw", 360.0f);
        c.minYaw = j.value("minYaw", .0f);
        c.jumpBufferMs = j.value("jumpBufferMs", 100.0f);
        c.acceleration = j.value("acceleration", 80.0f);
        c.deceleration = j.value("deceleration", 60.0f);
        c.airControl = j.value("airControl", .3f);
        if (j.contains("velocity")) c.velocity = Vec3FromJson(j["velocity"]);
        return true;
    }

    json ComponentFieldsToJson(const ComponentCamera& c) {
        return {
            {"isActive", c.isActive},
            {"yaw", c.yaw},
            {"pitch", c.pitch},
            {"forward", Vec3Json(c.forward)},
            {"target", Vec3Json(c.target)},
            {"fov", c.fov},
            {"aspectRatio", c.aspectRatio},
            {"nearPlane", c.nearPlane},
            {"farPlane", c.farPlane},
            {"smoothStep", c.smoothStep},
            {"smoothingStrength", c.smoothingStrength}
        };
    }

    bool ComponentFieldsFromJson(const json& j, ComponentCamera& c) {
        c.isActive = j.value("isActive", true);
        c.yaw = j.value("yaw", 0.0f);
        c.pitch = j.value("pitch", 0.0f);
        if (j.contains("forward")) c.forward = Vec3FromJson(j["forward"]);
        if (j.contains("target")) c.target = Vec3FromJson(j["target"]);
        c.fov = j.value("fov", 90.0f);
        c.aspectRatio = j.value("aspectRatio", 1680.0f / 960.0f);
        c.nearPlane = j.value("nearPlane", 0.1f);
        c.farPlane = j.value("farPlane", 10000.0f);
        c.smoothStep = j.value("smoothStep", false);
        c.smoothingStrength = j.value("smoothingStrength", 4.8f);
        return true;
    }

    json ComponentFieldsToJson(const ComponentCollider& c) {
        return {
            {"offset", Vec3Json(c.offset)},
            {"isActive", c.isActive},
            {"isTrigger", c.isTrigger},
            {"type", c.type},
            {"scale", Vec3Json(c.scale)},
            {"stepSize", c.stepSize},
            {"maxSlope", c.maxSlope}
        };
    }

    bool ComponentFieldsFromJson(const json& j, ComponentCollider& c) {
        c.isActive = j.value("isActive", true);
        c.isTrigger = j.value("isTrigger", false);
        c.type = j.value("type", COLLIDERTYPE_SPHERE);
        c.stepSize = j.value("stepSize", c.stepSize);
        c.maxSlope = j.value("maxSlope", c.maxSlope);
        if (j.contains("scale")) c.scale = Vec3FromJson(j["scale"]);
        if (j.contains("offset")) c.offset = Vec3FromJson(j["offset"]);
        return true;
    }

    json ComponentFieldsToJson(const ComponentRigidbody& c) {
        return {
            {"isStatic", c.isStatic},
            {"mass", c.mass},
            {"gravityScale", c.gravityScale},
            {"friction", c.friction}
        };
    }

    bool ComponentFieldsFromJson(const json& j, ComponentRigidbody& c) {
        c.isStatic = j.value("isStatic", true);
        c.mass = j.value("mass", 1.0f);
        c.gravityScale = j.value("gravityScale", 1.0f);
        c.friction = j.value("friction", 1.0f);
        return true;
    }

    json ComponentFieldsToJson(const ComponentModel& c) {
        return {
            {"offset", Vec3Json(c.offset)},
            {"fileName", c.fileName}
        };
    }

    bool ComponentFieldsFromJson(const json& j, ComponentModel& c) {
        c.fileName = j.value("fileName", std::string{});
        if (j.contains("offset")) c.offset = Vec3FromJson(j["offset"]);
        return true;
    }

    json ComponentFieldsToJson(const ComponentFlipbook& c) {
        return {
            {"flipbookFileName", c.flipbookFileName},
            {"spriteInstanceID", c.spriteInstanceID},
            {"speed", c.speed}
        };
    }

    bool ComponentFieldsFromJson(const json& j, ComponentFlipbook& c) {
        c.flipbookFileName = j.value("flipbookFileName", std::string{});
        c.spriteInstanceID = j.value("spriteInstanceID", INVALID_COMPONENT_INSTANCE_ID);
        c.speed = j.value("speed", 1.0f);
        return true;
    }
}

namespace {
    using LevelSerialization::ComponentFieldsFromJson;
    using LevelSerialization::ComponentFieldsToJson;

    // Loads one storage's array. Components whose owner isn't in the level
    // (or whose data is invalid) are skipped; Transform/UITransform keep
    // only the first per entity.
    template<typename T, typename Storage>
    void LoadComponentArray(const json& componentsJson, const char* key, Level& level, Storage& storage, const int bit) {
        if (!componentsJson.contains(key)) return;

        for (const json& componentJson : componentsJson[key]) {
            const ID ownerID = componentJson.value("ownerID", INVALID_ENTITY_ID);
            if (ownerID == INVALID_ENTITY_ID) continue;

            Entity* entity = level.GetEntity(ownerID);
            if (entity == nullptr) continue;

            if constexpr (IsSingleComponent<T>) if (storage.Has(ownerID)) continue;

            // Built whole and handed to InsertLoaded, which (for colliders)
            // files it under the right part of the storage for its type and
            // active state.
            T c{};
            if (!ComponentFieldsFromJson(componentJson, c)) continue;
            c.ownerID = ownerID;
            c.instanceID = componentJson.value("instanceID", INVALID_COMPONENT_INSTANCE_ID);
            storage.InsertLoaded(c);

            entity->componentsMask.set(bit);
        }
    }

    void LoadComponents(const json &levelData, Level &level) {
#define CLEAR_STORAGE(Type, Bit, Storage, LabelKey) level.Storage.Clear();
        TILKY_COMPONENTS(CLEAR_STORAGE)
#undef CLEAR_STORAGE

        if (!levelData.contains("components")) return;

        const json &componentsJson = levelData["components"];

        LoadComponentArray<ComponentTransform>(componentsJson, "transforms", level, level.transforms, CMP_TRANSFORM);
        LoadComponentArray<ComponentSprite>(componentsJson, "sprites", level, level.sprites, CMP_SPRITE);
        LoadComponentArray<ComponentAudioSource>(componentsJson, "audioSources", level, level.audioSources, CMP_AUDIO_SOURCE);

        if (componentsJson.contains("scripts")) {
            for (const json& scriptJson : componentsJson["scripts"]) {
                const ID ownerID = scriptJson.value("ownerID", INVALID_ENTITY_ID);
                if (ownerID == INVALID_ENTITY_ID) continue;

                Entity* entity = level.GetEntity(ownerID);
                if (entity == nullptr) continue;

                ComponentScript& c = level.scripts.Add(ownerID, scriptJson.value("instanceID", INVALID_SCRIPT_INSTANCE_ID));
                ComponentFieldsFromJson(scriptJson, c);

                entity->componentsMask.set(CMP_SCRIPT);
            }
        }

        LoadComponentArray<ComponentUITransform>(componentsJson, "uiTransforms", level, level.ui_transforms, CMP_UI_TRANSFORM);
        LoadComponentArray<ComponentUISprite>(componentsJson, "uiSprites", level, level.ui_sprites, CMP_UI_SPRITE);
        LoadComponentArray<ComponentUIText>(componentsJson, "uiTexts", level, level.ui_texts, CMP_UI_TEXT);
        LoadComponentArray<ComponentPlayerController>(componentsJson, "playerControllers", level, level.playerControllers, CMP_PLAYER_CONTROLLER);
        LoadComponentArray<ComponentCamera>(componentsJson, "cameras", level, level.cameras, CMP_CAMERA);
        LoadComponentArray<ComponentCollider>(componentsJson, "colliders", level, level.colliders, CMP_COLLIDER);
        LoadComponentArray<ComponentRigidbody>(componentsJson, "rigidbodies", level, level.rigidbodies, CMP_RIGIDBODY);
        LoadComponentArray<ComponentModel>(componentsJson, "models", level, level.models, CMP_MODEL);
        LoadComponentArray<ComponentFlipbook>(componentsJson, "flipbooks", level, level.flipbooks, CMP_FLIPBOOK);
    }

    // A storage's components grouped by entity, each entity's in their own
    // order, so the order (which one is "first") survives a save and load.
    // Only the given entities' components are included.
    template<typename Storage>
    auto InOwnerOrder(const std::vector<const Entity*>& entities, const Storage& storage) {
        using T = std::remove_cvref_t<decltype(storage.components.front())>;
        std::vector<std::reference_wrapper<const T>> result;
        result.reserve(storage.components.size());
        for (const Entity* entity : entities)
            for (const T* component : storage.GetAll(entity->id)) result.emplace_back(*component);
        return result;
    }

    template<typename Storage>
    void SaveComponentArray(json& componentsJson, const char* key, const std::vector<const Entity*>& entities, const Storage& storage) {
        json& array = componentsJson[key] = json::array();

        for (const auto& component : InOwnerOrder(entities, storage)) {
            json componentJson = ComponentFieldsToJson(component.get());
            componentJson["ownerID"] = component.get().ownerID;
            componentJson["instanceID"] = component.get().instanceID;
            array.push_back(std::move(componentJson));
        }
    }

    void SaveComponents(json &levelData, const Level &level, const std::vector<const Entity*>& entities) {
        json componentsJson;

        SaveComponentArray(componentsJson, "transforms", entities, level.transforms);
        SaveComponentArray(componentsJson, "sprites", entities, level.sprites);
        SaveComponentArray(componentsJson, "audioSources", entities, level.audioSources);

        std::unordered_set<ID> owners;
        for (const Entity* entity : entities) owners.insert(entity->id);

        componentsJson["scripts"] = json::array();
        for (const ComponentScript& c : level.scripts.components) {
            if (!owners.contains(c.ownerID)) continue;

            json scriptJson = ScriptAttachmentToJson(c);
            scriptJson["ownerID"] = c.ownerID;

            componentsJson["scripts"].push_back(std::move(scriptJson));
        }

        SaveComponentArray(componentsJson, "uiTransforms", entities, level.ui_transforms);
        SaveComponentArray(componentsJson, "uiSprites", entities, level.ui_sprites);
        SaveComponentArray(componentsJson, "uiTexts", entities, level.ui_texts);
        SaveComponentArray(componentsJson, "playerControllers", entities, level.playerControllers);
        SaveComponentArray(componentsJson, "cameras", entities, level.cameras);
        SaveComponentArray(componentsJson, "colliders", entities, level.colliders);
        SaveComponentArray(componentsJson, "rigidbodies", entities, level.rigidbodies);
        SaveComponentArray(componentsJson, "models", entities, level.models);
        SaveComponentArray(componentsJson, "flipbooks", entities, level.flipbooks);

        levelData["components"] = componentsJson;
    }

    // `what` names the kind of file in error messages ("level", "prefab").
    bool ReadBsonFile(const fs::path& path, const std::string& what, json& outData, std::string* errorMessage) {
        if (!fs::exists(path)) {
            SetError(errorMessage, what + " file does not exist: " + path.string());
            return false;
        }

        std::ifstream file(path, std::ios::binary);

        if (!file.is_open()) {
            SetError(errorMessage, "Could not open BSON " + what + " file: " + path.string());
            return false;
        }

        const std::vector<std::uint8_t> bsonData{
            std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()
        };

        try {
            outData = json::from_bson(bsonData);
        }
        catch (const std::exception& e) {
            SetError(errorMessage, "Failed to parse BSON " + what + " file " + path.string() + ": " + e.what());
            return false;
        }

        return true;
    }

    bool WriteBsonFile(const fs::path& path, const std::string& what, const json& data, std::string* errorMessage) {
        std::error_code ec;
        if (!path.parent_path().empty()) fs::create_directories(path.parent_path(), ec);
        if (ec) {
            SetError(errorMessage, "Failed to create " + what + " folder " + path.parent_path().string() + ": " + ec.message());
            return false;
        }

        const std::vector<std::uint8_t> bsonData = json::to_bson(data);

        std::ofstream file(path, std::ios::binary);

        if (!file.is_open()) {
            SetError(errorMessage, "Failed to open BSON " + what + " for saving: " + path.string());
            return false;
        }

        file.write(reinterpret_cast<const char*>(bsonData.data()), static_cast<std::streamsize>(bsonData.size()));
        return true;
    }

    std::vector<const Entity*> AllEntities(const Level& level) {
        std::vector<const Entity*> result;
        result.reserve(level.entities.size());
        for (const Entity& entity : level.entities) result.push_back(&entity);
        return result;
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

            // BSON has no unsigned ints, so IDs come back signed.
            if (metadatajson.contains("wallSnaps") && metadatajson["wallSnaps"].is_array()) {
                for (const json& snapJson : metadatajson["wallSnaps"]) {
                    if (!snapJson.is_object() ||
                        !snapJson.contains("entity") || !snapJson["entity"].is_number_integer() ||
                        !snapJson.contains("wall") || !snapJson["wall"].is_number_integer())
                        continue;

                    WallSnapData snap;
                    snap.wallID = snapJson["wall"].get<ID>();
                    snap.side = snapJson.value("flipped", false) ? -1.0f : 1.0f;
                    snap.gap = std::max(0.0f, snapJson.value("gap", WallSnapData::DEFAULT_GAP));

                    level.wallSnaps[snapJson["entity"].get<ID>()] = snap;
                }
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

        // Snaps of entities deleted since the editor last checked are left out.
        json wallSnapsJson = json::array();

        for (const auto& [entityID, snap] : level.wallSnaps) {
            if (level.GetEntity(entityID) == nullptr) continue;

            wallSnapsJson.push_back({
                {"entity", entityID},
                {"wall", snap.wallID},
                {"flipped", snap.side < 0.0f}, // on the other side of the wall's normal
                {"gap", snap.gap}
            });
        }

        metadatajson["wallSnaps"] = std::move(wallSnapsJson);

        levelData["metadata"] = metadatajson;
    }
#endif

    void SaveProjectWideInformation() {

    }

    void LoadProjectWideInformation() {
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

    std::vector<fs::path> ListLevelFiles() {
        std::vector<fs::path> levelFiles;
        const fs::path assetsPath = ProjectManager::GetAssetsPath();

        std::error_code ec;
        for (fs::recursive_directory_iterator it(assetsPath, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->is_regular_file() && it->path().extension() == ".bson") levelFiles.push_back(it->path());
        }

        std::ranges::sort(levelFiles);
        return levelFiles;
    }

    std::vector<fs::path> FindLevelFiles(const std::string& levelName) {
        const std::string cleanName = CleanLevelName(levelName);

        std::vector<fs::path> matches;
        for (fs::path& levelFile : ListLevelFiles())
            if (levelFile.stem().string() == cleanName) matches.push_back(std::move(levelFile));

        return matches;
    }

    fs::path FindLevelPath(const std::string& levelName, std::string* errorMessage) {
        const std::string cleanName = CleanLevelName(levelName);
        const std::vector<fs::path> matches = FindLevelFiles(cleanName);

        if (matches.empty()) {
            SetError(errorMessage, "There is no level called \"" + cleanName + "\" in the Assets folder");
            return {};
        }

        if (matches.size() > 1) {
            std::string message = "More than one level is called \"" + cleanName + "\"; rename all but one:";
            for (const fs::path& match : matches) message += "\n  " + match.string();

            SetError(errorMessage, message);
            return {};
        }

        return matches.front();
    }

    fs::path ResolveLevelSavePath(const std::string& levelName, std::string* errorMessage) {
        const std::string cleanName = CleanLevelName(levelName);
        const std::vector<fs::path> matches = FindLevelFiles(cleanName);

        if (matches.empty()) return ProjectManager::GetAssetsPath() / (cleanName + ".bson");
        if (matches.size() == 1) return matches.front();

        return FindLevelPath(cleanName, errorMessage); // reports the duplicates
    }

    bool LoadLevelFromFile(const fs::path& levelFile, Level& outLevel, LevelExtraData* outExtraData,std::string* errorMessage) {
        json levelData;
        if (!ReadBsonFile(levelFile, "level", levelData, errorMessage)) return false;

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

        // Prefab instances take whatever changed in their .prefab since the
        // level was saved.
        Prefab::SyncAllInstances(loadedLevel);

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
        const std::vector<const Entity*> entities = AllEntities(level);
        SaveEntities(levelData, entities);
        SaveComponents(levelData, level, entities);
        SaveWalls(levelData, level);
        SaveSectors(levelData, level);
        SaveTextures(levelData, level);
        SaveSounds(levelData, level);

#ifndef TILKY_STANDALONE
        SaveMetadata(levelData, level);
#endif
        return WriteBsonFile(levelFile, "level", levelData, errorMessage);
    }

    json EntitiesToJson(const Level& level, const std::vector<ID>& entityIDs) {
        std::vector<const Entity*> entities;
        for (const ID entityID : entityIDs)
            if (const Entity* entity = level.GetEntity(entityID)) entities.push_back(entity);

        json data;
        SaveEntities(data, entities);
        SaveComponents(data, level, entities);
        return data;
    }

    bool EntitiesFromJson(const json& data, Level& outLevel, std::string* errorMessage) {
        try {
            LoadEntities(data, outLevel);
            LoadComponents(data, outLevel);
        }
        catch (const std::exception& e) {
            SetError(errorMessage, std::string("Entity data schema error: ") + e.what());
            return false;
        }

        RebuildNextEntityID(outLevel);
        return true;
    }

    bool LoadPrefabFromFile(const fs::path& prefabFile, Level& outPrefab, std::string* errorMessage) {
        json data;
        if (!ReadBsonFile(prefabFile, "prefab", data, errorMessage)) return false;

        Level loaded;
        loaded.name = prefabFile.stem().string();

        std::string error;
        if (!EntitiesFromJson(data, loaded, &error)) {
            SetError(errorMessage, error + " (" + prefabFile.string() + ")");
            return false;
        }

        if (loaded.entities.size() != 1) {
            SetError(errorMessage, "A prefab must hold exactly one entity, found " +
                     std::to_string(loaded.entities.size()) + ": " + prefabFile.string());
            return false;
        }

        // A prefab is never itself an instance of another prefab.
        loaded.entities.front().prefabLink = {};

        outPrefab = std::move(loaded);
        return true;
    }

    bool SavePrefabToFile(const fs::path& prefabFile, const Level& prefab, std::string* errorMessage) {
        if (prefab.entities.size() != 1) {
            SetError(errorMessage, "A prefab must hold exactly one entity: " + prefabFile.string());
            return false;
        }

        return WriteBsonFile(prefabFile, "prefab", EntitiesToJson(prefab, {prefab.entities.front().id}), errorMessage);
    }
}

namespace LevelSerialization {
    std::string ScriptPublicFieldsToJsonText(const std::vector<ScriptPublicField>& fields) {
        json fieldsJson = json::array();

        for (const ScriptPublicField& field : fields) {
            json options = json::array();
            for (const ScriptEnumOption& option : field.enumOptions)
                options.push_back(json{{"name", option.name}, {"value", option.value}});

            fieldsJson.push_back(json{
                {"name", field.name},
                {"type", ScriptValueTypeToString(field.type)},
                {"default", ScriptValueToJson(field.defaultValue)},
                {"displayName", field.displayName},
                {"enumOptions", options},
                {"componentType", field.componentType},
                {"assetKind", static_cast<int>(field.assetKind)},
            });
        }

        return fieldsJson.dump(1, '	');
    }

    bool ScriptPublicFieldsFromJsonText(const std::string& text, std::vector<ScriptPublicField>& outFields, std::string* errorMessage) {
        outFields.clear();

        try {
            for (const json& fieldJson : json::parse(text)) {
                ScriptPublicField field;
                field.name = fieldJson.at("name").get<std::string>();
                field.type = ScriptValueTypeFromString(fieldJson.at("type").get<std::string>());
                field.defaultValue = ScriptValueFromJson(fieldJson.at("default"));
                field.displayName = fieldJson.value("displayName", field.name);
                field.componentType = fieldJson.value("componentType", -1);
                field.assetKind = static_cast<ScriptAssetKind>(fieldJson.value("assetKind", 0));

                for (const json& option : fieldJson.value("enumOptions", json::array()))
                    field.enumOptions.push_back({option.at("name").get<std::string>(), option.at("value").get<int>()});

                outFields.push_back(std::move(field));
            }
        } catch (const std::exception& exception) {
            if (errorMessage != nullptr) *errorMessage = exception.what();
            outFields.clear();
            return false;
        }

        return true;
    }
}

namespace LevelSerialization {
    // One-element levels, so these stay exactly the save format's per-wall
    // and per-sector objects instead of a second copy of it.
    json WallToJson(const Wall& wall) {
        Level level;
        level.walls.push_back(wall);

        json levelData;
        SaveWalls(levelData, level);

        return levelData.at("walls").at(0);
    }

    bool WallFromJson(const json& data, Wall& outWall) {
        try {
            Level level;
            LoadWalls(json{{"walls", json::array({data})}}, level);
            if (level.walls.size() != 1) return false;

            outWall = std::move(level.walls.front());
            return true;
        } catch (const std::exception& exception) {
            spdlog::warn("WallFromJson: {}", exception.what());
            return false;
        }
    }

    json SectorToJson(const Sector& sector) {
        Level level;
        level.sectors.push_back(sector);

        json levelData;
        SaveSectors(levelData, level);

        return levelData.at("sectors").at(0);
    }

    bool SectorFromJson(const json& data, Sector& outSector) {
        try {
            Level level;
            LoadSectors(json{{"sectors", json::array({data})}}, level);
            if (level.sectors.size() != 1) return false;

            outSector = std::move(level.sectors.front());
            return true;
        } catch (const std::exception& exception) {
            spdlog::warn("SectorFromJson: {}", exception.what());
            return false;
        }
    }
}
