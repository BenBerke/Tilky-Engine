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

# ============================================================
# GLAD
# ============================================================

add_library(glad STATIC
        glad/src/glad.c
)

target_include_directories(glad PUBLIC
        glad/include
)

target_link_libraries(glad PUBLIC
        OpenGL::GL
)

# ============================================================
# Dear ImGui
# External UI library. Do not put this directly into executables.
# ============================================================

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/External/imgui/imgui.h")
    add_library(imgui STATIC
            External/imgui/imgui.cpp
            External/imgui/imgui_draw.cpp
            External/imgui/imgui_tables.cpp
            External/imgui/imgui_widgets.cpp
            External/imgui/imgui_demo.cpp

            External/imgui/misc/cpp/imgui_stdlib.cpp

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

# ============================================================
# Shared project system
# Used by launcher and engine.
# ============================================================

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

# ============================================================
# Shared localisation system
# Used by launcher and engine.
# ============================================================

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

# ============================================================
# Engine source list
# Every engine file goes here.
# No EngineMain.cpp.
# No launcher files.
# No external library source files.
# ============================================================

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

        src/Objects/Entity.cpp
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
        Headers/Objects/EntityTypes.hpp
        Headers/Objects/Entity.hpp
        Headers/Objects/Level.hpp
        Headers/Objects/Components.hpp
        Headers/Objects/Sector.hpp
        Headers/Objects/Wall.hpp
        Headers/Map/MapQueries.hpp
        Headers/Map/LevelManager.hpp
        Headers/Math/Geometry/Geometry.hpp
)

# ============================================================
# Engine core library
# This compiles all engine files.
# ============================================================

add_library(Wolfy_EngineCore STATIC
        ${WOLFY_ENGINE_SOURCES}
)

target_precompile_headers(Wolfy_EngineCore PRIVATE
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

target_include_directories(Wolfy_EngineCore PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        src/MapEditor
)

target_link_libraries(Wolfy_EngineCore PUBLIC
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

target_compile_definitions(Wolfy_EngineCore PUBLIC
        WOLFY_CONTENT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
)

if (WOLFY_USE_IMGUI)
    target_compile_definitions(Wolfy_EngineCore PUBLIC WOLFY_USE_IMGUI=1)
else()
    target_compile_definitions(Wolfy_EngineCore PUBLIC WOLFY_USE_IMGUI=0)
endif()

# ============================================================
# Launcher source list
# Only launcher-specific files go here.
# No engine renderer/editor files.
# ============================================================

set(WOLFY_LAUNCHER_SOURCES
        src/Launcher/LauncherApp.cpp
        Headers/Launcher/LauncherApp.hpp
)

# ============================================================
# Launcher core library
# This compiles only launcher files.
# ============================================================

add_library(Wolfy_LauncherCore STATIC
        ${WOLFY_LAUNCHER_SOURCES}
)

target_include_directories(Wolfy_LauncherCore PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(Wolfy_LauncherCore PUBLIC
        SDL3::SDL3
        SDL3::Headers
        $<IF:$<TARGET_EXISTS:SDL3_image::SDL3_image-shared>,SDL3_image::SDL3_image-shared,SDL3_image::SDL3_image-static>
        imgui
        Wolfy_Project
        Wolfy_Local
        nlohmann_json::nlohmann_json
)

target_compile_definitions(Wolfy_LauncherCore PUBLIC
        WOLFY_CONTENT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
)

# ============================================================
# Runnable 1: Wolfy_Launcher
# ONLY launcher executable.
# Compiles LauncherMain.cpp + launcher-related library code.
# Does not compile engine files.
# ============================================================

add_executable(Wolfy_Launcher
        src/Launcher/LauncherMain.cpp
)

target_link_libraries(Wolfy_Launcher PRIVATE
        Wolfy_LauncherCore
)

# ============================================================
# Runnable 2: Wolfy_Engine
# ONLY engine executable.
# Compiles EngineMain.cpp + all engine files.
# Does not compile launcher files.
# ============================================================

add_executable(Wolfy_Engine
        src/EngineApp/EngineMain.cpp
)

target_link_libraries(Wolfy_Engine PRIVATE
        Wolfy_EngineCore
)

# ============================================================
# Runnable 3: Wolfy_All
# Launcher-style executable that also builds/links every engine file.
# Does not directly list ImGui, SDL, GLAD, etc. source files.
# ============================================================

add_executable(Wolfy_All
        src/Launcher/LauncherMain.cpp
)

target_link_libraries(Wolfy_All PRIVATE
        Wolfy_LauncherCore
        Wolfy_EngineCore
)

add_dependencies(Wolfy_All
        Wolfy_Launcher
        Wolfy_Engine
)