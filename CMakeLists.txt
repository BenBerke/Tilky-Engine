cmake_minimum_required(VERSION 4.1)

project(Tilky_Engine LANGUAGES C CXX RC)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

option(TILKY_USE_IMGUI "Enable Dear ImGui editor UI" ON)

# ============================================================
# Tracy default
# Debug: ON
# RelWithDebInfo: ON, useful for profiling optimized builds
# Release / MinSizeRel: forced OFF
# ============================================================

string(TOUPPER "${CMAKE_BUILD_TYPE}" TILKY_BUILD_TYPE_UPPER)

set(TRACY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/External/tracy")
set(TRACY_CLIENT_CPP "${TRACY_DIR}/public/TracyClient.cpp")

if(TILKY_BUILD_TYPE_UPPER STREQUAL "RELEASE"
        OR TILKY_BUILD_TYPE_UPPER STREQUAL "MINSIZEREL")
    set(TILKY_ENABLE_TRACY OFF CACHE BOOL "Enable Tracy profiler" FORCE)
else()
    option(TILKY_ENABLE_TRACY "Enable Tracy profiler" ON)
endif()

message(STATUS "TILKY_ENABLE_TRACY = ${TILKY_ENABLE_TRACY}")

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

    target_precompile_headers(imgui PRIVATE
            <cstdint>
            <cstdlib>
            <cstring>
            <cmath>
            <algorithm>
            <string>
            <vector>
    )
else()
    message(FATAL_ERROR "Dear ImGui not found at External/imgui/imgui.h")
endif()

# ============================================================
# Tracy Profiler
# ============================================================

if(TILKY_ENABLE_TRACY)
    if(EXISTS "${TRACY_CLIENT_CPP}")
        add_library(TracyClient STATIC
                "${TRACY_CLIENT_CPP}"
        )

        target_include_directories(TracyClient PUBLIC
                "${TRACY_DIR}/public"
        )

        target_compile_definitions(TracyClient PUBLIC
                TRACY_ENABLE
        )

        if(WIN32)
            target_link_libraries(TracyClient PUBLIC
                    ws2_32
                    wsock32
                    dbghelp
                    secur32
            )
        endif()
    else()
        message(WARNING "Tracy is enabled, but TracyClient.cpp was not found at: ${TRACY_CLIENT_CPP}")
        message(WARNING "Disabling TracyClient target. Either add External/tracy or configure with -DTILKY_ENABLE_TRACY=OFF")
        set(TILKY_ENABLE_TRACY OFF CACHE BOOL "Enable Tracy profiler" FORCE)
    endif()
endif()

# ============================================================
# Shared project system
# Used by launcher, editor, runtime, standalone, and exporter.
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
# Used by launcher, editor, runtime, standalone, and exporter.
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
# Runtime source list
# This is the editor-free engine/runtime part.
# Standalone.exe links this target, so do NOT put src/Editor/* here.
# ============================================================

set(TILKY_RUNTIME_SOURCES
        src/Runtime/Renderer/OpenGL/OpenGLTexture.cpp

        src/Engine/GameTime.cpp
        src/Engine/InputManager.cpp

        src/Objects/Entity.cpp
        src/Objects/Components.cpp
        src/Objects/Level.cpp

        src/Runtime/Gameplay/PlayerControllerSystem.cpp
        src/Runtime/RuntimeSession.cpp
        src/Runtime/ScriptSystem.cpp
        src/Runtime/LevelSystem.cpp

        src/Runtime/Sound/SoundManager.cpp
        src/Runtime/Sound/AudioSystem.cpp

        src/Runtime/Renderer/RendererMath.cpp
        src/Runtime/Renderer/RendererFactory.cpp
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
        src/Runtime/Renderer/OpenGL/OpenGL.cpp
        src/Runtime/Renderer/Vulkan/VulkanRenderer.cpp

        src/Math/Geometry/Geometry.cpp

        src/Map/LevelManager.cpp
        src/Map/LevelSerialization.cpp
        src/Map/MapQueries.cpp

        src/UISytem.cpp

        Headers/Objects/EntityTypes.hpp
        Headers/Objects/Entity.hpp
        Headers/Objects/Level.hpp
        Headers/Objects/Components.hpp
        Headers/Objects/Sector.hpp
        Headers/Objects/Wall.hpp
        Headers/Objects/Loadables.hpp
        Headers/Objects/Wrappers.hpp
        Headers/Map/LevelManager.hpp
        Headers/Map/LevelSerialization.hpp
        Headers/Map/MapQueries.hpp
        Headers/Map/LevelManager.hpp
        Headers/Math/Geometry/Geometry.hpp
        Headers/Math/Vector/Vector2Math.hpp
        Headers/Math/Vector/Vector3Math.hpp
        Headers/Runtime/Sound/SoundManager.hpp
        Headers/Runtime/Sound/AudioSystem.hpp
        Headers/Runtime/RuntimeSession.hpp
        Headers/Runtime/ScriptSystem.hpp
        Headers/Runtime/LevelSystem.hpp
        Headers/Runtime/Gameplay/CameraSystem.hpp
        Headers/Runtime/Renderer/IRenderer.hpp
        Headers/Runtime/Renderer/RendererBackend.hpp
        Headers/Runtime/Renderer/RendererFactory.hpp
        Headers/Runtime/Renderer/RendererMath.hpp
        Headers/Runtime/Renderer/OpenGL/OpenGL.hpp
        Headers/Runtime/Renderer/Vulkan/VulkanRenderer.hpp
        Headers/UISystem.hpp
        src/Runtime/Gameplay/GameFunctions.cpp
        src/Editor/ImGuiDrawFunctions.cpp
        src/Runtime/RuntimeEditor/EditorFunctions.cpp
        Headers/Runtime/RuntimeEditor/EditorFunctions.hpp
        src/Runtime/RuntimeEditor/RuntimeEditor.cpp
        Headers/Runtime/RuntimeEditor/RuntimeEditor.hpp
        src/Runtime/PhysicsSystem.cpp
        src/Runtime/Renderer/OpenGL/OpenGLCollider.cpp
)

# ============================================================
# Editor source list
# Editor-only files live here.
# Standalone.exe must never link this target.
# ============================================================

set(TILKY_EDITOR_SOURCES
        src/Editor/MapEditor/MapEditorDrawing.cpp
        src/Editor/MapEditor/MapEditorGeometry.cpp
        src/Editor/MapEditor/MapEditorUI.cpp
        src/Editor/MapEditor/MapEditorInput.cpp

        src/Editor/Editor.cpp
        src/Editor/EditorState.cpp
        src/Editor/EditorTextureCache.cpp

        src/Editor/UIEditor/UIEditorUI.cpp
        src/Editor/UIEditor/UIEditorInput.cpp
        src/Editor/UIEditor/UiEditorDraw.cpp

        Headers/Editor/EditorTextureCache.hpp
        Headers/Runtime/Gameplay/GameFunctions.hpp
        Headers/Objects/RayHit.hpp
        Headers/Editor/ImGuiDrawFunctions.hpp
        src/Editor/EditorSaveLoad.cpp
        Headers/Map/LevelSerialization.hpp
        Headers/Runtime/PhysicsSystem.hpp
        Headers/Math/Constants.hpp
        Headers/Math/SIMD/SSECompat.hpp
        Headers/Objects/ComponentRegistry.hpp
)

# ============================================================
# Common engine precompiled headers
# ============================================================

set(TILKY_ENGINE_PCH_HEADERS
        <vector>
        <string>
        <string_view>
        <array>
        <memory>
        <optional>
        <map>
        <unordered_map>
        <unordered_set>
        <algorithm>
        <cmath>
        <cstdint>
        <limits>
        <fstream>
        <filesystem>

        <SDL3/SDL.h>
        <nlohmann/json.hpp>
        <spdlog/spdlog.h>
)

# ============================================================
# Runtime core library
# This is the clean runtime target used by Standalone.exe and exporter.
# ============================================================

add_library(Tilky_RuntimeCore STATIC
        ${TILKY_RUNTIME_SOURCES}
)

set_target_properties(Tilky_RuntimeCore PROPERTIES CXX_SCAN_FOR_MODULES OFF)

target_precompile_headers(Tilky_RuntimeCore PRIVATE
        ${TILKY_ENGINE_PCH_HEADERS}
)

target_include_directories(Tilky_RuntimeCore PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${LUA_INCLUDE_DIR}
)

target_link_libraries(Tilky_RuntimeCore PUBLIC
        SDL3::SDL3
        SDL3::Headers
        spdlog::spdlog
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
)

target_compile_options(Tilky_RuntimeCore PRIVATE
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:GNU,Clang>>:-msse2>
)

if (TILKY_USE_IMGUI)
    target_compile_definitions(Tilky_RuntimeCore PUBLIC TILKY_USE_IMGUI=1)
else()
    target_compile_definitions(Tilky_RuntimeCore PUBLIC TILKY_USE_IMGUI=0)
endif()

# Keep Tracy headers visible so #include <tracy/Tracy.hpp> still works.
# Without TRACY_ENABLE, Tracy macros become no-op.
if(EXISTS "${TRACY_DIR}/public/tracy/Tracy.hpp")
    target_include_directories(Tilky_RuntimeCore PUBLIC "${TRACY_DIR}/public")
endif()

if(TARGET TracyClient AND TILKY_ENABLE_TRACY)
    target_link_libraries(Tilky_RuntimeCore PUBLIC TracyClient)
    target_compile_definitions(Tilky_RuntimeCore PUBLIC TRACY_ENABLE)
endif()

tilky_configure_unity_build(Tilky_RuntimeCore)

# ============================================================
# Editor core library
# Editor-only code. Links against runtime because editor manipulates levels,
# renderers, entities, assets, etc.
# ============================================================

add_library(Tilky_EditorCore STATIC
        ${TILKY_EDITOR_SOURCES}
)

set_target_properties(Tilky_EditorCore PROPERTIES CXX_SCAN_FOR_MODULES OFF)

target_precompile_headers(Tilky_EditorCore PRIVATE
        ${TILKY_ENGINE_PCH_HEADERS}
)

target_include_directories(Tilky_EditorCore PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        src/Editor
        src/Editor/MapEditor
        src/Editor/UIEditor
        ${LUA_INCLUDE_DIR}
)

target_link_libraries(Tilky_EditorCore PUBLIC
        Tilky_RuntimeCore
        imgui
)

target_compile_definitions(Tilky_EditorCore PUBLIC
        TILKY_EDITOR=1
        TILKY_STANDALONE=0
)

tilky_configure_unity_build(Tilky_EditorCore)

# ============================================================
# Engine core compatibility target
# Editor builds link this. It combines runtime + editor.
# Standalone builds should link Tilky_RuntimeCore directly.
# ============================================================

add_library(Tilky_EngineCore INTERFACE)

target_link_libraries(Tilky_EngineCore INTERFACE
        Tilky_RuntimeCore
        Tilky_EditorCore
)

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

set_target_properties(Tilky_LauncherCore PROPERTIES CXX_SCAN_FOR_MODULES OFF)

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

# ============================================================
# Windows icon
# ============================================================

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
# ============================================================

add_executable(Tilky_Launcher
        src/Launcher/LauncherMain.cpp
        ${TILKY_ICON_OBJECTS}
)

target_link_libraries(Tilky_Launcher PRIVATE
        Tilky_LauncherCore
)

set_target_properties(Tilky_Launcher PROPERTIES CXX_SCAN_FOR_MODULES OFF)

# ============================================================
# Runnable 2: Tilky_Engine
# Editor executable. Links runtime + editor through Tilky_EngineCore.
# ============================================================

add_executable(Tilky_Engine
        src/EngineApp/EngineMain.cpp
        ${TILKY_ICON_OBJECTS}
)

set_target_properties(Tilky_Engine PROPERTIES CXX_SCAN_FOR_MODULES OFF)

target_link_libraries(Tilky_Engine PRIVATE
        Tilky_EngineCore
)

target_compile_options(Tilky_Engine PRIVATE
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:GNU,Clang>>:-msse2>
)

target_compile_definitions(Tilky_Engine PRIVATE
        TILKY_EDITOR=1
        TILKY_STANDALONE=0
)

# ============================================================
# Runnable 3: Standalone
# Tilky_Standalone target outputs Standalone.exe. Links runtime only, no editor sources.
# Create src/Standalone/StandaloneMain.cpp if it does not exist yet.
# ============================================================

add_executable(Tilky_Standalone
        src/Standalone/StandaloneMain.cpp
        ${TILKY_ICON_OBJECTS}
)

target_compile_options(Tilky_Standalone PRIVATE
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:GNU,Clang>>:-msse2>
)

set_target_properties(Tilky_Standalone PROPERTIES
        CXX_SCAN_FOR_MODULES OFF
        OUTPUT_NAME "Standalone"
)

target_link_libraries(Tilky_Standalone PRIVATE
        Tilky_RuntimeCore
)

target_compile_definitions(Tilky_Standalone PRIVATE
        TILKY_EDITOR=0
        TILKY_STANDALONE=1
)

# ============================================================
# Runnable 4: Tilky_Exporter
# C++ exporter, if src/Exporter/ExporterMain.cpp exists.
# If you use the Rust exporter instead, this C++ target can be removed later.
# ============================================================

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/Exporter/ExporterMain.cpp")
    add_executable(Tilky_Exporter
            src/Exporter/ExporterMain.cpp
            ${TILKY_ICON_OBJECTS}
    )

    set_target_properties(Tilky_Exporter PROPERTIES CXX_SCAN_FOR_MODULES OFF)

    target_link_libraries(Tilky_Exporter PRIVATE
            Tilky_RuntimeCore
    )

    target_compile_definitions(Tilky_Exporter PRIVATE
            TILKY_EXPORTER=1
            TILKY_EDITOR=0
            TILKY_STANDALONE=0
    )
endif()

# ============================================================
# Runnable 5: Tilky_All
# Convenience target that builds launcher, editor, standalone, and exporter.
# ============================================================

add_executable(Tilky_All
        src/Launcher/LauncherMain.cpp
        ${TILKY_ICON_OBJECTS}
)

target_link_libraries(Tilky_All PRIVATE
        Tilky_LauncherCore
        Tilky_EngineCore
)

target_compile_options(Tilky_All PRIVATE
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:GNU,Clang>>:-msse2>
)

set_target_properties(Tilky_All PROPERTIES CXX_SCAN_FOR_MODULES OFF)

add_dependencies(Tilky_All
        Tilky_Launcher
        Tilky_Engine
        Tilky_Standalone
)

if(TARGET Tilky_Exporter)
    add_dependencies(Tilky_All Tilky_Exporter)
endif()

# ============================================================
# Runtime assets
# Copy folders needed by the .exe into the build output folder.
# For a final exported game, the Rust exporter will copy only the folders
# needed by the standalone player.
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
copy_runtime_assets(Tilky_Standalone)
copy_runtime_assets(Tilky_All)

if(TARGET Tilky_Exporter)
    copy_runtime_assets(Tilky_Exporter)
endif()

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
    copy_mingw_runtime_dlls(Tilky_Standalone)
    copy_mingw_runtime_dlls(Tilky_All)

    if(TARGET Tilky_Exporter)
        copy_mingw_runtime_dlls(Tilky_Exporter)
    endif()
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
make_gui_in_release(Tilky_Standalone)
#make_gui_in_release(Tilky_All)

copy_runtime_dlls(Tilky_Launcher)
copy_runtime_dlls(Tilky_Engine)
copy_runtime_dlls(Tilky_Standalone)
copy_runtime_dlls(Tilky_All)

if(TARGET Tilky_Exporter)
    copy_runtime_dlls(Tilky_Exporter)
endif()

# ============================================================
# Rust exporter tool
# Builds Tools/TilkyExporter with Cargo and copies it beside
# the editor executable.
# ============================================================

find_program(CARGO_EXECUTABLE cargo REQUIRED)

set(TILKY_RUST_EXPORTER_PACKAGE tilky_exporter)

set(TILKY_CARGO_TARGET_DIR
        "${CMAKE_BINARY_DIR}/cargo-target"
)

set(TILKY_RUST_PROFILE "debug")
set(TILKY_RUST_PROFILE_ARG "")

if(TILKY_BUILD_TYPE_UPPER STREQUAL "RELEASE"
        OR TILKY_BUILD_TYPE_UPPER STREQUAL "MINSIZEREL"
        OR TILKY_BUILD_TYPE_UPPER STREQUAL "RELWITHDEBINFO")
    set(TILKY_RUST_PROFILE "release")
    set(TILKY_RUST_PROFILE_ARG "--release")
endif()

if(WIN32)
    set(TILKY_RUST_EXPORTER_EXE
            "${TILKY_CARGO_TARGET_DIR}/${TILKY_RUST_PROFILE}/tilky_exporter.exe"
    )
else()
    set(TILKY_RUST_EXPORTER_EXE
            "${TILKY_CARGO_TARGET_DIR}/${TILKY_RUST_PROFILE}/tilky_exporter"
    )
endif()

add_custom_target(Tilky_RustExporter
        COMMAND ${CMAKE_COMMAND} -E env
        "CARGO_TARGET_DIR=${TILKY_CARGO_TARGET_DIR}"
        ${CARGO_EXECUTABLE} build ${TILKY_RUST_PROFILE_ARG} -p ${TILKY_RUST_EXPORTER_PACKAGE}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        BYPRODUCTS "${TILKY_RUST_EXPORTER_EXE}"
        COMMENT "Building Rust Tilky exporter"
        VERBATIM
)

add_custom_command(TARGET Tilky_RustExporter POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${TILKY_RUST_EXPORTER_EXE}"
        "$<TARGET_FILE_DIR:Tilky_Engine>/tilky_exporter.exe"
        COMMENT "Copying tilky_exporter.exe beside Tilky_Engine.exe"
)

add_dependencies(Tilky_Engine Tilky_RustExporter)
add_dependencies(Tilky_All Tilky_RustExporter)