//
// Created by berke on 5/19/2026.
//

#include "Headers/Runtime/Renderer/RendererFactory.hpp"

#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"
// Later:
// #include "Runtime/Renderer/Vulkan/VulkanRenderer.hpp"

std::unique_ptr<IRenderer> RendererFactory::Create(RendererBackend backend) {
    switch (backend) {
        case RendererBackend::OpenGL:
            return std::make_unique<OpenGL>();

        case RendererBackend::Vulkan:
            // Later:
            // return std::make_unique<VulkanRenderer>();
            return nullptr;

        case RendererBackend::Auto:
            return nullptr;
    }

    return nullptr;
}