#include <filesystem>
#include <glad/glad.h>
#include <SDL3/SDL_video.h>

#include "Headers/Engine/InputManager.h"
#include "Headers/Engine/GameTime.h"
#include "Headers/Renderer/Renderer.h"
#include "Headers/Renderer/Shader.h"

#define SCREEN_WIDTH 1080
#define SCREEN_HEIGHT 960

int main() {
    if (!Renderer::Initialize()) {
        SDL_Log("Failed to initialize Renderer: %s", SDL_GetError());
        return 1;
    }

    bool running = true;
    while (running) {
        InputManager::BeginFrame();
        GameTime::Update();

        running = !InputManager::GetKeyDown(SDL_SCANCODE_ESCAPE);

        Renderer::Update();

        SDL_GL_SwapWindow(Renderer::window);
    }

    Renderer::Destroy();
}