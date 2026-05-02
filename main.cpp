#include "MapEditorInternal.hpp"
#include "Headers/Engine/GameTime.hpp"
#include "Headers/Engine/InputManager.hpp"

#include "Headers/Renderer/Renderer/Renderer.hpp"
#include "Headers/Renderer/Shader.hpp"
#include "Headers/Renderer/MapEditor.hpp"
#include "../../Headers/Renderer/TextureManager.hpp"

#include "Headers/Objects/Player.hpp"
#include "../../Headers/Objects/Sector.hpp"
#include "Headers/Map/MapQueries.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "src/Renderer/Renderer/RendererInternal.hpp"

#define SCREEN_WIDTH 1080
#define SCREEN_HEIGHT 960

void Editor() {

}

int main() {
    bool editorMode = true;

    if (editorMode) MapEditor::Start();

    while (editorMode) {
        InputManager::BeginFrame();
        editorMode = !(MapEditor::QuitRequested());
        MapEditor::Update();
    }

    MapEditor::Destroy();

    MapEditor::LoadLevel(MapEditor::currentMap);

    if (!LevelManager::HasCurrentLevel()) {
        SDL_Log("No current level loaded");
        return 1;
    }

    Level& level = LevelManager::CurrentLevel();

    Player::position = MapEditor::playerStartPos;

    MapQueries::AssignWallsToSectors(
        level.sectors,
        level.walls
    );

    Player::Start(level.sectors);

    if (!Renderer::Initialize()) {
        SDL_Log("Failed to initialize Renderer: %s", SDL_GetError());
        return 1;
    }

    for (const auto& pathInput : MapEditorInternal::textureInputs) {
        if (pathInput[0] == '\0') {
            continue;
        }

        const int textureIndex = TextureManager::CreateTexture(pathInput.data());

        if (textureIndex == -1) {
            SDL_Log("Failed to load texture: %s", pathInput.data());
        }
    }

    RendererInternal::backgroundTextureIndex = MapEditor::backgroundTextureIndex;

    if (Player::currentSector == -1) {
        SDL_Log("Player is not inside any sector");
        return 1;
    }

    if (!Renderer::CreateMap()) {
        SDL_Log("Failed to create map");
        return 1;
    }

    InputManager::SetRelativeMouseMode(Renderer::window, true);

    static float timer = 0;
    static float timerHelper = 0;

    static int fps = 0;

    bool running = true;

    while (running) {
        timer = GameTime::time;

        InputManager::BeginFrame();
        GameTime::Update();

        Player::Update(
            level.walls,
            level.sectors
        );

        running =
            !InputManager::GetKeyDown(SDL_SCANCODE_ESCAPE) &&
            !InputManager::QuitRequested();

        if (InputManager::GetKeyDown(SDL_SCANCODE_1)) {
            Player::position.x += 1;
        }

        Renderer::Update(Player::position, Player::angle);

        Renderer::RenderTextRaw(
            "FPS:" + std::to_string(fps),
            0,
            0,
            0.5f,
            Vector3{255, 255, 255}
        );

        Renderer::RenderTextRaw(
            "NoClip:" + std::to_string(Player::noClip),
            100,
            0,
            0.5f,
            Vector3{255, 255, 255}
        );

        Renderer::RenderTextRaw(
            "CS:" + std::to_string(Player::currentSector),
            200,
            0,
            0.5f,
            Vector3{255, 255, 255}
        );

        SDL_GL_SwapWindow(Renderer::window);

        if (timer > timerHelper + 1.3f) {
            fps = static_cast<int>(GameTime::GetFPS());
            timerHelper = timer;
        }
    }

    Renderer::Destroy();

    return 0;
}