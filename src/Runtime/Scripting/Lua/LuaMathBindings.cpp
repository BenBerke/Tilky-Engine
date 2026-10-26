
// Created by berke on 6/20/2026.
//
#include "Headers/Engine/GameTime.hpp"
#include "Headers/Math/MathHelpers.hpp"
#include "Headers/Math/Quaternion/QuaternionMath.hpp"
#include "../../../../Headers/Runtime/Scripting/Lua/LuaScripting.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaBindingMetadata.hpp"
#include "sol/sol.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <numbers>
#include <tuple>

#include <spdlog/spdlog.h>

#include "Headers/Math/MathFunctions.hpp"

namespace {
    using namespace LuaBindingMetadata;

    void RegisterMathMetadata() {
        RegisterType(GlobalTable("mathT", "Global math helper table. Sin/Cos/Tan/Asin/Acos/Atan/Atan2 use radians; everything with Angle in its name uses degrees.", {
            Prop("Pi", "number", true, "3.14159..."),
            Prop("Tau", "number", true, "2 * Pi - one full turn in radians."),
            Prop("HalfPi", "number", true, "Pi / 2 - a quarter turn in radians."),
            Prop("E", "number", true, "Euler's number, 2.71828..."),
            Prop("Sqrt2", "number", true, "Square root of 2."),
            Prop("Infinity", "number", true, "Positive infinity."),
            Prop("NegativeInfinity", "number", true, "Negative infinity."),
            Prop("Epsilon", "number", true, "Default tolerance of Approximately (1e-6)."),
            Prop("MaxInteger", "integer", true, "Largest integer value."),
            Prop("MinInteger", "integer", true, "Smallest integer value."),
        }, {
            // --- Basic ---
            Method("Abs", {Param("value", "number")}, "number"),
            Method("Sign", {Param("value", "number")}, "number", "-1, 0 or 1."),
            Method("Floor", {Param("value", "number")}, "integer"),
            Method("Ceil", {Param("value", "number")}, "integer"),
            Method("Round", {Param("value", "number"), Param("digits", "integer")}, "number", "Rounds half away from zero. Without digits returns an integer; with digits rounds to that many decimals."),
            Method("Trunc", {Param("value", "number")}, "integer", "Drops the fractional part (rounds toward zero)."),
            Method("Frac", {Param("value", "number")}, "number", "The fractional part: value - Trunc(value)."),
            Method("Min", {Param("a", "number"), Param("b", "number")}, "number", "Smallest of any number of arguments."),
            Method("Max", {Param("a", "number"), Param("b", "number")}, "number", "Largest of any number of arguments."),
            Method("Mod", {Param("value", "number"), Param("divisor", "number")}, "number", "Floored modulo: the result has the divisor's sign, like Lua's % operator."),
            Method("Fmod", {Param("value", "number"), Param("divisor", "number")}, "number", "C-style remainder: the result has the value's sign."),
            Method("Sqrt", {Param("value", "number")}, "number"),
            Method("Pow", {Param("base", "number"), Param("exponent", "number")}, "number"),
            Method("Exp", {Param("value", "number")}, "number", "E raised to value."),
            Method("Log", {Param("value", "number"), Param("base", "number")}, "number", "Natural log, or the log in the given base."),
            Method("Log10", {Param("value", "number")}, "number"),
            Method("Log2", {Param("value", "number")}, "number"),
            Method("Hypot", {Param("x", "number"), Param("y", "number")}, "number", "Sqrt(x*x + y*y) without overflow."),

            // --- Trigonometry (radians) ---
            Method("Sin", {Param("radians", "number")}, "number"),
            Method("Cos", {Param("radians", "number")}, "number"),
            Method("Tan", {Param("radians", "number")}, "number"),
            Method("Asin", {Param("value", "number")}, "number", "Radians."),
            Method("Acos", {Param("value", "number")}, "number", "Radians."),
            Method("Atan", {Param("y", "number"), Param("x", "number")}, "number", "Radians. With one argument it is atan(y); with two it is Atan2(y, x)."),
            Method("Atan2", {Param("y", "number"), Param("x", "number")}, "number", "Radians, full -Pi..Pi range."),
            Method("Sinh", {Param("value", "number")}, "number"),
            Method("Cosh", {Param("value", "number")}, "number"),
            Method("Tanh", {Param("value", "number")}, "number"),
            Method("DegToRad", {Param("value", "number")}, "number"),
            Method("RadToDeg", {Param("value", "number")}, "number"),

            // --- Interpolation / ranges ---
            Method("Clamp", {Param("value", "number"), Param("minValue", "number"), Param("maxValue", "number")}, "number"),
            Method("Clamp01", {Param("value", "number")}, "number", "Clamps to 0..1."),
            Method("Lerp", {Param("a", "number"), Param("b", "number"), Param("t", "number")}, "number", "Unclamped: t outside 0..1 extrapolates."),
            Method("LerpClamped", {Param("a", "number"), Param("b", "number"), Param("t", "number")}, "number", "Like Lerp, but t is clamped to 0..1."),
            Method("InverseLerp", {Param("a", "number"), Param("b", "number"), Param("value", "number")}, "number", "Where value sits between a and b (0 at a, 1 at b)."),
            Method("Remap", {Param("value", "number"), Param("inMin", "number"), Param("inMax", "number"), Param("outMin", "number"), Param("outMax", "number")}, "number", "Maps value from inMin..inMax to outMin..outMax (unclamped)."),
            Method("SmoothStep", {Param("a", "number"), Param("b", "number"), Param("t", "number")}, "number", "Lerp from a to b with ease-in/ease-out. t is clamped to 0..1."),
            Method("MoveTowards", {Param("current", "number"), Param("target", "number"), Param("maxDelta", "number")}, "number", "Moves current toward target by at most maxDelta, never overshooting."),
            Method("SmoothDamp", {Param("current", "number"), Param("target", "number"), Param("velocity", "number"), Param("smoothTime", "number"), Param("deltaTime", "number")}, "number, number", "Spring-like smoothing. Returns the new value and the new velocity; pass that velocity into the next call. deltaTime defaults to GameTime.deltaTime."),
            Method("Repeat", {Param("value", "number"), Param("length", "number")}, "number", "Wraps value into 0..length."),
            Method("PingPong", {Param("value", "number"), Param("length", "number")}, "number", "Bounces value back and forth between 0 and length."),
            Method("Wrap", {Param("value", "number"), Param("minValue", "number"), Param("maxValue", "number")}, "number", "Wraps value into minValue..maxValue."),
            Method("Snap", {Param("value", "number"), Param("step", "number")}, "number", "Rounds value to the nearest multiple of step."),

            // --- Angles (degrees) ---
            Method("DeltaAngle", {Param("from", "number"), Param("to", "number")}, "number", "Shortest signed difference between two angles in degrees (-180..180)."),
            Method("LerpAngle", {Param("a", "number"), Param("b", "number"), Param("t", "number")}, "number", "Lerp between two angles in degrees, taking the short way around."),
            Method("MoveTowardsAngle", {Param("current", "number"), Param("target", "number"), Param("maxDelta", "number")}, "number", "MoveTowards for angles in degrees, taking the short way around."),
            Method("NormalizeAngle", {Param("degrees", "number")}, "number", "Wraps an angle in degrees into -180..180."),

            // --- Checks ---
            Method("Approximately", {Param("a", "number"), Param("b", "number"), Param("epsilon", "number")}, "boolean", "True if a and b differ by at most epsilon (default Epsilon)."),
            Method("IsNaN", {Param("value", "number")}, "boolean"),
            Method("IsInfinite", {Param("value", "number")}, "boolean"),
            Method("IsFinite", {Param("value", "number")}, "boolean", "False for NaN and infinities."),

            // --- Random ---
            Method("Random", {Param("min", "integer"), Param("max", "integer")}, "integer", "1 arg: [0,max). 2 args: [min,max]."),
            Method("RandomF", {Param("min", "number"), Param("max", "number")}, "number", "0 args: [0,1). 1 arg: [0,max). 2 args: [min,max]."),
            Method("RandomFast", {}, "number", "Cheaper, lower-quality [0,1) random - precomputed table lookup."),
            Method("RandomSeed", {Param("seed", "integer")}, "", "Reseeds Random/RandomF/RandomBool/RandomOn*/RandomInside*. The same seed gives the same sequence."),
            Method("RandomBool", {}, "boolean", "true or false, 50/50."),
            Method("RandomOnUnitCircle", {}, "Vector2", "A random direction of length 1."),
            Method("RandomInsideUnitCircle", {}, "Vector2", "A random point inside a circle of radius 1."),
            Method("RandomOnUnitSphere", {}, "Vector3", "A random 3D direction of length 1."),
            Method("RandomInsideUnitSphere", {}, "Vector3", "A random point inside a sphere of radius 1."),

            // --- Vector2 ---
            Method("Vector2Add", {Param("a", "Vector2"), Param("b", "Vector2")}, "Vector2"),
            Method("Vector2Sub", {Param("a", "Vector2"), Param("b", "Vector2")}, "Vector2"),
            Method("Vector2Mul", {Param("a", "Vector2"), Param("b", "Vector2")}, "Vector2", "Per component."),
            Method("Vector2Div", {Param("a", "Vector2"), Param("b", "Vector2")}, "Vector2", "Per component."),
            Method("Vector2Scale", {Param("v", "Vector2"), Param("scale", "number")}, "Vector2"),
            Method("Vector2Negate", {Param("v", "Vector2")}, "Vector2"),
            Method("Vector2Length", {Param("v", "Vector2")}, "number"),
            Method("Vector2LengthSquared", {Param("v", "Vector2")}, "number"),
            Method("Vector2Normalize", {Param("v", "Vector2")}, "Vector2", "Length-1 copy (a zero vector stays zero)."),
            Method("Vector2Distance", {Param("a", "Vector2"), Param("b", "Vector2")}, "number"),
            Method("Vector2DistanceSquared", {Param("a", "Vector2"), Param("b", "Vector2")}, "number"),
            Method("Vector2Dot", {Param("a", "Vector2"), Param("b", "Vector2")}, "number"),
            Method("Vector2Cross", {Param("a", "Vector2"), Param("b", "Vector2")}, "number", "2D cross product: positive when b is counter-clockwise from a."),
            Method("Vector2Lerp", {Param("a", "Vector2"), Param("b", "Vector2"), Param("t", "number")}, "Vector2"),
            Method("Vector2MoveTowards", {Param("current", "Vector2"), Param("target", "Vector2"), Param("maxDistance", "number")}, "Vector2"),
            Method("Vector2Angle", {Param("from", "Vector2"), Param("to", "Vector2")}, "number", "Unsigned angle between two vectors in degrees (0..180)."),
            Method("Vector2SignedAngle", {Param("from", "Vector2"), Param("to", "Vector2")}, "number", "Signed angle in degrees (-180..180), positive counter-clockwise."),
            Method("Vector2Rotate", {Param("v", "Vector2"), Param("degrees", "number")}, "Vector2", "Rotates counter-clockwise."),
            Method("Vector2FromAngle", {Param("degrees", "number")}, "Vector2", "Unit vector pointing at this angle (0 = +x, 90 = +y)."),
            Method("Vector2ToAngle", {Param("v", "Vector2")}, "number", "The angle of v in degrees (the inverse of Vector2FromAngle)."),
            Method("Vector2Perpendicular", {Param("v", "Vector2")}, "Vector2", "v rotated 90 degrees counter-clockwise."),
            Method("Vector2Reflect", {Param("v", "Vector2"), Param("normal", "Vector2")}, "Vector2", "Bounces v off a surface with this (unit) normal."),
            Method("Vector2Project", {Param("v", "Vector2"), Param("onto", "Vector2")}, "Vector2"),
            Method("Vector2ClampLength", {Param("v", "Vector2"), Param("maxLength", "number")}, "Vector2"),
            Method("Vector2Min", {Param("a", "Vector2"), Param("b", "Vector2")}, "Vector2", "Per-component minimum."),
            Method("Vector2Max", {Param("a", "Vector2"), Param("b", "Vector2")}, "Vector2", "Per-component maximum."),

            // --- Vector3 ---
            Method("Vector3Add", {Param("a", "Vector3"), Param("b", "Vector3")}, "Vector3"),
            Method("Vector3Sub", {Param("a", "Vector3"), Param("b", "Vector3")}, "Vector3"),
            Method("Vector3Mul", {Param("a", "Vector3"), Param("b", "Vector3")}, "Vector3", "Per component."),
            Method("Vector3Div", {Param("a", "Vector3"), Param("b", "Vector3")}, "Vector3", "Per component."),
            Method("Vector3Scale", {Param("v", "Vector3"), Param("scale", "number")}, "Vector3"),
            Method("Vector3Negate", {Param("v", "Vector3")}, "Vector3"),
            Method("Vector3Length", {Param("v", "Vector3")}, "number"),
            Method("Vector3LengthSquared", {Param("v", "Vector3")}, "number"),
            Method("Vector3Normalize", {Param("v", "Vector3")}, "Vector3", "Length-1 copy (a zero vector stays zero)."),
            Method("Vector3Distance", {Param("a", "Vector3"), Param("b", "Vector3")}, "number"),
            Method("Vector3DistanceSquared", {Param("a", "Vector3"), Param("b", "Vector3")}, "number"),
            Method("Vector3Dot", {Param("a", "Vector3"), Param("b", "Vector3")}, "number"),
            Method("Vector3Cross", {Param("a", "Vector3"), Param("b", "Vector3")}, "Vector3"),
            Method("Vector3Lerp", {Param("a", "Vector3"), Param("b", "Vector3"), Param("t", "number")}, "Vector3"),
            Method("Vector3MoveTowards", {Param("current", "Vector3"), Param("target", "Vector3"), Param("maxDistance", "number")}, "Vector3"),
            Method("Vector3Angle", {Param("from", "Vector3"), Param("to", "Vector3")}, "number", "Unsigned angle between two vectors in degrees (0..180)."),
            Method("Vector3Reflect", {Param("v", "Vector3"), Param("normal", "Vector3")}, "Vector3", "Bounces v off a surface with this (unit) normal."),
            Method("Vector3Project", {Param("v", "Vector3"), Param("onto", "Vector3")}, "Vector3"),
            Method("Vector3ProjectOnPlane", {Param("v", "Vector3"), Param("planeNormal", "Vector3")}, "Vector3", "Removes the part of v along planeNormal."),
            Method("Vector3ClampLength", {Param("v", "Vector3"), Param("maxLength", "number")}, "Vector3"),
            Method("Vector3Min", {Param("a", "Vector3"), Param("b", "Vector3")}, "Vector3", "Per-component minimum."),
            Method("Vector3Max", {Param("a", "Vector3"), Param("b", "Vector3")}, "Vector3", "Per-component maximum."),

            // --- Vector4 (colors) ---
            Method("Vector4Add", {Param("a", "Vector4"), Param("b", "Vector4")}, "Vector4"),
            Method("Vector4Sub", {Param("a", "Vector4"), Param("b", "Vector4")}, "Vector4"),
            Method("Vector4Mul", {Param("a", "Vector4"), Param("b", "Vector4")}, "Vector4", "Per component, e.g. tinting a color."),
            Method("Vector4Scale", {Param("v", "Vector4"), Param("scale", "number")}, "Vector4"),
            Method("Vector4Lerp", {Param("a", "Vector4"), Param("b", "Vector4"), Param("t", "number")}, "Vector4", "E.g. fading between two colors."),

            // --- Quaternions (Vector4 x, y, z, w; degrees) ---
            Method("QuaternionFromEuler", {Param("x", "number"), Param("y", "number"), Param("z", "number")}, "Vector4", "Rotation from X/Y/Z angles in degrees, the same angles the inspector shows. Y turns left/right like camera yaw."),
            Method("QuaternionToEuler", {Param("q", "Vector4")}, "Vector3", "X/Y/Z angles in degrees, the same angles the inspector shows. Y stays within -90..90, so Y 135 reads back as (180, 45, 180)."),
            Method("QuaternionAngleAxis", {Param("axis", "Vector3"), Param("degrees", "number")}, "Vector4", "Rotation of degrees around axis. A zero axis gives no rotation."),
            Method("QuaternionMultiply", {Param("a", "Vector4"), Param("b", "Vector4")}, "Vector4", "Combined rotation: b first, then a."),
            Method("QuaternionRotate", {Param("q", "Vector4"), Param("v", "Vector3")}, "Vector3", "Rotates v by q. QuaternionRotate(transform.rotation, Vector3(0, 0, 1)) is the way an entity faces."),
        }));
    }
}

namespace {
    // Whole-number results (Round/Trunc) come back as Lua integers, the way
    // Lua's own math.floor does, so they print as "3" and not "3.0".
    // Out-of-range values, NaN and infinities stay floats.
    sol::object IntegerOrNumber(const sol::this_state state, const double value) {
        if (value >= -9.2233720368547758e18 && value < 9.2233720368547758e18)
            return sol::make_object(state, static_cast<lua_Integer>(value));

        return sol::make_object(state, value);
    }
}

float LuaScriptSystem::SharedRandomUnitFloat() {
    return MathFunctions::RandomUnitFloat();
}

void LuaScriptSystem::RegisterMathBindings(sol::state &lua) {
    RegisterMathMetadata();

    const sol::object existing = lua["mathT"];
    sol::table math;

    if (existing.get_type() == sol::type::table) math = existing.as<sol::table>();
    else {
        if (existing.get_type() != sol::type::nil) spdlog::warn("Replacing Lua global 'mathT' because it is not a table");

        math = lua.create_named_table("mathT");
    }

    // =====================================
    //     Straight from Lua's math library
    // =====================================
    // Aliased rather than reimplemented, so they behave exactly like
    // math.abs/math.floor/... (integers stay integers, math.log's optional
    // base, math.atan's optional x, math.min/max take any number of args).
    // The math library is opened before any binding is registered - see
    // LuaScriptSystem::Initialize.
    const sol::table luaMath = lua["math"];

    math["Abs"] = luaMath.get<sol::object>("abs");
    math["Floor"] = luaMath.get<sol::object>("floor");
    math["Ceil"] = luaMath.get<sol::object>("ceil");
    math["Min"] = luaMath.get<sol::object>("min");
    math["Max"] = luaMath.get<sol::object>("max");
    math["Fmod"] = luaMath.get<sol::object>("fmod");
    math["Sqrt"] = luaMath.get<sol::object>("sqrt");
    math["Exp"] = luaMath.get<sol::object>("exp");
    math["Log"] = luaMath.get<sol::object>("log");
    math["Sin"] = luaMath.get<sol::object>("sin");
    math["Cos"] = luaMath.get<sol::object>("cos");
    math["Tan"] = luaMath.get<sol::object>("tan");
    math["Asin"] = luaMath.get<sol::object>("asin");
    math["Acos"] = luaMath.get<sol::object>("acos");
    math["Atan"] = luaMath.get<sol::object>("atan");

    math["Pi"] = Constants::Pi;
    math["Tau"] = 2.0f * Constants::Pi;
    math["HalfPi"] = .5f * Constants::Pi;
    math["E"] = Constants::Euler;
    math["Sqrt2"] = Constants::Sqrt2;
    math["Infinity"] = std::numeric_limits<double>::infinity();
    math["NegativeInfinity"] = -std::numeric_limits<double>::infinity();
    math["Epsilon"] = static_cast<double>(Constants::Epsilon);
    math["MaxInteger"] = luaMath.get<sol::object>("maxinteger");
    math["MinInteger"] = luaMath.get<sol::object>("mininteger");

    // =====================================
    //               Basic
    // =====================================

    math.set_function("Sign", [](const double value) -> lua_Integer {
        return (value > 0.0) - (value < 0.0);
    });

    math.set_function("Round", sol::overload(
        [](const sol::this_state state, const double value) -> sol::object {
            return IntegerOrNumber(state, std::round(value));
        },
        [](const double value, const int digits) -> double {
            const double scale = std::pow(10.0, digits);
            return std::round(value * scale) / scale;
        }
    ));

    math.set_function("Trunc", [](const sol::this_state state, const double value) -> sol::object {
        return IntegerOrNumber(state, std::trunc(value));
    });

    math.set_function("Frac", [](const double value) -> double {
        return value - std::trunc(value);
    });

    // Floored modulo, like Lua's own % operator: the result takes the
    // divisor's sign, so Mod(-1, 360) is 359 (Fmod would give -1).
    math.set_function("Mod", [](const double value, const double divisor) -> double {
        const double result = std::fmod(value, divisor);
        return (result != 0.0 && (result < 0.0) != (divisor < 0.0)) ? result + divisor : result;
    });

    math.set_function("Pow", [](const double base, const double exponent) -> double {
        return std::pow(base, exponent);
    });

    math.set_function("Log10", [](const double value) -> double {
        return std::log10(value);
    });

    math.set_function("Log2", [](const double value) -> double {
        return std::log2(value);
    });

    math.set_function("Hypot", [](const double x, const double y) -> double {
        return std::hypot(x, y);
    });

    // =====================================
    //             Trigonometry
    // =====================================

    math.set_function("Atan2", [](const double y, const double x) -> double {
        return std::atan2(y, x);
    });

    math.set_function("Sinh", [](const double value) -> double {
        return std::sinh(value);
    });

    math.set_function("Cosh", [](const double value) -> double {
        return std::cosh(value);
    });

    math.set_function("Tanh", [](const double value) -> double {
        return std::tanh(value);
    });

    math.set_function("DegToRad", [](const float value) -> float {
        return value * Constants::DegToRad;
    });

    math.set_function("RadToDeg", [](const float value) -> float {
        return value * Constants::RadToDeg;
    });

    // =====================================
    //        Interpolation / ranges
    // =====================================

    math.set_function("Clamp", [](const float value, const float minValue, const float maxValue) -> float {
        return std::clamp(value, minValue, maxValue);
    });

    math.set_function("Clamp01", [](const double value) -> double {
        return MathFunctions::Clamp01(value);
    });

    math.set_function("Lerp", [](const float a, const float b, const float t) -> float {
        return std::lerp(a, b, t);
    });

    math.set_function("LerpClamped", [](const double a, const double b, const double t) -> double {
        return std::lerp(a, b, MathFunctions::Clamp01(t));
    });

    math.set_function("InverseLerp", [](const float a, const float b, const float t) -> float {
        return MathHelpers::InverseLerp(a, b, t);
    });

    math.set_function("Remap", [](const double value, const double inMin, const double inMax, const double outMin, const double outMax) -> double {
        if (inMin == inMax) return outMin;
        return std::lerp(outMin, outMax, (value - inMin) / (inMax - inMin));
    });

    math.set_function("SmoothStep", [](const double a, const double b, const double t) -> double {
        const double x = MathFunctions::Clamp01(t);
        return std::lerp(a, b, x * x * (3.0 - 2.0 * x));
    });

    math.set_function("MoveTowards", [](const double current, const double target, const double maxDelta) -> double {
        return MathFunctions::MoveTowards(current, target, maxDelta);
    });

    // Unity's Mathf.SmoothDamp (Game Programming Gems 4, 1.10), minus the
    // maxSpeed cap. Lua has no out-parameters, so the updated velocity is
    // the second return value: `x, vel = mathT.SmoothDamp(x, target, vel, 0.3)`.
    const auto smoothDamp = [](const double current, const double target, const double velocity,
                               const double smoothTime, const double deltaTime) -> std::tuple<double, double> {
        if (deltaTime <= 0.0) return {current, velocity};

        const double clampedSmoothTime = std::max(0.0001, smoothTime);
        const double omega = 2.0 / clampedSmoothTime;
        const double x = omega * deltaTime;
        const double decay = 1.0 / (1.0 + x + 0.48 * x * x + 0.235 * x * x * x);

        const double change = current - target;
        const double temp = (velocity + omega * change) * deltaTime;

        double newVelocity = (velocity - omega * temp) * decay;
        double output = target + (change + temp) * decay;

        // Don't overshoot the target.
        if ((target - current > 0.0) == (output > target)) {
            output = target;
            newVelocity = 0.0;
        }

        return {output, newVelocity};
    };

    math.set_function("SmoothDamp", sol::overload(
        [smoothDamp](const double current, const double target, const double velocity, const double smoothTime) {
            return smoothDamp(current, target, velocity, smoothTime, GameTime::deltaTime);
        },
        smoothDamp
    ));

    math.set_function("Repeat", [](const double value, const double length) -> double {
        return MathFunctions::Repeat(value, length);
    });

    math.set_function("PingPong", [](const double value, const double length) -> double {
        const double wrapped = MathFunctions::Repeat(value, length * 2.0);
        return length - std::abs(wrapped - length);
    });

    math.set_function("Wrap", [](const double value, const double minValue, const double maxValue) -> double {
        return minValue + MathFunctions::Repeat(value - minValue, maxValue - minValue);
    });

    math.set_function("Snap", [](const double value, const double step) -> double {
        if (step == 0.0) return value;
        return std::round(value / step) * step;
    });

    // =====================================
    //            Angles (degrees)
    // =====================================

    math.set_function("DeltaAngle", [](const double from, const double to) -> double {
        return MathFunctions::DeltaAngle(from, to);
    });

    math.set_function("LerpAngle", [](const double a, const double b, const double t) -> double {
        return a + MathFunctions::DeltaAngle(a, b) * MathFunctions::Clamp01(t);
    });

    math.set_function("MoveTowardsAngle", [](const double current, const double target, const double maxDelta) -> double {
        const double delta = MathFunctions::DeltaAngle(current, target);
        if (-maxDelta < delta && delta < maxDelta) return target;
        return MathFunctions::MoveTowards(current, current + delta, maxDelta);
    });

    math.set_function("NormalizeAngle", [](const double degrees) -> double {
        return MathFunctions::DeltaAngle(0.0, degrees);
    });

    // =====================================
    //                Checks
    // =====================================

    math.set_function("Approximately", sol::overload(
        [](const double a, const double b) -> bool {
            return std::abs(a - b) <= Constants::Epsilon;
        },
        [](const double a, const double b, const double epsilon) -> bool {
            return std::abs(a - b) <= epsilon;
        }
    ));

    math.set_function("IsNaN", [](const double value) -> bool {
        return std::isnan(value);
    });

    math.set_function("IsInfinite", [](const double value) -> bool {
        return std::isinf(value);
    });

    math.set_function("IsFinite", [](const double value) -> bool {
        return std::isfinite(value);
    });

    // =====================================
    //                Random
    // =====================================

    math.set_function("Random", sol::overload(
        // One arg: int in [0, max).
        [](const int max) -> int {
            if (max <= 0) return 0;
            return MathFunctions::XorShift32() % max;
        },
        // Two args: int min to max
        [](const int min, const int max) -> int {
            if (min >= max) return min;
            return min + (MathFunctions::XorShift32() % (max - min + 1));
        }
    ));

    math.set_function("RandomF", sol::overload(
        []() -> float {
            return MathFunctions::GetRandomFloat(0.0f, 1.0f);
        },

        [](const float max) -> float {
            return MathFunctions::GetRandomFloat(0.0f, max);
        },

        [](const float min, const float max) -> float {
            return MathFunctions::GetRandomFloat(min, max);
        }
    ));

    math.set_function("RandomFast", []() -> float { return MathFunctions::GetRandomFast(); });

    // xorshift32 never leaves state 0, so a seed of 0 is remapped.
    math.set_function("RandomSeed", [](const lua_Integer seed) {
        const auto state = static_cast<uint32_t>(seed);
        MathFunctions::engineSeedState = state == 0 ? 1919u : state;
    });

    math.set_function("RandomBool", []() -> bool {
        return (MathFunctions::XorShift32() & 1u) != 0;
    });

    math.set_function("RandomOnUnitCircle", []() -> Vector2 {
        return MathFunctions::RandomOnUnitCircle();
    });

    // sqrt of a uniform radius keeps the points evenly spread over the area.
    math.set_function("RandomInsideUnitCircle", []() -> Vector2 {
        return MathFunctions::RandomOnUnitCircle() * std::sqrt(MathFunctions::RandomUnitFloat());
    });

    math.set_function("RandomOnUnitSphere", []() -> Vector3 {
        return MathFunctions::RandomOnUnitSphere();
    });

    // cbrt of a uniform radius keeps the points evenly spread over the volume.
    math.set_function("RandomInsideUnitSphere", []() -> Vector3 {
        return MathFunctions::RandomOnUnitSphere() * std::cbrt(MathFunctions::RandomUnitFloat());
    });

    // =====================================
    //            Vector2 Math
    // =====================================

    math.set_function("Vector2Distance", [](const Vector2& a, const Vector2& b) -> float {
        return Vector2Math::Distance(a, b);
    });

    math.set_function("Vector2DistanceSquared", [](const Vector2& a, const Vector2& b) -> float {
        return Vector2Math::DistanceSquared(a, b);
    });

    math.set_function("Vector2Dot", [](const Vector2& a, const Vector2& b) -> float {
       return Vector2Math::Dot(a, b);
    });

    math.set_function("Vector2Add", [](const Vector2& a, const Vector2& b) -> Vector2 {
        return a + b;
   });

    math.set_function("Vector2Sub", [](const Vector2& a, const Vector2& b) -> Vector2 {
        return a - b;
   });

    math.set_function("Vector2Mul", [](const Vector2& a, const Vector2& b) -> Vector2 {
        return a * b;
   });

    math.set_function("Vector2Div", [](const Vector2& a, const Vector2& b) -> Vector2 {
        return a / b;
   });

    math.set_function("Vector2Scale", [](const Vector2& v, const float scale) -> Vector2 {
        return v * scale;
    });

    math.set_function("Vector2Negate", [](const Vector2& v) -> Vector2 {
        return {-v.x, -v.y};
    });

    math.set_function("Vector2Length", [](const Vector2& v) -> float {
        return Vector2Math::Length(v);
    });

    math.set_function("Vector2LengthSquared", [](const Vector2& v) -> float {
        return Vector2Math::LengthSquared(v);
    });

    math.set_function("Vector2Normalize", [](const Vector2& v) -> Vector2 {
        return Vector2Math::Normalized(v);
    });

    math.set_function("Vector2Cross", [](const Vector2& a, const Vector2& b) -> float {
        return a.x * b.y - a.y * b.x;
    });

    math.set_function("Vector2Lerp", [](const Vector2& a, const Vector2& b, const float t) -> Vector2 {
        return {std::lerp(a.x, b.x, t), std::lerp(a.y, b.y, t)};
    });

    math.set_function("Vector2MoveTowards", [](const Vector2& current, const Vector2& target, const float maxDistance) -> Vector2 {
        return Vector2Math::MoveTowards(current, target, maxDistance);
    });

    math.set_function("Vector2Angle", [](const Vector2& from, const Vector2& to) -> float {
        return MathFunctions::AngleBetween(Vector2Math::Dot(from, to), Vector2Math::Length(from) * Vector2Math::Length(to));
    });

    math.set_function("Vector2SignedAngle", [](const Vector2& from, const Vector2& to) -> float {
        return std::atan2(from.x * to.y - from.y * to.x, from.x * to.x + from.y * to.y) * Constants::RadToDeg;
    });

    math.set_function("Vector2Rotate", [](const Vector2& v, const float degrees) -> Vector2 {
        const float radians = degrees * Constants::DegToRad;
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        return {v.x * c - v.y * s, v.x * s + v.y * c};
    });

    math.set_function("Vector2FromAngle", [](const float degrees) -> Vector2 {
        const float radians = degrees * Constants::DegToRad;
        return {std::cos(radians), std::sin(radians)};
    });

    math.set_function("Vector2ToAngle", [](const Vector2& v) -> float {
        return std::atan2(v.y, v.x) * Constants::RadToDeg;
    });

    math.set_function("Vector2Perpendicular", [](const Vector2& v) -> Vector2 {
        return {-v.y, v.x};
    });

    math.set_function("Vector2Reflect", [](const Vector2& v, const Vector2& normal) -> Vector2 {
        const float factor = -2.0f * Vector2Math::Dot(v, normal);
        return {v.x + normal.x * factor, v.y + normal.y * factor};
    });

    math.set_function("Vector2Project", [](const Vector2& v, const Vector2& onto) -> Vector2 {
        return Vector2Math::Project(v, onto);
    });

    math.set_function("Vector2ClampLength", [](const Vector2& v, const float maxLength) -> Vector2 {
        return Vector2Math::ClampLength(v, maxLength);
    });

    math.set_function("Vector2Min", [](const Vector2& a, const Vector2& b) -> Vector2 {
        return {std::min(a.x, b.x), std::min(a.y, b.y)};
    });

    math.set_function("Vector2Max", [](const Vector2& a, const Vector2& b) -> Vector2 {
        return {std::max(a.x, b.x), std::max(a.y, b.y)};
    });

    // =====================================
    //            Vector3 Math
    // =====================================

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

    math.set_function("Vector3Add", [](const Vector3& a, const Vector3& b) -> Vector3 {
        return a + b;
    });

    math.set_function("Vector3Sub", [](const Vector3& a, const Vector3& b) -> Vector3 {
        return a - b;
    });

    math.set_function("Vector3Mul", [](const Vector3& a, const Vector3& b) -> Vector3 {
        return {a.x * b.x, a.y * b.y, a.z * b.z};
    });

    math.set_function("Vector3Div", [](const Vector3& a, const Vector3& b) -> Vector3 {
        return {a.x / b.x, a.y / b.y, a.z / b.z};
   });

    math.set_function("Vector3Scale", [](const Vector3& v, const float scale) -> Vector3 {
        return v * scale;
    });

    math.set_function("Vector3Negate", [](const Vector3& v) -> Vector3 {
        return {-v.x, -v.y, -v.z};
    });

    math.set_function("Vector3Length", [](const Vector3& v) -> float {
        return Vector3Math::Length(v);
    });

    math.set_function("Vector3LengthSquared", [](const Vector3& v) -> float {
        return Vector3Math::LengthSquared(v);
    });

    math.set_function("Vector3Normalize", [](const Vector3& v) -> Vector3 {
        return Vector3Math::Normalized(v);
    });

    math.set_function("Vector3Lerp", [](const Vector3& a, const Vector3& b, const float t) -> Vector3 {
        return {std::lerp(a.x, b.x, t), std::lerp(a.y, b.y, t), std::lerp(a.z, b.z, t)};
    });

    math.set_function("Vector3MoveTowards", [](const Vector3& current, const Vector3& target, const float maxDistance) -> Vector3 {
        return Vector3Math::MoveTowards(current, target, maxDistance);
    });

    math.set_function("Vector3Angle", [](const Vector3& from, const Vector3& to) -> float {
        return MathFunctions::AngleBetween(Vector3Math::Dot(from, to), Vector3Math::Length(from) * Vector3Math::Length(to));
    });

    math.set_function("Vector3Reflect", [](const Vector3& v, const Vector3& normal) -> Vector3 {
        const float factor = -2.0f * Vector3Math::Dot(v, normal);
        return {v.x + normal.x * factor, v.y + normal.y * factor, v.z + normal.z * factor};
    });

    math.set_function("Vector3Project", [](const Vector3& v, const Vector3& onto) -> Vector3 {
        return Vector3Math::Project(v, onto);
    });

    math.set_function("Vector3ProjectOnPlane", [](const Vector3& v, const Vector3& planeNormal) -> Vector3 {
        return v - Vector3Math::Project(v, planeNormal);
    });

    math.set_function("Vector3ClampLength", [](const Vector3& v, const float maxLength) -> Vector3 {
        return Vector3Math::ClampLength(v, maxLength);
    });

    math.set_function("Vector3Min", [](const Vector3& a, const Vector3& b) -> Vector3 {
        return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
    });

    math.set_function("Vector3Max", [](const Vector3& a, const Vector3& b) -> Vector3 {
        return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
    });

    // =====================================
    //         Vector4 Math (colors)
    // =====================================

    math.set_function("Vector4Add", [](const Vector4& a, const Vector4& b) -> Vector4 {
        return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
    });

    math.set_function("Vector4Sub", [](const Vector4& a, const Vector4& b) -> Vector4 {
        return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w};
    });

    math.set_function("Vector4Mul", [](const Vector4& a, const Vector4& b) -> Vector4 {
        return {a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w};
    });

    math.set_function("Vector4Scale", [](const Vector4& v, const float scale) -> Vector4 {
        return {v.x * scale, v.y * scale, v.z * scale, v.w * scale};
    });

    math.set_function("Vector4Lerp", [](const Vector4& a, const Vector4& b, const float t) -> Vector4 {
        return {std::lerp(a.x, b.x, t), std::lerp(a.y, b.y, t), std::lerp(a.z, b.z, t), std::lerp(a.w, b.w, t)};
    });

    // =====================================
    //         Quaternion Math
    // =====================================
    // Quaternions are Vector4 (x, y, z, w) in Lua, like transform.rotation.

    const auto toQuaternion = [](const Vector4& q) { return Quaternion{q.x, q.y, q.z, q.w}; };
    const auto toVector4 = [](const Quaternion& q) { return Vector4{q.x, q.y, q.z, q.w}; };

    math.set_function("QuaternionFromEuler", [toVector4](const float x, const float y, const float z) -> Vector4 {
        return toVector4(Quaternion::FromEulerDegrees(x, y, z));
    });

    math.set_function("QuaternionToEuler", [toQuaternion](const Vector4& q) -> Vector3 {
        return toQuaternion(q).ToEulerDegrees();
    });

    math.set_function("QuaternionAngleAxis", [toVector4](const Vector3& axis, const float degrees) -> Vector4 {
        return toVector4(Quaternion::FromAxisAngle(axis.x, axis.y, axis.z, degrees * Constants::DegToRad));
    });

    math.set_function("QuaternionMultiply", [toQuaternion, toVector4](const Vector4& a, const Vector4& b) -> Vector4 {
        return toVector4((toQuaternion(a) * toQuaternion(b)).Normalized());
    });

    math.set_function("QuaternionRotate", [toQuaternion](const Vector4& q, const Vector3& v) -> Vector3 {
        return QuaternionMath::Rotate(toQuaternion(q), v);
    });
}
