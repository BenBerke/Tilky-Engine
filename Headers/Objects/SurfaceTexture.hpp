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

// One step of a surface animation: show `texture` for `duration` seconds,
// placed by its own offset, scale and flips, scrolling at its own speed.
struct TextureFrame {
    std::string texture;
    float duration = 0.5f;

    Vector2 textureOffset = {0.0f, 0.0f}; // map units
    Vector2 textureScale = {1.0f, 1.0f};  // repeats; 1 = the texture's own size
    bool flipTextureX = false;
    bool flipTextureY = false;
    Vector2 offsetPerSecond = {0.0f, 0.0f}; // map units per second while this frame shows

    // Exact compares: this spots whether an edit wrote a field.
    bool operator==(const TextureFrame& other) const {
        return texture == other.texture && duration == other.duration &&
               textureOffset.x == other.textureOffset.x && textureOffset.y == other.textureOffset.y &&
               textureScale.x == other.textureScale.x && textureScale.y == other.textureScale.y &&
               flipTextureX == other.flipTextureX && flipTextureY == other.flipTextureY &&
               offsetPerSecond.x == other.offsetPerSecond.x && offsetPerSecond.y == other.offsetPerSecond.y;
    }

    // Copies everything but the texture and duration.
    void CopyPlacement(const TextureFrame& from) {
        textureOffset = from.textureOffset;
        textureScale = from.textureScale;
        flipTextureX = from.flipTextureX;
        flipTextureY = from.flipTextureY;
        offsetPerSecond = from.offsetPerSecond;
    }
};

// What a wall piece, floor or ceiling shows: one or more frames played in
// order. One frame is a plain, still texture. No frames (or only empty ones)
// means untextured. Each frame carries its own placement and scroll speed;
// the scroll itself is one running value whose speed is the showing frame's,
// so changing frames never makes the texture jump.
//
// `frameTime` and the scroll are runtime state: never saved, ignored by ==,
// advanced by Level::UpdateSurfaceAnimations during Play and in the 3D
// editor view. Everything that only cares about "the" texture (the 2D
// canvas, tiling, stair building, same-texture selection) uses the first
// frame; the renderer and Lua's getters use the current one.
struct SurfaceTexture {
    std::vector<TextureFrame> frames;
    TextureAnimationMode animation = TextureAnimationMode::Loop;

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
        return frames == other.frames && animation == other.animation;
    }

    // The frame showing right now, or default placement with no texture.
    [[nodiscard]] const TextureFrame& Shown() const {
        static const TextureFrame NONE;
        if (frames.empty()) return NONE;
        return frames[static_cast<size_t>(CurrentFrameIndex())];
    }

    // The first frame, or default placement with no texture.
    [[nodiscard]] const TextureFrame& FirstFrame() const {
        static const TextureFrame NONE;
        return frames.empty() ? NONE : frames.front();
    }

    // Runs `edit` on every frame: how a single value (an offset nudge, an
    // alignment, a script writing topTextureOffset) applies to them all.
    template<typename Edit>
    void ForEachFrame(Edit&& edit) {
        for (TextureFrame& frame : frames) edit(frame);
    }

    // Field by field, frame by frame: what changed between `before` and
    // `after` is written to `target`, so an edit to one frame's offset on
    // one surface doesn't also copy its textures onto every other one. A
    // change in frame count (or a target with a different count) copies
    // all the frames. Returns true if anything was written.
    static bool ApplyChange(const SurfaceTexture& before, const SurfaceTexture& after, SurfaceTexture& target) {
        if (before == after) return false;

        bool wrote = false;

        if (after.animation != before.animation) {
            target.animation = after.animation;
            wrote = true;
        }

        if (after.frames.size() != before.frames.size() || target.frames.size() != before.frames.size()) {
            if (!(after.frames == before.frames)) {
                target.frames = after.frames;
                target.Restart();
                wrote = true;
            }
            return wrote;
        }

        for (size_t i = 0; i < after.frames.size(); ++i) {
            const TextureFrame& a = after.frames[i];
            const TextureFrame& b = before.frames[i];
            TextureFrame& t = target.frames[i];

            if (a.texture != b.texture) { t.texture = a.texture; wrote = true; }
            if (a.duration != b.duration) { t.duration = a.duration; wrote = true; }
            if (a.textureOffset.x != b.textureOffset.x || a.textureOffset.y != b.textureOffset.y) { t.textureOffset = a.textureOffset; wrote = true; }
            if (a.textureScale.x != b.textureScale.x || a.textureScale.y != b.textureScale.y) { t.textureScale = a.textureScale; wrote = true; }
            if (a.flipTextureX != b.flipTextureX) { t.flipTextureX = a.flipTextureX; wrote = true; }
            if (a.flipTextureY != b.flipTextureY) { t.flipTextureY = a.flipTextureY; wrote = true; }
            if (a.offsetPerSecond.x != b.offsetPerSecond.x || a.offsetPerSecond.y != b.offsetPerSecond.y) { t.offsetPerSecond = a.offsetPerSecond; wrote = true; }
        }

        return wrote;
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
    // empty). It keeps the first frame's duration, placement and scroll
    // speed, so a scrolling surface keeps scrolling with its new texture.
    void Set(std::string texture) {
        TextureFrame frame = FirstFrame();
        frames.clear();
        frame.texture = std::move(texture);
        if (!frame.texture.empty()) frames.push_back(std::move(frame));
        frameTime = 0.0;
    }

    // Takes `from`'s frames (textures, durations) and animation, but keeps
    // this surface's own placement: frame i keeps the offset, scale, flips
    // and scroll speed of this surface's frame i (its last one past the end).
    // What a texture-only paste does.
    void PasteFramesKeepingPlacement(const SurfaceTexture& from) {
        std::vector<TextureFrame> pasted = from.frames;
        if (!frames.empty())
            for (size_t i = 0; i < pasted.size(); ++i) pasted[i].CopyPlacement(frames[std::min(i, frames.size() - 1)]);
        frames = std::move(pasted);
        animation = from.animation;
        frameTime = 0.0;
    }

    // A new last frame, placed like the current last one.
    void AddFrame(std::string texture, const float duration = DEFAULT_FRAME_DURATION) {
        TextureFrame frame = frames.empty() ? TextureFrame{} : frames.back();
        frame.texture = std::move(texture);
        frame.duration = duration;
        frames.push_back(std::move(frame));
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
        // At the speed of the frame that showed during this step.
        const Vector2 speed = Shown().offsetPerSecond;
        scrollX += static_cast<double>(speed.x) * deltaTime;
        scrollY += static_cast<double>(speed.y) * deltaTime;

        if (frames.size() > 1) {
            frameTime += deltaTime;
            // Loops are kept inside one cycle; Once just stops counting.
            if (animation != TextureAnimationMode::Once) {
                if (const double cycle = CycleLength(); cycle > 0.0) frameTime = std::fmod(frameTime, cycle);
            }
            else frameTime = std::min(frameTime, TotalDuration());
        }
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
