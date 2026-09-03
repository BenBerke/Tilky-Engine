
// Created by berke on 6/20/2026.
//
#include "Headers/Engine/GameTime.hpp"
#include "../../../../Headers/Runtime/Scripting/Lua/LuaScripting.hpp"
#include "sol/sol.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>

#include <spdlog/spdlog.h>

namespace {
    //todo make this an engine setting
    uint32_t engineSeedState = 123456789;

    constexpr int RANDOM_NUMBER_SIZE = 512;

    const float randomNumbers[RANDOM_NUMBER_SIZE] = {
    0.840188f, 0.394383f, 0.783099f, 0.798440f, 0.911647f, 0.197551f, 0.335223f, 0.768230f,
    0.277775f, 0.553970f, 0.477397f, 0.628871f, 0.364784f, 0.513401f, 0.952230f, 0.916195f,
    0.635712f, 0.717297f, 0.141603f, 0.606969f, 0.016301f, 0.242887f, 0.137232f, 0.804177f,
    0.156679f, 0.400944f, 0.129790f, 0.108809f, 0.998925f, 0.218257f, 0.512932f, 0.839112f,
    0.612640f, 0.296032f, 0.637552f, 0.524287f, 0.493583f, 0.972775f, 0.292517f, 0.771358f,
    0.526745f, 0.769914f, 0.400229f, 0.891529f, 0.283315f, 0.352458f, 0.807725f, 0.919026f,
    0.001254f, 0.958921f, 0.364658f, 0.623179f, 0.806086f, 0.871032f, 0.155709f, 0.835478f,
    0.413284f, 0.141386f, 0.063852f, 0.084126f, 0.938837f, 0.697424f, 0.295282f, 0.361953f,
    0.071746f, 0.293816f, 0.923847f, 0.584739f, 0.875412f, 0.385719f, 0.492716f, 0.198375f,
    0.732958f, 0.412853f, 0.824916f, 0.639281f, 0.108492f, 0.583921f, 0.749102f, 0.284739f,
    0.672941f, 0.837194f, 0.093847f, 0.492816f, 0.371948f, 0.928471f, 0.573928f, 0.184926f,
    0.492837f, 0.718392f, 0.284719f, 0.938471f, 0.629184f, 0.384719f, 0.819472f, 0.502847f,
    0.391827f, 0.648291f, 0.183749f, 0.892741f, 0.472918f, 0.294718f, 0.739184f, 0.958271f,
    0.128472f, 0.593827f, 0.847291f, 0.301847f, 0.764928f, 0.429184f, 0.918274f, 0.658392f,
    0.239481f, 0.871928f, 0.548291f, 0.109284f, 0.793847f, 0.362849f, 0.948271f, 0.482719f,
    0.629481f, 0.174928f, 0.839271f, 0.518294f, 0.294817f, 0.749182f, 0.902847f, 0.384729f,
    0.471928f, 0.829471f, 0.193847f, 0.658291f, 0.928471f, 0.371928f, 0.584729f, 0.209481f,
    0.749281f, 0.482719f, 0.893741f, 0.128472f, 0.639184f, 0.958271f, 0.304817f, 0.782941f,
    0.519284f, 0.274819f, 0.849271f, 0.693827f, 0.138472f, 0.928471f, 0.402847f, 0.761938f,
    0.358291f, 0.894721f, 0.628471f, 0.173928f, 0.784912f, 0.439182f, 0.968271f, 0.291847f,
    0.582719f, 0.819472f, 0.348271f, 0.702948f, 0.194827f, 0.948271f, 0.472918f, 0.638291f,
    0.284719f, 0.759382f, 0.892741f, 0.148271f, 0.629481f, 0.384729f, 0.918274f, 0.503928f,
    0.739184f, 0.264819f, 0.849271f, 0.418294f, 0.972841f, 0.183749f, 0.658291f, 0.329481f,
    0.804918f, 0.593827f, 0.128472f, 0.749182f, 0.482719f, 0.938471f, 0.271948f, 0.684729f,
    0.392847f, 0.859271f, 0.528471f, 0.163928f, 0.794821f, 0.438291f, 0.902847f, 0.284719f,
    0.649281f, 0.193847f, 0.829471f, 0.573928f, 0.318472f, 0.748291f, 0.962847f, 0.409284f,
    0.783921f, 0.248271f, 0.892741f, 0.518294f, 0.138472f, 0.672948f, 0.948271f, 0.362849f,
    0.491827f, 0.839271f, 0.274819f, 0.709382f, 0.982741f, 0.158294f, 0.629481f, 0.448271f,
    0.793847f, 0.328471f, 0.918274f, 0.584729f, 0.209481f, 0.864928f, 0.472918f, 0.739184f,
    0.128472f, 0.658291f, 0.948271f, 0.384729f, 0.819472f, 0.271948f, 0.593827f, 0.902847f,
    0.438291f, 0.764928f, 0.183749f, 0.849271f, 0.528471f, 0.294817f, 0.972841f, 0.639184f,
    0.358291f, 0.892741f, 0.718392f, 0.148271f, 0.784912f, 0.402847f, 0.938471f, 0.264819f,
    0.584729f, 0.829471f, 0.391827f, 0.672948f, 0.109284f, 0.958271f, 0.482719f, 0.749182f,
    0.239481f, 0.871928f, 0.603928f, 0.328471f, 0.793847f, 0.448271f, 0.918274f, 0.173928f,
    0.692841f, 0.518294f, 0.849271f, 0.284719f, 0.938471f, 0.371928f, 0.629481f, 0.804918f,
    0.158294f, 0.749182f, 0.492816f, 0.982741f, 0.274819f, 0.658291f, 0.892741f, 0.418294f,
    0.761938f, 0.304817f, 0.948271f, 0.584729f, 0.128472f, 0.839271f, 0.472918f, 0.693827f,
    0.218257f, 0.782941f, 0.902847f, 0.362849f, 0.629481f, 0.438291f, 0.972841f, 0.502847f,
    0.849271f, 0.193847f, 0.718392f, 0.384729f, 0.928471f, 0.271948f, 0.648291f, 0.892741f,
    0.402847f, 0.759382f, 0.138472f, 0.819472f, 0.573928f, 0.348271f, 0.962847f, 0.639184f,
    0.284719f, 0.871928f, 0.702948f, 0.183749f, 0.794821f, 0.491827f, 0.938471f, 0.318472f,
    0.658291f, 0.829471f, 0.428271f, 0.749182f, 0.093847f, 0.948271f, 0.528471f, 0.684729f,
    0.274819f, 0.892741f, 0.619382f, 0.358291f, 0.783921f, 0.472918f, 0.918274f, 0.148271f,
    0.739184f, 0.409284f, 0.859271f, 0.294817f, 0.982741f, 0.384729f, 0.638291f, 0.804918f,
    0.163928f, 0.764928f, 0.502847f, 0.928471f, 0.248271f, 0.672948f, 0.892741f, 0.438291f,
    0.793847f, 0.318472f, 0.958271f, 0.573928f, 0.118472f, 0.849271f, 0.482719f, 0.709382f,
    0.209481f, 0.784912f, 0.918274f, 0.371928f, 0.648291f, 0.429184f, 0.968271f, 0.519284f,
    0.839271f, 0.183749f, 0.728392f, 0.392847f, 0.902847f, 0.284719f, 0.658291f, 0.871928f,
    0.394383f, 0.769914f, 0.141603f, 0.824916f, 0.584739f, 0.335223f, 0.952230f, 0.628871f,
    0.292517f, 0.891529f, 0.697424f, 0.155709f, 0.807725f, 0.477397f, 0.938837f, 0.301847f,
    0.672941f, 0.804177f, 0.412853f, 0.749102f, 0.084126f, 0.958921f, 0.513401f, 0.717297f,
    0.242887f, 0.875412f, 0.635712f, 0.364784f, 0.783099f, 0.493583f, 0.916195f, 0.137232f,
    0.732958f, 0.400944f, 0.837194f, 0.296032f, 0.972775f, 0.385719f, 0.612640f, 0.840188f,
    0.156679f, 0.768230f, 0.524287f, 0.911647f, 0.277775f, 0.637552f, 0.891529f, 0.400229f,
    0.798440f, 0.352458f, 0.958921f, 0.553970f, 0.108809f, 0.835478f, 0.492716f, 0.769914f,
    0.218257f, 0.806086f, 0.919026f, 0.364658f, 0.606969f, 0.413284f, 0.998925f, 0.526745f,
    0.871032f, 0.197551f, 0.717297f, 0.384719f, 0.923847f, 0.283315f, 0.623179f, 0.807725f,
    0.400944f, 0.771358f, 0.129790f, 0.839112f, 0.583921f, 0.361953f, 0.938837f, 0.628871f,
    0.295282f, 0.875412f, 0.697424f, 0.141603f, 0.804177f, 0.477397f, 0.952230f, 0.296032f,
    0.635712f, 0.824916f, 0.412853f, 0.749102f, 0.071746f, 0.958921f, 0.512932f, 0.718392f,
    0.242887f, 0.891529f, 0.606969f, 0.364784f, 0.783099f, 0.493583f, 0.916195f, 0.156679f,
    0.732958f, 0.394383f, 0.840188f, 0.277775f, 0.972775f, 0.385719f, 0.637552f, 0.807725f,
    0.197551f, 0.768230f, 0.526745f, 0.911647f, 0.283315f, 0.612640f, 0.891529f, 0.400229f,
    0.798440f, 0.335223f, 0.958921f, 0.553970f, 0.016301f, 0.839112f, 0.492716f, 0.769914f
    };

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

    float GetRandomFast() {
        static std::size_t currentIndex = 0;
        if (currentIndex >= RANDOM_NUMBER_SIZE) currentIndex = 0;
        return randomNumbers[++currentIndex];
    }
}

void LuaScriptSystem::RegisterMathBindings(sol::state &lua) {
    const sol::object existing = lua["Tmath"];
    sol::table math;

    if (existing.get_type() == sol::type::table) {
        math = existing.as<sol::table>();
    }
    else {
        if (existing.get_type() != sol::type::nil)
            spdlog::warn("Replacing Lua global 'Tmath' because it is not a table");

        math = lua.create_named_table("Tmath");
    }

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
        // One arg: int in [0, max).
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

    math.set_function("RandomFast", []() -> float { return GetRandomFast(); });
}