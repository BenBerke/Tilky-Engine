#include <format>
#include "Headers/Engine/GameTime.hpp"
#include "Headers/Engine/InputManager.hpp"

#include "Headers/Renderer/Renderer.hpp"
#include "Headers/Renderer/Shader.hpp"
#include "Headers/Renderer/MapEditor.hpp"
#include "../../Headers/Renderer/TextureManager.hpp"

#include "Headers/Objects/Player.hpp"
#include "../../Headers/Objects/Sector.hpp"

#define SCREEN_WIDTH 1080
#define SCREEN_HEIGHT 960

#include <nlohmann/json.hpp>
using json = nlohmann::json;

int main() {
    bool editorMode = true;

    if (editorMode) MapEditor::Start();
    while (editorMode) {
        InputManager::BeginFrame();
        editorMode = !(MapEditor::QuitRequested());
        MapEditor::Update();
    }
    MapEditor::Destroy();

    MapEditor::LoadLevel("../Assets/Levels/test_level.json");

    if (!Renderer::Initialize()) {
        SDL_Log("Failed to initialize Renderer: %s", SDL_GetError());
        return 1;
    }

    const int brickTexture = TextureManager::CreateTexture("../Assets/Textures/wall.png");
    const int woodTexture = TextureManager::CreateTexture("../Assets/Textures/wood.png");
    const int whiteTexture = TextureManager::CreateTexture("../Assets/Textures/white.png");
    const int floorTexture = TextureManager::CreateTexture("../Assets/Textures/floor.png");
    const int humanTexture = TextureManager::CreateTexture("../Assets/Textures/human.png");
    if (brickTexture == -1 || woodTexture == -1 || whiteTexture == -1) {
        SDL_Log("Failed to load one or more textures");
        return 1;
    }

    Player::position = MapEditor::playerStartPos;
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

        running = !InputManager::GetKeyDown(SDL_SCANCODE_ESCAPE) || InputManager::QuitRequested();

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
