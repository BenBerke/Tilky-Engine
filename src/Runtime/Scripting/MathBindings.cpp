//
// Created by berke on 6/20/2026.
//
#include "Headers/Engine/GameTime.hpp"
#include "Headers/Runtime/Scripting/ScriptSystem.hpp"
#include "sol/sol.hpp"

namespace {
    //todo make this an engine setting
    uint32_t engineSeedState = 123456789;

    uint32_t XorShift32() {
        uint32_t x = engineSeedState;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        engineSeedState = x;
        return x;
    }

    float GetRandomFloat(const float min, const float max) {
        if (min >= max) return min;
        const float normalized = static_cast<float>(XorShift32()) / static_cast<float>(0xFFFFFFFF);
        return min + normalized * (max - min);
    }
}

void ScriptSystem::RegisterMathBindings(sol::state &lua) {
    sol::table math;

    if (lua["Tmath"].valid()) math = lua["Tmath"];
    else math = lua.create_named_table("Tmath");

    math.set_function("DegToRad", [](const float value) -> float {
        return value * Constants::DegToRad;
    });

    math.set_function("RadToDeg", [](const float value) -> float {
        return value * Constants::RadToDeg;
    });

    math.set_function("Clamp", [](const float value, const float minValue, const float maxValue) -> float {
        return std::clamp(value, minValue, maxValue);
    });

    math.set_function("Lerp", [](const float a, const float b, const float t) -> float {
        return std::lerp(a, b, t);
    });

    math.set_function("Vector2Distance", [](const Vector2& a, const Vector2& b) -> float {
        return Vector2Math::Distance(a, b);
    });

    math.set_function("Vector2DistanceSquared", [](const Vector2& a, const Vector2& b) -> float {
        return Vector2Math::DistanceSquared(a, b);
    });

    math.set_function("Vector2Dot", [](const Vector2& a, const Vector2& b) -> float {
       return Vector2Math::Dot(a, b);
    });

    math.set_function("Vector3Dot", [](const Vector3& a, const Vector3& b) -> float {
       return Vector3Math::Dot(a, b);
    });

    math.set_function("Vector3Distance", [](const Vector3& a, const Vector3& b) -> float {
        return Vector3Math::Distance(a, b);
    });

    math.set_function("Vector3DistanceSquared", [](const Vector3& a, const Vector3& b) -> float {
        return Vector3Math::DistanceSquared(a, b);
    });

    math.set_function("Vector3Cross", [](const Vector3& a, const Vector3& b) -> Vector3 {
        return Vector3Math::Cross(a, b);
    });

    math.set_function("Random", sol::overload(
        // No args: float 0.0 to 1.0
        // One arg: int 1 to max
        [](const int max) -> int {
            if (max <= 0) return 0;
            return XorShift32() % max;
        },
        // Two args: int min to max
        [](const int min, const int max) -> int {
            if (min >= max) return min;
            return min + (XorShift32() % (max - min + 1));
        }
    ));

    math.set_function("RandomF", sol::overload(
        []() -> float {
            return GetRandomFloat(0.0f, 1.0f);
        },

        [](const float max) -> float {
            return GetRandomFloat(0.0f, max);
        },

        [](const float min, const float max) -> float {
            return GetRandomFloat(min, max);
        }
    ));
}
