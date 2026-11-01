#ifndef TILKY_ENGINE_SURFACE_TEXTURE_HPP
#define TILKY_ENGINE_SURFACE_TEXTURE_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../Math/Vector/Vector2.hpp"

// How a surface's frames play once the last one is reached.
enum class TextureAnimationMode : int {
    Loop = 0,     // 1 2 3 1 2 3 ...
    PingPong = 1, // 1 2 3 2 1 2 ... (the end frames are not shown twice)
    Once = 2,     // 1 2 3, then stays on 3
};

struct TextureAnimationModeInfo {
    TextureAnimationMode mode;
    const char* name;     // level files and the Lua TextureAnimation table
    const char* labelKey; // editor label (localisation key)
    const char* tooltipKey;
};

inline constexpr std::array TEXTURE_ANIMATION_MODES = {
    TextureAnimationModeInfo{TextureAnimationMode::Loop, "Loop", "texture.animation.loop", "editor.tooltip.texture.animation.loop"},
    TextureAnimationModeInfo{TextureAnimationMode::PingPong, "PingPong", "texture.animation.ping_pong", "editor.tooltip.texture.animation.ping_pong"},
    TextureAnimationModeInfo{TextureAnimationMode::Once, "Once", "texture.animation.once", "editor.tooltip.texture.animation.once"},
};

inline const TextureAnimationModeInfo* FindTextureAnimationMode(const TextureAnimationMode mode) {
    for (const TextureAnimationModeInfo& info : TEXTURE_ANIMATION_MODES) if (info.mode == mode) return &info;
    return nullptr;
}

inline std::optional<TextureAnimationMode> FindTextureAnimationModeByName(const std::string_view name) {
    for (const TextureAnimationModeInfo& info : TEXTURE_ANIMATION_MODES) if (name == info.name) return info.mode;
    return std::nullopt;
}

inline std::optional<TextureAnimationMode> TextureAnimationModeFromInt(const int value) {
    for (const TextureAnimationModeInfo& info : TEXTURE_ANIMATION_MODES) if (static_cast<int>(info.mode) == value) return info.mode;
    return std::nullopt;
}

// One step of a surface animation: show `texture` for `duration` seconds.
struct TextureFrame {
    std::string texture;
    float duration = 0.5f;

    bool operator==(const TextureFrame&) const = default;
};

// What a wall piece, floor or ceiling shows: one or more frames played in
// order, plus a scroll that moves the texture by `offsetPerSecond` map
// units every second. One frame is a plain, still texture. No frames (or
// only empty ones) means untextured.
//
// `frameTime` and the scroll are runtime state: never saved, ignored by ==,
// advanced by Level::UpdateSurfaceAnimations during Play and in the 3D
// editor view. Everything that only cares about "the" texture (the 2D
// canvas, tiling, stair building, same-texture selection) uses the first
// frame; the renderer and Lua's xTexture getters use the current one.
struct SurfaceTexture {
    std::vector<TextureFrame> frames;
    TextureAnimationMode animation = TextureAnimationMode::Loop;
    Vector2 offsetPerSecond = {0.0f, 0.0f};

    // Doubles, so hours of scrolling don't wear the precision down; the
    // renderer wraps the scroll by the texture's tile size before the GPU.
    double frameTime = 0.0;
    double scrollX = 0.0;
    double scrollY = 0.0;

    // A new frame shows for at least this long, so a zero duration can't
    // make a frame vanish or the loop spin forever.
    static constexpr float MIN_FRAME_DURATION = 0.001f;
    static constexpr float DEFAULT_FRAME_DURATION = 0.5f;

    bool operator==(const SurfaceTexture& other) const {
        return frames == other.frames &&
               animation == other.animation &&
               offsetPerSecond.x == other.offsetPerSecond.x &&
               offsetPerSecond.y == other.offsetPerSecond.y;
    }

    // The first frame's texture: what the surface "is" outside Play.
    [[nodiscard]] const std::string& First() const {
        static const std::string NONE;
        return frames.empty() ? NONE : frames.front().texture;
    }

    // A still surface showing `texture` (none if it is empty).
    [[nodiscard]] static SurfaceTexture Single(std::string texture) {
        SurfaceTexture result;
        result.Set(std::move(texture));
        return result;
    }

    // True if no frame has a texture.
    [[nodiscard]] bool Empty() const {
        return std::ranges::all_of(frames, [](const TextureFrame& frame) { return frame.texture.empty(); });
    }

    // Only the first frame, or any frame, uses `texture`.
    [[nodiscard]] bool Uses(const std::string& texture) const {
        return std::ranges::any_of(frames, [&](const TextureFrame& frame) { return frame.texture == texture; });
    }

    // Replaces every frame with one still frame of `texture` (none if it is
    // empty). The animation mode and scroll speed stay, so a scrolling
    // surface keeps scrolling with its new texture.
    void Set(std::string texture) {
        const float duration = frames.empty() ? DEFAULT_FRAME_DURATION : frames.front().duration;
        frames.clear();
        if (!texture.empty()) frames.push_back({std::move(texture), duration});
        frameTime = 0.0;
    }

    void AddFrame(std::string texture, const float duration = DEFAULT_FRAME_DURATION) {
        frames.push_back({std::move(texture), duration});
    }

    void Clear() {
        frames.clear();
        frameTime = 0.0;
    }

    // Back to the first frame. The scroll carries on.
    void Restart() { frameTime = 0.0; }

    // Renames `from` to `to` in every frame. Returns true if one changed.
    bool ReplaceTexture(const std::string& from, const std::string& to) {
        bool changed = false;
        for (TextureFrame& frame : frames) {
            if (frame.texture != from) continue;
            frame.texture = to;
            changed = true;
        }
        return changed;
    }

    void Advance(const float deltaTime) {
        if (frames.size() > 1) {
            frameTime += deltaTime;
            // Loops are kept inside one cycle; Once just stops counting.
            if (animation != TextureAnimationMode::Once) {
                if (const double cycle = CycleLength(); cycle > 0.0) frameTime = std::fmod(frameTime, cycle);
            }
            else frameTime = std::min(frameTime, TotalDuration());
        }

        scrollX += static_cast<double>(offsetPerSecond.x) * deltaTime;
        scrollY += static_cast<double>(offsetPerSecond.y) * deltaTime;
    }

    // The scroll so far, wrapped into one repeat of a `tileSize` texture.
    [[nodiscard]] Vector2 WrappedScroll(const Vector2 tileSize) const {
        const auto wrap = [](const double value, const float tile) {
            return tile > 0.0f ? static_cast<float>(std::fmod(value, static_cast<double>(tile))) : static_cast<float>(value);
        };
        return {wrap(scrollX, tileSize.x), wrap(scrollY, tileSize.y)};
    }

    // Zero-based index of the frame showing at `frameTime`.
    [[nodiscard]] int CurrentFrameIndex() const {
        const int count = static_cast<int>(frames.size());
        if (count <= 1) return 0;

        double t = frameTime;

        if (animation == TextureAnimationMode::Once) {
            for (int i = 0; i < count; ++i) {
                t -= Duration(i);
                if (t < 0.0) return i;
            }
            return count - 1;
        }

        const double cycle = CycleLength();
        if (cycle > 0.0) t = std::fmod(std::max(t, 0.0), cycle);

        // Forward through every frame...
        for (int i = 0; i < count; ++i) {
            t -= Duration(i);
            if (t < 0.0) return i;
        }

        // ...then, for ping-pong, back through the inner ones.
        if (animation == TextureAnimationMode::PingPong) {
            for (int i = count - 2; i >= 1; --i) {
                t -= Duration(i);
                if (t < 0.0) return i;
            }
            return 0;
        }

        return count - 1;
    }

    // The texture showing right now.
    [[nodiscard]] const std::string& Current() const {
        static const std::string NONE;
        if (frames.empty()) return NONE;
        return frames[static_cast<size_t>(CurrentFrameIndex())].texture;
    }

private:
    [[nodiscard]] double Duration(const int index) const {
        return std::max(frames[static_cast<size_t>(index)].duration, MIN_FRAME_DURATION);
    }

    [[nodiscard]] double TotalDuration() const {
        double total = 0.0;
        for (int i = 0; i < static_cast<int>(frames.size()); ++i) total += Duration(i);
        return total;
    }

    // One full pass: every frame, plus the way back for ping-pong.
    [[nodiscard]] double CycleLength() const {
        double cycle = TotalDuration();
        if (animation == TextureAnimationMode::PingPong)
            for (int i = 1; i + 1 < static_cast<int>(frames.size()); ++i) cycle += Duration(i);
        return cycle;
    }
};

#endif //TILKY_ENGINE_SURFACE_TEXTURE_HPP
