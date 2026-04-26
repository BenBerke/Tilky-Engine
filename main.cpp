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

int main() {
    if (!Renderer::Initialize()) {
        SDL_Log("Failed to initialize Renderer: %s", SDL_GetError());
        return 1;
    }

    const Wall walls[] = {
    // =========================================================
    // Sector 0: Spawn room
    // floor = 0, ceiling = 40
    // Door/opening to corridor is on the north wall between x -35 and 35.
    // Door/opening to raised room is on the east wall between y 10 and 80.
    // =========================================================

    // South wall
    { { -120.0f, -80.0f }, {  120.0f, -80.0f }, { 180.0f, 180.0f, 180.0f, 255.0f }, -1, 0 },

    // West wall
    { { -120.0f, -80.0f }, { -120.0f, 160.0f }, { 180.0f, 180.0f, 180.0f, 255.0f }, -1, 0 },

    // East wall split because of raised room opening
    { { 120.0f, -80.0f }, { 120.0f,  10.0f }, { 180.0f, 180.0f, 180.0f, 255.0f }, -1, 0 },
    { { 120.0f,  80.0f }, { 120.0f, 160.0f }, { 180.0f, 180.0f, 180.0f, 255.0f }, -1, 0 },

    // North wall split because of corridor opening
    { { -120.0f, 160.0f }, { -35.0f, 160.0f }, { 180.0f, 180.0f, 180.0f, 255.0f }, -1, 0 },
    { {   35.0f, 160.0f }, { 120.0f, 160.0f }, { 180.0f, 180.0f, 180.0f, 255.0f }, -1, 0 },


    // =========================================================
    // Sector 1: Corridor
    // floor = 0, ceiling = 36
    // =========================================================

    // Left corridor wall
    { { -35.0f, 160.0f }, { -35.0f, 330.0f }, { 130.0f, 160.0f, 220.0f, 255.0f }, -1, 1 },

    // Right corridor wall
    { { 35.0f, 160.0f }, { 35.0f, 330.0f }, { 130.0f, 160.0f, 220.0f, 255.0f }, -1, 1 },


    // =========================================================
    // Sector 2: Large room
    // floor = 0, ceiling = 48
    // Opening to corridor is on the south wall between x -35 and 35.
    // Opening to triangular chamber is on the west wall between y 420 and 500.
    // =========================================================

    // South wall split for corridor opening
    { { -160.0f, 330.0f }, { -35.0f, 330.0f }, { 200.0f, 140.0f, 100.0f, 255.0f }, -1, 2 },
    { {   35.0f, 330.0f }, { 160.0f, 330.0f }, { 200.0f, 140.0f, 100.0f, 255.0f }, -1, 2 },

    // East wall
    { { 160.0f, 330.0f }, { 160.0f, 560.0f }, { 200.0f, 140.0f, 100.0f, 255.0f }, -1, 2 },

    // North wall
    { { 160.0f, 560.0f }, { -160.0f, 560.0f }, { 200.0f, 140.0f, 100.0f, 255.0f }, -1, 2 },

    // West wall split for triangular chamber opening
    { { -160.0f, 330.0f }, { -160.0f, 420.0f }, { 200.0f, 140.0f, 100.0f, 255.0f }, -1, 2 },
    { { -160.0f, 500.0f }, { -160.0f, 560.0f }, { 200.0f, 140.0f, 100.0f, 255.0f }, -1, 2 },


    // =========================================================
    // Sector 3: Raised side room
    // floor = 8, ceiling = 56
    // Opening to spawn room is on west wall between y 10 and 80.
    // =========================================================

    // South wall
    { { 120.0f, 10.0f }, { 280.0f, 10.0f }, { 100.0f, 220.0f, 130.0f, 255.0f }, -1, 3 },

    // East wall
    { { 280.0f, 10.0f }, { 280.0f, 130.0f }, { 100.0f, 220.0f, 130.0f, 255.0f }, -1, 3 },

    // North wall
    { { 280.0f, 130.0f }, { 120.0f, 130.0f }, { 100.0f, 220.0f, 130.0f, 255.0f }, -1, 3 },

    // Small west wall above the opening
    { { 120.0f, 130.0f }, { 120.0f, 80.0f }, { 100.0f, 220.0f, 130.0f, 255.0f }, -1, 3 },


    // =========================================================
    // Sector 4: Triangular chamber
    // floor = -4, ceiling = 44
    // Opening to large room is roughly on the right side.
    // =========================================================

    { { -160.0f, 420.0f }, { -300.0f, 380.0f }, { 170.0f, 100.0f, 220.0f, 255.0f }, -1, 4 },
    { { -300.0f, 380.0f }, { -330.0f, 540.0f }, { 170.0f, 100.0f, 220.0f, 255.0f }, -1, 4 },
    { { -330.0f, 540.0f }, { -160.0f, 500.0f }, { 170.0f, 100.0f, 220.0f, 255.0f }, -1, 4 }
};

    const Sector sectors[] {
    // =========================================================
    // Sector 0: Spawn room
    // =========================================================
    {
        {
            { -120.0f, -80.0f },
            {  120.0f, -80.0f },
            {  120.0f, 160.0f },
            { -120.0f, 160.0f }
        },
        {},
        40.0f, // ceilingHeight
        0.0f,  // floorHeight
        { 60.0f,  70.0f,  95.0f },   // ceilingColor
        { 95.0f,  95.0f,  95.0f }    // floorColor
    },

    // =========================================================
    // Sector 1: Corridor
    // =========================================================
    {
        {
            { -35.0f, 160.0f },
            {  35.0f, 160.0f },
            {  35.0f, 330.0f },
            { -35.0f, 330.0f }
        },
        {},
        36.0f,
        0.0f,
        { 85.0f,  80.0f,  55.0f },   // ceilingColor
        { 110.0f, 100.0f,  70.0f }   // floorColor
    },

    // =========================================================
    // Sector 2: Large room
    // =========================================================
    {
        {
            { -160.0f, 330.0f },
            {  160.0f, 330.0f },
            {  160.0f, 560.0f },
            { -160.0f, 560.0f }
        },
        {},
        48.0f,
        0.0f,
        { 70.0f,  55.0f,  55.0f },   // ceilingColor
        { 120.0f, 85.0f,  70.0f }    // floorColor
    },

    // =========================================================
    // Sector 3: Raised side room
    // =========================================================
    {
        {
            { 120.0f,  10.0f },
            { 280.0f,  10.0f },
            { 280.0f, 130.0f },
            { 120.0f, 130.0f }
        },
        {},
        56.0f,
        8.0f,
        { 50.0f,  90.0f,  65.0f },   // ceilingColor
        { 85.0f, 140.0f,  95.0f }    // floorColor
    },

    // =========================================================
    // Sector 4: Triangular chamber
    // =========================================================
    {
        {
            { -160.0f, 420.0f },
            { -300.0f, 380.0f },
            { -330.0f, 540.0f },
            { -160.0f, 500.0f }
        },
        {},
        44.0f,
        -4.0f,
        { 90.0f,  60.0f, 110.0f },   // ceilingColor
        { 70.0f,  45.0f,  85.0f }    // floorColor
    }
};

    for (const Wall& wall : walls) MapEditor::AddWall(wall);
    for (const Sector& sector : sectors) MapEditor::AddSector(sector);

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