#ifndef TILKY_ENGINE_LUASCRIPTRUNTIME_HPP
#define TILKY_ENGINE_LUASCRIPTRUNTIME_HPP

// Thin accessor API into the live script-instance registry owned by
// LuaSystem.cpp. LuaWrappers.hpp's GameObject/Behaviour wrapper structs are
// plain header-only data ({Level*, ID, ...}), same as every other ScriptXxx
// wrapper - they must stay that way so they remain cheap, copyable Lua
// userdata. But a Behaviour reference needs to read/write another script
// instance's *live* sol::environment, and GameObject::Destroy() needs to
// queue against the running Update() loop - both of those are runtime state
// that only LuaSystem.cpp actually owns. This header is the seam between the
// two: wrapper structs call these free functions instead of reaching into
// scripting internals directly.
struct Level;

#include <sol/sol.hpp>

#include "Headers/Objects/EntityTypes.hpp"
#include "Headers/Objects/ScriptPublicType.hpp"

namespace LuaScriptRuntime {
    // True if a script instance with this ScriptInstanceID currently has a
    // loaded Lua environment (i.e. it survived its most recent Start()).
    // entityId is a defensive cross-check, not the lookup key - instanceId
    // is already unique across the whole level (see ScriptComponentStorage).
    bool IsInstanceValid(ID entityId, ScriptInstanceID instanceId);

    // Reads/writes ComponentScript::enabled for the instance's *own* enabled
    // flag - independent from the owning GameObject's enabled flag (see
    // Entity::enabled). Returns false / no-ops if the instance or its
    // ComponentScript no longer exists.
    bool GetInstanceEnabled(Level& level, ID entityId, ScriptInstanceID instanceId);
    void SetInstanceEnabled(Level& level, ID entityId, ScriptInstanceID instanceId, bool enabled);

    // Reads/writes one named field (a plain global in the target instance's
    // sol::environment) - this is how a Behaviour reference reaches another
    // script's public state *and* its functions without any owner-ID or
    // filename lookup on the Lua side. Returns nil if the instance is
    // invalid; silently no-ops on Set if the instance is invalid.
    sol::object GetInstanceField(ID entityId, ScriptInstanceID instanceId, const std::string& key, sol::this_state state);
    void SetInstanceField(ID entityId, ScriptInstanceID instanceId, const std::string& key, sol::object value);

    // GameObject:Destroy() queues here instead of mutating Level mid-Update()
    // (component storages use swap-and-pop; erasing under an active iterator
    // elsewhere in the same Update() would corrupt indices other systems
    // still hold this frame). LuaScriptSystem::Update() flushes this queue
    // once, after every instance's Update()/FixedUpdate() has run, calling
    // OnDestroy on every affected script first. See ProcessPendingDestroys in
    // LuaSystem.cpp.
    void QueueEntityDestroy(ID entityId);

    // Converts one serialized field value into the live Lua object a script
    // actually sees when the field is seeded at instance-load time:
    // primitives/math types pass through as themselves; GameObjectRefValue /
    // ComponentRefValue / BehaviourRefValue resolve into a real GameObject /
    // component wrapper / Behaviour proxy (or nil if the target no longer
    // exists); AssetRefValue resolves to the plain reference string (the same
    // format every other asset-path field in the engine already uses).
    sol::object ResolveScriptValue(sol::state_view lua, Level& level, const ScriptValue& value);
}

#endif //TILKY_ENGINE_LUASCRIPTRUNTIME_HPP
