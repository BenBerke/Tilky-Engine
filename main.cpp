#include <format>

#include "Headers/Engine/GameTime.h"
#include "Headers/Engine/InputManager.h"

#include "Headers/Renderer/Renderer.h"
#include "Headers/Renderer/Shader.h"
#include "Headers/Renderer/MapEditor.h"

#include "Headers/Objects/Player.h"
#include "Headers/Objects/Wall.h"

#define SCREEN_WIDTH 1080
#define SCREEN_HEIGHT 960


int main() {
    if (!Renderer::Initialize()) {
        SDL_Log("Failed to initialize Renderer: %s", SDL_GetError());
        return 1;
    }

    const Wall walls[] = {
        { { 0.0f, 70.0f }, { 100.0f, 100.0f }, {100.0f, 175.0f, 159.0f, 25.0f} },
        { { 0.0f, 80.0f }, { 200.0f, 60.0f }, {10.0f, 1.0f, 159.0f, 255.0f} },
    };

    for (const Wall& wall : walls) MapEditor::AddWall(wall);

    if (!Renderer::CreateMap()) {
        SDL_Log("Failed to create map");
        return 1;
    }

    static float timer = 0;
    static float timerHelper = 0;

    static int fps = 0;

    bool running = true;
    while (running) {
        timer = GameTime::time;
        InputManager::BeginFrame();
        GameTime::Update();
        Player::Update();

        running = !InputManager::GetKeyDown(SDL_SCANCODE_ESCAPE);

        if (InputManager::GetKeyDown(SDL_SCANCODE_1)) Player::position.x += 1;

        Renderer::Update(Player::position, Player::angle);
        Renderer::RenderTextRaw(std::format("FPS: {}", fps), 0, 0, 0.5f, Vector3{255, 255, 255});

        SDL_GL_SwapWindow(Renderer::window);

        if (timer > timerHelper + 1.3f) {
            fps = static_cast<int>(GameTime::GetFPS());
            timerHelper = timer;
        }
    }

    Renderer::Destroy();
}