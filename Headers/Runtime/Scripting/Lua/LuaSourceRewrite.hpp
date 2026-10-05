#ifndef TILKY_ENGINE_LUASOURCEREWRITE_HPP
#define TILKY_ENGINE_LUASOURCEREWRITE_HPP

#include <string>
#include <string_view>

// Load-time rewrite that makes `a.b.position.y = 50` write through to the
// component. Vector properties (transform.position, rigidbody.velocity, ...)
// return copies, so plain Lua would set .y on a temporary and throw it away.
// Lua cannot tell that chain apart from `local p = a.b.position; p.y = 50`
// (which must stay a copy), so the distinction is made on the source text:
//
//     a.b.position.y = 50   ->   __vref(a.b, "position").y = 50
//
// Only assignment targets of the form `<prefix>.<name>.<x|y|z|w>` are
// rewritten. __vref (registered in RegisterVectorBindings) reads the vector,
// sets the component and assigns the whole vector back through the
// property's setter. Line numbers are preserved, so error messages still
// point at the original lines.
namespace LuaSourceRewrite {
    // Name of the Lua global the rewritten code calls.
    inline constexpr const char* kVectorRefFunction = "__vref";

    [[nodiscard]] std::string RewriteVectorComponentAssignments(std::string_view source);
}

#endif //TILKY_ENGINE_LUASOURCEREWRITE_HPP
