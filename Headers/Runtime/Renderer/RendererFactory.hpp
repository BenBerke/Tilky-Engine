//
// Created by berke on 5/19/2026.
//

#ifndef TILKY_ENGINE_RENDERERFACTORY_HPP
#define TILKY_ENGINE_RENDERERFACTORY_HPP

#include <memory>

#include "Headers/Runtime/Renderer/IRenderer.hpp"
#include "Headers/Runtime/Renderer/RendererBackend.hpp"

class RendererFactory {
public:
    static std::unique_ptr<IRenderer> CreateRenderer(RendererBackend backend);
};

#endif //TILKY_ENGINE_RENDERERFACTORY_HPP