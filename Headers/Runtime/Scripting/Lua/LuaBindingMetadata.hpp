#ifndef TILKY_ENGINE_LUABINDINGMETADATA_HPP
#define TILKY_ENGINE_LUABINDINGMETADATA_HPP

#include <string>
#include <string_view>
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
// so the two never drift apart silently. LuaSystem.cpp does the same for
// the callbacks it looks up in a script and the globals it injects into
// one, so everything a script can see or define is described here: the
// editor's autocomplete and tooltips, reserved-name checks and the LuaLS
// stub all read it instead of keeping their own lists.
namespace LuaBindingMetadata {
    struct ParamDoc {
        std::string name;
        std::string luaType; // e.g. "number", "string", "Entity"
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

    // One named value of an enum table (Component.Sprite, Key.Space, ...).
    struct EnumValueDoc {
        std::string name;
        int value = 0;
        std::string doc;
    };

    struct TypeDoc {
        std::string name; // the Lua-visible type name, e.g. "Entity"
        std::string doc;
        std::vector<PropertyDoc> properties;
        std::vector<MethodDoc> methods;
        // True for a global table/singleton (mathT, Game, Input, Debug,
        // GameTime): the LuaLS stub then declares it as a global and its
        // functions with `.` (mathT.Clamp(...)) instead of `:`.
        bool isGlobalTable = false;
        // Non-empty for a global enum table (Component, Key): the stub
        // declares it as a LuaLS `---@enum`, so a parameter typed with its
        // name autocompletes to its values. Such a type has no
        // properties/methods.
        std::vector<EnumValueDoc> enumValues;
    };

    // Which scripts a callback is looked up in.
    enum class CallbackOwner { Any, Entity, Sector };

    // A function a script defines and the engine calls (Start,
    // OnTriggerEnter, ...).
    struct CallbackDoc {
        std::string name;
        CallbackOwner owner = CallbackOwner::Any;
        std::vector<ParamDoc> params;
        std::string doc;
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

    // Same as Type(), for a global table/singleton - see TypeDoc::isGlobalTable.
    inline TypeDoc GlobalTable(std::string name, std::string doc, std::vector<PropertyDoc> properties = {}, std::vector<MethodDoc> methods = {}) {
        return {std::move(name), std::move(doc), std::move(properties), std::move(methods), true};
    }

    // A global table of named integers, see TypeDoc::enumValues.
    inline TypeDoc Enum(std::string name, std::string doc, std::vector<EnumValueDoc> values) {
        return {std::move(name), std::move(doc), {}, {}, true, std::move(values)};
    }

    // Registers one type's documentation. Call once per usertype, right
    // alongside its sol::new_usertype<>() registration.
    void RegisterType(TypeDoc type);

    // Every type registered so far, in registration order.
    const std::vector<TypeDoc>& AllTypes();

    // Registers one callback's documentation. LuaSystem.cpp calls this
    // from the same table it looks the callbacks up with.
    void RegisterCallback(CallbackDoc callback);
    const std::vector<CallbackDoc>& AllCallbacks();

    // Registers a global the engine injects into every script's
    // environment (entity, sector, Global), as opposed to a global table
    // registered with RegisterType().
    void RegisterScriptGlobal(PropertyDoc global);
    const std::vector<PropertyDoc>& AllScriptGlobals();

    // Lua 5.4's reserved words.
    const std::vector<std::string>& LuaKeywordNames();
    [[nodiscard]] bool IsLuaKeyword(std::string_view name);

    // True for any name the engine defines for scripts: a callback, a
    // script global, or a registered type/global table/enum. A script
    // variable with such a name would hide the engine's, so public fields
    // and flipbook event functions can't use one. Lua keywords are checked
    // separately (IsLuaKeyword) so callers can word the error differently.
    // Only complete once scripting has initialized.
    [[nodiscard]] bool IsEngineName(std::string_view name);

    // "OnTriggerEnter(other: Entity)" - a callback's signature as shown in
    // the editor.
    [[nodiscard]] std::string CallbackSignature(const CallbackDoc& callback);

    // "Entity scripts only. " / "Sector scripts only. " / "" - prefixed to
    // a callback's doc wherever it is shown.
    [[nodiscard]] std::string CallbackOwnerNote(CallbackOwner owner);

    // Writes a LuaLS-compatible `---@meta` stub file (---@class / ---@field
    // / ---@param / ---@return annotations) built from AllTypes(). Point
    // LuaLS's `Lua.workspace.library` setting at the containing folder to
    // get autocomplete/hover/signature-help for every registered engine
    // type inside Tilky scripts. Returns false (and logs why) if the file
    // couldn't be written.
    bool GenerateLuaLSStub(const std::string& outputPath);
}

#endif //TILKY_ENGINE_LUABINDINGMETADATA_HPP
