
// Created by berke on 6/20/2026.
//
#include "Headers/Engine/GameTime.hpp"
#include "Headers/Math/MathHelpers.hpp"
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
        }));
    }
}

namespace {
    //todo TILKYTODO make this an engine setting
    uint32_t engineSeedState = 1919;

    constexpr int RANDOM_NUMBER_SIZE = 512;

    float randomNumbers[512] = {
    0.825335503f, 0.130333558f, 0.295177311f, 0.999960482f, 0.343606323f, 0.738692164f, 0.738617837f, 0.452012748f,
    0.462683946f, 0.702471316f, 0.365370125f, 0.406945139f, 0.462080747f, 0.0514371432f, 0.633266330f, 0.355732292f,
    0.505068243f, 0.511051595f, 0.523521245f, 0.449080378f, 0.125626817f, 0.906294107f, 0.406431943f, 0.453932136f,
    0.975711584f, 0.524295807f, 0.715656221f, 0.917119920f, 0.626195192f, 0.0593863763f, 0.360939890f, 0.0720367506f,
    0.952561200f, 0.211967066f, 0.705081701f, 0.388566583f, 0.207947388f, 0.965902507f, 0.882275462f, 0.460814446f,
    0.981547415f, 0.228063121f, 0.580877006f, 0.434842139f, 0.637648582f, 0.153643742f, 0.653565228f, 0.419172913f,
    0.312843233f, 0.0834636167f, 0.577760160f, 0.575340986f, 0.938641608f, 0.298806459f, 0.0515804328f, 0.251074165f,
    0.582869947f, 0.376759797f, 0.350585610f, 0.831893146f, 0.0230858941f, 0.965388179f, 0.450242192f, 0.826107025f,
    0.622667134f, 0.689403236f, 0.829832554f, 0.692071855f, 0.488520771f, 0.395281643f, 0.593978167f, 0.485465437f,
    0.0241045970f, 0.113047071f, 0.291406780f, 0.563212156f, 0.0991553143f, 0.499352068f, 0.419116527f, 0.248181537f,
    0.533262789f, 0.681655407f, 0.262781709f, 0.533470035f, 0.264020771f, 0.273760647f, 0.676240146f, 0.855009377f,
    0.873266578f, 0.179532662f, 0.480607420f, 0.579483032f, 0.231624022f, 0.201245919f, 0.482006401f, 1.0f,
    0.813777149f, 0.768652439f, 0.0230923314f, 0.630888581f, 0.477597743f, 0.975214183f, 0.256898791f, 0.841738105f,
    0.470818251f, 0.966209412f, 0.788204372f, 0.737799048f, 0.286642581f, 0.547291815f, 0.373925835f, 0.232385829f,
    0.248395279f, 0.220560804f, 0.861585021f, 0.104125507f, 0.791931629f, 0.0281287450f, 0.128431335f, 0.639413297f,
    0.484554201f, 0.269819885f, 0.666808665f, 0.0487994589f, 0.392667145f, 0.764671504f, 0.442901462f, 0.278261572f,
    0.593960583f, 0.461501628f, 0.751586020f, 0.0363633670f, 0.371549398f, 0.852496803f, 0.335783273f, 0.143807307f,
    0.315433294f, 0.0255990643f, 0.206218138f, 0.330893964f, 0.504383445f, 0.146494105f, 0.332287699f, 0.674719930f,
    0.741295636f, 0.577325165f, 0.761671543f, 0.922384679f, 0.599585056f, 0.796350598f, 0.659807026f, 0.452784449f,
    0.179531828f, 0.305324703f, 0.723970532f, 0.909705997f, 0.498676687f, 0.169245914f, 0.817326009f, 0.502256274f,
    0.686502337f, 0.108431049f, 0.996950030f, 0.953365088f, 0.862582207f, 0.262576431f, 0.660940945f, 0.410384327f,
    0.402291149f, 0.761469245f, 0.537768722f, 0.793656409f, 0.991593182f, 0.378550261f, 0.0392380990f, 0.896948695f,
    0.943401992f, 0.0259051938f, 0.00680160569f, 0.105428524f, 0.960773110f, 0.360867769f, 0.959622383f, 0.421007961f,
    0.742933393f, 0.771361351f, 0.391359597f, 0.493826061f, 0.278158695f, 0.988161206f, 0.893657684f, 0.289473444f,
    0.0285632033f, 0.911365211f, 0.408685058f, 0.318299145f, 0.591409087f, 0.491802722f, 0.008194637f, 0.780611634f,
    0.525994301f, 0.183345392f, 0.878635705f, 0.339527994f, 0.840965927f, 0.699486792f, 0.715403736f, 0.406173199f,
    0.995737851f, 0.379615694f, 0.639658868f, 0.381478637f, 0.951978981f, 0.437197953f, 0.900301337f, 0.249959841f,
    0.924658000f, 0.230807021f, 0.884059489f, 0.438911766f, 0.193736389f, 0.639206052f, 0.296271533f, 0.0881601050f,
    0.221198514f, 0.770250380f, 0.979959130f, 0.369678348f, 0.144767359f, 0.188892439f, 0.695650756f, 0.0194710512f,
    0.176133350f, 0.509971917f, 0.855514824f, 0.397875220f, 0.650363743f, 0.701991141f, 0.607803881f, 0.716216683f,
    0.622514069f, 0.895976663f, 0.242102697f, 0.841625690f, 0.000482913f, 0.414966792f, 0.730115712f, 0.505195796f,
    0.0399348177f, 0.0708577111f, 0.559444129f, 0.766557276f, 0.809833765f, 0.665321708f, 0.971753836f, 0.190335587f,
    0.187435582f, 0.699498892f, 0.0726034716f, 0.821867764f, 0.698290884f, 0.274862140f, 0.961115420f, 0.199697688f,
    0.216595665f, 0.144027010f, 0.0166205186f, 0.0199706573f, 0.727531016f, 0.967706263f, 0.163795784f, 0.150772765f,
    0.272355884f, 0.460686475f, 0.205446556f, 0.630770743f, 0.153228000f, 0.393156499f, 0.960346878f, 0.105827637f,
    0.384213895f, 0.928276241f, 0.0565854944f, 0.961271465f, 0.497379035f, 0.482053727f, 0.0384992398f, 0.948134720f,
    0.629792690f, 0.864034951f, 0.315123290f, 0.863522947f, 0.0f, 0.0230872054f, 0.998913944f, 0.0416649617f,
    0.924396098f, 0.408145696f, 0.920985579f, 0.843862176f, 0.313853055f, 0.107098408f, 0.875609219f, 0.701525092f,
    0.913026631f, 0.118314929f, 0.768929362f, 0.224778250f, 0.607487261f, 0.371428341f, 0.787511289f, 0.492299289f,
    0.725516796f, 0.375905961f, 0.367158204f, 0.157746270f, 0.649548233f, 0.903011143f, 0.386597723f, 0.905519962f,
    0.464483589f, 0.187226072f, 0.900912881f, 0.764767468f, 0.383703738f, 0.261073679f, 0.681044340f, 0.143500164f,
    0.462372929f, 0.543873250f, 0.790461361f, 0.189155951f, 0.0312256236f, 0.609685063f, 0.739791334f, 0.746801376f,
    0.377979308f, 0.965691924f, 0.416977376f, 0.856219590f, 0.0382043757f, 0.513674974f, 0.249285117f, 0.812469602f,
    0.988014281f, 0.172203317f, 0.906933904f, 0.0717152432f, 0.179033414f, 0.805551112f, 0.856522501f, 0.833773077f,
    0.121481605f, 0.250527114f, 0.187464550f, 0.532282114f, 0.179525331f, 0.962641239f, 0.948734283f, 0.707434595f,
    0.887826085f, 0.650158942f, 0.0476511158f, 0.0122738490f, 0.386901289f, 0.869514108f, 0.378709108f, 0.518278658f,
    0.381781429f, 0.867012084f, 0.652441144f, 0.315465838f, 0.780365467f, 0.808589756f, 0.500689089f, 0.866367400f,
    0.616758108f, 0.794062734f, 0.548663735f, 0.210980609f, 0.328850120f, 0.0898246244f, 0.801181197f, 0.301806718f,
    0.0442512669f, 0.872873187f, 0.941289961f, 0.786167622f, 0.500154495f, 0.172527924f, 0.196876362f, 0.413053423f,
    0.677251458f, 0.472649008f, 0.195222989f, 0.227952257f, 0.338257879f, 0.394439548f, 0.731085181f, 0.162299946f,
    0.213694587f, 0.214932159f, 0.917613268f, 0.0132906446f, 0.334493071f, 0.0477376021f, 0.788246453f, 0.621095657f,
    0.164869204f, 0.508961797f, 0.241297439f, 0.415413111f, 0.105393536f, 0.591106176f, 0.347385436f, 0.468676120f,
    0.702464461f, 0.741809309f, 0.346474141f, 0.172132626f, 0.702777207f, 0.0231727976f, 0.270514458f, 0.446189195f,
    0.233090475f, 0.120363720f, 0.587531090f, 0.770114303f, 0.966958940f, 0.814533114f, 0.965396762f, 0.352660686f,
    0.0265955944f, 0.845921695f, 0.371949404f, 0.0887285545f, 0.0224098582f, 0.219760075f, 0.339300364f, 0.175203517f,
    0.857928514f, 0.495910138f, 0.598222375f, 0.353224605f, 0.582929850f, 0.714924812f, 0.573652864f, 0.445321828f,
    0.737963438f, 0.984755814f, 0.591749132f, 0.707469702f, 0.00000488758133f, 0.777565539f, 0.777129948f, 0.0122999558f,
    0.669553697f, 0.0546674766f, 0.996778667f, 0.903463721f, 0.686736524f, 0.210735932f, 0.003706251f, 0.723737001f,
    0.0170834679f, 0.405270487f, 0.972504616f, 0.614262700f, 0.883938193f, 0.206411913f, 0.958607972f, 0.679018259f,
    0.000031741f, 0.836185873f, 0.126010731f, 0.0977380946f, 0.0456764735f, 0.963354051f, 0.970063269f, 0.974347770f,
    0.345228106f, 0.992286205f, 0.525453746f, 0.465983182f, 0.903494000f, 0.460812181f, 0.229365721f, 0.833280027f,
    0.992330372f, 0.565054059f, 0.836961210f, 0.406006485f, 0.744421482f, 0.596777439f, 0.926744044f, 0.475012511f,
    0.149508074f, 0.131423250f, 0.697178960f, 0.615592480f, 0.750541747f, 0.953704298f, 0.796669066f, 0.686065137f,
    0.423552245f, 0.233667091f, 0.839643717f, 0.114712603f, 0.175167874f, 0.0752579048f, 0.249539509f, 0.744677246f
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

        // Post-increment-then-wrap, not pre-increment: the old
        // `randomNumbers[++currentIndex]` skipped index 0 entirely and, once
        // currentIndex reached RANDOM_NUMBER_SIZE - 1 (511, still < 512 so the
        // reset check above it never fired), read randomNumbers[512] - one
        // past the end of a 512-element array.
        const float value = randomNumbers[currentIndex];
        currentIndex = (currentIndex + 1) % RANDOM_NUMBER_SIZE;
        return value;
    }

    // Whole-number results (Round/Trunc) come back as Lua integers, the way
    // Lua's own math.floor does, so they print as "3" and not "3.0".
    // Out-of-range values, NaN and infinities stay floats.
    sol::object IntegerOrNumber(const sol::this_state state, const double value) {
        if (value >= -9.2233720368547758e18 && value < 9.2233720368547758e18)
            return sol::make_object(state, static_cast<lua_Integer>(value));

        return sol::make_object(state, value);
    }

    double Clamp01(const double value) {
        return std::clamp(value, 0.0, 1.0);
    }

    // Wraps value into [0, length). Same as Unity's Mathf.Repeat.
    double Repeat(const double value, const double length) {
        if (length <= 0.0) return 0.0;
        return std::clamp(value - std::floor(value / length) * length, 0.0, length);
    }

    // Shortest signed difference from `from` to `to`, in degrees (-180..180].
    double DeltaAngle(const double from, const double to) {
        double delta = Repeat(to - from, 360.0);
        if (delta > 180.0) delta -= 360.0;
        return delta;
    }

    double MoveTowards(const double current, const double target, const double maxDelta) {
        if (std::abs(target - current) <= maxDelta) return target;
        return current + (target > current ? maxDelta : -maxDelta);
    }

    float RandomUnitFloat() {
        return GetRandomFloat(0.0f, 1.0f);
    }

    Vector2 RandomOnUnitCircle() {
        const float angle = RandomUnitFloat() * Constants::TwoPi;
        return {std::cos(angle), std::sin(angle)};
    }

    Vector3 RandomOnUnitSphere() {
        // Uniform on the sphere: uniform z in [-1, 1] and a uniform angle.
        const float z = GetRandomFloat(-1.0f, 1.0f);
        const float angle = RandomUnitFloat() * Constants::TwoPi;
        const float ring = std::sqrt(std::max(0.0f, 1.0f - z * z));
        return {ring * std::cos(angle), ring * std::sin(angle), z};
    }

    // Vector helpers below are written per component so they don't depend
    // on which operators each Vector struct defines in C++.

    Vector2 Scale2(const Vector2& v, const float s) { return {v.x * s, v.y * s}; }
    Vector3 Scale3(const Vector3& v, const float s) { return {v.x * s, v.y * s, v.z * s}; }

    Vector2 MoveTowards2(const Vector2& current, const Vector2& target, const float maxDistance) {
        const Vector2 delta{target.x - current.x, target.y - current.y};
        const float distance = Vector2Math::Length(delta);
        if (distance <= maxDistance || distance == 0.0f) return target;
        return {current.x + delta.x / distance * maxDistance, current.y + delta.y / distance * maxDistance};
    }

    Vector3 MoveTowards3(const Vector3& current, const Vector3& target, const float maxDistance) {
        const Vector3 delta{target.x - current.x, target.y - current.y, target.z - current.z};
        const float distance = Vector3Math::Length(delta);
        if (distance <= maxDistance || distance == 0.0f) return target;
        const float step = maxDistance / distance;
        return {current.x + delta.x * step, current.y + delta.y * step, current.z + delta.z * step};
    }

    // Unsigned angle in degrees from a dot product and the product of the
    // two lengths; 0 if either vector is zero.
    float AngleBetween(const float dot, const float lengthProduct) {
        if (lengthProduct == 0.0f) return 0.0f;
        return std::acos(std::clamp(dot / lengthProduct, -1.0f, 1.0f)) * Constants::RadToDeg;
    }

    Vector2 ClampLength2(const Vector2& v, const float maxLength) {
        const float length = Vector2Math::Length(v);
        if (length <= maxLength || length == 0.0f) return v;
        return Scale2(v, maxLength / length);
    }

    Vector3 ClampLength3(const Vector3& v, const float maxLength) {
        const float length = Vector3Math::Length(v);
        if (length <= maxLength || length == 0.0f) return v;
        return Scale3(v, maxLength / length);
    }

    Vector2 Project2(const Vector2& v, const Vector2& onto) {
        const float lengthSquared = Vector2Math::LengthSquared(onto);
        if (lengthSquared == 0.0f) return {};
        return Scale2(onto, Vector2Math::Dot(v, onto) / lengthSquared);
    }

    Vector3 Project3(const Vector3& v, const Vector3& onto) {
        const float lengthSquared = Vector3Math::LengthSquared(onto);
        if (lengthSquared == 0.0f) return {};
        return Scale3(onto, Vector3Math::Dot(v, onto) / lengthSquared);
    }
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

    math["Pi"] = std::numbers::pi;
    math["Tau"] = 2.0 * std::numbers::pi;
    math["HalfPi"] = std::numbers::pi / 2.0;
    math["E"] = std::numbers::e;
    math["Sqrt2"] = std::numbers::sqrt2;
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
        return Clamp01(value);
    });

    math.set_function("Lerp", [](const float a, const float b, const float t) -> float {
        return std::lerp(a, b, t);
    });

    math.set_function("LerpClamped", [](const double a, const double b, const double t) -> double {
        return std::lerp(a, b, Clamp01(t));
    });

    math.set_function("InverseLerp", [](const float a, const float b, const float t) -> float {
        return MathHelpers::InverseLerp(a, b, t);
    });

    math.set_function("Remap", [](const double value, const double inMin, const double inMax, const double outMin, const double outMax) -> double {
        if (inMin == inMax) return outMin;
        return std::lerp(outMin, outMax, (value - inMin) / (inMax - inMin));
    });

    math.set_function("SmoothStep", [](const double a, const double b, const double t) -> double {
        const double x = Clamp01(t);
        return std::lerp(a, b, x * x * (3.0 - 2.0 * x));
    });

    math.set_function("MoveTowards", [](const double current, const double target, const double maxDelta) -> double {
        return MoveTowards(current, target, maxDelta);
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
        return Repeat(value, length);
    });

    math.set_function("PingPong", [](const double value, const double length) -> double {
        const double wrapped = Repeat(value, length * 2.0);
        return length - std::abs(wrapped - length);
    });

    math.set_function("Wrap", [](const double value, const double minValue, const double maxValue) -> double {
        return minValue + Repeat(value - minValue, maxValue - minValue);
    });

    math.set_function("Snap", [](const double value, const double step) -> double {
        if (step == 0.0) return value;
        return std::round(value / step) * step;
    });

    // =====================================
    //            Angles (degrees)
    // =====================================

    math.set_function("DeltaAngle", [](const double from, const double to) -> double {
        return DeltaAngle(from, to);
    });

    math.set_function("LerpAngle", [](const double a, const double b, const double t) -> double {
        return a + DeltaAngle(a, b) * Clamp01(t);
    });

    math.set_function("MoveTowardsAngle", [](const double current, const double target, const double maxDelta) -> double {
        const double delta = DeltaAngle(current, target);
        if (-maxDelta < delta && delta < maxDelta) return target;
        return MoveTowards(current, current + delta, maxDelta);
    });

    math.set_function("NormalizeAngle", [](const double degrees) -> double {
        return DeltaAngle(0.0, degrees);
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

    // xorshift32 never leaves state 0, so a seed of 0 is remapped.
    math.set_function("RandomSeed", [](const lua_Integer seed) {
        const auto state = static_cast<uint32_t>(seed);
        engineSeedState = state == 0 ? 1919u : state;
    });

    math.set_function("RandomBool", []() -> bool {
        return (XorShift32() & 1u) != 0;
    });

    math.set_function("RandomOnUnitCircle", []() -> Vector2 {
        return RandomOnUnitCircle();
    });

    // sqrt of a uniform radius keeps the points evenly spread over the area.
    math.set_function("RandomInsideUnitCircle", []() -> Vector2 {
        return Scale2(RandomOnUnitCircle(), std::sqrt(RandomUnitFloat()));
    });

    math.set_function("RandomOnUnitSphere", []() -> Vector3 {
        return RandomOnUnitSphere();
    });

    // cbrt of a uniform radius keeps the points evenly spread over the volume.
    math.set_function("RandomInsideUnitSphere", []() -> Vector3 {
        return Scale3(RandomOnUnitSphere(), std::cbrt(RandomUnitFloat()));
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
        return Scale2(v, scale);
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
        return MoveTowards2(current, target, maxDistance);
    });

    math.set_function("Vector2Angle", [](const Vector2& from, const Vector2& to) -> float {
        return AngleBetween(Vector2Math::Dot(from, to), Vector2Math::Length(from) * Vector2Math::Length(to));
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
        return Project2(v, onto);
    });

    math.set_function("Vector2ClampLength", [](const Vector2& v, const float maxLength) -> Vector2 {
        return ClampLength2(v, maxLength);
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
        return Scale3(v, scale);
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
        return MoveTowards3(current, target, maxDistance);
    });

    math.set_function("Vector3Angle", [](const Vector3& from, const Vector3& to) -> float {
        return AngleBetween(Vector3Math::Dot(from, to), Vector3Math::Length(from) * Vector3Math::Length(to));
    });

    math.set_function("Vector3Reflect", [](const Vector3& v, const Vector3& normal) -> Vector3 {
        const float factor = -2.0f * Vector3Math::Dot(v, normal);
        return {v.x + normal.x * factor, v.y + normal.y * factor, v.z + normal.z * factor};
    });

    math.set_function("Vector3Project", [](const Vector3& v, const Vector3& onto) -> Vector3 {
        return Project3(v, onto);
    });

    math.set_function("Vector3ProjectOnPlane", [](const Vector3& v, const Vector3& planeNormal) -> Vector3 {
        const Vector3 along = Project3(v, planeNormal);
        return {v.x - along.x, v.y - along.y, v.z - along.z};
    });

    math.set_function("Vector3ClampLength", [](const Vector3& v, const float maxLength) -> Vector3 {
        return ClampLength3(v, maxLength);
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
}
