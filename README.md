# Tilky Engine

Tilky Engine is a custom C++ 2.5D game engine inspired by classic sector-based engines.  
The project includes a standalone launcher, project management system, level editor, runtime renderer, localisation support, and an ECS system.

The goal of the project is to build a small but complete engine toolchain:

Launcher → Project System → Level Editor → Runtime

## Current Features
Launcher
Create and open Tilky Engine projects.
Stores projects inside the user’s documents folder.
Supports launcher-wide settings through a Launcher.tilky file.
Supports localisation/language switching.
Launches the engine with the selected project file.

## Project System
Each project contains its own metadata and assets:

```
Tilky Engine/  
└── Projects/  
    └── MyProject/  
        ├── project.tilky  
        └── Assets/  
            ├── Levels/  
            └── Textures/
```
The engine loads project-specific levels, textures, and metadata through the project manager.

## Level Editor
The editor is built using DearImGui and supports:

Dot placement
Wall creation
Sector creation
Floor/ceiling editing
Texture index editing
Entity placement
Component editing
Level save/load
Project-specific level files
Basic editor camera movement and zooming

## Entity/Component System

Tilky Engine uses a data-oriented ECS structure where entities own IDs and components are stored in separate component arrays.

Current component types include:

```
ComponentTransform
ComponentSprite
ComponentDecal
ComponentPlayerSpawn
```

This allows editor-side entity editing while keeping runtime systems cache-friendly by iterating over component storage.

## Renderer

The runtime renderer supports:

2.5D wall projection
Sector-based floors and ceilings
Textured walls
Floor/ceiling textures
Sprites
Wall decals
Depth handling
OpenGL shader-based rendering
Custom shaders with GLSL
SSBO-based geometry buffers

## Localisation

The launcher and editor use JSON-based localisation files stored in:
EngineAssets/Local/ 

## Tech Stack
```
C++20
CMake
SDL3
OpenGL
GLAD
Dear ImGui
SDL3_image
SDL3_ttf
FreeType
nlohmann/json
```

## Building
Requirements

You need:
```
A C++20-compatible compiler
CMake
SDL3
SDL3_image
SDL3_ttf
FreeType
nlohmann/json
OpenGL
GLAD
Dear ImGui
```

The project is currently developed on Windows using CLion and vcpkg.

## CMake Targets

The project has three main runnable targets:

**Wolfy_Launcher**
Builds and runs only the launcher.

**Wolfy_Engine**
Builds and runs only the engine/editor/runtime.

**Wolfy_All**
Builds the launcher and engine code together for development convenience.

## Basic Usage
Run the launcher.
Create or open a project.
The launcher starts the engine with the selected project.
Use the editor to create a level.
Use Save & Play to enter the runtime.

## Editor Controls
Current editor modes include:

Dot Mode
Wall Mode
Sector Mode
Entity Mode

```
General controls:
Q               Change editor mode
Left Mouse      Place/select/edit depending on mode
Middle Mouse    Pan editor camera
Mouse Wheel     Zoom editor view
Ctrl + Z        Undo recent editor action
Escape          Quit
```

## File Formats
Project File

Each project has a project.tilky file:
```
{
    "name": "MyProject",
    "assetsFolder": "Assets"
}
```

## Level File

Levels are saved as JSON inside:

Assets/Levels/

A level stores:
Walls
Sectors
Textures
Entities
Components
Level metadata 

## Current Status

Tilky Engine currently has a working launcher, project system, level editor, JSON save/load pipeline, entity/component editor, and OpenGL-based 2.5D renderer.

The project is still in development, so internal APIs and file formats may change.

## Roadmap
Roadmap:
```
Audio system with OpenAL
        |
Visual UI Editor
        |
Packaged builds -> Beta release on itch.io
        |
Improved editor workflow
        |
Runtime scripting with LUA
        |
Better documentation for engine architecture -> Release on Steam
        |
Networking Support
```

## License

This project is licensed under the **Apache License 2.0**.
See the LICENSE file for details.

Games made with Tilky Engine may be sold commercially. You do not need to open-source your game code just because you used the engine.

Assets, fonts, textures, and example project files may have separate licences where stated.

## Author

Created by Berke as a custom C++ game engine and editor project.

## Translations
Special thanks to:
    ```
    Ilya Brezhnev (Russian & Kazakh)
    ThatGuyMiki (Polish)
    ```
for helping to translate the Tilky Engine
