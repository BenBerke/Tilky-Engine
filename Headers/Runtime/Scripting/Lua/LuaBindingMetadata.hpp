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
// Every binding file (LuaEntityBindings.cpp, LuaComponentBindings.cpp,
// LuaVectorBindings.cpp, ...) registers its usertypes' documentation here
// right alongside the matching sol::new_usertype<>()/set_function() calls,
// so the two never drift apart silently.
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

    // Terse, positional constructors for RegisterType() call sites - a
    // binding file documenting a few dozen properties/methods gets
    // unwieldy fast with `.name = ..., .luaType = ..., ...` on every single
    // entry, so these are the normal way to build one.
    inline ParamDoc Param(std::string name, std::string luaType) {
        return {std::move(name), std::move(luaType)};
    }

    inline PropertyDoc Prop(std::string name, std::string luaType, bool readOnly = false, std::string doc = {}) {
        return {std::move(name), std::move(luaType), readOnly, std::move(doc)};
    }

    inline MethodDoc Method(std::string name, std::vector<ParamDoc> params = {}, std::string returnType = {}, std::string doc = {}) {
        return {std::move(name), std::move(params), std::move(returnType), std::move(doc)};
    }

    inline TypeDoc Type(std::string name, std::string doc, std::vector<PropertyDoc> properties = {}, std::vector<MethodDoc> methods = {}) {
        return {std::move(name), std::move(doc), std::move(properties), std::move(methods)};
    }

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
