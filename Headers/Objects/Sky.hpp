#ifndef TILKY_ENGINE_SKY_HPP
#define TILKY_ENGINE_SKY_HPP

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "Headers/Math/Vector/Vector3.hpp"

// How the sky image is wrapped around the camera. The registry below is the
// one list every system reads (editor combo, level files, the Lua SkyMode
// table), so a new mode only needs an entry there plus its shader branch.
enum class SkyMode : int {
    Cubemap = 0,         // six square images, one per face
    Equirectangular = 1, // one 2:1 panorama on a sphere
    Cylinder = 2,        // DOOM's sky: one strip, 4 repeats around, fixed to the screen vertically
};

// Directions in world space: Front is +Z (where yaw 0 looks), Right is -X
// (screen-right at yaw 0), Up is +Y. Up and Down are seen with Front at
// their bottom and top edge respectively.
enum class SkyFace : int {
    Right = 0,
    Left = 1,
    Up = 2,
    Down = 3,
    Front = 4,
    Back = 5,
};

inline constexpr int SKY_FACE_COUNT = 6;

struct SkyModeInfo {
    SkyMode mode;
    const char* name;     // level files and the Lua SkyMode table
    const char* labelKey; // editor label (localisation key)
    const char* tooltipKey;
};

struct SkyFaceInfo {
    SkyFace face;
    const char* name;     // level files and the Lua SkyFace table
    const char* labelKey; // editor label (localisation key)
};

inline constexpr std::array SKY_MODES = {
    SkyModeInfo{SkyMode::Cubemap, "Cubemap", "settings.rendering.sky.mode.cubemap", "settings.rendering.tooltip.sky.mode.cubemap"},
    SkyModeInfo{SkyMode::Equirectangular, "Equirectangular", "settings.rendering.sky.mode.equirectangular", "settings.rendering.tooltip.sky.mode.equirectangular"},
    SkyModeInfo{SkyMode::Cylinder, "Cylinder", "settings.rendering.sky.mode.cylinder", "settings.rendering.tooltip.sky.mode.cylinder"},
};

inline constexpr std::array<SkyFaceInfo, SKY_FACE_COUNT> SKY_FACES = {
    SkyFaceInfo{SkyFace::Right, "Right", "settings.rendering.sky.face.right"},
    SkyFaceInfo{SkyFace::Left, "Left", "settings.rendering.sky.face.left"},
    SkyFaceInfo{SkyFace::Up, "Up", "settings.rendering.sky.face.up"},
    SkyFaceInfo{SkyFace::Down, "Down", "settings.rendering.sky.face.down"},
    SkyFaceInfo{SkyFace::Front, "Front", "settings.rendering.sky.face.front"},
    SkyFaceInfo{SkyFace::Back, "Back", "settings.rendering.sky.face.back"},
};

inline const SkyModeInfo* FindSkyMode(const SkyMode mode) {
    for (const SkyModeInfo& info : SKY_MODES) if (info.mode == mode) return &info;
    return nullptr;
}

inline std::optional<SkyMode> FindSkyModeByName(const std::string_view name) {
    for (const SkyModeInfo& info : SKY_MODES) if (name == info.name) return info.mode;
    return std::nullopt;
}

inline std::optional<SkyMode> SkyModeFromInt(const int value) {
    for (const SkyModeInfo& info : SKY_MODES) if (static_cast<int>(info.mode) == value) return info.mode;
    return std::nullopt;
}

inline bool IsValidSkyFace(const int value) {
    return value >= 0 && value < SKY_FACE_COUNT;
}

// The level's sky, drawn behind everything.
struct SkySettings {
    SkyMode mode = SkyMode::Equirectangular;

    // Equirectangular and Cylinder. Path relative to Assets.
    std::string texture;

    // Cubemap, indexed by SkyFace. Paths relative to Assets.
    std::array<std::string, SKY_FACE_COUNT> cubemapFaces;

    float rotation = 0.0f;      // degrees, same direction as camera yaw
    float rotationSpeed = 0.0f; // degrees per second, only while the game runs
    float horizonOffset = 0.0f; // degrees; positive raises the horizon

    Vector3 tint = {255.0f, 255.0f, 255.0f};         // multiplies the image, 0..255
    Vector3 fallbackColor = {45.0f, 45.0f, 45.0f};   // shown when there is no usable image, 0..255

    // Runtime-only: rotation added by rotationSpeed so far. Never saved.
    float spin = 0.0f;

    [[nodiscard]] std::string& Face(const SkyFace face) { return cubemapFaces[static_cast<int>(face)]; }
    [[nodiscard]] const std::string& Face(const SkyFace face) const { return cubemapFaces[static_cast<int>(face)]; }
};

#endif
