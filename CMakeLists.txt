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
)

target_include_directories(glad PUBLIC
        glad/include
)

target_link_libraries(glad PUBLIC
        OpenGL::GL
)

add_executable(Wolfy_Engine
        main.cpp
        src/Renderer/Renderer.cpp
        src/Engine/GameTime.cpp
        src/Engine/InputManager.cpp
        src/Objects/Player.cpp
        src/Renderer/MapEditor.cpp
        src/Renderer/TextureManager.cpp

        Headers/Objects/Sector.hpp
        Headers/Renderer/TextureManager.hpp
)

target_include_directories(Wolfy_Engine PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
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
    )

    target_include_directories(imgui PUBLIC
            External/imgui
            External/imgui/backends
    )

    target_link_libraries(imgui PUBLIC
            SDL3::SDL3
            SDL3::Headers
    )

    target_link_libraries(Wolfy_Engine PRIVATE imgui)
    target_compile_definitions(Wolfy_Engine PRIVATE WOLFY_USE_IMGUI=1)
else()
    target_compile_definitions(Wolfy_Engine PRIVATE WOLFY_USE_IMGUI=0)
endif()