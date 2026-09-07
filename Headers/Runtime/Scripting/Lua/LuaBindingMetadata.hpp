#ifndef TILKY_ENGINE_LUABINDINGMETADATA_HPP
#define TILKY_ENGINE_LUABINDINGMETADATA_HPP

#include <string>
#include <vector>

// A small, engine-side registry describing the Lua-visible scripting API
// (types, properties, methods, parameter/return types, documentation) as
// plain data, independent of sol2. This exists so Tilky can eventually
// generate LuaLS (Lua Language Server) type-definition stubs from ONE
// source of truth instead of hand-maintaining separate autocomplete
// definitions - see GenerateLuaLSStub below.
//
// Scope note: this pass wires metadata registration into the newly-added
// GameObject and Behaviour bindings only (see RegisterEntityBindings in
// LuaEntityBindings.cpp and RegisterBehaviourRefBindings in LuaSystem.cpp)
// as a working example of the pattern. Retrofitting every other existing
// binding (Transform, Rigidbody, Camera, Sprite, ...) with matching
// RegisterType() calls is intentionally left as follow-up work - it is
// mechanical but high-volume, and out of scope for "ensure the architecture
// can support LuaLS metadata" (see the scripting redesign notes).
namespace LuaBindingMetadata {
    struct ParamDoc {
        std::string name;
        std::string luaType; // e.g. "number", "string", "GameObject"
    };

    struct MethodDoc {
        std::string name;
        std::vector<ParamDoc> params;
        std::string returnType; // empty = no return value
        std::string doc;
    };

    struct PropertyDoc {
        std::string name;
        std::string luaType;
        bool readOnly = false;
        std::string doc;
    };

    struct TypeDoc {
        std::string name; // the Lua-visible type name, e.g. "GameObject"
        std::string doc;
        std::vector<PropertyDoc> properties;
        std::vector<MethodDoc> methods;
    };

    // Registers one type's documentation. Call once per usertype, right
    // alongside its sol::new_usertype<>() registration.
    void RegisterType(TypeDoc type);

    // Every type registered so far, in registration order.
    const std::vector<TypeDoc>& AllTypes();

    // Writes a LuaLS-compatible `---@meta` stub file (---@class / ---@field
    // / ---@param / ---@return annotations) built from AllTypes(). Point
    // LuaLS's `Lua.workspace.library` setting at the containing folder to
    // get autocomplete/hover/signature-help for every registered engine
    // type inside Tilky scripts. Returns false (and logs why) if the file
    // couldn't be written.
    bool GenerateLuaLSStub(const std::string& outputPath);
}

#endif //TILKY_ENGINE_LUABINDINGMETADATA_HPP
