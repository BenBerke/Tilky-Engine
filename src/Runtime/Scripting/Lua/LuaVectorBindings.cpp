//
// Created by berke on 6/20/2026.
//

#include "Headers/Runtime/Scripting/Lua/LuaScripting.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaBindingMetadata.hpp"
#include "sol/sol.hpp"

#include <fmt/format.h>

namespace {
    using namespace LuaBindingMetadata;

    void RegisterVectorMetadata() {
        RegisterType(Type("Vector2", "A 2D vector. Callable as Vector2(x, y) or Vector2(). Supports +, -, * and / with another Vector2 (per component) or a number, unary -, == and tostring().", {
            Prop("x", "number"),
            Prop("y", "number"),
            Prop("length", "number", true),
            Prop("lengthSquared", "number", true),
            Prop("normalized", "Vector2", true),
        }));

        RegisterType(Type("Vector3", "A 3D vector. Callable as Vector3(x, y, z) or Vector3(). Supports +, -, * and / with another Vector3 (per component) or a number, unary -, == and tostring().", {
            Prop("x", "number"),
            Prop("y", "number"),
            Prop("z", "number"),
            Prop("length", "number", true),
            Prop("lengthSquared", "number", true),
            Prop("normalized", "Vector3", true),
        }));

        RegisterType(Type("Vector4", "A 4D vector, commonly used for RGBA colors. Callable as Vector4(x, y, z, w) or Vector4(). Supports +, -, * and / with another Vector4 (per component) or a number, unary -, == and tostring().", {
            Prop("x", "number"),
            Prop("y", "number"),
            Prop("z", "number"),
            Prop("w", "number"),
        }));
    }
}

void LuaScriptSystem::RegisterVectorBindings(sol::state& lua) {
    RegisterVectorMetadata();

    lua.new_usertype<Vector2>(
        "Vector2",
        sol::call_constructor,
        sol::constructors<
            Vector2(),
            Vector2(float, float)
        >(),
        "x", &Vector2::x,
        "y", &Vector2::y,

        "length", sol::property([](const Vector2& self) {
            return Vector2Math::Length(self);
        }),
        "lengthSquared", sol::property([](const Vector2& self) {
            return Vector2Math::LengthSquared(self);
        }),
        "normalized", sol::property([](const Vector2& self) {
            return Vector2Math::Normalized(self);
        }),

        sol::meta_function::addition, [](const Vector2& a, const Vector2& b) { return Vector2(a.x + b.x, a.y + b.y); },
        sol::meta_function::subtraction, [](const Vector2& a, const Vector2& b) { return Vector2(a.x - b.x, a.y - b.y); },
        sol::meta_function::multiplication, sol::overload(
            [](const Vector2& a, const Vector2& b) { return Vector2(a.x * b.x, a.y * b.y); },
            [](const Vector2& v, const float s) { return Vector2(v.x * s, v.y * s); },
            [](const float s, const Vector2& v) { return Vector2(v.x * s, v.y * s); }
        ),
        sol::meta_function::division, sol::overload(
            [](const Vector2& a, const Vector2& b) { return Vector2(a.x / b.x, a.y / b.y); },
            [](const Vector2& v, const float s) { return Vector2(v.x / s, v.y / s); }
        ),
        sol::meta_function::unary_minus, [](const Vector2& v) { return Vector2(-v.x, -v.y); },
        sol::meta_function::equal_to, [](const Vector2& a, const Vector2& b) { return a.x == b.x && a.y == b.y; },
        sol::meta_function::to_string, [](const Vector2& v) { return fmt::format("Vector2({}, {})", v.x, v.y); }
    );

    lua.new_usertype<Vector3>(
        "Vector3",
        sol::call_constructor,
        sol::constructors<
            Vector3(),
            Vector3(float, float, float)
        >(),
        "x", &Vector3::x,
        "y", &Vector3::y,
        "z", &Vector3::z,

        "length", sol::property([](const Vector3& self) {
            return Vector3Math::Length(self);
        }),
        "lengthSquared", sol::property([](const Vector3& self) {
            return Vector3Math::LengthSquared(self);
        }),
        "normalized", sol::property([](const Vector3& self) {
            return Vector3Math::Normalized(self);
        }),

        sol::meta_function::addition, [](const Vector3& a, const Vector3& b) { return Vector3(a.x + b.x, a.y + b.y, a.z + b.z); },
        sol::meta_function::subtraction, [](const Vector3& a, const Vector3& b) { return Vector3(a.x - b.x, a.y - b.y, a.z - b.z); },
        sol::meta_function::multiplication, sol::overload(
            [](const Vector3& a, const Vector3& b) { return Vector3(a.x * b.x, a.y * b.y, a.z * b.z); },
            [](const Vector3& v, const float s) { return Vector3(v.x * s, v.y * s, v.z * s); },
            [](const float s, const Vector3& v) { return Vector3(v.x * s, v.y * s, v.z * s); }
        ),
        sol::meta_function::division, sol::overload(
            [](const Vector3& a, const Vector3& b) { return Vector3(a.x / b.x, a.y / b.y, a.z / b.z); },
            [](const Vector3& v, const float s) { return Vector3(v.x / s, v.y / s, v.z / s); }
        ),
        sol::meta_function::unary_minus, [](const Vector3& v) { return Vector3(-v.x, -v.y, -v.z); },
        sol::meta_function::equal_to, [](const Vector3& a, const Vector3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; },
        sol::meta_function::to_string, [](const Vector3& v) { return fmt::format("Vector3({}, {}, {})", v.x, v.y, v.z); }
    );

    lua.new_usertype<Vector4>(
        "Vector4",
        sol::call_constructor,
        sol::constructors<
            Vector4(),
            Vector4(float, float, float, float)
        >(),
        "x", &Vector4::x,
        "y", &Vector4::y,
        "z", &Vector4::z,
        "w", &Vector4::w,

        sol::meta_function::addition, [](const Vector4& a, const Vector4& b) { return Vector4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w); },
        sol::meta_function::subtraction, [](const Vector4& a, const Vector4& b) { return Vector4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w); },
        sol::meta_function::multiplication, sol::overload(
            [](const Vector4& a, const Vector4& b) { return Vector4(a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w); },
            [](const Vector4& v, const float s) { return Vector4(v.x * s, v.y * s, v.z * s, v.w * s); },
            [](const float s, const Vector4& v) { return Vector4(v.x * s, v.y * s, v.z * s, v.w * s); }
        ),
        sol::meta_function::division, sol::overload(
            [](const Vector4& a, const Vector4& b) { return Vector4(a.x / b.x, a.y / b.y, a.z / b.z, a.w / b.w); },
            [](const Vector4& v, const float s) { return Vector4(v.x / s, v.y / s, v.z / s, v.w / s); }
        ),
        sol::meta_function::unary_minus, [](const Vector4& v) { return Vector4(-v.x, -v.y, -v.z, -v.w); },
        sol::meta_function::equal_to, [](const Vector4& a, const Vector4& b) { return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w; },
        sol::meta_function::to_string, [](const Vector4& v) { return fmt::format("Vector4({}, {}, {}, {})", v.x, v.y, v.z, v.w); }
    );
}