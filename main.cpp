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
    Renderer::Initialize();
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
        SDL_Log("Failed to initialize GLAD");
        return -1;
    }
    glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

    constexpr float vertices[] = {
        // positions          // colours
        -1.0f,  1.0f, 0.0f,   1.0f, 0.0f, 0.0f,   // top left
        -1.0f, -1.0f, 0.0f,   0.0f, 1.0f, 0.0f,   // bottom left
         1.0f,  1.0f, 0.0f,   0.0f, 0.0f, 1.0f,   // top right
         1.0f, -1.0f, 0.0f,   1.0f, 1.0f, 0.0f    // bottom right
    };

    const unsigned int indices[] = {
        0, 1, 2,
        1, 3, 2
    };

    unsigned int VAO;
    glGenVertexArrays(1, &VAO);
    glBindVertexArray(VAO);

    unsigned int EBO;
    glGenBuffers(1, &EBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);


    unsigned int VBO;
    glGenBuffers(1, &VBO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    const Shader shader = Shader("../Shaders/vertex.vs", "../Shaders/frag.fs");
    if (shader.ID == 0) {
        SDL_Log("Shader creation failed");
        return -1;
    }

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    bool running = true;
    while (running) {
        InputManager::BeginFrame();
        GameTime::Update();

        running = !InputManager::GetKeyDown(SDL_SCANCODE_ESCAPE);

        shader.use();
        glClearColor(.2f, .3f, .3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

        SDL_GL_SwapWindow(Renderer::window);
    }

    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
}