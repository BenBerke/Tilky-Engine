
// Created by berke on 6/20/2026.
//
#include "Headers/Engine/GameTime.hpp"
#include "Headers/Math/MathHelpers.hpp"
#include "../../../../Headers/Runtime/Scripting/Lua/LuaScripting.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaBindingMetadata.hpp"
#include "sol/sol.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>

#include <spdlog/spdlog.h>

namespace {
    using namespace LuaBindingMetadata;

    void RegisterMathMetadata() {
        RegisterType(Type("mathT", "Global math helper table (degrees/radians, clamp, lerp, inverselerp, vector math, random).", {}, {
            Method("DegToRad", {Param("value", "number")}, "number"),
            Method("RadToDeg", {Param("value", "number")}, "number"),
            Method("Clamp", {Param("value", "number"), Param("minValue", "number"), Param("maxValue", "number")}, "number"),
            Method("Lerp", {Param("a", "number"), Param("b", "number"), Param("t", "number")}, "number"),
            Method("InverseLerp", {Param("a", "number"), Param("b", "number"), Param("t", "number")}, "number"),
            Method("Vector2Distance", {Param("a", "Vector2"), Param("b", "Vector2")}, "number"),
            Method("Vector2DistanceSquared", {Param("a", "Vector2"), Param("b", "Vector2")}, "number"),
            Method("Vector2Dot", {Param("a", "Vector2"), Param("b", "Vector2")}, "number"),
            Method("Vector3Dot", {Param("a", "Vector3"), Param("b", "Vector3")}, "number"),
            Method("Vector3Distance", {Param("a", "Vector3"), Param("b", "Vector3")}, "number"),
            Method("Vector3DistanceSquared", {Param("a", "Vector3"), Param("b", "Vector3")}, "number"),
            Method("Vector3Cross", {Param("a", "Vector3"), Param("b", "Vector3")}, "Vector3"),
            Method("Random", {Param("min", "integer"), Param("max", "integer")}, "integer", "1 arg: [0,max). 2 args: [min,max]."),
            Method("RandomF", {Param("min", "number"), Param("max", "number")}, "number", "0 args: [0,1). 1 arg: [0,max). 2 args: [min,max]."),
            Method("RandomFast", {}, "number", "Cheaper, lower-quality [0,1) random - precomputed table lookup."),
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

    math.set_function("InverseLerp", [](const float a, const float b, const float t) -> float {
        return MathHelpers::InverseLerp(a, b, t);
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

    // =====================================
    //            Vector Math
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

    math.set_function("Vector3Div", [](const Vector3& a, const Vector3& b) -> Vector3 {
        return a / b;
   });
}