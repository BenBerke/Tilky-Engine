cmake_minimum_required(VERSION 4.1)

project(Tilky_Engine LANGUAGES C CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

option(TILKY_USE_IMGUI "Enable Dear ImGui editor UI" ON)
option(TILKY_ENABLE_TRACY "Enable Tracy profiler" ON)

# ============================================================
# Unity / Jumbo build
# Speeds up full rebuilds by compiling batches of .cpp files together.
# Keep OFF by default because unity builds can expose hidden ODR/name clashes.
# Enable with:
#   cmake -S . -B build -DTILKY_ENABLE_UNITY_BUILD=ON
# ============================================================

option(TILKY_ENABLE_UNITY_BUILD "Enable CMake unity/jumbo builds for Tilky project targets" OFF)

set(TILKY_UNITY_BUILD_BATCH_SIZE "8" CACHE STRING
        "Maximum number of source files per generated unity translation unit"
)

function(tilky_configure_unity_build target_name)
    if(NOT TARGET ${target_name})
        message(FATAL_ERROR "tilky_configure_unity_build called for missing target: ${target_name}")
    endif()

    set_target_properties(${target_name} PROPERTIES
            UNITY_BUILD ${TILKY_ENABLE_UNITY_BUILD}
            UNITY_BUILD_MODE BATCH
            UNITY_BUILD_BATCH_SIZE ${TILKY_UNITY_BUILD_BATCH_SIZE}
            UNITY_BUILD_UNIQUE_ID TILKY_UNITY_BUILD_ID
    )
endfunction()

find_package(OpenGL REQUIRED)
find_package(OpenAL REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(SDL3 REQUIRED CONFIG COMPONENTS SDL3 Headers)
find_package(SDL3_image REQUIRED CONFIG)
find_package(SDL3_ttf REQUIRED CONFIG)
find_package(Freetype REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(sol2 CONFIG REQUIRED)
find_package(Lua REQUIRED)

# ============================================================
# GLAD
# ============================================================

add_library(glad STATIC
        External/glad/src/glad.c
)

target_include_directories(glad PUBLIC
        External/glad/include
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
# Tracy Profiler
# ============================================================

set(TRACY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/External/tracy)

add_library(TracyClient STATIC
        ${TRACY_DIR}/public/TracyClient.cpp
)

target_include_directories(TracyClient PUBLIC
        ${TRACY_DIR}/public
)

target_compile_definitions(TracyClient PUBLIC TRACY_ENABLE)

target_link_libraries(TracyClient PUBLIC
        ws2_32
        wsock32
        dbghelp
        secur32
)

# ============================================================
# Shared project system
# Used by launcher and engine.
# ============================================================

add_library(Tilky_Project STATIC
        src/Project/ProjectManager.cpp
        Headers/Project/ProjectManager.hpp
)

target_include_directories(Tilky_Project PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(Tilky_Project PUBLIC
        SDL3::SDL3
        SDL3::Headers
        spdlog::spdlog
        nlohmann_json::nlohmann_json
)

target_compile_definitions(Tilky_Project PUBLIC
        TILKY_CONTENT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
)

tilky_configure_unity_build(Tilky_Project)

# ============================================================
# Shared localisation system
# Used by launcher and engine.
# ============================================================

add_library(Tilky_Local STATIC
        src/Engine/Local/Local.cpp
        Headers/Engine/Local/Local.hpp
)

target_include_directories(Tilky_Local PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(Tilky_Local PUBLIC
        SDL3::SDL3
        SDL3::Headers
        spdlog::spdlog
        nlohmann_json::nlohmann_json
)

target_compile_definitions(Tilky_Local PUBLIC
        TILKY_CONTENT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
)

tilky_configure_unity_build(Tilky_Local)

# ============================================================
# Engine source list
# Every engine file goes here.
# No EngineMain.cpp.
# No launcher files.
# No external library source files.
# ============================================================

set(TILKY_ENGINE_SOURCES
        src/Runtime/Renderer/OpenGL/OpenGLTexture.cpp

        src/Editor/MapEditor/MapEditorDrawing.cpp
        src/Editor/MapEditor/MapEditorGeometry.cpp
        src/Editor/MapEditor/MapEditorUI.cpp
        src/Editor/MapEditor/MapEditorInput.cpp

        src/Editor/Editor.cpp
        src/Editor/EditorState.cpp
        src/Editor/EditorSaveLoad.cpp

        src/Engine/GameTime.cpp
        src/Engine/InputManager.cpp

        src/Objects/Entity.cpp
        src/Runtime/PlayerControllerSystem.cpp

        src/Runtime/Renderer/RendererMath.cpp
        src/Runtime/Renderer/OpenGL/OpenGlInit.cpp
        src/Runtime/Renderer/OpenGL/OpenGLText.cpp
        src/Runtime/Renderer/OpenGL/OpenGLMapBuild.cpp
        src/Runtime/Renderer/OpenGL/OpenGLSprite.cpp
        src/Runtime/Renderer/OpenGL/OpenGLUpdate.cpp
        src/Runtime/Renderer/OpenGL/OpenGLDestroy.cpp
        src/Runtime/Renderer/OpenGL/OpenGLDecal.cpp
        src/Runtime/Renderer/OpenGL/OpenGLSector.cpp
        src/Runtime/Renderer/OpenGL/OpenGLBackground.cpp
        src/Runtime/Renderer/OpenGL/OpenGLUI.cpp

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
        src/Runtime/Sound/SoundManager.cpp
        Headers/Runtime/Sound/SoundManager.hpp
        Headers/Objects/Loadables.hpp
        src/Runtime/RuntimeSession.cpp
        Headers/Runtime/RuntimeSession.hpp
        src/Runtime/Sound/AudioSystem.cpp
        Headers/Runtime/Sound/AudioSystem.hpp
        Headers/Objects/Wrappers.hpp
        src/Runtime/ScriptSystem.cpp
        Headers/Runtime/ScriptSystem.hpp
        src/Editor/UIEditor/UIEditorUI.cpp
        src/Editor/UIEditor/UIEditorInput.cpp
        src/Editor/UIEditor/UiEditorDraw.cpp
        src/Editor/EditorTextureCache.cpp
        Headers/Editor/EditorTextureCache.hpp
        Headers/Runtime/Renderer/IRenderer.hpp
        Headers/Runtime/Renderer/RendererBackend.hpp
        Headers/Runtime/Renderer/RendererFactory.hpp
        Headers/Runtime/Renderer/OpenGL/OpenGL.hpp
        Headers/Runtime/Renderer/Vulkan/VulkanRenderer.hpp
        src/Runtime/Renderer/RendererFactory.cpp
        src/Runtime/Renderer/OpenGL/OpenGL.cpp
        src/Runtime/Renderer/Vulkan/VulkanRenderer.cpp
        Headers/Runtime/Renderer/RendererMath.hpp
        Headers/UISystem.hpp
        src/UISytem.cpp
        Headers/Runtime/CameraSystem.hpp
        src/Runtime/LevelSystem.cpp
        Headers/Runtime/LevelSystem.hpp
        src/Objects/Components.cpp
        src/Objects/Level.cpp
)

# ============================================================
# Engine core library
# This compiles all engine files.
# ============================================================

add_library(Tilky_EngineCore STATIC
        ${TILKY_ENGINE_SOURCES}
)

target_precompile_headers(Tilky_EngineCore PRIVATE
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

target_include_directories(Tilky_EngineCore PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        src/MapEditor
        ${LUA_INCLUDE_DIR}
)

target_link_libraries(Tilky_EngineCore PUBLIC
        SDL3::SDL3
        spdlog::spdlog
        SDL3::Headers
        OpenAL::OpenAL
        $<IF:$<TARGET_EXISTS:SDL3_image::SDL3_image-shared>,SDL3_image::SDL3_image-shared,SDL3_image::SDL3_image-static>
        SDL3_ttf::SDL3_ttf
        Freetype::Freetype
        glad
        imgui
        Tilky_Project
        Tilky_Local
        nlohmann_json::nlohmann_json
        sol2::sol2
        ${LUA_LIBRARIES}
        TracyClient
)

target_compile_definitions(Tilky_EngineCore PUBLIC
        TILKY_CONTENT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
)

if (TILKY_USE_IMGUI)
    target_compile_definitions(Tilky_EngineCore PUBLIC TILKY_USE_IMGUI=1)
else()
    target_compile_definitions(Tilky_EngineCore PUBLIC TILKY_USE_IMGUI=0)
endif()

tilky_configure_unity_build(Tilky_EngineCore)

# ============================================================
# Launcher source list
# Only launcher-specific files go here.
# No engine renderer/editor files.
# ============================================================

set(TILKY_LAUNCHER_SOURCES
        src/Launcher/LauncherApp.cpp
        Headers/Launcher/LauncherApp.hpp
)

# ============================================================
# Launcher core library
# This compiles only launcher files.
# ============================================================

add_library(Tilky_LauncherCore STATIC
        ${TILKY_LAUNCHER_SOURCES}
)

target_include_directories(Tilky_LauncherCore PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(Tilky_LauncherCore PUBLIC
        SDL3::SDL3
        spdlog::spdlog
        SDL3::Headers
        $<IF:$<TARGET_EXISTS:SDL3_image::SDL3_image-shared>,SDL3_image::SDL3_image-shared,SDL3_image::SDL3_image-static>
        imgui
        Tilky_Project
        Tilky_Local
        nlohmann_json::nlohmann_json
)

target_compile_definitions(Tilky_LauncherCore PUBLIC
        TILKY_CONTENT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
)

tilky_configure_unity_build(Tilky_LauncherCore)

set(TILKY_ICON_OBJECTS "")

if(WIN32)
    set(TILKY_ICON_PATH
            "${CMAKE_CURRENT_SOURCE_DIR}/LauncherAssets/Fox.ico"
    )

    if(NOT EXISTS "${TILKY_ICON_PATH}")
        message(FATAL_ERROR "Missing icon file: ${TILKY_ICON_PATH}")
    endif()

    set(TILKY_ICON_PATH_RC "${TILKY_ICON_PATH}")
    string(REPLACE "\\" "/" TILKY_ICON_PATH_RC "${TILKY_ICON_PATH_RC}")

    configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/TilkyIcon.rc.in"
            "${CMAKE_CURRENT_BINARY_DIR}/TilkyIcon.generated.rc"
            @ONLY
    )

    add_library(Tilky_Icon OBJECT
            "${CMAKE_CURRENT_BINARY_DIR}/TilkyIcon.generated.rc"
    )

    set(TILKY_ICON_OBJECTS $<TARGET_OBJECTS:Tilky_Icon>)
endif()

# ============================================================
# Runnable 1: Tilky_Launcher
# ONLY launcher executable.
# Compiles LauncherMain.cpp + launcher-related library code.
# Does not compile engine files.
# ============================================================

add_executable(Tilky_Launcher
        src/Launcher/LauncherMain.cpp
        ${TILKY_ICON_OBJECTS}
)

target_link_libraries(Tilky_Launcher PRIVATE
        Tilky_LauncherCore
)

# ============================================================
# Runnable 2: Tilky_Engine
# ONLY engine executable.
# Compiles EngineMain.cpp + all engine files.
# Does not compile launcher files.
# ============================================================

add_executable(Tilky_Engine
        src/EngineApp/EngineMain.cpp
        ${TILKY_ICON_OBJECTS}
)

target_link_libraries(Tilky_Engine PRIVATE
        Tilky_EngineCore
)

# ============================================================
# Runnable 3: Tilky_All
# Launcher-style executable that also builds/links every engine file.
# Does not directly list ImGui, SDL, GLAD, etc. source files.
# ============================================================

add_executable(Tilky_All
        src/Launcher/LauncherMain.cpp
        ${TILKY_ICON_OBJECTS}
)

target_link_libraries(Tilky_All PRIVATE
        Tilky_LauncherCore
        Tilky_EngineCore
)

add_dependencies(Tilky_All
        Tilky_Launcher
        Tilky_Engine
)


# ============================================================
# Runtime assets
# Copy folders needed by the .exe into the build output folder.
# ============================================================

set(TILKY_RUNTIME_ASSET_DIRS
        EngineAssets
        LauncherAssets
        Shaders
        Fonts
        Textures
)

function(copy_runtime_assets target_name)
    foreach(asset_dir IN LISTS TILKY_RUNTIME_ASSET_DIRS)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${asset_dir}")
            add_custom_command(TARGET ${target_name} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CMAKE_CURRENT_SOURCE_DIR}/${asset_dir}"
                    "$<TARGET_FILE_DIR:${target_name}>/${asset_dir}"
                    COMMENT "Copying ${asset_dir} to $<TARGET_FILE_DIR:${target_name}>"
            )
        else()
            message(STATUS "Skipping missing runtime asset folder: ${asset_dir}")
        endif()
    endforeach()
endfunction()

copy_runtime_assets(Tilky_Launcher)
copy_runtime_assets(Tilky_Engine)
copy_runtime_assets(Tilky_All)

# ============================================================
# Runtime DLLs
# Copy SDL/vcpkg DLLs beside the .exe on Windows.
# ============================================================

function(copy_runtime_dlls target_name)
    if(WIN32)
        add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:${target_name}>
                $<TARGET_FILE_DIR:${target_name}>
                COMMAND_EXPAND_LISTS
                COMMENT "Copying runtime DLLs for ${target_name}"
        )
    endif()
endfunction()

# ============================================================
# MinGW runtime DLLs
# Copy the exact C++ runtime DLLs used by the current compiler.
# This avoids loading the wrong libstdc++-6.dll from PATH.
# ============================================================

if(WIN32 AND MINGW)
    get_filename_component(MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)

    function(copy_mingw_runtime_dlls target_name)
        foreach(runtime_dll
                libstdc++-6.dll
                libgcc_s_seh-1.dll
                libwinpthread-1.dll
        )
            if(EXISTS "${MINGW_BIN_DIR}/${runtime_dll}")
                add_custom_command(TARGET ${target_name} POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${MINGW_BIN_DIR}/${runtime_dll}"
                        "$<TARGET_FILE_DIR:${target_name}>"
                        COMMENT "Copying ${runtime_dll} for ${target_name}"
                )
            else()
                message(WARNING "Could not find MinGW runtime DLL: ${MINGW_BIN_DIR}/${runtime_dll}")
            endif()
        endforeach()
    endfunction()

    copy_mingw_runtime_dlls(Tilky_Launcher)
    copy_mingw_runtime_dlls(Tilky_Engine)
    copy_mingw_runtime_dlls(Tilky_All)
endif()

# ============================================================
# Hide console window in Release builds on Windows
# ============================================================

function(make_gui_in_release target_name)
    if(WIN32)
        set_target_properties(${target_name} PROPERTIES
                WIN32_EXECUTABLE "$<CONFIG:Release>"
        )
    endif()
endfunction()

make_gui_in_release(Tilky_Launcher)
#make_gui_in_release(Tilky_Engine)
#make_gui_in_release(Tilky_All)

copy_runtime_dlls(Tilky_Launcher)
copy_runtime_dlls(Tilky_Engine)
copy_runtime_dlls(Tilky_All)