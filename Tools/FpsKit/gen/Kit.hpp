// Small helpers shared by the Classic FPS Kit generator. Everything goes through
// the engine's own structs and serializers, so the files it writes are exactly
// what the editor would save.
#pragma once

#include <SDL3/SDL.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Headers/GlobalEventVariables.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Map/LevelSerialization.hpp"
#include "Headers/Map/MapQueries.hpp"
#include "Headers/Map/MapTopology.hpp"
#include "Headers/Objects/FlipbookAsset.hpp"
#include "Headers/Objects/Prefab.hpp"
#include "Headers/Objects/UILayouts.hpp"
#include "Headers/Project/ProjectManager.hpp"
#include "Headers/TagRegistry.hpp"

namespace fs = std::filesystem;
using nlohmann::json;
using Op = PayloadDispatchChangeVariable::Operation;
using Str = std::string;

inline fs::path gAssets;
inline json gManifest;
inline int gErrors = 0;

inline void Fail(const Str& message) {
    std::cout << "ERROR: " << message << "\n";
    ++gErrors;
}

inline std::string Sound(const Str& path) { return "Sounds/" + path + ".wav"; }

// ── Variables and tags ──────────────────────────────────────────────────────

inline EventVariableID Var(const Str& name) {
    if (const auto id = GlobalEventVariables::Find(name)) return *id;
    Fail("unknown variable " + name);
    return 0;
}
inline TagId Tag(const Str& name) { return TagRegistry::GetOrCreate(name, TagRegistry::TagKind::NORMAL); }
inline TagId LinkTag(const Str& name) { return TagRegistry::GetOrCreate(name, TagRegistry::TagKind::EVENT); }

// ── Conditions, calls, filters ──────────────────────────────────────────────

inline Condition If(const Str& var, Condition::ConditionType type, float value) {
    return Condition{Var(var), type, value};
}
inline Condition IfVars(const Str& var, Condition::ConditionType type, const Str& other) {
    return Condition{Var(var), type, Var(other)};
}
inline constexpr auto EQ = Condition::EQUAL;
inline constexpr auto LT = Condition::LESS_THAN;
inline constexpr auto GT = Condition::GREATER_THAN;
inline constexpr auto LE = Condition::LESS_THAN_OR_EQUAL;
inline constexpr auto GE = Condition::GREATER_THAN_OR_EQUAL;

inline DirectCall Self(const Str& name) { return {CallTarget::SELF, INVALID_ID, 0, name}; }
inline DirectCall Inst(const Str& name) { return {CallTarget::INSTIGATOR, INVALID_ID, 0, name}; }
inline DirectCall Tagged(const Str& tag, const Str& name) { return {CallTarget::TAGGED, INVALID_ID, Tag(tag), name}; }

inline TagFilter Only(const Str& tag) { return {TagType::INCLUDE, {Tag(tag)}}; }
inline TagFilter Any() { return {TagType::ALL, {}}; }

// ── Events and dispatches (no link tags: direct calls read better) ──────────

template <typename P>
inline Event Ev(P payload, std::vector<DirectCall> calls, Condition condition = {}, uint32_t maxTriggers = 0,
                float chance = 100.0f) {
    Event event;
    event.payload = std::move(payload);
    event.useTag = false;
    event.calls = std::move(calls);
    event.condition = condition;
    event.maxTriggers = maxTriggers;
    event.chance = chance;
    return event;
}

template <typename P>
inline Dispatch Dp(const Str& name, P payload, float delay = 0.0f, std::vector<DispatchFlow> flows = {}) {
    Dispatch dispatch;
    dispatch.payload = std::move(payload);
    dispatch.name = name;
    dispatch.useTag = false;
    dispatch.delay = delay;
    dispatch.flows = std::move(flows);
    return dispatch;
}

inline DispatchFlow Then(DirectCall call, float wait = 0.0f) { return {std::move(call), wait, FlowAlways{}}; }
inline DispatchFlow ThenIf(DirectCall call, Condition condition, float wait = 0.0f) {
    return {std::move(call), wait, FlowVariable{condition}};
}

// ── Dispatch payload shortcuts ──────────────────────────────────────────────

inline PayloadDispatchChangeVariable SetVar(const Str& v, float value) { return {Var(v), Op::SET, value}; }
inline PayloadDispatchChangeVariable AddVar(const Str& v, float value) { return {Var(v), Op::ADD, value}; }

inline PayloadDispatchPlaySound Play(const Str& sound, float gain = 1.0f) { return {Sound(sound), gain, 1.0f}; }
inline PayloadDispatchShowMessage Msg(const Str& text, float seconds = 3.0f) {
    return {text, seconds, {1.0f, 0.22f, 0.16f}};
}
inline PayloadDispatchSpawnPrefab Spawn(const Str& prefab, Vector3 offset = {0, 0, 0}) { return {prefab, offset, {}, false}; }

inline FieldValue Num(float x) { FieldValue v; v.number = {x, 0, 0, 0}; return v; }
inline FieldValue Vec(float x, float y, float z, float w = 0) { FieldValue v; v.number = {x, y, z, w}; return v; }
inline FieldValue Flag(bool on) { FieldValue v; v.flag = on; return v; }
inline FieldValue Text(const Str& text) { FieldValue v; v.text = text; return v; }

inline PayloadDispatchEntitySetField SetF(FieldComponent component, const Str& field, FieldValue value) {
    PayloadDispatchEntitySetField set;
    set.component = component;
    set.field = field;
    set.value = std::move(value);
    return set;
}

// ── Flipbooks ───────────────────────────────────────────────────────────────

struct Frame {
    std::array<Str, 8> textures;
    float seconds = 0.1f;
};

inline Frame Pic(const Str& texture, float seconds) {
    Frame frame;
    frame.textures.fill(texture);
    frame.seconds = seconds;
    return frame;
}

// One frame of a sprite the extractor wrote: either 8 rotations or one picture.
inline Frame ArtFrame(const json& art, const Str& letter, float seconds) {
    Frame frame;
    frame.seconds = seconds;
    if (!art.contains("frames") || !art["frames"].contains(letter)) {
        Fail("missing frame " + letter + " in " + art.value("path", Str("?")));
        return frame;
    }
    const Str path = art["path"].get<Str>() + "/" + letter;
    if (art["frames"][letter].get<bool>()) {
        for (int slot = 0; slot < 8; ++slot) frame.textures[slot] = path + std::to_string(slot) + ".png";
    } else {
        frame.textures.fill(path + ".png");
    }
    return frame;
}

inline std::vector<Frame> ArtFrames(const json& art, const Str& letters, float seconds) {
    std::vector<Frame> frames;
    for (const char letter : letters) frames.push_back(ArtFrame(art, Str(1, letter), seconds));
    return frames;
}

inline float TotalTime(const std::vector<Frame>& frames) {
    float total = 0.0f;
    for (const Frame& frame : frames) total += frame.seconds;
    return total;
}

// Writes Assets/<reference> and returns the reference.
inline Str Book(const Str& reference, const std::vector<Frame>& frames, FlipbookLoopMode mode) {
    FlipbookAsset asset;
    asset.loopMode = mode;
    asset.playOnStart = true;
    int index = 1;
    for (const Frame& frame : frames) {
        FlipbookFrame out;
        out.name = "Frame" + std::to_string(index++);
        out.textures = frame.textures;
        out.duration = frame.seconds;
        asset.frames.push_back(std::move(out));
    }
    const fs::path file = gAssets / reference;
    fs::create_directories(file.parent_path());
    Str error;
    if (!FlipbookIO::Save(file, asset, &error)) Fail("flipbook " + reference + ": " + error);
    return reference;
}

// ── Prefabs ─────────────────────────────────────────────────────────────────

// Builds Assets/<reference> from an empty prefab. Returns the reference.
inline Str MakePrefab(const Str& reference, const bool uiEntity, const std::function<void(Level&, Entity&)>& fill) {
    const Str name = fs::path(reference).stem().string();
    Level prefab = Prefab::MakeEmpty(name, uiEntity);
    {
        LevelManager::ScopedCurrentLevel scope(prefab);
        fill(prefab, prefab.entities[0]);
    }
    const fs::path file = gAssets / reference;
    fs::create_directories(file.parent_path());
    Str error;
    if (!LevelSerialization::SavePrefabToFile(file, prefab, &error)) Fail("prefab " + reference + ": " + error);
    Prefab::Invalidate(reference);
    return reference;
}

// The sprite sits with its bottom on the Transform's position.
inline ComponentSprite* AddSprite(Entity& entity, const float widthPx, const float heightPx, const bool eightSided) {
    auto* transform = entity.GetComponent<ComponentTransform>();
    transform->scale = {widthPx * 0.25f, heightPx * 0.25f, heightPx * 0.25f};
    auto* sprite = entity.AddComponent<ComponentSprite>();
    sprite->sideCount = eightSided ? SIDECOUNT_45 : SIDECOUNT_SINGLE;
    sprite->isStatic = false;
    return sprite;
}

inline void SetSpriteFrame(ComponentSprite* sprite, const Frame& frame) { sprite->textureFileNames = frame.textures; }

// Collider sizes are local and get multiplied by the Transform's scale, so ask
// for world units here.
inline ComponentCollider* AddCapsule(Level& level, Entity& entity, const float radius, const float height,
                                     const bool trigger = false) {
    const auto* transform = entity.GetComponent<ComponentTransform>();
    auto* collider = entity.AddComponent<ComponentCollider>();
    collider->type = COLLIDERTYPE_CAPSULE;
    collider->scale = {radius / transform->scale.x, std::max(height, radius * 2.0f) / transform->scale.y,
                       radius / transform->scale.x};
    collider->isTrigger = trigger;
    collider->stepSize = 6.0f;
    (void)level;
    return collider;
}

inline ComponentRigidbody* AddBody(Entity& entity, const bool isStatic, const float mass = 1.0f) {
    auto* body = entity.AddComponent<ComponentRigidbody>();
    body->isStatic = isStatic;
    body->mass = mass;
    return body;
}

inline ComponentScript& AddLua(Entity& entity, const Str& scriptId) {
    ComponentScript& script = entity.AddScript();
    script.fileName = scriptId;
    script.enabled = true;
    return script;
}

inline ScriptValue PrefabRef(const Str& reference) { return EntityRefValue{INVALID_ID, reference}; }
inline ScriptValue AssetRef(const Str& reference) { return AssetRefValue{reference}; }

inline const json& Art(const Str& group, const Str& name) {
    static const json none = json::object();
    if (!gManifest[group].contains(name)) {
        Fail("no art for " + group + "/" + name);
        return none;
    }
    return gManifest[group][name];
}
