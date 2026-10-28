#ifndef TILKY_ENGINE_LUASCRIPTCOMPILER_HPP
#define TILKY_ENGINE_LUASCRIPTCOMPILER_HPP

#include "Headers/Objects/ScriptPublicType.hpp"

#include <string>
#include <string_view>
#include <vector>

// Turns a Tilky script into plain Lua plus the data the engine needs about it.
// Runs on the source text only - the script is never executed.
//
// Declarations. A declaration keyword at the top level of a script declares a
// variable with a type:
//
//     public number speed = 15
//     public Entity player = nil
//     public range(0, 100) number health = 100      -- attributes go before the type
//     public enum(Idle, Patrol, Chase) mode = Patrol
//     hidden public number ammo = 5                -- other scripts can use it, the Inspector doesn't show it
//
// Grammar: <keyword> {<attribute>[(<args>)]} <Type>[(<args>)] <name> [= <literal>]
// The declaration is replaced by `name = <value>`, so the engine runs plain
// Lua. `public` is the only keyword and there are no attributes yet; both are
// registries in LuaScriptCompiler.cpp (DeclarationKeywords, FieldAttributes),
// so adding one is one handler plus one table entry. `hidden` may only appear
// right before `public` on a variable; anything else is a compile error.
//
// Public functions. `ref:Fn(a)` passes the Behaviour handle as the first
// argument, so a function other scripts call needs a parameter for it.
// `public function` adds that parameter, and the script's own calls pass nil
// for it:
//
//     public function TakeDamage(amount)  ->  function TakeDamage(__self, amount)
//     TakeDamage(5)                       ->  TakeDamage(nil, 5)
//
// A public function can only be called by name: using it as a value
// (`pcall(TakeDamage, 5)`, `cb = TakeDamage`), redefining it or shadowing it
// with a local, parameter or loop variable is a compile error, since those
// would skip the rewrite.
//
// Vector write-through. Vector properties (transform.position, ...) return
// copies, so `a.b.position.y = 50` would set .y on a temporary. That exact
// assignment form is rewritten to `__vref(a.b, "position").y = 50`, which
// writes the whole vector back through the property's setter; reading a
// vector into a variable still gives a copy. __vref is registered in
// RegisterVectorBindings.
//
// The output always has the same line count as the input, so Lua error
// messages point at the lines the user wrote.
namespace LuaScriptCompiler {
    // Name of the Lua global the vector write-through code calls.
    inline constexpr const char* kVectorRefFunction = "__vref";
    // Name of the hidden first parameter of every public function.
    inline constexpr const char* kSelfParameter = "__self";

    struct Diagnostic {
        int line = 0; // 1-based
        std::string message;
    };

    // One declaration as written, for editor tooling.
    struct DeclarationInfo {
        std::string keyword;  // "public"
        std::string typeName; // as written: "number", "Entity", "enum", ...
        std::string luaType;  // the Lua usertype the value has at runtime ("Entity", "Vector3", ...), or empty
        std::string name;
        int line = 0;
        bool hidden = false;  // `hidden public`: reachable from scripts, not shown in the Inspector
    };

    struct Result {
        std::string luaSource;
        std::vector<ScriptPublicField> publicFields;
        std::vector<DeclarationInfo> declarations;
        std::vector<std::string> publicFunctions;
        std::vector<Diagnostic> errors;

        [[nodiscard]] bool Succeeded() const { return errors.empty(); }
    };

    [[nodiscard]] Result Compile(std::string_view source);

    // For the script editor's highlighting and autocomplete.
    [[nodiscard]] std::vector<std::string> DeclarationKeywordNames();
    [[nodiscard]] std::vector<std::string> FieldTypeNames();
    [[nodiscard]] std::vector<std::string> FieldAttributeNames();

    // "moveSpeed" -> "Move Speed", "jump_key" -> "Jump Key", "UIScale" -> "UI Scale".
    [[nodiscard]] std::string DisplayNameFromIdentifier(std::string_view identifier);
}

#endif //TILKY_ENGINE_LUASCRIPTCOMPILER_HPP
