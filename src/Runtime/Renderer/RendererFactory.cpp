//
// Created by berke on 5/19/2026.
//

#include "Headers/Runtime/Renderer/RendererFactory.hpp"

#include "common/TracyQueue.hpp"
#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"
#include "Headers/Runtime/Renderer/Vulkan/Vulkan.hpp"
// Later:
// #include "Runtime/Renderer/Vulkan/VulkanRenderer.hpp"

std::unique_ptr<IRenderer> RendererFactory::CreateRenderer(RendererBackend backend) {
    switch (backend) {
        case RendererBackend::OPENGL:
            return std::make_unique<OpenGL>();

        case RendererBackend::VULKAN:
            return std::make_unique<Vulkan>();
            return nullptr;

        case RendererBackend::AUTO:
            // Default to OpenGL since its more likely to be supported
            // Maybe add SDL_Gpu?
            return std::make_unique<OpenGL>();
    }

    return nullptr;
}