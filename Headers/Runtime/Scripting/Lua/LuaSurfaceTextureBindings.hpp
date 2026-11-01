#ifndef TILKY_ENGINE_LUA_SURFACE_TEXTURE_BINDINGS_HPP
#define TILKY_ENGINE_LUA_SURFACE_TEXTURE_BINDINGS_HPP

#include <string>
#include <vector>

#include "Headers/Objects/LuaWrappers.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaBindingMetadata.hpp"
#include "sol/sol.hpp"

// The frame/animation/scroll members every textured surface gets in Lua:
// wall Top/Bottom/Middle and SectorFloor Floor/Ceiling. `lower`/`upper`
// name the surface ("top"/"Top"), so a wall gets topAnimation,
// AddTopFrame, ... and a sector floor floorAnimation, AddFloorFrame, ...
namespace LuaSurfaceTextureBindings {
    inline void AppendDocs(std::vector<LuaBindingMetadata::PropertyDoc>& properties,
                           std::vector<LuaBindingMetadata::MethodDoc>& methods,
                           const std::string& lower, const std::string& upper, const std::string& what) {
        using namespace LuaBindingMetadata;

        properties.push_back(Prop(lower + "Animation", "TextureAnimation", false,
                                  "How the " + what + "'s frames play: TextureAnimation.Loop, PingPong or Once."));
        properties.push_back(Prop(lower + "ScrollSpeed", "Vector2", false,
                                  "The " + what + "'s Offset Per Second, in map units (scaled game time). Each frame has its own: reading gives the showing frame's, writing sets every frame's."));

        methods.push_back(Method("Get" + upper + "Frames", {}, "table",
                                 "The " + what + "'s frames as a list of {texture, duration, offset, scale, flipX, flipY, scrollSpeed}."));
        methods.push_back(Method("Set" + upper + "Frames", {Param("frames", "table")}, {},
                                 "Replaces the " + what + "'s frames with a list of {texture = \"...\", duration, offset, scale, flipX, flipY, scrollSpeed} (only texture required; a key left out keeps the old frame's value) or texture paths, and restarts it from frame 1."));
        methods.push_back(Method("Add" + upper + "Frame", {Param("texture", "string"), Param("duration", "number?")}, {},
                                 "Adds a frame at the end of the " + what + "'s animation, placed like the last frame. Duration in seconds, default 0.5."));
        methods.push_back(Method("Clear" + upper + "Frames", {}, {},
                                 "Removes every frame: the " + what + " is untextured. Same as Clear" + upper + "Texture."));
        methods.push_back(Method("Restart" + upper + "Animation", {}, {},
                                 "Jumps the " + what + " back to frame 1. The scroll carries on."));
    }

    // `Access` provides Read(wrapper) -> const SurfaceTexture& and
    // Write(wrapper, fn), where fn takes a SurfaceTexture&.
    template<typename Wrapper, typename Access>
    void Bind(sol::usertype<Wrapper>& type, const std::string& lower, const std::string& upper) {
        type[lower + "Animation"] = sol::property(
            [](const Wrapper& wrapper) { return static_cast<int>(Access::Read(wrapper).animation); },
            [](const Wrapper& wrapper, const int value) {
                const TextureAnimationMode mode = LuaSurfaceTextures::ToMode(value);
                Access::Write(wrapper, [mode](SurfaceTexture& texture) { texture.animation = mode; });
            });

        type[lower + "ScrollSpeed"] = sol::property(
            [](const Wrapper& wrapper) { return Access::Read(wrapper).Shown().offsetPerSecond; },
            [](const Wrapper& wrapper, const Vector2& value) {
                Access::Write(wrapper, [&value](SurfaceTexture& texture) {
                    texture.ForEachFrame([&value](TextureFrame& frame) { frame.offsetPerSecond = value; });
                });
            });

        type["Get" + upper + "Frames"] = [](const Wrapper& wrapper, const sol::this_state state) {
            return LuaSurfaceTextures::GetFrames(Access::Read(wrapper), state);
        };

        type["Set" + upper + "Frames"] = [](const Wrapper& wrapper, const sol::table& frames) {
            // Read first, so a bad list leaves the surface as it was.
            std::vector<TextureFrame> read = LuaSurfaceTextures::ReadFrames(frames, Access::Read(wrapper));
            Access::Write(wrapper, [&read](SurfaceTexture& texture) {
                texture.frames = std::move(read);
                texture.Restart();
            });
        };

        type["Add" + upper + "Frame"] = [](const Wrapper& wrapper, const std::string& texture, const sol::optional<float> duration) {
            Access::Write(wrapper, [&](SurfaceTexture& surface) { LuaSurfaceTextures::AddFrame(surface, texture, duration); });
        };

        type["Clear" + upper + "Frames"] = [](const Wrapper& wrapper) {
            Access::Write(wrapper, [](SurfaceTexture& texture) { texture.Clear(); });
        };

        type["Restart" + upper + "Animation"] = [](const Wrapper& wrapper) {
            Access::Write(wrapper, [](SurfaceTexture& texture) { texture.Restart(); });
        };
    }
}

#endif //TILKY_ENGINE_LUA_SURFACE_TEXTURE_BINDINGS_HPP
