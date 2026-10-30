
// Created by berke on 6/22/2026.
//

#include "Headers/Objects/Prefab.hpp"
#include "Headers/Runtime/EventSystem.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaScriptRuntime.hpp"
#include "Headers/Map/MapQueries.hpp"
#include "Headers/Objects/LuaWrappers.hpp"
#include "Headers/Runtime/Gameplay/GameFunctions.hpp"
#include "Headers/Runtime/PhysicsSystem.hpp"
#include "Headers/Runtime/Sound/SoundManager.hpp"
#include <sol/state.hpp>

#include "Headers/Map/LevelManager.hpp"
#include "Headers/Map/LevelSerialization.hpp"
#include "Headers/Runtime/LevelSystem.hpp"
#include "../../../../Headers/Runtime/Scripting/Lua/LuaScripting.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaBindingMetadata.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaRayHit.hpp"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

namespace {
    using namespace LuaBindingMetadata;

    struct RayTypeInfo {
        const char* name;
        RayType type;
    };

    constexpr RayTypeInfo RAY_TYPES[] = {
        {"General", RayType::GENERAL},
        {"Interact", RayType::INTERACT},
        {"LineOfSight", RayType::LINE_OF_SIGHT},
        {"Bullet", RayType::BULLET},
        {"Custom", RayType::CUSTOM},
    };

    // ── Property tables (Game.MessageBox, Game.Sky) ─────────────────────────

    struct NamedValue {
        const char* name;
        int value;
    };

    // The Lua name of the table whose properties edit a `Settings`, for errors.
    template <typename Settings> constexpr const char* PROPERTY_TABLE = nullptr;
    template <> constexpr const char* PROPERTY_TABLE<MessageStyle> = "Game.MessageBox";
    template <> constexpr const char* PROPERTY_TABLE<SkySettings> = "Game.Sky";

    // One property of such a table: reads a field of the current level's
    // `Settings` into a Lua value, or checks and writes one.
    template <typename Settings>
    struct LuaProperty {
        const char* name;
        const char* luaType;
        const char* doc;
        std::function<sol::object(sol::state_view, const Settings&)> get;
        std::function<void(Settings&, const sol::object&)> set;
    };

    template <typename Settings>
    [[noreturn]] void ThrowPropertyType(const char* property, const std::string& expected) {
        throw sol::error(std::string(PROPERTY_TABLE<Settings>) + "." + property + " expects " + expected);
    }

    template <typename Settings, typename T>
    T PropertyValue(const sol::object& value, const char* property, const char* expected) {
        if (!value.is<T>()) ThrowPropertyType<Settings>(property, expected);
        return value.as<T>();
    }

    template <typename Settings>
    LuaProperty<Settings> NumberProperty(const char* name, const char* doc, float Settings::* field,
                                         const float minimum = -std::numeric_limits<float>::max()) {
        return {
            name, "number", doc,
            [field](sol::state_view lua, const Settings& settings) { return sol::make_object(lua, settings.*field); },
            [name, field, minimum](Settings& settings, const sol::object& value) {
                settings.*field = std::max(minimum, PropertyValue<Settings, float>(value, name, "a number"));
            },
        };
    }

    template <typename Settings, typename T>
    LuaProperty<Settings> ValueProperty(const char* name, const char* luaType, const char* expected, const char* doc,
                                        T Settings::* field) {
        return {
            name, luaType, doc,
            [field](sol::state_view lua, const Settings& settings) { return sol::make_object(lua, settings.*field); },
            [name, expected, field](Settings& settings, const sol::object& value) {
                settings.*field = PropertyValue<Settings, T>(value, name, expected);
            },
        };
    }

    template <typename Settings>
    LuaProperty<Settings> BoolProperty(const char* name, const char* doc, bool Settings::* field) {
        return ValueProperty(name, "boolean", "a boolean", doc, field);
    }

    template <typename Settings>
    LuaProperty<Settings> StringProperty(const char* name, const char* doc, std::string Settings::* field) {
        return ValueProperty(name, "string", "a string", doc, field);
    }

    template <typename Settings>
    LuaProperty<Settings> Vector2Property(const char* name, const char* doc, Vector2 Settings::* field) {
        return ValueProperty(name, "Vector2", "a Vector2", doc, field);
    }

    template <typename Settings>
    LuaProperty<Settings> Vector3Property(const char* name, const char* doc, Vector3 Settings::* field) {
        return ValueProperty(name, "Vector3", "a Vector3", doc, field);
    }

    // Stored 0..1, seen from Lua as 0..255 like Game.MessageBox.SetMessage's colour.
    template <typename Settings>
    LuaProperty<Settings> ColorProperty(const char* name, const char* doc, Vector4 Settings::* field) {
        return {
            name, "Vector4", doc,
            [field](sol::state_view lua, const Settings& settings) { return sol::make_object(lua, (settings.*field) * 255.0f); },
            [name, field](Settings& settings, const sol::object& value) {
                const Vector4 color = PropertyValue<Settings, Vector4>(value, name, "a Vector4 (0..255 per channel)");
                settings.*field = {
                    std::clamp(color.x / 255.0f, 0.0f, 1.0f), std::clamp(color.y / 255.0f, 0.0f, 1.0f),
                    std::clamp(color.z / 255.0f, 0.0f, 1.0f), std::clamp(color.w / 255.0f, 0.0f, 1.0f)
                };
            },
        };
    }

    // `values` is the Lua enum table `luaType`'s names and values.
    template <typename Settings, typename Enum>
    LuaProperty<Settings> EnumProperty(const char* name, const char* luaType, const char* doc, Enum Settings::* field,
                                       const std::span<const NamedValue> values) {
        return {
            name, luaType, doc,
            [field](sol::state_view lua, const Settings& settings) { return sol::make_object(lua, static_cast<int>(settings.*field)); },
            [name, luaType, field, values](Settings& settings, const sol::object& value) {
                const std::string expected = std::string("a ") + luaType + " value, e.g. " + luaType + "." + values[0].name;
                if (!value.is<int>()) ThrowPropertyType<Settings>(name, expected);

                const int number = value.as<int>();
                if (std::ranges::none_of(values, [&](const NamedValue& v) { return v.value == number; }))
                    ThrowPropertyType<Settings>(name, expected);
                settings.*field = static_cast<Enum>(number);
            },
        };
    }

    template <typename Settings>
    const LuaProperty<Settings>& FindProperty(const std::vector<LuaProperty<Settings>>& properties, const std::string& name) {
        for (const LuaProperty<Settings>& property : properties)
            if (name == property.name) return property;
        throw sol::error(std::string(PROPERTY_TABLE<Settings>) + " has no property or function \"" + name + "\"");
    }

    template <typename Settings>
    std::vector<PropertyDoc> PropertyDocs(const std::vector<LuaProperty<Settings>>& properties) {
        std::vector<PropertyDoc> docs;
        for (const LuaProperty<Settings>& property : properties)
            docs.push_back(Prop(property.name, property.luaType, false, property.doc));
        return docs;
    }

    // Reading or writing a name `table` doesn't have goes to `properties`,
    // on whatever `current()` returns then (the current level's settings).
    template <typename Settings>
    void InstallProperties(sol::state& lua, sol::table& table, const std::vector<LuaProperty<Settings>>& (*properties)(),
                           Settings& (*current)()) {
        sol::table meta = lua.create_table();
        meta[sol::meta_function::index] = [properties, current](const sol::this_state state, const sol::table&,
                                                                 const std::string& key) {
            return FindProperty(properties(), key).get(sol::state_view(state), current());
        };
        meta[sol::meta_function::new_index] = [properties, current](const sol::table&, const std::string& key,
                                                                     const sol::object& value) {
            FindProperty(properties(), key).set(current(), value);
        };
        table[sol::metatable_key] = meta;
    }

    // ── Game.MessageBox ─────────────────────────────────────────────────────

    constexpr NamedValue MESSAGE_ANCHORS[] = {
        {"TopLeft", static_cast<int>(MessageAnchor::TOP_LEFT)},
        {"TopCenter", static_cast<int>(MessageAnchor::TOP_CENTER)},
        {"TopRight", static_cast<int>(MessageAnchor::TOP_RIGHT)},
        {"MiddleLeft", static_cast<int>(MessageAnchor::MIDDLE_LEFT)},
        {"Center", static_cast<int>(MessageAnchor::CENTER)},
        {"MiddleRight", static_cast<int>(MessageAnchor::MIDDLE_RIGHT)},
        {"BottomLeft", static_cast<int>(MessageAnchor::BOTTOM_LEFT)},
        {"BottomCenter", static_cast<int>(MessageAnchor::BOTTOM_CENTER)},
        {"BottomRight", static_cast<int>(MessageAnchor::BOTTOM_RIGHT)},
    };

    constexpr NamedValue TEXT_ALIGNS[] = {
        {"Left", static_cast<int>(MessageTextAlign::LEFT)},
        {"Center", static_cast<int>(MessageTextAlign::CENTER)},
        {"Right", static_cast<int>(MessageTextAlign::RIGHT)},
    };

    MessageStyle& CurrentMessageStyle() {
        return LevelManager::CurrentLevel().worldSettings.messageStyle;
    }

    const std::vector<LuaProperty<MessageStyle>>& MessageBoxProperties() {
        static const std::vector<LuaProperty<MessageStyle>> properties = {
            StringProperty("font", "Font file relative to Assets (.ttf/.otf). \"\" = the engine's font.", &MessageStyle::font),
            NumberProperty("fontSize", "Pixels at the UI Reference Height. At least 1.", &MessageStyle::fontSize, 1.0f),
            EnumProperty("anchor", "MessageAnchor", "Where on screen the messages sit, e.g. MessageAnchor.BottomLeft.",
                         &MessageStyle::anchor, MESSAGE_ANCHORS),
            Vector2Property("offset", "Moves the messages from the anchor. +x right, +y down.", &MessageStyle::offset),
            EnumProperty("align", "TextAlign", "How lines of different widths line up, e.g. TextAlign.Left.",
                         &MessageStyle::align, TEXT_ALIGNS),
            NumberProperty("wrapWidth", "Lines longer than this wrap between words. 0 = no wrapping.", &MessageStyle::wrapWidth, 0.0f),
            BoolProperty("background", "Draws a box behind each message.", &MessageStyle::background),
            ColorProperty("backgroundColor", "The box colour, 0..255 per channel, alpha included. Tints the texture.",
                          &MessageStyle::backgroundColor),
            StringProperty("backgroundTexture", "Image stretched over the box, relative to Assets. \"\" = a solid box.",
                           &MessageStyle::backgroundTexture),
            Vector2Property("padding", "Space between the text and the box's edges (x left/right, y top/bottom).",
                            &MessageStyle::padding),
            BoolProperty("shadow", "Draws a copy of the text behind it, moved by shadowOffset.", &MessageStyle::shadow),
            ColorProperty("shadowColor", "0..255 per channel, alpha included.", &MessageStyle::shadowColor),
            Vector2Property("shadowOffset", "+x right, +y down.", &MessageStyle::shadowOffset),
            BoolProperty("outline", "Draws a border around the letters.", &MessageStyle::outline),
            ColorProperty("outlineColor", "0..255 per channel, alpha included.", &MessageStyle::outlineColor),
            NumberProperty("outlineThickness", "At least 0.", &MessageStyle::outlineThickness, 0.0f),
            NumberProperty("fadeIn", "Seconds to fade in, inside each message's duration.", &MessageStyle::fadeIn, 0.0f),
            NumberProperty("fadeOut", "Seconds to fade out, inside each message's duration.", &MessageStyle::fadeOut, 0.0f),
            BoolProperty("stack", "Off: a new message replaces the shown one. On: they stack, newest at the top.",
                         &MessageStyle::stack),
            {
                "maxMessages", "integer", "With stacking, the oldest goes once there are more than this. At least 1.",
                [](sol::state_view lua, const MessageStyle& style) { return sol::make_object(lua, style.maxMessages); },
                [](MessageStyle& style, const sol::object& value) {
                    style.maxMessages = std::max(1, PropertyValue<MessageStyle, int>(value, "maxMessages", "an integer"));
                },
            },
            NumberProperty("spacing", "Space between stacked messages.", &MessageStyle::spacing),
        };
        return properties;
    }

    void RegisterMessageBoxMetadata() {
        RegisterType(GlobalTable("Game.MessageBox",
            "The level's message box (World Settings > Message Box): show text in it and change how it looks. "
            "Style changes last until the level loads again.",
            PropertyDocs(MessageBoxProperties()), {
                Method("SetMessage", {Param("text", "string"), Param("color", "Vector3?"), Param("duration", "number?")}, {},
                       "Shows `text`, like a Show Message dispatch. `color` is 0..255 per channel, white by default; "
                       "`duration` is in seconds, 3 by default."),
                Method("Clear", {}, {}, "Removes every message on screen now."),
                Method("Reset", {}, {}, "Puts the style back to how the level was saved."),
                Method("GetMessages", {}, "string[]", "The texts on screen, top to bottom (newest first when stacking)."),
            }));

        std::vector<EnumValueDoc> anchors;
        for (const NamedValue& anchor : MESSAGE_ANCHORS) anchors.push_back({anchor.name, anchor.value, {}});
        RegisterType(Enum("MessageAnchor", "Where Game.MessageBox messages sit on screen.", std::move(anchors)));

        std::vector<EnumValueDoc> aligns;
        for (const NamedValue& align : TEXT_ALIGNS) aligns.push_back({align.name, align.value, {}});
        RegisterType(Enum("TextAlign", "How the lines of a Game.MessageBox message line up.", std::move(aligns)));
    }

    // ── Game.Sky ────────────────────────────────────────────────────────────

    constexpr auto SKY_MODE_VALUES = [] {
        std::array<NamedValue, SKY_MODES.size()> values{};
        for (size_t i = 0; i < SKY_MODES.size(); ++i) values[i] = {SKY_MODES[i].name, static_cast<int>(SKY_MODES[i].mode)};
        return values;
    }();

    SkySettings& CurrentSky() {
        return LevelManager::CurrentLevel().sky;
    }

    const std::vector<LuaProperty<SkySettings>>& SkyProperties() {
        static const std::vector<LuaProperty<SkySettings>> properties = {
            EnumProperty("mode", "SkyMode", "How the sky image wraps around the camera, e.g. SkyMode.Cylinder.",
                         &SkySettings::mode, SKY_MODE_VALUES),
            StringProperty("texture", "The image for Equirectangular and Cylinder, relative to Assets. \"\" clears it.",
                           &SkySettings::texture),
            NumberProperty("rotation", "Degrees, in the same direction as camera yaw.", &SkySettings::rotation),
            NumberProperty("rotationSpeed", "Degrees per second the sky keeps turning while the game runs.",
                           &SkySettings::rotationSpeed),
            Vector3Property("tint", "Multiplies the sky image. 0..255 per channel.", &SkySettings::tint),
            NumberProperty("horizonOffset", "Degrees; positive raises the horizon.", &SkySettings::horizonOffset),
            Vector3Property("fallbackColor", "Shown when the sky has no usable image. 0..255 per channel.",
                            &SkySettings::fallbackColor),
        };
        return properties;
    }

    void RegisterSkyMetadata() {
        RegisterType(GlobalTable("Game.Sky",
            "The level's sky (World Settings > Rendering > Sky). Changes last until the level loads again.",
            PropertyDocs(SkyProperties()), {
                Method("SetCubemapFace", {Param("face", "SkyFace"), Param("path", "string")}, {},
                       "One Cubemap face image, relative to Assets, e.g. SkyFace.Up. \"\" clears it."),
                Method("GetCubemapFace", {Param("face", "SkyFace")}, "string"),
                Method("Reset", {}, {}, "Puts the sky back to how the level was saved. Keeps how far rotationSpeed has turned it."),
            }));

        std::vector<EnumValueDoc> modes;
        for (const SkyModeInfo& mode : SKY_MODES) modes.push_back({mode.name, static_cast<int>(mode.mode), {}});
        RegisterType(Enum("SkyMode", "Sky projection modes for Game.Sky.mode.", std::move(modes)));

        std::vector<EnumValueDoc> faces;
        for (const SkyFaceInfo& face : SKY_FACES) faces.push_back({face.name, static_cast<int>(face.face), {}});
        RegisterType(Enum("SkyFace", "Cubemap faces for Game.Sky.SetCubemapFace. Front is +Z, Right is -X, Up is +Y.",
                          std::move(faces)));
    }

    SkyFace ToSkyFace(const int value, const char *function) {
        if (!IsValidSkyFace(value)) throw sol::error(std::string(function) + " expects a SkyFace value, e.g. SkyFace.Up");
        return static_cast<SkyFace>(value);
    }

    void RegisterGameMetadata() {
        RegisterType(GlobalTable("Game", "Global game/level table.", {
            Prop("levelName", "string", true, "The current level's name, as passed to LoadLevel."),
            Prop("MessageBox", "Game.MessageBox", true, "The level's message box: show text in it and change how it looks."),
            Prop("Sky", "Game.Sky", true, "The level's sky: its image, rotation and colours."),
        }, {
            Method("LoadLevel", {Param("levelName", "string")}, {},
                   "Switches to the level `levelName` at the end of this frame. The level file can be in any "
                   "folder under Assets; levelName is its file name without .bson. "
                   "Raises an error if there is no such level, or more than one."),
            Method(
                "Raycast",
                {
                    Param("origin", "Vector3"), Param("direction", "Vector3"), Param("length", "number"),
                    Param("ignoredEntityID", "integer"), Param("requireCollider", "boolean"), Param("rayType", "RayType?"),
                },
                "table",
                "Returns nil on miss, else a table with type/typeID/position/distance/"
                "entityID/wallID/sectorID and (whichever applies) entity/wall/sector. "
                "A rayType other than RayType.General (the default) fires the Receive Ray events of what it hits and of "
                "every linedef it passes, with the ignored entity as the instigator."
            ),
            Method("Signal", {Param("name", "string"), Param("instigator", "Entity?")}, {},
                   "Fires every Lua Signal event named `name`, on any wall, sector, entity, linedef or the level. "
                   "`instigator` is what their tag filters check and what their dispatches get."),
            Method("LineOfSight", {Param("from", "Vector3"), Param("to", "Vector3")}, "boolean",
                   "true when nothing solid is between `from` and `to`. Walls, floors and ceilings block it; "
                   "entities don't. Much cheaper than Raycast for sight checks."),
            Method("CreateEntity", {Param("isUIEntityOrEntity", "boolean|Entity?"), Param("position", "Vector3?"), Param("rotation", "Vector4?")}, "Entity?",
                   "Adds a new Entity to the level and returns it. With nothing or a boolean it is empty, named \"Entity\", "
                   "with a Transform at the origin (a UITransform when true). Given an Entity it is a copy of it, scripts "
                   "and all - pass an Entity field holding a prefab to spawn that prefab. position and rotation (a quaternion, "
                   "e.g. mathT.QuaternionFromEuler(0, 90, 0)) place a world entity; each can be nil to keep the default."),
            Method("FindEntity", {Param("name", "string")}, "Entity?", "First Entity with this exact name, or nil."),
            Method("FindEntities", {Param("name", "string")}, "Entity[]", "Every Entity with this exact name."),
            Method("FindEntitiesWithTag", {Param("tag", "string")}, "Entity[]", "Every Entity that has this tag."),
            Method("FindNearestWithTag", {Param("tag", "string"), Param("from", "Vector3|Entity"), Param("maxDistance", "number?")}, "Entity?",
                   "The enabled Entity with this tag whose position is closest to `from`, or nil if none is within maxDistance "
                   "(no limit when left out). Given an Entity, measures from its position and never returns it."),
            Method("OverlapSphere", {Param("center", "Vector3"), Param("radius", "number"), Param("tag", "string?")}, "Entity[]",
                   "Every Entity with an active collider (triggers too) touching the sphere, nearest first, each once. "
                   "Only ones with `tag` when given. Walls don't block it - check Game.LineOfSight for that."),
            Method("GetEntity", {Param("id", "integer")}, "Entity?", "The Entity with this ID (e.g. a Raycast hit's entityID), or nil."),
            Method("GetEntities", {}, "Entity[]", "Every Entity in the level."),
            Method("GetSectorAt", {Param("position", "Vector2|Vector3")}, "Sector?",
                   "The innermost sector containing `position` (x, z; height ignored), or nil if it is outside the map."),
            Method("GetVariable", {Param("name", "string")}, "number",
                   "The value of an event variable: one of this level's (World Settings) or a global one (Project Settings). "
                   "Raises an error if there is no variable with that name."),
            Method("SetVariable", {Param("name", "string"), Param("value", "number")}, {},
                   "Sets an event variable, as a Change Variable dispatch would. Level variables reset when a level loads; "
                   "global ones keep their value until play stops. Raises an error if there is no variable with that name."),
        }));

        std::vector<EnumValueDoc> rayTypes;
        for (const RayTypeInfo& rayType : RAY_TYPES) rayTypes.push_back({rayType.name, static_cast<int>(rayType.type), {}});
        RegisterType(Enum("RayType",
                          "What a ray is for, for Game.Raycast and Camera:Raycast. Any but General fires Receive Ray events.",
                          std::move(rayTypes)));

        RegisterMessageBoxMetadata();
        RegisterSkyMetadata();

        RegisterType(GlobalTable("Audio", "Sounds that play once, without an Audio Source component.", {}, {
            Method("PlayAt", {Param("sound", "string"), Param("position", "Vector3"), Param("gain", "number?"), Param("pitch", "number?")}, {},
                   "Plays a sound once at a point in the world, fading with distance like an Audio Source (Unity's AudioSource.PlayClipAtPoint). "
                   "sound is a .wav path inside Assets, extension optional. gain and pitch default to 1. Several can play at once."),
            Method("Play2D", {Param("sound", "string"), Param("gain", "number?"), Param("pitch", "number?")}, {},
                   "Plays a sound once, the same everywhere: no position, distance or direction. For UI clicks, pickups, announcer lines."),
        }));
    }

    // Audio.PlayAt / Audio.Play2D's sound, gain and pitch.
    std::string OneShotSound(const sol::object& sound, const char* function) {
        if (sound.get_type() != sol::type::string)
            throw sol::error(std::string(function) + ": sound must be a string, e.g. \"Sounds/explosion\"");

        const std::string name = sound.as<std::string>();
        if (!SoundManager::HasSound(name))
            throw sol::error(std::string(function) + ": there is no sound \"" + name + "\" (a .wav inside Assets)");

        return name;
    }

    float OneShotNumber(const sol::object& value, const char* function, const char* name, const float minimum) {
        if (value.get_type() == sol::type::lua_nil) return 1.0f;
        if (value.get_type() != sol::type::number) throw sol::error(std::string(function) + ": " + name + " must be a number");
        return std::max(value.as<float>(), minimum);
    }

}

static const char *RayHitTypeToString(const RayHitType type) {
    switch (type) {
        case RayHitType::Entity: return "Entity";
        case RayHitType::Wall: return "Wall";
        case RayHitType::SectorFloor: return "SectorFloor";
        case RayHitType::SectorCeiling: return "SectorCeiling";
        default: return "None";
    }
}

void LuaScriptSystem::RegisterGameBindings(sol::state &lua) {
    RegisterGameMetadata();

    const sol::object existing = lua["Game"];
    sol::table game;

    if (existing.get_type() == sol::type::table) game = existing.as<sol::table>();
    else {
        if (existing.get_type() != sol::type::nil)
            spdlog::warn("Replacing Lua global 'Game' because it is not a table");

        game = lua.create_named_table("Game");
    }

    // Only queues the switch; RuntimeSession does it once this frame is over.
    // The file is checked here so a wrong name is an error at the call.
    game.set_function("LoadLevel", [](const sol::object& value) {
        if (value.get_type() != sol::type::string)
            throw sol::error("Game.LoadLevel expects a level name (string)");

        const std::string levelName = LevelSerialization::CleanLevelName(value.as<std::string>());

        if (levelName.empty()) throw sol::error("Game.LoadLevel expects a level name, got an empty string");

        std::string errorMessage;
        if (LevelSerialization::FindLevelPath(levelName, &errorMessage).empty())
            throw sol::error("Game.LoadLevel: " + errorMessage);

        LevelSystem::RequestLevelLoad(levelName);
    });

    // Game is a plain table, so levelName is served from its metatable: it
    // always reads the current level, and assigning to it is an error.
    sol::table gameMeta = lua.create_table();

    gameMeta[sol::meta_function::index] = [](const sol::table&, const sol::object& key, const sol::this_state state) -> sol::object {
        if (key.is<std::string>() && key.as<std::string>() == "levelName")
            return sol::make_object(state, LevelManager::CurrentLevel().name);

        return sol::make_object(state, sol::nil);
    };

    gameMeta[sol::meta_function::new_index] = [](sol::table self, const sol::object& key, const sol::object& value) {
        if (key.is<std::string>() && key.as<std::string>() == "levelName")
            throw sol::error("Game.levelName is read-only");

        self.raw_set(key, value);
    };

    game[sol::metatable_key] = gameMeta;

    // Game.CreateEntity(), Game.CreateEntity(isUIEntity), or
    // Game.CreateEntity(entity): a copy of an entity - a prefab instance
    // when `entity` came from an Entity field holding a prefab. A world
    // entity can also be given a position and a rotation (quaternion), like
    // Unity's Instantiate(original, position, rotation), each optional.
    game.set_function("CreateEntity", [](sol::this_state state, const sol::object argument,
                                         const sol::object position, const sol::object rotation) -> sol::object {
        Level& level = LevelManager::CurrentLevel();

        if (position.get_type() != sol::type::lua_nil && !position.is<Vector3>())
            throw sol::error("Game.CreateEntity: position must be a Vector3");
        if (rotation.get_type() != sol::type::lua_nil && !rotation.is<Vector4>())
            throw sol::error("Game.CreateEntity: rotation must be a Vector4 quaternion, e.g. mathT.QuaternionFromEuler(0, 90, 0)");

        const bool placed = position.get_type() != sol::type::lua_nil || rotation.get_type() != sol::type::lua_nil;
        const auto place = [&](const ID id) {
            if (!placed) return;

            const ScriptTransform transform{&level, id};
            if (position.get_type() != sol::type::lua_nil) transform.SetPosition(position.as<Vector3>());
            if (rotation.get_type() != sol::type::lua_nil) transform.SetRotation(rotation.as<Vector4>());
        };
        const auto throwIfUI = [&](const bool isUIEntity) {
            if (placed && isUIEntity)
                throw sol::error("Game.CreateEntity: a UI entity has no Transform to place - set its uiTransform.position instead");
        };

        if (argument.is<ScriptEntity>()) {
            const ScriptEntity source = argument.as<ScriptEntity>();
            Entity* original = source.GetEntity();
            if (original == nullptr) return sol::make_object(state, sol::nil);

            throwIfUI(source.level->transforms.Get(original->id) == nullptr);

            ID newID = INVALID_ID;
            if (const std::string prefab = Prefab::ScriptCopyReference(source.level); !prefab.empty())
                newID = Prefab::Instantiate(level, *source.level, prefab);
            else if (source.level == &level) {
                Entity copy = *original; // CreateEntity can grow the entity list
                newID = level.CreateEntity(copy);
            }

            if (newID == INVALID_ID) return sol::make_object(state, sol::nil);

            place(newID);
            LuaScriptRuntime::QueueEntityScriptsStart(newID);
            return sol::make_object(state, ScriptEntity{&level, newID});
        }

        const bool isUIEntity = argument.is<bool>() && argument.as<bool>();
        throwIfUI(isUIEntity);

        const ID newID = level.CreateEntity(isUIEntity);
        place(newID);
        return sol::make_object(state, ScriptEntity{&level, newID});
    });

    game.set_function("FindEntity", [](sol::this_state state, const std::string& name) -> sol::object {
        Level& level = LevelManager::CurrentLevel();

        for (const Entity& candidate : level.entities)
            if (candidate.name == name) return sol::make_object(state, ScriptEntity{&level, candidate.id});

        return sol::make_object(state, sol::nil);
    });

    game.set_function("FindEntities", [](const std::string& name) -> sol::as_table_t<std::vector<ScriptEntity>> {
        Level& level = LevelManager::CurrentLevel();
        std::vector<ScriptEntity> result;

        for (const Entity& candidate : level.entities)
            if (candidate.name == name) result.push_back({&level, candidate.id});

        return sol::as_table(std::move(result));
    });

    game.set_function("FindEntitiesWithTag", [](const std::string& tag) -> sol::as_table_t<std::vector<ScriptEntity>> {
        Level& level = LevelManager::CurrentLevel();
        std::vector<ScriptEntity> result;

        for (const Entity& candidate : level.entities) {
            const ScriptEntity entity{&level, candidate.id};
            if (entity.HasTag(tag)) result.push_back(entity);
        }

        return sol::as_table(std::move(result));
    });

    // Game.FindNearestWithTag(tag, from, [maxDistance]). `from` is a point,
    // or an Entity: measured from its position and never the answer itself.
    // Like Unity's tag lookups, disabled entities are skipped.
    game.set_function("FindNearestWithTag", [](sol::this_state state, const std::string& tag, const sol::object from,
                                               const sol::object maxDistance) -> sol::object {
        Level& level = LevelManager::CurrentLevel();

        Vector3 origin;
        ID self = INVALID_ENTITY_ID;

        if (from.is<ScriptEntity>()) {
            self = from.as<ScriptEntity>().ownerID;
            const ComponentTransform* transform = level.transforms.Get(self);
            if (transform == nullptr) return sol::make_object(state, sol::nil); // gone, or a UI entity
            origin = transform->position;
        }
        else if (from.is<Vector3>()) origin = from.as<Vector3>();
        else throw sol::error("Game.FindNearestWithTag: from must be a Vector3 or an Entity");

        float limit = std::numeric_limits<float>::infinity();
        if (maxDistance.get_type() != sol::type::lua_nil) {
            if (maxDistance.get_type() != sol::type::number) throw sol::error("Game.FindNearestWithTag: maxDistance must be a number");
            limit = maxDistance.as<float>();
        }

        const std::optional<TagId> tagId = TagRegistry::Find(tag);
        if (!tagId.has_value()) return sol::make_object(state, sol::nil);

        ID nearest = INVALID_ENTITY_ID;
        float nearestSq = limit * limit;

        for (const Entity& candidate : level.entities) {
            if (candidate.id == self || !candidate.enabled) continue;
            if (std::ranges::find(candidate.tagIds, *tagId) == candidate.tagIds.end()) continue;

            const ComponentTransform* transform = level.transforms.Get(candidate.id);
            if (transform == nullptr) continue;

            // <= so a maxDistance of exactly the distance still counts.
            const float distanceSq = Vector3Math::LengthSquared(transform->position - origin);
            if (distanceSq <= nearestSq && (nearest == INVALID_ENTITY_ID || distanceSq < nearestSq)) {
                nearest = candidate.id;
                nearestSq = distanceSq;
            }
        }

        if (nearest == INVALID_ENTITY_ID) return sol::make_object(state, sol::nil);
        return sol::make_object(state, ScriptEntity{&level, nearest});
    });

    // Game.OverlapSphere(center, radius, [tag]): like Unity's
    // Physics.OverlapSphere, every entity with an active collider (triggers
    // included) touching the sphere - once each, nearest first. Walls don't
    // block it.
    game.set_function("OverlapSphere", [](const sol::object center, const sol::object radius, const sol::object tag)
                                          -> sol::as_table_t<std::vector<ScriptEntity>> {
        if (!center.is<Vector3>()) throw sol::error("Game.OverlapSphere: center must be a Vector3");
        if (radius.get_type() != sol::type::number) throw sol::error("Game.OverlapSphere: radius must be a number");
        if (tag.get_type() != sol::type::lua_nil && tag.get_type() != sol::type::string)
            throw sol::error("Game.OverlapSphere: tag must be a string");

        Level& level = LevelManager::CurrentLevel();
        const Vector3 point = center.as<Vector3>();
        const float reach = std::max(radius.as<float>(), 0.0f);

        std::optional<TagId> tagId;
        if (tag.get_type() == sol::type::string) {
            tagId = TagRegistry::Find(tag.as<std::string>());
            if (!tagId.has_value()) return sol::as_table(std::vector<ScriptEntity>{});
        }

        // Entity -> its distance from the centre, for sorting.
        std::vector<std::pair<float, ID>> found;

        // Only enabled entities' active colliders are in this range.
        for (const ComponentCollider& collider : level.colliders.ActiveColliders()) {
            if (std::ranges::find(found, collider.ownerID, &std::pair<float, ID>::second) != found.end()) continue;

            const ComponentTransform* transform = level.transforms.Get(collider.ownerID);
            if (transform == nullptr || !PhysicsSystem::ColliderOverlapsSphere(*transform, collider, point, reach)) continue;

            if (tagId.has_value()) {
                const Entity* owner = level.GetEntity(collider.ownerID);
                if (owner == nullptr || std::ranges::find(owner->tagIds, *tagId) == owner->tagIds.end()) continue;
            }

            found.emplace_back(Vector3Math::LengthSquared(transform->position - point), collider.ownerID);
        }

        std::ranges::sort(found);

        std::vector<ScriptEntity> result;
        result.reserve(found.size());
        for (const auto& [distanceSq, id] : found) result.push_back(ScriptEntity{&level, id});

        return sol::as_table(std::move(result));
    });

    // Event variables, level or global, found by name. Never creates one.
    const auto variableValue = [](const std::string& name, const char* function) -> float& {
        Level& level = LevelManager::CurrentLevel();
        const std::optional<EventVariableID> id = level.FindEventVariable(name);
        float* value = id ? level.GetEventVariableValue(*id) : nullptr;

        if (!value) throw sol::error(std::string(function) + ": no event variable named \"" + name + "\"");
        return *value;
    };

    game.set_function("GetVariable", [variableValue](const std::string& name) {
        return variableValue(name, "Game.GetVariable");
    });

    game.set_function("SetVariable", [variableValue](const std::string& name, const float value) {
        variableValue(name, "Game.SetVariable") = value;
    });

    game.set_function("GetEntity", [](sol::this_state state, const ID id) -> sol::object {
        Level& level = LevelManager::CurrentLevel();

        if (level.GetEntity(id) == nullptr) return sol::make_object(state, sol::nil);

        return sol::make_object(state, ScriptEntity{&level, id});
    });

    game.set_function("GetEntities", []() -> sol::as_table_t<std::vector<ScriptEntity>> {
        Level& level = LevelManager::CurrentLevel();
        std::vector<ScriptEntity> result;
        result.reserve(level.entities.size());

        for (const Entity& candidate : level.entities) result.push_back({&level, candidate.id});

        return sol::as_table(std::move(result));
    });

    // Same lookup that decides which sector an entity is in.
    const auto sectorAt = [](const sol::this_state state, const Vector2 point) -> sol::object {
        Level& level = LevelManager::CurrentLevel();

        const int index = MapQueries::FindSectorContainingPoint(level.sectors, point);
        if (index < 0) return sol::make_object(state, sol::nil);

        return sol::make_object(state, ScriptSector{&level, level.sectors[index].id});
    };

    game.set_function("GetSectorAt", sol::overload(
        [sectorAt](const sol::this_state state, const Vector2& position) { return sectorAt(state, position); },
        [sectorAt](const sol::this_state state, const Vector3& position) {
            return sectorAt(state, {position.x, position.z});
        }
    ));

    // ---- Sky (Level::sky) ----
    sol::table skyMode = lua.create_named_table("SkyMode");
    for (const SkyModeInfo& mode : SKY_MODES) skyMode[mode.name] = static_cast<int>(mode.mode);

    sol::table skyFace = lua.create_named_table("SkyFace");
    for (const SkyFaceInfo& face : SKY_FACES) skyFace[face.name] = static_cast<int>(face.face);

    // Functions are plain fields (called with `.`); the settings go through
    // the metatable to the current level's SkySettings.
    sol::table sky = lua.create_table();

    // One-shot sounds, on the event system's temporary sources (the same as
    // a Play Sound dispatch), so they are cleaned up the same way.
    sol::table audio = lua.create_named_table("Audio");

    audio.set_function("PlayAt", [](const sol::object sound, const sol::object position, const sol::object gain,
                                    const sol::object pitch) {
        const std::string name = OneShotSound(sound, "Audio.PlayAt");
        if (!position.is<Vector3>()) throw sol::error("Audio.PlayAt: position must be a Vector3");

        // OpenAL takes no pitch of 0 or less.
        EventSystem::PlayOneShot(name, OneShotNumber(gain, "Audio.PlayAt", "gain", 0.0f),
                                 OneShotNumber(pitch, "Audio.PlayAt", "pitch", 0.01f), position.as<Vector3>());
    });

    audio.set_function("Play2D", [](const sol::object sound, const sol::object gain, const sol::object pitch) {
        const std::string name = OneShotSound(sound, "Audio.Play2D");
        EventSystem::PlayOneShot(name, OneShotNumber(gain, "Audio.Play2D", "gain", 0.0f),
                                 OneShotNumber(pitch, "Audio.Play2D", "pitch", 0.01f), std::nullopt);
    });

    sky.set_function("SetCubemapFace", [](const int face, const std::string& path) {
        CurrentSky().Face(ToSkyFace(face, "Game.Sky.SetCubemapFace")) = path;
    });
    sky.set_function("GetCubemapFace", [](const int face) {
        return CurrentSky().Face(ToSkyFace(face, "Game.Sky.GetCubemapFace"));
    });
    sky.set_function("Reset", [] {
        SkySettings& current = CurrentSky();
        const float spin = current.spin;
        current = LevelSystem::StartingSky();
        current.spin = spin;
    });

    InstallProperties(lua, sky, &SkyProperties, &CurrentSky);
    game["Sky"] = sky;

    sol::table rayType = lua.create_named_table("RayType");
    for (const RayTypeInfo& info : RAY_TYPES) rayType[info.name] = static_cast<int>(info.type);

    game.set_function("Raycast",
                      [](sol::this_state state,
                         const Vector3 &pos,
                         const Vector3 &dir,
                         const float length,
                         const ID ignoredID,
                         const bool requireCollider,
                         const sol::optional<int> type) -> sol::object {
                          Level &level = LevelManager::CurrentLevel();

                          return LuaRayHit::ToLua(state, level, GameFunctions::Raycast(
                              level,
                              pos,
                              dir,
                              length,
                              ignoredID,
                              requireCollider,
                              LuaRayHit::ToRayType(type, "Game.Raycast")
                          ));
                      }
    );

    game.set_function("Signal", [](const std::string& name, const sol::optional<ScriptEntity> instigator) {
        Level& level = LevelManager::CurrentLevel();
        EventSystem::Signal(level, name, instigator ? instigator->ownerID : INVALID_ID);
    });

    sol::table messageAnchor = lua.create_named_table("MessageAnchor");
    for (const NamedValue& anchor : MESSAGE_ANCHORS) messageAnchor[anchor.name] = anchor.value;

    sol::table textAlign = lua.create_named_table("TextAlign");
    for (const NamedValue& align : TEXT_ALIGNS) textAlign[align.name] = align.value;

    // Functions are plain fields (called with `.`); the style properties go
    // through the metatable to the current level's MessageStyle.
    sol::table messageBox = lua.create_table();

    messageBox.set_function("SetMessage", [](const std::string& text, const sol::optional<Vector3> color,
                                             const sol::optional<float> duration) {
        const float seconds = duration.value_or(3.0f);
        if (!(seconds > 0.0f))
            throw sol::error("Game.MessageBox.SetMessage expects a duration above 0, got " + std::to_string(seconds));

        EventSystem::SetMessage(LevelManager::CurrentLevel(), text, color.value_or(Vector3{255.0f, 255.0f, 255.0f}), seconds);
    });

    messageBox.set_function("Clear", [] { EventSystem::ClearMessages(); });
    messageBox.set_function("Reset", [] { EventSystem::ResetMessageStyle(LevelManager::CurrentLevel()); });

    messageBox.set_function("GetMessages", [](const sol::this_state state) {
        sol::state_view view(state);
        sol::table texts = view.create_table();

        // Top to bottom: CurrentMessages is oldest first and the newest is drawn at the top.
        const std::vector<EventSystem::Message> messages = EventSystem::CurrentMessages();
        int index = 1;
        for (auto it = messages.rbegin(); it != messages.rend(); ++it) texts[index++] = it->text;
        return texts;
    });

    InstallProperties(lua, messageBox, &MessageBoxProperties, &CurrentMessageStyle);

    game["MessageBox"] = messageBox;

    game.set_function("LineOfSight", [](const Vector3& from, const Vector3& to) {
        return GameFunctions::LineOfSight(LevelManager::CurrentLevel(), from, to);
    });
}

RayType LuaRayHit::ToRayType(const sol::optional<int>& value, const char* function) {
    if (!value) return RayType::GENERAL;

    if (*value < 0 || *value >= static_cast<int>(RayType::COUNT))
        throw sol::error(std::string(function) + " expects a RayType value, e.g. RayType.Interact");

    return static_cast<RayType>(*value);
}

sol::object LuaRayHit::ToLua(sol::state_view lua, Level &level, const std::optional<RayHit> &hit) {
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