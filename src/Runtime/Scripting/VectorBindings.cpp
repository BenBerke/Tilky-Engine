//
// Created by berke on 6/20/2026.
//

#include "Headers/Runtime/Scripting/ScriptSystem.hpp"
#include "sol/sol.hpp"

void ScriptSystem::RegisterVectorBindings(sol::state& lua) {
    lua.new_usertype<Vector2>(
            "Vector2",
            sol::constructors<Vector2(), Vector2(float, float)>(),
            "x", &Vector2::x,
            "y", &Vector2::y,

            // Properties
            "length", sol::property([](const Vector2 &self) {
                return Vector2Math::Length(self);
            }),
            "lengthSquared", sol::property([](const Vector2 &self) {
                return Vector2Math::LengthSquared(self);
            }),
            "normalized", sol::property([](const Vector2 &self) {
                return Vector2Math::Normalized(self);
            })
        );

    lua.new_usertype<Vector3>(
        "Vector3",
        sol::constructors<Vector3(), Vector3(float, float, float)>(),
        "x", &Vector3::x,
        "y", &Vector3::y,
        "z", &Vector3::z,
        "length", sol::property([](const Vector3 &self) {
            return Vector3Math::Length(self);
        }),
        "lengthSquared", sol::property([](const Vector3 &self) {
            return Vector3Math::LengthSquared(self);
        }),
        "normalized", sol::property([](const Vector3 &self) {
            return Vector3Math::Normalized(self);
        })
    );

    lua.new_usertype<Vector4>(
        "Vector4",
        sol::constructors<Vector4(), Vector4(float, float, float)>(),
        "x", &Vector4::x,
        "y", &Vector4::y,
         "z", &Vector4::z,
         "w", &Vector4::w
    );
}