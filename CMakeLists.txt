cmake_minimum_required(VERSION 4.1)

project(Wolfy_Engine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

option(WOLFY_USE_IMGUI "Enable Dear ImGui editor UI" ON)

find_package(OpenGL REQUIRED)
find_package(SDL3 REQUIRED CONFIG COMPONENTS SDL3 Headers)
find_package(SDL3_image REQUIRED CONFIG)
find_package(SDL3_ttf REQUIRED CONFIG)
find_package(Freetype REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# -------------------------
# GLAD
# -------------------------

add_library(glad STATIC
        glad/src/glad.c
)

target_include_directories(glad PUBLIC
        glad/include
)

target_link_libraries(glad PUBLIC
        OpenGL::GL
)

# -------------------------
# Dear ImGui
# -------------------------

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/External/imgui/imgui.h")
    add_library(imgui STATIC
            External/imgui/imgui.cpp
            External/imgui/imgui_draw.cpp
            External/imgui/imgui_tables.cpp
            External/imgui/imgui_widgets.cpp
            External/imgui/imgui_demo.cpp

            External/imgui/backends/imgui_impl_sdl3.cpp
            External/imgui/backends/imgui_impl_sdlrenderer3.cpp
            External/imgui/backends/imgui_impl_opengl3.cpp
    )

    target_include_directories(imgui PUBLIC
            External/imgui
            External/imgui/backends
    )

    target_link_libraries(imgui PUBLIC
            SDL3::SDL3
            SDL3::Headers
            OpenGL::GL
    )
else()
    message(FATAL_ERROR "Dear ImGui not found at External/imgui/imgui.h")
endif()

# -------------------------
# Shared project library
# -------------------------

add_library(Wolfy_Project STATIC
        src/Project/ProjectManager.cpp
        Headers/Project/ProjectManager.hpp
)

target_include_directories(Wolfy_Project PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(Wolfy_Project PUBLIC
        SDL3::SDL3
        SDL3::Headers
        nlohmann_json::nlohmann_json
)

target_compile_definitions(Wolfy_Project PUBLIC
        WOLFY_CONTENT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
)

# -------------------------
# Shared localisation library
# Used by both engine and launcher.
# -------------------------

add_library(Wolfy_Local STATIC
        src/Engine/Local/Local.cpp
        Headers/Engine/Local/Local.hpp
)

target_include_directories(Wolfy_Local PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(Wolfy_Local PUBLIC
        SDL3::SDL3
        SDL3::Headers
        nlohmann_json::nlohmann_json
)

target_compile_definitions(Wolfy_Local PUBLIC
        WOLFY_CONTENT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
)

# -------------------------
# Engine sources
# Engine-only code. No launcher files here.
# -------------------------

set(WOLFY_ENGINE_SOURCES
        src/Renderer/TextureManager.cpp

        src/MapEditor/MapEditor.cpp
        src/MapEditor/MapEditorState.cpp
        src/MapEditor/MapEditorGeometry.cpp
        src/MapEditor/MapEditorDrawing.cpp
        src/MapEditor/MapEditorInput.cpp
        src/MapEditor/MapEditorUI.cpp
        src/MapEditor/MapEditorSaveLoad.cpp

        src/Engine/GameTime.cpp
        src/Engine/InputManager.cpp

        src/Objects/Player.cpp

        src/Renderer/Renderer/RendererState.cpp
        src/Renderer/Renderer/RendererMath.cpp
        src/Renderer/Renderer/RendererInit.cpp
        src/Renderer/Renderer/RendererText.cpp
        src/Renderer/Renderer/RendererMapBuild.cpp
        src/Renderer/Renderer/RendererSprites.cpp
        src/Renderer/Renderer/RendererDebug.cpp
        src/Renderer/Renderer/RendererUpdate.cpp
        src/Renderer/Renderer/RendererDestroy.cpp
        src/Renderer/Renderer/RendererDecals.cpp
        src/Renderer/Renderer/RendererSectors.cpp
        src/Renderer/Renderer/RenderBackground.cpp
        src/Renderer/Renderer/RendererUI.cpp

        src/Math/Geometry/Geometry.cpp

        src/Map/MapQueries.cpp
        src/Map/LevelManager.cpp

        config.h
        Headers/Objects/Entity.hpp
        Headers/Objects/Level.hpp
        Headers/Objects/Components.hpp
        Headers/Objects/Sector.hpp
        Headers/Objects/Wall.hpp
        Headers/Map/MapQueries.hpp
        Headers/Map/LevelManager.hpp
        Headers/Math/Geometry/Geometry.hpp
)

# -------------------------
# Engine executable
# -------------------------

add_executable(Wolfy_Engine
        src/EngineApp/EngineMain.cpp
        ${WOLFY_ENGINE_SOURCES}
)

target_precompile_headers(Wolfy_Engine PRIVATE
        <vector>
        <string>
        <array>
        <memory>
        <map>
        <algorithm>
        <cmath>
        <fstream>
        <filesystem>
        <SDL3/SDL.h>
        <nlohmann/json.hpp>
)

target_include_directories(Wolfy_Engine PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        src/MapEditor
)

target_link_libraries(Wolfy_Engine PRIVATE
        SDL3::SDL3
        SDL3::Headers
        $<IF:$<TARGET_EXISTS:SDL3_image::SDL3_image-shared>,SDL3_image::SDL3_image-shared,SDL3_image::SDL3_image-static>
        SDL3_ttf::SDL3_ttf
        Freetype::Freetype
        glad
        imgui
        Wolfy_Project
        Wolfy_Local
        nlohmann_json::nlohmann_json
)

target_compile_definitions(Wolfy_Engine PRIVATE
        WOLFY_CONTENT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
)

if (WOLFY_USE_IMGUI)
    target_compile_definitions(Wolfy_Engine PRIVATE WOLFY_USE_IMGUI=1)
else()
    target_compile_definitions(Wolfy_Engine PRIVATE WOLFY_USE_IMGUI=0)
endif()

# -------------------------
# Launcher sources
# -------------------------

set(WOLFY_LAUNCHER_SOURCES
        src/Launcher/LauncherApp.cpp
        Headers/Launcher/LauncherApp.hpp
)

# -------------------------
# Launcher executable
# -------------------------

add_executable(Wolfy_Launcher
        src/Launcher/LauncherMain.cpp
        ${WOLFY_LAUNCHER_SOURCES}
)

target_include_directories(Wolfy_Launcher PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(Wolfy_Launcher PRIVATE
        SDL3::SDL3
        SDL3::Headers
        imgui
        Wolfy_Project
        Wolfy_Local
        nlohmann_json::nlohmann_json
)

target_compile_definitions(Wolfy_Launcher PRIVATE
        WOLFY_CONTENT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
)

# -------------------------
# All executable
# Builds engine + launcher, then runs launcher-style executable.
# -------------------------

add_executable(Wolfy_All
        src/Launcher/LauncherMain.cpp
        ${WOLFY_LAUNCHER_SOURCES}
)

target_include_directories(Wolfy_All PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(Wolfy_All PRIVATE
        SDL3::SDL3
        SDL3::Headers
        imgui
        Wolfy_Project
        Wolfy_Local
        nlohmann_json::nlohmann_json
)

target_compile_definitions(Wolfy_All PRIVATE
        WOLFY_CONTENT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_dependencies(Wolfy_All
        Wolfy_Engine
        Wolfy_Launcher
)