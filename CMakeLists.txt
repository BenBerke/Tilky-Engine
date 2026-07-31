cmake_minimum_required(VERSION 4.1)

project(Tilky_Engine LANGUAGES C CXX RC)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# Prevent Windows.h min/max macros from colliding with std::min / std::max
if(WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

option(TILKY_USE_IMGUI "Enable Dear ImGui editor UI" ON)

# ============================================================
# Build type / Tracy default
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
# Optimizations
# ============================================================

option(TILKY_ENABLE_LTO "Enable link-time optimization in optimized builds" ON)
option(TILKY_ENABLE_UNITY_BUILD "Enable CMake unity/jumbo builds for Tilky project targets" OFF)

set(TILKY_UNITY_BUILD_BATCH_SIZE "8" CACHE STRING
        "Maximum number of source files per generated unity translation unit"
)

include(CheckIPOSupported)

if(TILKY_ENABLE_LTO)
    check_ipo_supported(RESULT TILKY_IPO_SUPPORTED OUTPUT TILKY_IPO_ERROR)

    if(TILKY_IPO_SUPPORTED)
        set(TILKY_IPO_ENABLED ON)
    else()
        set(TILKY_IPO_ENABLED OFF)
        message(WARNING "IPO/LTO not supported: ${TILKY_IPO_ERROR}")
    endif()
endif()

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

# ============================================================
# Packages
# ============================================================

find_package(OpenGL REQUIRED)
find_package(OpenAL CONFIG REQUIRED)
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
# ============================================================

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/External/imgui/imgui.h")
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
# ImGui text editor
# ============================================================

add_library(imgui_text_editor STATIC
        External/ImGuiColorTextEdit/TextEditor.cpp
)

target_include_directories(imgui_text_editor PUBLIC
        External/ImGuiColorTextEdit
)

target_link_libraries(imgui_text_editor PUBLIC
        imgui
)

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
# ============================================================

set(TILKY_RUNTIME_SOURCES
        src/Runtime/Renderer/OpenGL/OpenGLTexture.cpp

        src/Engine/GameTime.cpp
        src/Engine/InputManager.cpp

        src/Objects/Entity.cpp
        src/Objects/Components.cpp
        src/Objects/Level.cpp

        src/Runtime/Gameplay/PlayerControllerSystem.cpp
        src/Runtime/Gameplay/GameFunctions.cpp

        src/Runtime/RuntimeSession.cpp
        src/Runtime/LevelSystem.cpp

        src/Runtime/Sound/SoundManager.cpp
        src/Runtime/Sound/AudioSystem.cpp

        src/Runtime/Scripting/Lua/LuaWallBindings.cpp
        src/Runtime/Scripting/Lua/LuaSectorBindings.cpp

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
        src/Runtime/Renderer/OpenGL/OpenGLCollider.cpp
        src/Runtime/Renderer/OpenGL/OpenGL.cpp
        src/Runtime/Renderer/Vulkan/VulkanRenderer.cpp

        src/Runtime/PhysicsSystem.cpp

        src/Runtime/Scripting/Lua/LuaSystem.cpp
        src/Runtime/Scripting/Lua/LuaVectorBindings.cpp
        src/Runtime/Scripting/Lua/LuaComponentBindings.cpp
        src/Runtime/Scripting/Lua/LuaEntityBindings.cpp
        src/Runtime/Scripting/Lua/LuaInputBindings.cpp
        src/Runtime/Scripting/Lua/LuaMathBindings.cpp
        src/Runtime/Scripting/Lua/LuaEditorFunctionBindings.cpp
        src/Runtime/Scripting/Lua/LuaGameBindings.cpp

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
        Headers/Objects/LuaWrappers.hpp

        Headers/Map/LevelManager.hpp
        Headers/Map/LevelSerialization.hpp
        Headers/Map/MapQueries.hpp

        Headers/Math/Geometry/Geometry.hpp
        Headers/Math/Vector/Vector2Math.hpp
        Headers/Math/Vector/Vector3Math.hpp

        Headers/Runtime/Sound/SoundManager.hpp
        Headers/Runtime/Sound/AudioSystem.hpp
        Headers/Runtime/RuntimeSession.hpp
        Headers/Runtime/LevelSystem.hpp
        Headers/Runtime/Scripting/Lua/LuaScripting.hpp
        Headers/Runtime/Scripting/CSharp/CSharpScripting.hpp
        Headers/Runtime/Gameplay/CameraSystem.hpp
        Headers/Runtime/Gameplay/GameFunctions.hpp

        Headers/Runtime/Renderer/IRenderer.hpp
        Headers/Runtime/Renderer/RendererBackend.hpp
        Headers/Runtime/Renderer/RendererFactory.hpp
        Headers/Runtime/Renderer/RendererMath.hpp
        Headers/Runtime/Renderer/OpenGL/OpenGL.hpp
        Headers/Runtime/Renderer/Vulkan/VulkanRenderer.hpp

        Headers/Runtime/PhysicsSystem.hpp
        Headers/UISystem.hpp
)

# ============================================================
# Build-generated unified executable sources
# Repository source files are not modified.
# ============================================================

set(TILKY_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/Generated")
file(MAKE_DIRECTORY "${TILKY_GENERATED_DIR}")

# The editor currently starts tilky_exporter as a separate executable. Generate
# a build-tree copy that starts Tilky_Engine --export instead, keeping the final
# runtime directory limited to Tilky_Engine and Standalone.
set(TILKY_MAP_EDITOR_UI_SOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/MapEditor/MapEditorUI.cpp"
)
set(TILKY_GENERATED_MAP_EDITOR_UI
        "${TILKY_GENERATED_DIR}/MapEditorUI.generated.cpp"
)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${TILKY_MAP_EDITOR_UI_SOURCE}"
)

file(READ "${TILKY_MAP_EDITOR_UI_SOURCE}" TILKY_MAP_EDITOR_UI_CONTENT)

string(FIND "${TILKY_MAP_EDITOR_UI_CONTENT}"
        [=[engineBasePath / "tilky_exporter.exe"]=]
        TILKY_EXPORTER_WINDOWS_PATH_POSITION
)
string(FIND "${TILKY_MAP_EDITOR_UI_CONTENT}"
        [=[engineBasePath / "tilky_exporter"]=]
        TILKY_EXPORTER_UNIX_PATH_POSITION
)
string(FIND "${TILKY_MAP_EDITOR_UI_CONTENT}"
        [=[L"\"" + exporterExe.wstring() + L"\" " +]=]
        TILKY_EXPORTER_WINDOWS_COMMAND_POSITION
)
string(FIND "${TILKY_MAP_EDITOR_UI_CONTENT}"
        [=[exporterExe.c_str(), exporterExe.c_str(),]=]
        TILKY_EXPORTER_UNIX_COMMAND_POSITION
)

if(TILKY_EXPORTER_WINDOWS_PATH_POSITION EQUAL -1 OR
        TILKY_EXPORTER_WINDOWS_COMMAND_POSITION EQUAL -1 OR
        TILKY_EXPORTER_UNIX_PATH_POSITION EQUAL -1 OR
        TILKY_EXPORTER_UNIX_COMMAND_POSITION EQUAL -1)
    message(FATAL_ERROR
            "MapEditorUI.cpp exporter code no longer matches the CMake-only integration"
    )
endif()

string(REPLACE
        [=[engineBasePath / "tilky_exporter.exe"]=]
        [=[engineBasePath / "Tilky_Engine.exe"]=]
        TILKY_MAP_EDITOR_UI_CONTENT
        "${TILKY_MAP_EDITOR_UI_CONTENT}"
)
string(REPLACE
        [=[engineBasePath / "tilky_exporter"]=]
        [=[engineBasePath / "Tilky_Engine"]=]
        TILKY_MAP_EDITOR_UI_CONTENT
        "${TILKY_MAP_EDITOR_UI_CONTENT}"
)
string(REPLACE
        [=[L"\"" + exporterExe.wstring() + L"\" " +]=]
        [=[L"\"" + exporterExe.wstring() + L"\" --export " +]=]
        TILKY_MAP_EDITOR_UI_CONTENT
        "${TILKY_MAP_EDITOR_UI_CONTENT}"
)
string(REPLACE
        [=[exporterExe.c_str(), exporterExe.c_str(),]=]
        [=[exporterExe.c_str(), exporterExe.c_str(), "--export",]=]
        TILKY_MAP_EDITOR_UI_CONTENT
        "${TILKY_MAP_EDITOR_UI_CONTENT}"
)

file(WRITE "${TILKY_GENERATED_MAP_EDITOR_UI}"
        "${TILKY_MAP_EDITOR_UI_CONTENT}"
)

# LauncherMain.cpp and EngineMain.cpp already contain their own main functions.
# Rename those functions for this target and provide one generated dispatcher.
set(TILKY_GENERATED_ENTRY
        "${TILKY_GENERATED_DIR}/TilkyUnifiedEntry.generated.cpp"
)

file(WRITE "${TILKY_GENERATED_ENTRY}" [=[
#include <cstring>

#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>

int TilkyLauncherMain(int argc, char** argv);
int TilkyEditorMain(int argc, char** argv);
int TilkyExporterMain(int argc, char** argv);

int main(const int argc, char** argv) {
    SDL_SetMainReady();

    if (argc > 1 && std::strcmp(argv[1], "--export") == 0)
        return TilkyExporterMain(argc - 1, argv + 1);

    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--project") == 0)
            return TilkyEditorMain(argc, argv);

    return TilkyLauncherMain(argc, argv);
}
]=])

# ============================================================
# Editor source list
# ============================================================

set(TILKY_EDITOR_SOURCES
        src/Editor/MapEditor/MapEditorDrawing.cpp
        src/Editor/MapEditor/MapEditorGeometry.cpp
        ${TILKY_GENERATED_MAP_EDITOR_UI}
        src/Editor/MapEditor/MapEditorInput.cpp

        src/Editor/ImGuiDrawFunctions.cpp

        src/Editor/Editor.cpp
        src/Editor/EditorState.cpp
        src/Editor/EditorTextureCache.cpp
        src/Editor/EditorSaveLoad.cpp
        src/Editor/AssetBrowser.cpp

        src/Map/MapTopology.cpp
        Headers/Map/MapTopology.hpp

        src/Runtime/RuntimeEditor/EditorFunctions.cpp
        src/Runtime/RuntimeEditor/RuntimeEditor.cpp

        src/Editor/UIEditor/UIEditorUI.cpp
        src/Editor/UIEditor/UIEditorInput.cpp
        src/Editor/UIEditor/UiEditorDraw.cpp

        Headers/Editor/AssetBrowser.hpp
        Headers/Editor/EditorTextureCache.hpp
        Headers/Editor/ImGuiDrawFunctions.hpp

        Headers/Runtime/RuntimeEditor/EditorFunctions.hpp
        Headers/Runtime/RuntimeEditor/RuntimeEditor.hpp

        Headers/Runtime/Gameplay/GameFunctions.hpp
        Headers/Objects/RayHit.hpp
        Headers/Map/LevelSerialization.hpp
        Headers/Runtime/PhysicsSystem.hpp
        Headers/Math/Constants.hpp
        Headers/Math/SIMD/SSECompat.hpp
        Headers/Objects/ComponentRegistry.hpp
        Headers/Objects/ScriptPublicType.hpp
)

# ============================================================
# Combined editor + runtime engine source list
# ============================================================

set(TILKY_ENGINE_SOURCES
        ${TILKY_RUNTIME_SOURCES}
        ${TILKY_EDITOR_SOURCES}
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
# Heavy sol2 binding files
# ============================================================

if(MINGW)
    set_source_files_properties(
            src/Runtime/Scripting/Lua/LuaVectorBindings.cpp
            src/Runtime/Scripting/Lua/LuaComponentBindings.cpp
            src/Runtime/Scripting/Lua/LuaEntityBindings.cpp
            src/Runtime/Scripting/Lua/LuaInputBindings.cpp
            src/Runtime/Scripting/Lua/LuaMathBindings.cpp

            PROPERTIES
            COMPILE_OPTIONS "-g0;-Wa,-mbig-obj;-fno-lto"
    )
endif()

# ============================================================
# Launcher source list
# ============================================================

set(TILKY_LAUNCHER_SOURCES
        src/Launcher/LauncherApp.cpp
        Headers/Launcher/LauncherApp.hpp
        Headers/Runtime/Scripting/IScripting.hpp
)

# ============================================================
# Launcher core library
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

    set(TILKY_GENERATED_ICON_RC
            "${CMAKE_CURRENT_BINARY_DIR}/TilkyIcon.generated.rc"
    )

    configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/TilkyIcon.rc.in"
            "${TILKY_GENERATED_ICON_RC}"
            @ONLY
    )

    # Recompile the resource whenever Fox.ico changes.
    set_source_files_properties(
            "${TILKY_GENERATED_ICON_RC}"
            PROPERTIES
            OBJECT_DEPENDS "${TILKY_ICON_PATH}"
    )

    add_library(Tilky_Icon OBJECT
            "${TILKY_GENERATED_ICON_RC}"
    )

    set(TILKY_ICON_OBJECTS
            $<TARGET_OBJECTS:Tilky_Icon>
    )
endif()

# ============================================================
# Shared engine executable configuration
# ============================================================

function(configure_tilky_engine_executable target_name)
    set_target_properties(${target_name} PROPERTIES CXX_SCAN_FOR_MODULES OFF)

    if(TILKY_ENABLE_LTO AND TILKY_IPO_ENABLED)
        set_target_properties(${target_name} PROPERTIES
                INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
                INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL ON
                INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON
        )
    endif()

    target_precompile_headers(${target_name} PRIVATE
            ${TILKY_ENGINE_PCH_HEADERS}
    )

    target_include_directories(${target_name} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            src/Editor
            src/Editor/MapEditor
            src/Editor/UIEditor
            ${LUA_INCLUDE_DIR}
    )

    target_link_libraries(${target_name} PRIVATE
            SDL3::SDL3
            SDL3::Headers
            spdlog::spdlog
            OpenAL::OpenAL
            $<IF:$<TARGET_EXISTS:SDL3_image::SDL3_image-shared>,SDL3_image::SDL3_image-shared,SDL3_image::SDL3_image-static>
            SDL3_ttf::SDL3_ttf
            Freetype::Freetype
            glad
            imgui
            imgui_text_editor
            Tilky_Project
            Tilky_Local
            nlohmann_json::nlohmann_json
            sol2::sol2
            ${LUA_LIBRARIES}
    )

    target_compile_options(${target_name} PRIVATE
            $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:GNU,Clang>>:-msse2>

            $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:GNU,Clang>,$<CONFIG:Debug>>:-Og;-g3>
            $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:GNU,Clang>,$<CONFIG:Release>>:-O3;-ffunction-sections;-fdata-sections>
            $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:GNU,Clang>,$<CONFIG:RelWithDebInfo>>:-O2;-g;-ffunction-sections;-fdata-sections>
            $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:GNU,Clang>,$<CONFIG:MinSizeRel>>:-Os;-ffunction-sections;-fdata-sections>

            $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/O2;/Gy>
            $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:MSVC>,$<CONFIG:RelWithDebInfo>>:/O2;/Zi;/Gy>
            $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:MSVC>,$<CONFIG:MinSizeRel>>:/O1;/Gy>
    )

    target_link_options(${target_name} PRIVATE
            $<$<AND:$<CXX_COMPILER_ID:GNU,Clang>,$<CONFIG:Release>>:-Wl,--gc-sections>
            $<$<AND:$<CXX_COMPILER_ID:GNU,Clang>,$<CONFIG:RelWithDebInfo>>:-Wl,--gc-sections>
            $<$<AND:$<CXX_COMPILER_ID:GNU,Clang>,$<CONFIG:MinSizeRel>>:-Wl,--gc-sections>

            $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/OPT:REF;/OPT:ICF>
            $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:RelWithDebInfo>>:/OPT:REF;/OPT:ICF>
            $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:MinSizeRel>>:/OPT:REF;/OPT:ICF>
    )

    if(TILKY_USE_IMGUI)
        target_compile_definitions(${target_name} PRIVATE TILKY_USE_IMGUI=1)
    else()
        target_compile_definitions(${target_name} PRIVATE TILKY_USE_IMGUI=0)
    endif()

    if(EXISTS "${TRACY_DIR}/public/tracy/Tracy.hpp")
        target_include_directories(${target_name} PRIVATE "${TRACY_DIR}/public")
    endif()

    if(TARGET TracyClient AND TILKY_ENABLE_TRACY)
        target_link_libraries(${target_name} PRIVATE TracyClient)
        target_compile_definitions(${target_name} PRIVATE TRACY_ENABLE)
    endif()

    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
        find_path(SSE2NEON_INCLUDE_DIR
                NAMES sse2neon.h
                PATH_SUFFIXES sse2neon
                REQUIRED
        )

        target_include_directories(${target_name} PRIVATE ${SSE2NEON_INCLUDE_DIR})
    endif()

    tilky_configure_unity_build(${target_name})
endfunction()

# ============================================================
# Unified executable entry points
# ============================================================

set_source_files_properties(
        "${TILKY_GENERATED_ENTRY}"
        PROPERTIES
        SKIP_PRECOMPILE_HEADERS ON
        SKIP_UNITY_BUILD_INCLUSION ON
)

set_source_files_properties(
        src/Launcher/LauncherMain.cpp
        PROPERTIES
        COMPILE_DEFINITIONS "SDL_MAIN_HANDLED;main=TilkyLauncherMain"
        SKIP_PRECOMPILE_HEADERS ON
        SKIP_UNITY_BUILD_INCLUSION ON
)

set_source_files_properties(
        src/EngineApp/EngineMain.cpp
        PROPERTIES
        COMPILE_DEFINITIONS "SDL_MAIN_HANDLED;main=TilkyEditorMain"
        SKIP_PRECOMPILE_HEADERS ON
        SKIP_UNITY_BUILD_INCLUSION ON
)

set_source_files_properties(
        Tools/TilkyExporter/TilkyExporter.cpp
        PROPERTIES
        COMPILE_DEFINITIONS "main=TilkyExporterMain"
        SKIP_PRECOMPILE_HEADERS ON
        SKIP_UNITY_BUILD_INCLUSION ON
)

set_source_files_properties(
        Tools/TilkyLocalise/TilkyLocalise.cpp
        PROPERTIES
        COMPILE_DEFINITIONS "main=TilkyLocaliseMain"
        SKIP_PRECOMPILE_HEADERS ON
        SKIP_UNITY_BUILD_INCLUSION ON
)

# Remove files left by the previous executable layout. These are not targets in
# this build and must not remain launchable beside the current binaries.
foreach(stale_executable
        Tilky_Launcher
        Tilky_All
        Tilky_Exporter
        tilky_exporter
        tilky_localise
)
    file(REMOVE
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${stale_executable}${CMAKE_EXECUTABLE_SUFFIX}"
    )
endforeach()

# ============================================================
# Runnable 1: Tilky_Engine
# No arguments: launcher
# --project <project.tilky>: editor
# --export <project.tilky> <output> <Standalone>: exporter
# ============================================================

add_executable(Tilky_Engine
        "${TILKY_GENERATED_ENTRY}"
        src/Launcher/LauncherMain.cpp
        src/EngineApp/EngineMain.cpp
        Tools/TilkyExporter/TilkyExporter.cpp
        ${TILKY_ENGINE_SOURCES}
        ${TILKY_ICON_OBJECTS}
)

configure_tilky_engine_executable(Tilky_Engine)

target_link_libraries(Tilky_Engine PRIVATE
        Tilky_LauncherCore
)

target_compile_definitions(Tilky_Engine PRIVATE
        TILKY_EDITOR=1
)

# ============================================================
# Runnable 2: Standalone
# Copied into exported projects by the integrated exporter.
# ============================================================

add_executable(Tilky_Standalone
        src/Standalone/StandaloneMain.cpp
        ${TILKY_ENGINE_SOURCES}
        ${TILKY_ICON_OBJECTS}
)

set_target_properties(Tilky_Standalone PROPERTIES
        CXX_SCAN_FOR_MODULES OFF
        OUTPUT_NAME "Standalone"
)

configure_tilky_engine_executable(Tilky_Standalone)

target_compile_definitions(Tilky_Standalone PRIVATE
        TILKY_STANDALONE=1
)

add_dependencies(Tilky_Engine Tilky_Standalone)

# Build group only. It does not create or launch an executable.
add_custom_target(Tilky_All)
add_dependencies(Tilky_All
        Tilky_Engine
        Tilky_Standalone
)

# ============================================================
# Runtime assets
# ============================================================

set(TILKY_RUNTIME_ASSET_DIRS
        EngineAssets
        LauncherAssets
        Shaders
        Fonts
        Textures
)

set(TILKY_RUNTIME_ASSET_COPY_COMMANDS "")

foreach(asset_dir IN LISTS TILKY_RUNTIME_ASSET_DIRS)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${asset_dir}")
        list(APPEND TILKY_RUNTIME_ASSET_COPY_COMMANDS
                COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/${asset_dir}"
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${asset_dir}"
        )
    else()
        message(STATUS "Skipping missing runtime asset folder: ${asset_dir}")
    endif()
endforeach()

add_custom_target(Tilky_RuntimeAssets
        ${TILKY_RUNTIME_ASSET_COPY_COMMANDS}
        COMMENT "Updating Tilky runtime assets"
        VERBATIM
)

add_dependencies(Tilky_Engine Tilky_RuntimeAssets)
add_dependencies(Tilky_Standalone Tilky_RuntimeAssets)


# ============================================================
# Runtime DLLs
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

    copy_mingw_runtime_dlls(Tilky_Engine)
    copy_mingw_runtime_dlls(Tilky_Standalone)

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

# keep Tilky_Engine console visible
make_gui_in_release(Tilky_Standalone)

copy_runtime_dlls(Tilky_Engine)
copy_runtime_dlls(Tilky_Standalone)