cmake_minimum_required(VERSION 4.1)
project(Wolfy_Engine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenGL REQUIRED)
find_package(SDL3 REQUIRED CONFIG COMPONENTS SDL3 Headers)
find_package(SDL3_image REQUIRED CONFIG)
find_package(SDL3_ttf REQUIRED CONFIG)
find_package(Freetype REQUIRED)

add_library(glad STATIC
        glad/src/glad.c
        Headers/Math/Matrix/Matrix4.h
        Headers/Math/Vector/Vector2Math.h
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
        Headers/Objects/Sector.h
        Headers/Renderer/TextureManager.h
        src/Renderer/TextureManager.cpp
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
)