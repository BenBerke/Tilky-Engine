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
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

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

    // A level's own event variables: [{id, name, value}]. Broken entries,
    // global-scope IDs and duplicates are skipped with a warning.
    // MessageStyle enums by name, in enum order, so files never depend on enum values.
    constexpr const char* kMessageAnchorNames[] = {
        "top_left", "top_center", "top_right",
        "middle_left", "center", "middle_right",
        "bottom_left", "bottom_center", "bottom_right"
    };
    constexpr const char* kMessageTextAlignNames[] = {"left", "center", "right"};

    template <typename Enum, size_t N>
    void ReadEnumByName(const json& j, const char* key, const char* const (&names)[N], Enum& out) {
        if (!j.contains(key)) return;
        const std::string name = j.at(key).get<std::string>();
        for (size_t i = 0; i < N; ++i)
            if (name == names[i]) {
                out = static_cast<Enum>(i);
                return;
            }
        spdlog::warn("worldSettings: unknown {} \"{}\", ignored", key, name);
    }

    json MessageStyleToJson(const MessageStyle& style) {
        const auto vec2 = [](const Vector2& v) { return json::array({v.x, v.y}); };
        const auto vec4 = [](const Vector4& v) { return json::array({v.x, v.y, v.z, v.w}); };

        return {
            {"font", style.font},
            {"fontSize", style.fontSize},
            {"anchor", kMessageAnchorNames[static_cast<size_t>(style.anchor)]},
            {"offset", vec2(style.offset)},
            {"align", kMessageTextAlignNames[static_cast<size_t>(style.align)]},
            {"wrapWidth", style.wrapWidth},
            {"background", style.background},
            {"backgroundColor", vec4(style.backgroundColor)},
            {"backgroundTexture", style.backgroundTexture},
            {"padding", vec2(style.padding)},
            {"shadow", style.shadow},
            {"shadowColor", vec4(style.shadowColor)},
            {"shadowOffset", vec2(style.shadowOffset)},
            {"outline", style.outline},
            {"outlineColor", vec4(style.outlineColor)},
            {"outlineThickness", style.outlineThickness},
            {"fadeIn", style.fadeIn},
            {"fadeOut", style.fadeOut},
            {"stack", style.stack},
            {"maxMessages", style.maxMessages},
            {"spacing", style.spacing}
        };
    }

    // Missing fields keep their defaults; a bad one is skipped with a warning.
    MessageStyle MessageStyleFromJson(const json& j) {
        MessageStyle style;

        const auto read = [&](const char* key, auto&& apply) {
            if (!j.contains(key)) return;
            try { apply(j.at(key)); }
            catch (const std::exception& exception) {
                spdlog::warn("worldSettings: message style \"{}\" skipped: {}", key, exception.what());
            }
        };
        const auto vec2 = [](const json& v) { return Vector2{v.at(0).get<float>(), v.at(1).get<float>()}; };
        const auto vec4 = [](const json& v) {
            return Vector4{v.at(0).get<float>(), v.at(1).get<float>(), v.at(2).get<float>(), v.at(3).get<float>()};
        };

        read("font", [&](const json& v) { style.font = v.get<std::string>(); });
        read("fontSize", [&](const json& v) { style.fontSize = std::max(1.0f, v.get<float>()); });
        read("anchor", [&](const json&) { ReadEnumByName(j, "anchor", kMessageAnchorNames, style.anchor); });
        read("offset", [&](const json& v) { style.offset = vec2(v); });
        read("align", [&](const json&) { ReadEnumByName(j, "align", kMessageTextAlignNames, style.align); });
        read("wrapWidth", [&](const json& v) { style.wrapWidth = std::max(0.0f, v.get<float>()); });
        read("background", [&](const json& v) { style.background = v.get<bool>(); });
        read("backgroundColor", [&](const json& v) { style.backgroundColor = vec4(v); });
        read("backgroundTexture", [&](const json& v) { style.backgroundTexture = v.get<std::string>(); });
        read("padding", [&](const json& v) { style.padding = vec2(v); });
        read("shadow", [&](const json& v) { style.shadow = v.get<bool>(); });
        read("shadowColor", [&](const json& v) { style.shadowColor = vec4(v); });
        read("shadowOffset", [&](const json& v) { style.shadowOffset = vec2(v); });
        read("outline", [&](const json& v) { style.outline = v.get<bool>(); });
        read("outlineColor", [&](const json& v) { style.outlineColor = vec4(v); });
        read("outlineThickness", [&](const json& v) { style.outlineThickness = std::max(0.0f, v.get<float>()); });
        read("fadeIn", [&](const json& v) { style.fadeIn = std::max(0.0f, v.get<float>()); });
        read("fadeOut", [&](const json& v) { style.fadeOut = std::max(0.0f, v.get<float>()); });
        read("stack", [&](const json& v) { style.stack = v.get<bool>(); });
        read("maxMessages", [&](const json& v) { style.maxMessages = std::max(1, v.get<int>()); });
        read("spacing", [&](const json& v) { style.spacing = v.get<float>(); });

        return style;
    }

    void LoadEventVariables(const json& worldSettingsJson, WorldSettings& worldSettings) {
        worldSettings.eventVariables.clear();
        worldSettings.eventVariableNames.clear();
        worldSettings.eventVariableIdsByName.clear();
        worldSettings.nextEventVariableID = 1;

        if (!worldSettingsJson.contains("eventVariables")) return;

        const json& variables = worldSettingsJson.at("eventVariables");

        if (!variables.is_array()) {
            spdlog::warn("worldSettings: \"eventVariables\" is not an array, ignored");
            return;
        }

        // Never hand out an ID at or below one already saved, even if
        // nextEventVariableId is missing or too low.
        EventVariableID minimumNextId = 1;

        for (size_t i = 0; i < variables.size(); ++i) {
            try {
                const json& variable = variables[i];

                const auto id = variable.at("id").get<EventVariableID>();
                const auto name = variable.at("name").get<std::string>();
                const float value = variable.value("value", 0.0f);

                if (id == 0 || IsGlobalEventVariable(id)) throw std::runtime_error("id is not a level event variable id");
                if (name.empty()) throw std::runtime_error("empty name");
                if (worldSettings.eventVariables.contains(id)) throw std::runtime_error("duplicate id");
                if (worldSettings.eventVariableIdsByName.contains(name)) throw std::runtime_error("duplicate name \"" + name + "\"");

                worldSettings.eventVariables[id] = value;
                worldSettings.eventVariableNames[id] = name;
                worldSettings.eventVariableIdsByName[name] = id;
                minimumNextId = std::max(minimumNextId, id + 1);
            }
            catch (const std::exception& exception) {
                spdlog::warn("worldSettings: skipped event variable {}: {}", i, exception.what());
            }
        }

        worldSettings.nextEventVariableID = std::max(
            worldSettingsJson.value("nextEventVariableId", minimumNextId), minimumNextId);
    }

    json SaveEventVariables(const WorldSettings& worldSettings) {
        json variables = json::array();

        for (const auto& [id, name] : worldSettings.eventVariableNames) {
            const auto value = worldSettings.eventVariables.find(id);

            variables.push_back({
                {"id", id},
                {"name", name},
                {"value", value != worldSettings.eventVariables.end() ? value->second : 0.0f}
            });
        }

        return variables;
    }

    // Loads the "tagIds" array shared by sectors, walls and entities into
    // outTagIds (names live only in TagRegistry). A missing field produces
    // an empty vector; malformed entries (wrong JSON type) are skipped and
    // logged, keeping whatever parsed cleanly.
    void LoadTagIds(const json& objectJson, const char* kind, const ID objectId, std::vector<uint16_t>& outTagIds) {
        outTagIds.clear();

        if (!objectJson.contains("tagIds")) return;

        bool malformedEntry = false;

        if (objectJson.at("tagIds").is_array()) {
            for (const json& tagIdJson : objectJson.at("tagIds")) {
                if (tagIdJson.is_number_unsigned() || tagIdJson.is_number_integer())
                    outTagIds.push_back(tagIdJson.get<uint16_t>());
                else malformedEntry = true;
            }
        } else malformedEntry = true;

        if (malformedEntry)
            spdlog::warn("{} {}: malformed tag data - invalid entries were skipped", kind, objectId);
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

    // Defined with the rest of the event code below.
    void LoadEventsAndDispatches(const json& objectJson, const char* ownerKind, ID ownerID,
                                 std::vector<Event>& events, std::vector<Dispatch>& dispatches);
    void SaveEventsAndDispatches(json& objectJson, const std::vector<Event>& events, const std::vector<Dispatch>& dispatches);

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
            worldSettings.maxPhysicsSubsteps = std::clamp(worldSettingsJson.value("maxPhysicsSubsteps", 8), 1, 64);

            LoadEventVariables(worldSettingsJson, worldSettings);
            worldSettings.messageStyle = worldSettingsJson.contains("messageStyle")
                ? MessageStyleFromJson(worldSettingsJson.at("messageStyle"))
                : MessageStyle{};
            LoadEventsAndDispatches(worldSettingsJson, "Level", 0, worldSettings.events, worldSettings.dispatches);
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
                    {"gravity", worldSettings.gravity},
                    {"maxPhysicsSubsteps", worldSettings.maxPhysicsSubsteps},
                    {"eventVariables", SaveEventVariables(worldSettings)},
                    {"nextEventVariableId", worldSettings.nextEventVariableID},
                    {"messageStyle", MessageStyleToJson(worldSettings.messageStyle)}
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

        SaveEventsAndDispatches(levelData["levelStats"]["worldSettings"], worldSettings.events, worldSettings.dispatches);
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

    // ── Events and dispatches ───────────────────────────────────────────────
    // Saved as {"payload": {"type": <saveName>, ...fields}, "tag", ...}.
    // Every field is optional on load (missing = the struct's default); an
    // entry with an unknown type or a field of the wrong type is skipped
    // with a warning, and the rest of the level still loads.

    // Enum <-> name tables, so files never depend on enum values.
    template <typename Enum, size_t N>
    const char* EnumName(const std::pair<Enum, const char*> (&names)[N], const Enum value) {
        for (const auto& [enumValue, name] : names)
            if (enumValue == value) return name;
        return names[0].second;
    }

    template <typename Enum, size_t N>
    Enum EnumFromName(const std::pair<Enum, const char*> (&names)[N], const std::string& name) {
        for (const auto& [enumValue, enumName] : names)
            if (name == enumName) return enumValue;
        throw std::runtime_error("unknown value \"" + name + "\"");
    }

    constexpr std::pair<TagType, const char*> kTagTypeNames[] = {
        {TagType::INCLUDE, "include"}, {TagType::EXCLUDE, "exclude"}, {TagType::ALL, "all"}
    };

    constexpr std::pair<PayloadLineEntityCross::EntityCrossDirection, const char*> kCrossDirectionNames[] = {
        {PayloadLineEntityCross::FORWARD, "forward"},
        {PayloadLineEntityCross::BACKWARD, "backward"},
        {PayloadLineEntityCross::EITHER, "either"}
    };

    constexpr std::pair<SectorCollision, const char*> kSectorCollisionNames[] = {
        {SECTOR_FORCE_MOVE, "force_move"}, {SECTOR_STOP, "stop"}, {SECTOR_RETRACT, "retract"}, {SECTOR_DONT_MOVE, "dont_move"}
    };

    constexpr std::pair<Condition::ConditionType, const char*> kConditionTypeNames[] = {
        {Condition::NONE, "none"},
        {Condition::EQUAL, "equal"},
        {Condition::LESS_THAN, "less_than"},
        {Condition::GREATER_THAN, "greater_than"},
        {Condition::LESS_THAN_OR_EQUAL, "less_than_or_equal"},
        {Condition::GREATER_THAN_OR_EQUAL, "greater_than_or_equal"}
    };

    constexpr std::pair<PayloadDispatchChangeVariable::Operation, const char*> kVariableOperationNames[] = {
        {PayloadDispatchChangeVariable::SET, "set"},
        {PayloadDispatchChangeVariable::ADD, "add"},
        {PayloadDispatchChangeVariable::SUBTRACT, "subtract"}
    };

    constexpr std::pair<LineSide, const char*> kLineSideNames[] = {
        {LineSide::FRONT, "front"}, {LineSide::BACK, "back"}, {LineSide::EITHER, "either"}
    };

    constexpr std::pair<SectorSurfacePick, const char*> kSurfacePickNames[] = {
        {SectorSurfacePick::FLOOR, "floor"}, {SectorSurfacePick::CEILING, "ceiling"}, {SectorSurfacePick::BOTH, "both"}
    };

    constexpr std::pair<ToggleMode, const char*> kToggleModeNames[] = {
        {ToggleMode::ON, "on"}, {ToggleMode::OFF, "off"}, {ToggleMode::TOGGLE, "toggle"}
    };

    constexpr std::pair<RayType, const char*> kRayTypeNames[] = {
        {RayType::GENERAL, "general"},
        {RayType::INTERACT, "interact"},
        {RayType::LINE_OF_SIGHT, "line_of_sight"},
        {RayType::BULLET, "bullet"},
        {RayType::CUSTOM, "custom"}
    };

    constexpr std::pair<PayloadSectorMoveFinished::Reason, const char*> kMoveEndReasonNames[] = {
        {PayloadSectorMoveFinished::ANY, "any"},
        {PayloadSectorMoveFinished::REACHED, "reached"},
        {PayloadSectorMoveFinished::STOPPED, "stopped"},
        {PayloadSectorMoveFinished::RETRACTED, "retracted"}
    };

    constexpr std::pair<WallPiece, const char*> kWallPieceNames[] = {
        {WallPiece::TOP, "top"}, {WallPiece::MIDDLE, "middle"}, {WallPiece::BOTTOM, "bottom"}
    };

    constexpr std::pair<SectorHeightMode, const char*> kHeightModeNames[] = {
        {SectorHeightMode::ABSOLUTE_HEIGHT, "absolute"},
        {SectorHeightMode::RELATIVE_OFFSET, "relative"},
        {SectorHeightMode::LOWEST_NEIGHBOR_FLOOR, "lowest_neighbor_floor"},
        {SectorHeightMode::HIGHEST_NEIGHBOR_FLOOR, "highest_neighbor_floor"},
        {SectorHeightMode::LOWEST_NEIGHBOR_CEILING, "lowest_neighbor_ceiling"},
        {SectorHeightMode::HIGHEST_NEIGHBOR_CEILING, "highest_neighbor_ceiling"}
    };

    constexpr std::pair<PayloadDispatchSectorDoor::OpenMode, const char*> kDoorOpenModeNames[] = {
        {PayloadDispatchSectorDoor::LOWEST_NEIGHBOR_CEILING, "lowest_neighbor_ceiling"},
        {PayloadDispatchSectorDoor::FIXED_HEIGHT, "fixed_height"}
    };

    constexpr std::pair<PayloadDispatchSectorDoor::Retrigger, const char*> kDoorRetriggerNames[] = {
        {PayloadDispatchSectorDoor::RESTART, "restart"},
        {PayloadDispatchSectorDoor::TOGGLE, "toggle"},
        {PayloadDispatchSectorDoor::IGNORE_WHILE_OPEN, "ignore"}
    };

    constexpr std::pair<ToggleableComponent, const char*> kToggleableComponentNames[] = {
        {ToggleableComponent::SPRITE, "sprite"},
        {ToggleableComponent::SCRIPT, "script"},
        {ToggleableComponent::PLAYER_CONTROLLER, "player_controller"},
        {ToggleableComponent::CAMERA, "camera"},
        {ToggleableComponent::COLLIDER, "collider"},
        {ToggleableComponent::UI_SPRITE, "ui_sprite"}
    };

    // Reads `key` into `out` if present; throws on a value of the wrong type.
    template <typename T>
    void ReadOptional(const json& j, const char* key, T& out) {
        if (j.contains(key)) out = j.at(key).get<T>();
    }

    template <typename Enum, size_t N>
    void ReadOptionalEnum(const json& j, const char* key, const std::pair<Enum, const char*> (&names)[N], Enum& out) {
        if (j.contains(key)) out = EnumFromName(names, j.at(key).get<std::string>());
    }

    void ReadOptionalVector3(const json& j, const char* key, Vector3& out) {
        if (!j.contains(key)) return;
        const json& v = j.at(key);
        out = {v.at(0).get<float>(), v.at(1).get<float>(), v.at(2).get<float>()};
    }

    // One side of a condition: {"variable": id} or {"number": value}.
    json ConditionFieldToJson(const Condition::InputField& field) {
        if (const float* number = std::get_if<float>(&field)) return {{"number", *number}};
        return {{"variable", std::get<EventVariableID>(field)}};
    }

    Condition::InputField ConditionFieldFromJson(const json& j) {
        if (j.contains("variable")) return j.at("variable").get<EventVariableID>();
        if (j.contains("number")) return j.at("number").get<float>();
        throw std::runtime_error("condition side has neither \"variable\" nor \"number\"");
    }

    json ConditionToJson(const Condition& condition) {
        return {
            {"type", EnumName(kConditionTypeNames, condition.type)},
            {"left", ConditionFieldToJson(condition.left)},
            {"right", ConditionFieldToJson(condition.right)}
        };
    }

    Condition ConditionFromJson(const json& j) {
        Condition condition;
        ReadOptionalEnum(j, "type", kConditionTypeNames, condition.type);
        if (j.contains("left")) condition.left = ConditionFieldFromJson(j.at("left"));
        if (j.contains("right")) condition.right = ConditionFieldFromJson(j.at("right"));
        return condition;
    }

    json TagFilterToJson(const TagFilter& filter) {
        return {{"type", EnumName(kTagTypeNames, filter.tagType)}, {"tags", filter.tags}};
    }

    void TagFilterFromJson(const json& j, TagFilter& filter) {
        ReadOptionalEnum(j, "type", kTagTypeNames, filter.tagType);
        ReadOptional(j, "tags", filter.tags);
    }

    // Per payload type: its fields (without "type").
    template <typename Payload>
    json PayloadFieldsToJson(const Payload& payload) {
        json j = json::object();

        if constexpr (requires { payload.filter; }) j["filter"] = TagFilterToJson(payload.filter);

        if constexpr (std::is_same_v<Payload, PayloadLineEntityCross>)
            j["direction"] = EnumName(kCrossDirectionNames, payload.direction);

        if constexpr (std::is_same_v<Payload, PayloadDispatchLineSetColor>) {
            j["color"] = {payload.color.x, payload.color.y, payload.color.z};
            j["speed"] = payload.speed;
        }

        if constexpr (std::is_same_v<Payload, PayloadDispatchSectorMoveFloorToCeiling> ||
                      std::is_same_v<Payload, PayloadDispatchSectorMoveCeilingToFloor>) {
            j["gap"] = payload.gap;
            j["speed"] = payload.speed;
            j["floorIndex"] = payload.floorIndex;
            j["collision"] = EnumName(kSectorCollisionNames, payload.collisionType);
        }

        if constexpr (std::is_same_v<Payload, PayloadDispatchEntityTeleport>) {
            j["position"] = {payload.position.x, payload.position.y, payload.position.z};
            j["rotation"] = {payload.rotation.x, payload.rotation.y, payload.rotation.z, payload.rotation.w};
        }

        if constexpr (std::is_same_v<Payload, PayloadDispatchEntityCallFunction>)
            j["functionName"] = payload.functionName;

        if constexpr (std::is_same_v<Payload, PayloadDispatchChangeVariable>) {
            j["variable"] = payload.variable;
            j["operation"] = EnumName(kVariableOperationNames, payload.operation);
            j["value"] = payload.value;
        }

        if constexpr (std::is_same_v<Payload, PayloadLineEntityCollide>)
            j["side"] = EnumName(kLineSideNames, payload.side);

        if constexpr (std::is_same_v<Payload, PayloadSectorEntityStay>)
            j["interval"] = payload.interval;

        if constexpr (std::is_same_v<Payload, PayloadSectorMoveFinished>) {
            j["surface"] = EnumName(kSurfacePickNames, payload.surface);
            j["floorIndex"] = payload.floorIndex;
            j["reason"] = EnumName(kMoveEndReasonNames, payload.reason);
        }

        if constexpr (std::is_same_v<Payload, PayloadReceiveRay>) {
            j["rayType"] = EnumName(kRayTypeNames, payload.rayType);
            j["firstHitOnly"] = payload.firstHitOnly;
            j["side"] = EnumName(kLineSideNames, payload.side);
        }

        if constexpr (std::is_same_v<Payload, PayloadTimer>) {
            j["interval"] = payload.interval;
            j["firstDelay"] = payload.firstDelay;
        }

        if constexpr (std::is_same_v<Payload, PayloadLuaSignal>)
            j["name"] = payload.name;

        if constexpr (std::is_same_v<Payload, PayloadDispatchLineSetTexture>) {
            j["piece"] = EnumName(kWallPieceNames, payload.piece);
            j["texture"] = payload.texture;
        }

        if constexpr (std::is_same_v<Payload, PayloadDispatchLineSetCollision> ||
                      std::is_same_v<Payload, PayloadDispatchLinedefSetEnabled>)
            j["mode"] = EnumName(kToggleModeNames, payload.mode);

        if constexpr (std::is_same_v<Payload, PayloadDispatchSectorMoveToHeight>) {
            j["ceiling"] = payload.ceiling;
            j["mode"] = EnumName(kHeightModeNames, payload.mode);
            j["height"] = payload.height;
            j["speed"] = payload.speed;
            j["floorIndex"] = payload.floorIndex;
            j["collision"] = EnumName(kSectorCollisionNames, payload.collisionType);
        }

        if constexpr (std::is_same_v<Payload, PayloadDispatchSectorDoor>) {
            j["openMode"] = EnumName(kDoorOpenModeNames, payload.openMode);
            j["lip"] = payload.lip;
            j["openHeight"] = payload.openHeight;
            j["openSpeed"] = payload.openSpeed;
            j["closeSpeed"] = payload.closeSpeed;
            j["wait"] = payload.wait;
            j["floorIndex"] = payload.floorIndex;
            j["closeCollision"] = EnumName(kSectorCollisionNames, payload.closeCollision);
            j["retrigger"] = EnumName(kDoorRetriggerNames, payload.retrigger);
        }

        if constexpr (std::is_same_v<Payload, PayloadDispatchSectorStopMovement>) {
            j["surface"] = EnumName(kSurfacePickNames, payload.surface);
            j["floorIndex"] = payload.floorIndex;
        }

        if constexpr (std::is_same_v<Payload, PayloadDispatchSectorSetLight>) {
            j["light"] = {payload.light.x, payload.light.y, payload.light.z};
            j["speed"] = payload.speed;
        }

        if constexpr (std::is_same_v<Payload, PayloadDispatchSectorSetTexture>) {
            j["surface"] = EnumName(kSurfacePickNames, payload.surface);
            j["floorIndex"] = payload.floorIndex;
            j["texture"] = payload.texture;
        }

        if constexpr (std::is_same_v<Payload, PayloadDispatchChangeTag>) {
            j["remove"] = payload.remove;
            j["tag"] = payload.tag;
        }

        if constexpr (std::is_same_v<Payload, PayloadDispatchEntityChangeVelocity>) {
            j["set"] = payload.set;
            j["velocity"] = {payload.velocity.x, payload.velocity.y, payload.velocity.z};
        }

        if constexpr (std::is_same_v<Payload, PayloadDispatchEntityMoveTo>) {
            j["position"] = {payload.position.x, payload.position.y, payload.position.z};
            j["relative"] = payload.relative;
            j["useDuration"] = payload.useDuration;
            j["speed"] = payload.speed;
            j["duration"] = payload.duration;
            j["rotate"] = payload.rotate;
            j["rotation"] = {payload.rotation.x, payload.rotation.y, payload.rotation.z};
        }

        if constexpr (std::is_same_v<Payload, PayloadDispatchEntitySetComponent>) {
            j["component"] = EnumName(kToggleableComponentNames, payload.component);
            j["mode"] = EnumName(kToggleModeNames, payload.mode);
        }

        if constexpr (std::is_same_v<Payload, PayloadDispatchEntitySetSprite>) {
            j["textures"] = payload.textures;
        }

        if constexpr (std::is_same_v<Payload, PayloadDispatchSpawnPrefab>) {
            j["prefab"] = payload.prefab;
            j["offset"] = {payload.offset.x, payload.offset.y, payload.offset.z};
            j["rotation"] = {payload.rotation.x, payload.rotation.y, payload.rotation.z};
            j["worldPosition"] = payload.worldPosition;
        }

        if constexpr (std::is_same_v<Payload, PayloadDispatchPlaySound>) {
            j["sound"] = payload.sound;
            j["gain"] = payload.gain;
            j["pitch"] = payload.pitch;
        }

        if constexpr (std::is_same_v<Payload, PayloadDispatchShowMessage>) {
            j["text"] = payload.text;
            j["duration"] = payload.duration;
            j["color"] = {payload.color.x, payload.color.y, payload.color.z};
        }

        if constexpr (std::is_same_v<Payload, PayloadDispatchRelay> || std::is_same_v<Payload, PayloadDispatchResetEvent>)
            j["target"] = payload.target;

        if constexpr (std::is_same_v<Payload, PayloadDispatchLoadLevel>)
            j["levelName"] = payload.levelName;

        return j;
    }

    template <typename Payload>
    void PayloadFieldsFromJson(const json& j, Payload& payload) {
        if constexpr (requires { payload.filter; })
            if (j.contains("filter")) TagFilterFromJson(j.at("filter"), payload.filter);

        if constexpr (std::is_same_v<Payload, PayloadLineEntityCross>)
            ReadOptionalEnum(j, "direction", kCrossDirectionNames, payload.direction);

        if constexpr (std::is_same_v<Payload, PayloadDispatchLineSetColor>) {
            ReadOptionalVector3(j, "color", payload.color);
            ReadOptional(j, "speed", payload.speed);
        }

        if constexpr (std::is_same_v<Payload, PayloadDispatchSectorMoveFloorToCeiling> ||
                      std::is_same_v<Payload, PayloadDispatchSectorMoveCeilingToFloor>) {
            ReadOptional(j, "gap", payload.gap);
            ReadOptional(j, "speed", payload.speed);
            ReadOptional(j, "floorIndex", payload.floorIndex);
            ReadOptionalEnum(j, "collision", kSectorCollisionNames, payload.collisionType);
        }

        if constexpr (std::is_same_v<Payload, PayloadDispatchEntityTeleport>) {
            ReadOptionalVector3(j, "position", payload.position);
            if (j.contains("rotation")) {
                const json& r = j.at("rotation");
                payload.rotation = Quaternion{
                    r.at(0).get<float>(), r.at(1).get<float>(), r.at(2).get<float>(), r.at(3).get<float>()
                }.Normalized();
            }
        }

        if constexpr (std::is_same_v<Payload, PayloadDispatchEntityCallFunction>)
            ReadOptional(j, "functionName", payload.functionName);

        if constexpr (std::is_same_v<Payload, PayloadDispatchChangeVariable>) {
            ReadOptional(j, "variable", payload.variable);
            ReadOptionalEnum(j, "operation", kVariableOperationNames, payload.operation);
            ReadOptional(j, "value", payload.value);
        }

        if constexpr (std::is_same_v<Payload, PayloadLineEntityCollide>)
            ReadOptionalEnum(j, "side", kLineSideNames, payload.side);

        if constexpr (std::is_same_v<Payload, PayloadSectorEntityStay>)
            ReadOptional(j, "interval", payload.interval);

        if constexpr (std::is_same_v<Payload, PayloadSectorMoveFinished>) {
            ReadOptionalEnum(j, "surface", kSurfacePickNames, payload.surface);
            ReadOptional(j, "floorIndex", payload.floorIndex);
            ReadOptionalEnum(j, "reason", kMoveEndReasonNames, payload.reason);
        }

        if constexpr (std::is_same_v<Payload, PayloadReceiveRay>) {
            ReadOptionalEnum(j, "rayType", kRayTypeNames, payload.rayType);
            ReadOptional(j, "firstHitOnly", payload.firstHitOnly);
            ReadOptionalEnum(j, "side", kLineSideNames, payload.side);
        }

        if constexpr (std::is_same_v<Payload, PayloadTimer>) {
            ReadOptional(j, "interval", payload.interval);
            ReadOptional(j, "firstDelay", payload.firstDelay);
        }

        if constexpr (std::is_same_v<Payload, PayloadLuaSignal>)
            ReadOptional(j, "name", payload.name);

        if constexpr (std::is_same_v<Payload, PayloadDispatchLineSetTexture>) {
            ReadOptionalEnum(j, "piece", kWallPieceNames, payload.piece);
            ReadOptional(j, "texture", payload.texture);
        }

        if constexpr (std::is_same_v<Payload, PayloadDispatchLineSetCollision> ||
                      std::is_same_v<Payload, PayloadDispatchLinedefSetEnabled>)
            ReadOptionalEnum(j, "mode", kToggleModeNames, payload.mode);

        if constexpr (std::is_same_v<Payload, PayloadDispatchSectorMoveToHeight>) {
            ReadOptional(j, "ceiling", payload.ceiling);
            ReadOptionalEnum(j, "mode", kHeightModeNames, payload.mode);
            ReadOptional(j, "height", payload.height);
            ReadOptional(j, "speed", payload.speed);
            ReadOptional(j, "floorIndex", payload.floorIndex);
            ReadOptionalEnum(j, "collision", kSectorCollisionNames, payload.collisionType);
        }

        if constexpr (std::is_same_v<Payload, PayloadDispatchSectorDoor>) {
            ReadOptionalEnum(j, "openMode", kDoorOpenModeNames, payload.openMode);
            ReadOptional(j, "lip", payload.lip);
            ReadOptional(j, "openHeight", payload.openHeight);
            ReadOptional(j, "openSpeed", payload.openSpeed);
            ReadOptional(j, "closeSpeed", payload.closeSpeed);
            ReadOptional(j, "wait", payload.wait);
            ReadOptional(j, "floorIndex", payload.floorIndex);
            ReadOptionalEnum(j, "closeCollision", kSectorCollisionNames, payload.closeCollision);
            ReadOptionalEnum(j, "retrigger", kDoorRetriggerNames, payload.retrigger);
        }

        if constexpr (std::is_same_v<Payload, PayloadDispatchSectorStopMovement>) {
            ReadOptionalEnum(j, "surface", kSurfacePickNames, payload.surface);
            ReadOptional(j, "floorIndex", payload.floorIndex);
        }

        if constexpr (std::is_same_v<Payload, PayloadDispatchSectorSetLight>) {
            ReadOptionalVector3(j, "light", payload.light);
            ReadOptional(j, "speed", payload.speed);
        }

        if constexpr (std::is_same_v<Payload, PayloadDispatchSectorSetTexture>) {
            ReadOptionalEnum(j, "surface", kSurfacePickNames, payload.surface);
            ReadOptional(j, "floorIndex", payload.floorIndex);
            ReadOptional(j, "texture", payload.texture);
        }

        if constexpr (std::is_same_v<Payload, PayloadDispatchChangeTag>) {
            ReadOptional(j, "remove", payload.remove);
            ReadOptional(j, "tag", payload.tag);
        }

        if constexpr (std::is_same_v<Payload, PayloadDispatchEntityChangeVelocity>) {
            ReadOptional(j, "set", payload.set);
            ReadOptionalVector3(j, "velocity", payload.velocity);
        }

        if constexpr (std::is_same_v<Payload, PayloadDispatchEntityMoveTo>) {
            ReadOptionalVector3(j, "position", payload.position);
            ReadOptional(j, "relative", payload.relative);
            ReadOptional(j, "useDuration", payload.useDuration);
            ReadOptional(j, "speed", payload.speed);
            ReadOptional(j, "duration", payload.duration);
            ReadOptional(j, "rotate", payload.rotate);
            ReadOptionalVector3(j, "rotation", payload.rotation);
        }

        if constexpr (std::is_same_v<Payload, PayloadDispatchEntitySetComponent>) {
            ReadOptionalEnum(j, "component", kToggleableComponentNames, payload.component);
            ReadOptionalEnum(j, "mode", kToggleModeNames, payload.mode);
        }

        if constexpr (std::is_same_v<Payload, PayloadDispatchEntitySetSprite>) {
            ReadOptional(j, "textures", payload.textures);
        }

        if constexpr (std::is_same_v<Payload, PayloadDispatchSpawnPrefab>) {
            ReadOptional(j, "prefab", payload.prefab);
            ReadOptionalVector3(j, "offset", payload.offset);
            ReadOptionalVector3(j, "rotation", payload.rotation);
            ReadOptional(j, "worldPosition", payload.worldPosition);
        }

        if constexpr (std::is_same_v<Payload, PayloadDispatchPlaySound>) {
            ReadOptional(j, "sound", payload.sound);
            ReadOptional(j, "gain", payload.gain);
            ReadOptional(j, "pitch", payload.pitch);
        }

        if constexpr (std::is_same_v<Payload, PayloadDispatchShowMessage>) {
            ReadOptional(j, "text", payload.text);
            ReadOptional(j, "duration", payload.duration);
            ReadOptionalVector3(j, "color", payload.color);
        }

        if constexpr (std::is_same_v<Payload, PayloadDispatchRelay> || std::is_same_v<Payload, PayloadDispatchResetEvent>)
            ReadOptional(j, "target", payload.target);

        if constexpr (std::is_same_v<Payload, PayloadDispatchLoadLevel>)
            ReadOptional(j, "levelName", payload.levelName);
    }

    template <typename Variant>
    json PayloadToJson(const Variant& payload) {
        return std::visit([](const auto& p) {
            json j = PayloadFieldsToJson(p);
            j["type"] = std::decay_t<decltype(p)>::saveName;
            return j;
        }, payload);
    }

    // Throws if the type is unknown or a field is malformed.
    template <typename Variant>
    Variant PayloadFromJson(const json& j) {
        const std::string type = j.at("type").get<std::string>();
        std::optional<Variant> result;

        [&]<size_t... I>(std::index_sequence<I...>) {
            ([&] {
                using Payload = std::variant_alternative_t<I, Variant>;
                if (result || type != Payload::saveName) return;

                Payload payload;
                PayloadFieldsFromJson(j, payload);
                result = std::move(payload);
            }(), ...);
        }(std::make_index_sequence<std::variant_size_v<Variant>>{});

        if (!result) throw std::runtime_error("unknown type \"" + type + "\"");
        return std::move(*result);
    }

    json EventToJson(const Event& event) {
        json j = {
            {"payload", PayloadToJson(event.payload)},
            {"tag", event.tagId},
            {"maxTriggers", event.maxTriggers},
            {"chance", event.chance}
        };

        // Only written when there is one, so events without a condition save as before.
        if (event.condition.type != Condition::NONE) j["condition"] = ConditionToJson(event.condition);

        return j;
    }

    Event EventFromJson(const json& j) {
        Event event;
        event.payload = PayloadFromJson<EventPayload>(j.at("payload"));
        ReadOptional(j, "tag", event.tagId);
        ReadOptional(j, "maxTriggers", event.maxTriggers);
        ReadOptional(j, "chance", event.chance);
        if (j.contains("condition")) event.condition = ConditionFromJson(j.at("condition"));
        return event;
    }

    json DispatchToJson(const Dispatch& dispatch) {
        json j = {{"payload", PayloadToJson(dispatch.payload)}, {"tag", dispatch.tagId}};

        // Only written when there is one, so dispatches without a delay save as before.
        if (dispatch.delay > 0.0f) j["delay"] = dispatch.delay;
        if (dispatch.cooldown > 0.0f) j["cooldown"] = dispatch.cooldown;
        if (dispatch.cooldownTriggers != 1) j["cooldownTriggers"] = dispatch.cooldownTriggers;

        return j;
    }

    Dispatch DispatchFromJson(const json& j) {
        Dispatch dispatch;
        dispatch.payload = PayloadFromJson<DispatchPayload>(j.at("payload"));
        ReadOptional(j, "tag", dispatch.tagId);
        ReadOptional(j, "delay", dispatch.delay);
        ReadOptional(j, "cooldown", dispatch.cooldown);
        ReadOptional(j, "cooldownTriggers", dispatch.cooldownTriggers);
        dispatch.cooldownTriggers = std::max(1, dispatch.cooldownTriggers);
        return dispatch;
    }

    // Only written when there is something to write, so objects without
    // events save exactly as before.
    void SaveEventsAndDispatches(json& objectJson, const std::vector<Event>& events, const std::vector<Dispatch>& dispatches) {
        if (!events.empty()) {
            json eventArray = json::array();
            for (const Event& event : events) eventArray.push_back(EventToJson(event));
            objectJson["events"] = std::move(eventArray);
        }

        if (!dispatches.empty()) {
            json dispatchArray = json::array();
            for (const Dispatch& dispatch : dispatches) dispatchArray.push_back(DispatchToJson(dispatch));
            objectJson["dispatches"] = std::move(dispatchArray);
        }
    }

    template <typename T, typename FromJson>
    void LoadList(const json& objectJson, const char* key, const char* ownerKind, const ID ownerID,
                  std::vector<T>& out, FromJson&& fromJson) {
        out.clear();
        if (!objectJson.contains(key)) return;

        const json& array = objectJson.at(key);
        if (!array.is_array()) {
            spdlog::warn("{} {}: \"{}\" is not an array, ignored", ownerKind, ownerID, key);
            return;
        }

        for (size_t i = 0; i < array.size(); ++i) {
            try {
                out.push_back(fromJson(array[i]));
            } catch (const std::exception& exception) {
                spdlog::warn("{} {}: skipped {} entry {}: {}", ownerKind, ownerID, key, i, exception.what());
            }
        }
    }

    void LoadEventsAndDispatches(const json& objectJson, const char* ownerKind, const ID ownerID,
                                 std::vector<Event>& events, std::vector<Dispatch>& dispatches) {
        LoadList(objectJson, "events", ownerKind, ownerID, events, EventFromJson);
        LoadList(objectJson, "dispatches", ownerKind, ownerID, dispatches, DispatchFromJson);
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

            LoadTagIds(entityJson, "Entity", entity.id, entity.tagIds);
            LoadEventsAndDispatches(entityJson, "Entity", entity.id, entity.events, entity.dispatches);

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
                {"tagIds", entity->tagIds}
            };

            if (entity->prefabLink.IsInstance()) entityJson["prefab"] = PrefabLinkToJson(entity->prefabLink);
            SaveEventsAndDispatches(entityJson, entity->events, entity->dispatches);

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

            LoadTagIds(wallJson, "Wall", wall.id, wall.tagIds);
            LoadEventsAndDispatches(wallJson, "Wall", wall.id, wall.events, wall.dispatches);

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
            json wallJson = {
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
                {"tagIds", wall.tagIds},
                {"angle", wall.angle}
            };

            SaveEventsAndDispatches(wallJson, wall.events, wall.dispatches);
            levelData["walls"].push_back(std::move(wallJson));
        }
    }

    void LoadLinedefs(const json &levelData, Level &level) {
        level.linedefs.clear();

        if (!levelData.contains("linedefs") || !levelData.at("linedefs").is_array()) return;

        ID highestLinedefID = 0;
        std::unordered_set<ID> seenLinedefIDs;

        const json &linedefsJson = levelData.at("linedefs");

        for (int i = 0; i < static_cast<int>(linedefsJson.size()); ++i) {
            const json &linedefJson = linedefsJson.at(i);

            Linedef linedef(
                {linedefJson.at("start").at(0).get<float>(), linedefJson.at("start").at(1).get<float>()},
                {linedefJson.at("end").at(0).get<float>(), linedefJson.at("end").at(1).get<float>()}
            );

            linedef.id = LoadIDField(linedefJson, "id", static_cast<ID>(i));
            if (linedef.id == INVALID_ID) linedef.id = static_cast<ID>(i);

            if (seenLinedefIDs.contains(linedef.id)) {
                const ID reassigned = highestLinedefID + 1;
                spdlog::warn("LoadLinedefs: duplicate linedef id {} at array index {} - reassigning to {}",
                             linedef.id, i, reassigned);
                linedef.id = reassigned;
            }

            seenLinedefIDs.insert(linedef.id);
            highestLinedefID = std::max(highestLinedefID, linedef.id);

            linedef.name = linedefJson.value("name", std::string{});
            linedef.enabled = linedefJson.value("enabled", true);
            LoadEventsAndDispatches(linedefJson, "Linedef", linedef.id, linedef.events, linedef.dispatches);

            level.linedefs.push_back(std::move(linedef));
        }

        level.nextLinedefID = std::max(level.nextLinedefID, highestLinedefID + 1);
    }

    void SaveLinedefs(json &levelData, const Level &level) {
        levelData["linedefs"] = json::array();

        for (const Linedef &linedef: level.linedefs) {
            json linedefJson = {
                {"id", linedef.id},
                {"name", linedef.name},
                {"start", {linedef.start.x, linedef.start.y}},
                {"end", {linedef.end.x, linedef.end.y}},
                {"enabled", linedef.enabled}
            };

            SaveEventsAndDispatches(linedefJson, linedef.events, linedef.dispatches);
            levelData["linedefs"].push_back(std::move(linedefJson));
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

            if (sectorJson.contains("tagIds")) {
                LoadTagIds(sectorJson, "Sector", sector.id, sector.tagIds);
            } else if (sectorJson.contains("tagId")) {
                // Legacy single-tag format (pre-multi-tag) - load as a
                // one-element vector rather than dropping the tag.
                sector.tagIds.clear();

                const json &legacyTagId = sectorJson.at("tagId");

                if (legacyTagId.is_number_unsigned() || legacyTagId.is_number_integer())
                    sector.tagIds.push_back(legacyTagId.get<uint16_t>());
                else
                    spdlog::warn("Sector {}: malformed legacy tag data - skipping", sector.id);
            } else {
                sector.tagIds.clear();
            }

            LoadSectorScripts(sectorJson, sector);
            LoadEventsAndDispatches(sectorJson, "Sector", sector.id, sector.events, sector.dispatches);

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

            SaveEventsAndDispatches(sectorJson, sector.events, sector.dispatches);

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
            {"airControl", c.airControl},
            {"useKey", static_cast<int>(c.useKey)},
            {"useReach", c.useReach},
            {"useRayType", static_cast<int>(c.useRayType)}
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
        c.useKey = static_cast<SDL_Scancode>(j.value("useKey", static_cast<int>(SDL_SCANCODE_E)));
        c.useReach = j.value("useReach", 16.0f);
        c.useRayType = static_cast<RayType>(std::clamp(j.value("useRayType", static_cast<int>(RayType::INTERACT)),
                                                       0, static_cast<int>(RayType::COUNT) - 1));
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

            const auto loadIDs = [](const json& groupJson, const char* key, std::vector<ID>& out) {
                if (!groupJson.contains(key) || !groupJson[key].is_array()) return;

                for (const json& id : groupJson[key])
                    if (id.is_number_integer()) out.push_back(id.get<ID>());
            };

            if (metadatajson.contains("selectionGroups") && metadatajson["selectionGroups"].is_array()) {
                const json& groupsJson = metadatajson["selectionGroups"];

                for (std::size_t i = 0; i < groupsJson.size() && i < level.selectionGroups.size(); ++i) {
                    if (!groupsJson[i].is_object()) continue;

                    EditorSelectionGroup& group = level.selectionGroups[i];
                    loadIDs(groupsJson[i], "walls", group.walls);
                    loadIDs(groupsJson[i], "sectors", group.sectors);
                    loadIDs(groupsJson[i], "linedefs", group.linedefs);
                    loadIDs(groupsJson[i], "entities", group.entities);
                }
            }

            if (metadatajson.contains("comments") && metadatajson["comments"].is_array()) {
                for (const json& commentJson : metadatajson["comments"]) {
                    if (!commentJson.is_object() || !commentJson.contains("id") || !commentJson["id"].is_number_integer()) continue;

                    EditorComment comment;
                    comment.id = commentJson["id"].get<ID>();
                    comment.target = static_cast<EditorCommentTarget>(std::clamp(commentJson.value("target", 0), 0, static_cast<int>(EditorCommentTarget::ENTITY)));
                    comment.targetID = commentJson.value("targetID", static_cast<int64_t>(INVALID_ID));
                    comment.text = commentJson.value("text", std::string());

                    if (commentJson.contains("position") && commentJson["position"].is_array() && commentJson["position"].size() == 2)
                        comment.position = {commentJson["position"][0].get<float>(), commentJson["position"][1].get<float>()};

                    level.nextCommentID = std::max(level.nextCommentID, comment.id + 1);
                    level.comments.push_back(std::move(comment));
                }
            }

            if (metadatajson.contains("grid") && metadatajson["grid"].is_object()) {
                const json& gridJson = metadatajson["grid"];

                if (gridJson.contains("origin") && gridJson["origin"].is_array() && gridJson["origin"].size() == 2)
                    level.gridTransform.origin = {gridJson["origin"][0].get<float>(), gridJson["origin"][1].get<float>()};

                level.gridTransform.angle = gridJson.value("angle", 0.0f);
            }

            if (metadatajson.contains("backgroundImage") && metadatajson["backgroundImage"].is_object()) {
                const json& imageJson = metadatajson["backgroundImage"];
                EditorBackgroundImage& image = level.backgroundImage;

                image.texture = imageJson.value("texture", std::string());
                image.scale = std::max(imageJson.value("scale", 1.0f), 0.0001f);
                image.opacity = std::clamp(imageJson.value("opacity", 0.5f), 0.0f, 1.0f);
                image.visible = imageJson.value("visible", true);

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

        json groupsJson = json::array();

        for (const EditorSelectionGroup& group : level.selectionGroups) {
            groupsJson.push_back({
                {"walls", group.walls},
                {"sectors", group.sectors},
                {"linedefs", group.linedefs},
                {"entities", group.entities}
            });
        }

        metadatajson["selectionGroups"] = std::move(groupsJson);

        // Comments on objects deleted since are left out.
        json commentsJson = json::array();

        for (const EditorComment& comment : level.comments) {
            const bool targetExists =
                comment.target == EditorCommentTarget::PIN ||
                (comment.target == EditorCommentTarget::WALL && level.wallIDToIndex.contains(comment.targetID)) ||
                (comment.target == EditorCommentTarget::SECTOR && level.GetSector(comment.targetID) != nullptr) ||
                (comment.target == EditorCommentTarget::LINEDEF && level.GetLinedef(comment.targetID) != nullptr) ||
                (comment.target == EditorCommentTarget::ENTITY && level.GetEntity(comment.targetID) != nullptr);

            if (!targetExists) continue;

            commentsJson.push_back({
                {"id", comment.id},
                {"target", static_cast<int>(comment.target)},
                {"targetID", comment.targetID},
                {"position", {comment.position.x, comment.position.y}},
                {"text", comment.text}
            });
        }

        metadatajson["comments"] = std::move(commentsJson);

        metadatajson["grid"] = {
            {"origin", {level.gridTransform.origin.x, level.gridTransform.origin.y}},
            {"angle", level.gridTransform.angle}
        };

        const EditorBackgroundImage& image = level.backgroundImage;

        metadatajson["backgroundImage"] = {
            {"texture", image.texture},
            {"scale", image.scale},
            {"opacity", image.opacity},
            {"visible", image.visible}
        };

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
            LoadLinedefs(levelData, loadedLevel);
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

    bool StripEditorData(const fs::path& levelFile, bool* removed, std::string* errorMessage) {
        if (removed != nullptr) *removed = false;

        json levelData;
        if (!ReadBsonFile(levelFile, "level", levelData, errorMessage)) return false;

        // Everything SaveMetadata writes lives under this one key.
        if (!levelData.is_object() || !levelData.contains("metadata")) return true;

        levelData.erase("metadata");
        if (!WriteBsonFile(levelFile, "level", levelData, errorMessage)) return false;

        if (removed != nullptr) *removed = true;
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
        SaveLinedefs(levelData, level);
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
                {"private", field.isPrivate},
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
                field.isPrivate = fieldJson.value("private", false);

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
