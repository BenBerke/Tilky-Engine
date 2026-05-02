cmake_minimum_required(VERSION 4.1)
project(Wolfy_Engine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenGL REQUIRED)
find_package(SDL3 REQUIRED CONFIG COMPONENTS SDL3 Headers)
find_package(SDL3_image REQUIRED CONFIG)
find_package(SDL3_ttf REQUIRED CONFIG)
find_package(Freetype REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

option(WOLFY_USE_IMGUI "Enable Dear ImGui editor UI" ON)

add_library(glad STATIC
        glad/src/glad.c
        src/Renderer/Renderer/RendererInternal.hpp
)

target_include_directories(glad PUBLIC
        glad/include
)

target_link_libraries(glad PUBLIC
        OpenGL::GL
)

add_executable(Wolfy_Engine
        main.cpp

        src/Renderer/TextureManager.cpp

        src/Renderer/MapEditor/MapEditor.cpp
        src/Renderer/MapEditor/MapEditorState.cpp
        src/Renderer/MapEditor/MapEditorGeometry.cpp
        src/Renderer/MapEditor/MapEditorDrawing.cpp
        src/Renderer/MapEditor/MapEditorInput.cpp
        src/Renderer/MapEditor/MapEditorUI.cpp
        src/Renderer/MapEditor/MapEditorSaveLoad.cpp

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
        Headers/Objects/Entity.hpp
        src/Renderer/Renderer/RendererDecals.cpp
        src/Renderer/Renderer/RendererSectors.cpp
        config.h
        Headers/Math/Geometry/Geometry.h
        src/Math/Geometry/Geometry.cpp
        Headers/Map/MapQueries.hpp
        src/Map/MapQueries.cpp
        src/Renderer/Renderer/RenderBackground.cpp
        src/Renderer/Renderer/RendererUI.cpp
        Headers/Objects/Level.hpp
        Headers/Map/LevelManager.hpp
        src/Map/LevelManager.cpp
        Headers/Objects/Components.hpp
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
        <SDL3/SDL.h>
        <nlohmann/json.hpp>
)

target_include_directories(Wolfy_Engine PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/MapEditor
)

target_link_libraries(Wolfy_Engine PRIVATE
        SDL3::SDL3
        SDL3::Headers
        $<IF:$<TARGET_EXISTS:SDL3_image::SDL3_image-shared>,SDL3_image::SDL3_image-shared,SDL3_image::SDL3_image-static>
        SDL3_ttf::SDL3_ttf
        Freetype::Freetype
        glad
        nlohmann_json::nlohmann_json
)

if (WOLFY_USE_IMGUI AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/External/imgui/imgui.h")
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

    target_link_libraries(Wolfy_Engine PRIVATE imgui)
    target_compile_definitions(Wolfy_Engine PRIVATE WOLFY_USE_IMGUI=1)
else()
    target_compile_definitions(Wolfy_Engine PRIVATE WOLFY_USE_IMGUI=0)
endif()