#include <format>
#include <iostream>
#include "Headers/Engine/GameTime.h"
#include "Headers/Engine/InputManager.h"

#include "Headers/Renderer/Renderer.h"
#include "Headers/Renderer/Shader.h"
#include "Headers/Renderer/MapEditor.h"
#include "../../Headers/Renderer/TextureManager.h"

#include "Headers/Objects/Player.h"
#include "../../Headers/Objects/Sector.h"

#define SCREEN_WIDTH 1080
#define SCREEN_HEIGHT 960

#include <nlohmann/json.hpp>
using json = nlohmann::json;

int main() {
    bool editorMode = true;

    json j_object = {
        {"name", "John Doe"},
        {"age", 30},
        {"is_student", false},
        {"skills", {"C++", "Python", "JavaScript"}}
    };
    std::cout << j_object["skills"] << std::endl;
    j_object["skills"].push_back("Rust");
    std::cout << j_object["skills"] << std::endl;

    if (editorMode) MapEditor::Start();
    while (editorMode) {
        InputManager::BeginFrame();
        editorMode = !InputManager::GetKeyDown(SDL_SCANCODE_ESCAPE);
        MapEditor::Update();
    }
    MapEditor::Destroy();

    if (!Renderer::Initialize()) {
        SDL_Log("Failed to initialize Renderer: %s", SDL_GetError());
        return 1;
    }

    int brickTexture = TextureManager::CreateTexture("../Assets/Textures/wall.png");
    int woodTexture = TextureManager::CreateTexture("../Assets/Textures/wood.png");
    int whiteTexture = TextureManager::CreateTexture("../Assets/Textures/white.png");
    int floorTexture = TextureManager::CreateTexture("../Assets/Textures/floor.png");
    int humanTexture = TextureManager::CreateTexture("../Assets/Textures/human.png");
    if (brickTexture == -1 || woodTexture == -1 || whiteTexture == -1) {
        SDL_Log("Failed to load one or more textures");
        return 1;
    }


    Player::position = {00.0f, 0.0f};
    Player::FindCurrentSector(MapEditor::sectors);

    if (Player::currentSector == -1) {
        SDL_Log("Player is not inside any sector");
        return 1;
    }

    if (!Renderer::CreateMap()) {
        SDL_Log("Failed to create map");
        return 1;
    }
    // endregion

    static float timer = 0;
    static float timerHelper = 0;

    static int fps = 0;

    bool running = true;
    while (running) {
        timer = GameTime::time;
        InputManager::BeginFrame();
        GameTime::Update();
        Player::Update(MapEditor::walls, MapEditor::sectors);


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
