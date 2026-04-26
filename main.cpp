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


#include "../../Headers/Objects/Sector.h"
#include "../../Headers/Renderer/TextureManager.h"

int main() {
    if (!Renderer::Initialize()) {
        SDL_Log("Failed to initialize Renderer: %s", SDL_GetError());
        return 1;
    }
    int brickTexture = TextureManager::CreateTexture("../Assets/Textures/wall.png");
    int woodTexture = TextureManager::CreateTexture("../Assets/Textures/wood.png");
    int whiteTexture = TextureManager::CreateTexture("../Assets/Textures/white.png");
    if (brickTexture == -1 || woodTexture == -1 || whiteTexture == -1) {
        SDL_Log("Failed to load one or more textures");
        return 1;
    }

    // region Map

    Vector4 WHITE = {255.0f, 255.0f, 255.0f, 255.0f};
    Vector4 PORTAL_RED = {255.0f, 80.0f, 80.0f, 255.0f};

    // =========================================================
    // Main shape points
    // =========================================================

    // Sector 0: pentagon spawn room
    constexpr Vector2 A = {-120.0f, -60.0f};
    constexpr Vector2 B = {40.0f, -80.0f};
    constexpr Vector2 C = {100.0f, -10.0f};
    constexpr Vector2 D = {40.0f, 80.0f};
    constexpr Vector2 E = {-110.0f, 60.0f};

    // Sector 1: angled connector
    constexpr Vector2 F = {170.0f, 5.0f};
    constexpr Vector2 G = {145.0f, 105.0f};

    // Sector 2: hexagon room
    constexpr Vector2 H2 = {270.0f, 20.0f};
    constexpr Vector2 H3 = {310.0f, 100.0f};
    constexpr Vector2 H4 = {250.0f, 180.0f};
    constexpr Vector2 H5 = {170.0f, 170.0f};

    // Sector 3: triangle room
    constexpr Vector2 T = {210.0f, 260.0f};

    // Sector 4: raised side room
    constexpr Vector2 R0 = {360.0f, 10.0f};
    constexpr Vector2 R1 = {390.0f, 120.0f};
    constexpr Vector2 R2 = {330.0f, 170.0f};

    // Sector 5: sunken triangle alcove
    constexpr Vector2 S = {-220.0f, 0.0f};

    const Wall walls[] = {
        // =========================================================
        // Sector 0: Pentagon spawn room
        // floor = 0, ceiling = 40
        // portals:
        //   C-D -> Sector 1
        //   E-A -> Sector 5
        // =========================================================

        {A, B, WHITE, 0, -1, brickTexture},
        {B, C, WHITE, 0, -1, brickTexture},

        // Portal: spawn <-> angled connector
        {C, D, PORTAL_RED, 0, 1, whiteTexture},

        {D, E, WHITE, 0, -1, brickTexture},

        // Portal: spawn <-> sunken alcove
        {E, A, PORTAL_RED, 0, 5, whiteTexture},


        // =========================================================
        // Sector 1: Angled connector
        // floor = 0, ceiling = 36
        // portals:
        //   D-C -> Sector 0
        //   F-G -> Sector 2
        // =========================================================

        {C, F, WHITE, 1, -1, woodTexture},

        // Portal: connector <-> hex room
        {F, G, PORTAL_RED, 1, 2, whiteTexture},

        {G, D, WHITE, 1, -1, woodTexture},


        // =========================================================
        // Sector 2: Hexagon room
        // floor = 4, ceiling = 48
        // portals:
        //   G-F   -> Sector 1
        //   H2-H3 -> Sector 4
        //   H4-H5 -> Sector 3
        // =========================================================

        {F, H2, WHITE, 2, -1, brickTexture},

        // Portal: hex room <-> raised side room
        {H2, H3, PORTAL_RED, 2, 4, whiteTexture},

        {H3, H4, WHITE, 2, -1, brickTexture},

        // Portal: hex room <-> triangle room
        {H4, H5, PORTAL_RED, 2, 3, whiteTexture},

        {H5, G, WHITE, 2, -1, brickTexture},


        // =========================================================
        // Sector 3: Triangle room
        // floor = 8, ceiling = 52
        // portal:
        //   H5-H4 -> Sector 2
        // =========================================================

        {H4, T, WHITE, 3, -1, woodTexture},
        {T, H5, WHITE, 3, -1, woodTexture},


        // =========================================================
        // Sector 4: Raised side room
        // floor = 12, ceiling = 60
        // portal:
        //   H3-H2 -> Sector 2
        // =========================================================

        {H2, R0, WHITE, 4, -1, brickTexture},
        {R0, R1, WHITE, 4, -1, brickTexture},
        {R1, R2, WHITE, 4, -1, brickTexture},
        {R2, H3, WHITE, 4, -1, brickTexture},


        // =========================================================
        // Sector 5: Sunken triangle alcove
        // floor = -4, ceiling = 34
        // portal:
        //   A-E -> Sector 0
        // =========================================================

        {E, S, WHITE, 5, -1, woodTexture},
        {S, A, WHITE, 5, -1, woodTexture},
    };

    const Sector sectors[] = {
        // =========================================================
        // Sector 0: Pentagon spawn room
        // =========================================================
        {
            {A, B, C, D, E},
            {},
            40.0f,
            0.0f,
            {60.0f, 70.0f, 95.0f},
            {95.0f, 95.0f, 95.0f}
        },

        // =========================================================
        // Sector 1: Angled connector
        // =========================================================
        {
            {C, F, G, D},
            {},
            36.0f,
            0.0f,
            {85.0f, 80.0f, 55.0f},
            {110.0f, 100.0f, 70.0f}
        },

        // =========================================================
        // Sector 2: Hexagon room
        // =========================================================
        {
            {F, H2, H3, H4, H5, G},
            {},
            48.0f,
            4.0f,
            {70.0f, 55.0f, 55.0f},
            {120.0f, 85.0f, 70.0f}
        },

        // =========================================================
        // Sector 3: Triangle room
        // =========================================================
        {
            {H5, H4, T},
            {},
            52.0f,
            8.0f,
            {50.0f, 90.0f, 65.0f},
            {85.0f, 140.0f, 95.0f}
        },

        // =========================================================
        // Sector 4: Raised side room
        // =========================================================
        {
            {H2, R0, R1, R2, H3},
            {},
            60.0f,
            12.0f,
            {90.0f, 60.0f, 110.0f},
            {70.0f, 45.0f, 85.0f}
        },

        // =========================================================
        // Sector 5: Sunken triangle alcove
        // =========================================================
        {
            {A, E, S},
            {},
            34.0f,
            -4.0f,
            {40.0f, 55.0f, 85.0f},
            {60.0f, 70.0f, 115.0f}
        }
    };
    // endregion

    for (const Wall& wall : walls) {
        MapEditor::AddWall(wall);
    }

    for (const Sector& sector : sectors) {
        MapEditor::AddSector(sector);
    }

    Player::position = {-40.0f, 0.0f};
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
