#ifndef TILKY_ENGINE_EMBEDDED_SHADERS_HPP
#define TILKY_ENGINE_EMBEDDED_SHADERS_HPP

#include <optional>
#include <string_view>

// The GLSL in Shaders/, compiled into the executable at build time
// (cmake/EmbedShaders.cmake), so no shader files are shipped or read.
namespace EmbeddedShaders {
    // `path` is relative to Shaders/ with forward slashes, e.g.
    // "Rendering/Rendering.vs.glsl". The text is followed by a 0.
    std::optional<std::string_view> Find(std::string_view path);
}

#endif // TILKY_ENGINE_EMBEDDED_SHADERS_HPP
