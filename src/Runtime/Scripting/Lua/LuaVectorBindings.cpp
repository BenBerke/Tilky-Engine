//
// Created by berke on 6/20/2026.
//

#include "Headers/Runtime/Scripting/Lua/LuaScripting.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaBindingMetadata.hpp"
#include "sol/sol.hpp"

namespace {
    using namespace LuaBindingMetadata;

    void RegisterVectorMetadata() {
        RegisterType(Type("Vector2", "A 2D vector. Callable as Vector2(x, y) or Vector2().", {
            Prop("x", "number"),
            Prop("y", "number"),
            Prop("length", "number", true),
            Prop("lengthSquared", "number", true),
            Prop("normalized", "Vector2", true),
        }));

        RegisterType(Type("Vector3", "A 3D vector. Callable as Vector3(x, y, z) or Vector3().", {
            Prop("x", "number"),
            Prop("y", "number"),
            Prop("z", "number"),
            Prop("length", "number", true),
            Prop("lengthSquared", "number", true),
            Prop("normalized", "Vector3", true),
        }));

        RegisterType(Type("Vector4", "A 4D vector, commonly used for RGBA colors. Callable as Vector4(x, y, z, w) or Vector4().", {
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
        })
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
        })
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
        "w", &Vector4::w
    );
}