cmake_minimum_required(VERSION 4.1)
project(Wolfy_Engine)

find_package(SDL3 REQUIRED CONFIG)
find_package(SDL3_image REQUIRED CONFIG)
find_package(SDL3_ttf REQUIRED CONFIG)
find_package(OpenGL REQUIRED)

set(CMAKE_CXX_STANDARD 20)

add_library(glad glad/src/glad.c
        Headers/Renderer/Shader.h)
target_include_directories(glad PUBLIC glad/include)
target_link_libraries(glad PUBLIC OpenGL::GL)

add_executable(Wolfy_Engine main.cpp
        src/Renderer/Renderer.cpp
        Headers/Renderer/Renderer.h
        Headers/Engine/GameTime.h
        Headers/Engine/InputManager.h
        Headers/Math/Vector.h
        src/Engine/GameTime.cpp
        src/Engine/InputManager.cpp)

target_link_libraries(Wolfy_Engine PRIVATE
        SDL3::SDL3
        $<IF:$<TARGET_EXISTS:SDL3_image::SDL3_image-shared>,SDL3_image::SDL3_image-shared,SDL3_image::SDL3_image-static>
        SDL3_ttf::SDL3_ttf
        glad
)