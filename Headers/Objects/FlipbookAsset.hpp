//
// Created by berke on 10/9/2026.
//

#ifndef TILKY_ENGINE_FLIPBOOKASSET_HPP
#define TILKY_ENGINE_FLIPBOOKASSET_HPP

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// A sprite animation stored in a .fpk file (BSON). Holds only what the
// animation looks like; playback state (current frame, timer, speed) lives on
// each ComponentFlipbook, so many entities can share one file.
//
// A flipbook has no side count of its own - it uses the driven sprite's.
// Every frame always stores all 8 texture slots, in the same order as
// ComponentSprite::textureFileNames.

enum class FlipbookLoopMode : int {
    Once,     // plays to the last frame and holds it
    Loop,     // last frame -> first frame
    PingPong  // first -> last -> first -> ...
};

struct FlipbookFrame {
    // Unique within the flipbook. Scripts use it: flipbook:SetFrame("windup").
    std::string name;

    std::array<std::string, 8> textures;

    // Seconds this frame stays on screen. 0 or less uses 1 / fps.
    float duration = 0.0f;

    // Name of a script function called on the owner entity's scripts when
    // playback reaches this frame. Empty = no event.
    std::string eventFunction;
};

struct FlipbookAsset {
    float fps = 10.0f;
    FlipbookLoopMode loopMode = FlipbookLoopMode::Loop;
    bool playOnStart = true;

    std::vector<FlipbookFrame> frames;

    // Seconds frame `index` stays on screen. Always > 0.
    [[nodiscard]] float FrameDuration(int index) const;

    // Moves `frame` one step along loopMode. `direction` is the ping-pong
    // direction (+1 / -1), updated in place. False when a Once flipbook is
    // already on its last frame (frame stays there). Shared by the game and
    // the editor preview so both play a flipbook the same way.
    bool StepFrame(int& frame, int& direction) const;

    // Index of the frame called `name`, or -1.
    [[nodiscard]] int FindFrame(std::string_view name) const;

    // A name like "Frame3" that no frame uses yet.
    [[nodiscard]] std::string MakeUniqueFrameName() const;
};

namespace FlipbookIO {
    inline constexpr std::string_view kExtension = ".fpk";

    bool Load(const std::filesystem::path& file, FlipbookAsset& outAsset, std::string* errorMessage = nullptr);
    bool Save(const std::filesystem::path& file, const FlipbookAsset& asset, std::string* errorMessage = nullptr);

    // Whether `name` can be a frame event function: a plain Lua identifier
    // that isn't a keyword, a lifecycle function (Start, Update, ...) or an
    // engine global. Empty is valid (no event). On false, `reason` says why.
    bool IsValidEventFunction(std::string_view name, std::string* reason = nullptr);

    // Replaces every texture equal to oldReference in every .fpk under the
    // project's Assets folder, rewriting the files that changed. Returns how
    // many files were rewritten.
    int RenameTextureReferenceInProject(const std::string& oldReference, const std::string& newReference);
}

// Loaded .fpk files, keyed by their Assets-relative reference with extension
// (e.g. "Animations/walk.fpk"). Loaded on first use and kept until
// invalidated, so every ComponentFlipbook using a file shares one copy.
namespace FlipbookLibrary {
    // nullptr if the file is missing or broken (logged once per reference
    // until it's invalidated).
    const FlipbookAsset* Get(const std::string& reference);

    // Forgets one file, so the next Get reloads it (after the editor saves it).
    void Invalidate(const std::string& reference);

    void Clear();
}

#endif //TILKY_ENGINE_FLIPBOOKASSET_HPP
