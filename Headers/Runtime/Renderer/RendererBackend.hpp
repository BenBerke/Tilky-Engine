//
// Created by berke on 5/19/2026.
//

#ifndef TILKY_ENGINE_RENDERERBACKEND_HPP
#define TILKY_ENGINE_RENDERERBACKEND_HPP

#include <string>

enum class RendererBackend {
    AUTO,
    VULKAN,
    OPENGL,
};

// project.tilky's "renderer" value: "vulkan" or "opengl" (anything else).
inline RendererBackend RendererBackendFromName(const std::string& name) {
    return name == "vulkan" ? RendererBackend::VULKAN : RendererBackend::OPENGL;
}

#endif //TILKY_ENGINE_RENDERERBACKEND_HPP