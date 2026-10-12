# 🛠️ Tilky Engine 

**Tilky Engine** is a custom **sector-based 3D game engine, editor, and project toolchain** built in C++20.

Inspired by classic 90s engines such as *Doom* and *Build Engine*, Tilky Engine uses a specialized 3D rendering pipeline where maps are constructed from topological sectors, textured walls, floors, ceilings, slopes, sprites, and entities.

Tilky Engine is designed as a complete development environment rather than only a   rendering library. It includes a project launcher, visual level editor, asset browser, embedded Lua editor, OpenGL runtime, custom physics, OpenAL audio, engine version management, automated releases, localization tooling, profiling, and standalone project exporting.

## 🚀 Download Tilky Engine

The recommended way to install and use Tilky Engine is through **Tilky Engine** on Steam. The launcher handles project creation, engine installation, version switching, and opening projects with the correct engine version. No manual compilation is required! Alternatively, you can install from our GitHub repository.

[**Install Tilky Engine on Steam →**](https://store.steampowered.com/app/4956400/Tilky_Engine/?beta=0)

[**Download the Tilky Launcher from GitHub Releases →**](https://github.com/BenBerke/Tilky-Engine-Launcher/releases)

The source build instructions below are intended for contributors and developers modifying the engine itself.

## 🕹️ Editor & Engine Workflow

This minute long walkthrough demonstrates the UI layout, texture and sector workflows, asset pipeline, and real-time editor-to-runtime testing.

<p align="center">
  <a href="https://youtu.be/SaR1wiwHZIg">
    <img src="https://img.youtube.com/vi/SaR1wiwHZIg/maxresdefault.jpg" alt="Editor Workflow" width="700">
  </a>
</p>

## 🔬 Example Project Showcase

Doom's first level (E1M1) remade from scratch in Tilky Engine

<p align="center">
  <a href="https://www.youtube.com/watch?v=4jTljLHOTcw">
    <img src="https://img.youtube.com/vi/4jTljLHOTcw/maxresdefault.jpg" alt="But can it run DOOM?" width="700">
  </a>
</p>

Showcase levels that can be installed directly through the launcher

<p align="center">
  <a href="https://www.youtube.com/watch?v=sqfCeQslgs4">
    <img src="https://img.youtube.com/vi/sqfCeQslgs4/maxresdefault.jpg" alt="Showcase Project" width="700">
  </a>
</p>

<img width="1917" height="1171" alt="Screenshot 2026-10-11 214935" src="https://github.com/user-attachments/assets/d7a204d7-ad88-45d4-8078-8e2dfb74de77" />

---

## 🏗️ Core Architecture

### Sector-Based 3D Rendering

Unlike conventional triangle-mesh engines, Tilky Engine builds levels from connected sectors. Each sector can contain multiple independent floor and ceiling pairs, allowing stacked spaces, vertical level design, and room-based environments.

The sector architecture supports:

* **Sector-Based Worlds:** Maps are built from connected sectors, walls, surfaces, sprites, and entities.
* **True-Room-Over-Room** A single sector can contain multiple rooms stacked vertically in full 3D space. No trickery!
* **Sloped Surfaces:** Floors and ceilings support configurable slope directions and strengths.
* **True 3D Perspective:** Walls, floors, ceilings, sprites, and entities are rendered inside a complete 3D scene.
* **RGBA Surface Colors:** Walls, floors, ceilings, and sprites support color tinting and alpha transparency.
* **Upper and Lower Wall Textures:** Walls between sectors of different heights get separate top and bottom textures, each with its own scale, offset, and flipping.
* **Sector Lighting:** Every sector carries a light color that scripts can fade and flicker at runtime.
* **Sector Hierarchy:** Sectors can be organized into parent and child groups.
* **Skyboxes:** Each level can have a cubemap, panoramic, or classic DOOM-style cylinder sky.
* **3D Models:** `.glb`, `.gltf`, `.obj`, `.fbx`, `.dae`, `.3ds`, `.ply`, and `.stl` models load through Assimp, with embedded or external textures and backface culling.
* **Modern GPU Pipeline:** OpenGL shaders and SSBO-backed scene data are used for efficient rendering.

### Entity Component System

Tilky Engine uses a data-oriented entity component architecture based on entity IDs and type-specific component arrays.

Available systems include:

* Transform components with position, scale, and quaternion rotation.
* Sprite, Model, and Flipbook (frame animation) rendering components.
* Rigidbody and collision components.
* Camera and Player Controller components, with acceleration-based movement.
* Audio related components.
* Lua script components.
* UI components: UI Transform, UI Sprite, and UI Text with custom fonts and colors.
* Multiple components of the same type on one entity.
* Project-wide tags on entities, walls, and sectors.
* Component serialization and project loading.
* Fast component lookup through entity ownership IDs.

### Physics and Collision

The engine includes a custom lightweight physics and collision implementation designed specifically for sector-based environments.

Features include:

* Gravity and configurable gravity scaling.
* Velocity-based movement.
* Friction and air resistance.
* Static and dynamic rigidbodies.
* Ground detection and ground-normal calculation.
* Sector floor and ceiling collision.
* Multi-floor sector selection.
* Correct handling of entities above ceilings and between vertical sector levels.
* SIMD-accelerated mathematical and collision operations.
* Tracy profiling instrumentation for collision and physics workloads.

---

## 🧰 Integrated Toolchain

Tilky Engine provides the applications and workflows required to create, test, version, and export a project.

### Tilky Launcher

The launcher acts as the entry point for the toolchain.

* Create and open `.tilky` projects.
* Validate project files and directory structures.
* Manage project assets and settings.
* Associate projects with specific engine versions.
* Download and install engine versions directly from GitHub Releases.
* Download sample projects to learn from.
* Switch projects between installed engine versions.
* Launch projects using the correct editor build.
* Access releases without requiring users to manually browse or download files from GitHub.

### Visual Level Editor

The Dear ImGui-powered editor provides dedicated editing modes for sectors, walls, entities, and project content.

* Draw walls and construct sectors visually with freehand, rectangle, regular polygon, circle, curve, and staircase tools.
* Multi-select and move vertices, walls, sectors, and entities.
* Name, tag, and attach scripts to sectors and walls.
* Add multiple floor and ceiling levels to sectors.
* Configure floor and ceiling heights.
* Create sloped floors and ceilings.
* Place and edit entities.
* Add and modify entity components.
* Edit position, scale, and unrestricted quaternion-based rotation.
* Configure sprite direction modes and static rotation.
* Edit RGBA colors and transparency.
* Assign textures, scripts, and audio assets, or drag and drop textures straight onto walls and surfaces.
* Edit texture scale, offsets, and flipping for walls, floors, and ceilings.
* Browse level and UI entities in the hierarchy.
* Build sprite animations in the Flipbook Editor.
* Per-user editor settings that persist between sessions.
* Test the current project directly inside the runtime.
* Use a runtime editor and free camera for in-game inspection.

### Asset Browser

The integrated asset browser provides project-level file management without leaving the editor.

* Navigate project directories.
* Create files and folders.
* Rename and delete assets.
* Use file and folder context menus.
* Preview supported image assets (PNG, JPG, and more) and play audio files.
* Drag and drop assets into compatible editor fields.
* Open Lua scripts directly inside the editor.
* Validate asset paths against the project root.

### Embedded Lua Editor

Lua scripts can be edited directly inside Tilky Engine.

* Lua syntax highlighting.
* Autocomplete for the full engine API.
* Compile errors shown in the inspector before you press play.
* Shortcuts such as `Ctrl + S`, `Ctrl + /` (toggle comment), and `Ctrl + D` (duplicate line).
* Script loading and saving.
* Unsaved-change tracking.
* Keyboard focus and text input handling.
* Asset-browser integration.
* Project-root path protection.
* Direct editing of entity gameplay scripts.

### Runtime

The runtime loads exported project data and executes the complete game simulation.

* OpenGL rendering.
* Lua gameplay scripting.
* Entity and component updates.
* Custom physics and collision.
* OpenAL positional audio.
* Input processing.
* Project asset loading.
* Runtime logging and error reporting.
* Optional runtime editor and free-camera controls.

### Standalone Exporting

Projects can be packaged into standalone builds using the integrated export pipeline.

* Build the standalone runtime.
* Copy required runtime libraries.
* Package project assets and data.
* Precompile Lua scripts to bytecode.
* Generate a distributable project directory.
* Launch exported projects independently from the editor.
* Rebuild the standalone executable as part of the development configuration.

### Engine Releases and Version Management

Tilky Engine includes an automated engine distribution workflow.

* GitHub Actions builds release-ready engine packages.
* Engine executables, libraries, and required files are packaged into versioned archives.
* Release archives are uploaded to GitHub Releases.
* Launcher-readable version metadata is generated automatically.
* Users can install and switch engine versions from the launcher.
* Projects retain their selected engine version.

### Localization Tooling

Tilky Engine includes localization support for both the editor and projects.

* JSON-based translation files.
* Runtime language lookup.
* Editor and launcher localization.
* External translation-generation tooling.
* Support for adding community translations.
* Available languages: English, Turkish, Spanish, Russian, Kazakh, Polish, and Bulgarian.

### Performance Profiling

Tracy is integrated throughout the engine for source-level performance analysis.

Instrumented systems include:

* Frame processing.
* Rendering.
* Physics.
* Collision detection.
* Lua scripting.
* Sector operations.
* Runtime and editor workloads.

---

## 📜 Lua Scripting

Gameplay logic can be attached to entities and sectors using Lua scripts through sol2.

The scripting environment supports:

* Entity scripts and sector scripts (doors, lifts, traps, flickering lights).
* Lifecycle callbacks: `Start`, `Update`, `FixedUpdate`, `OnEnable`, `OnDisable`, and `OnDestroy`.
* Collision, trigger, sector-change, and sector-occupancy callbacks.
* `public` fields that appear in the inspector, including entity, asset, and enum types.
* Vector, quaternion, and `mathT` math helpers.
* Keyboard and mouse input.
* Component property access, raycasts, and entity lookup by name or tag.
* Sector API for moving floors and ceilings, fading lights, and changing textures.
* Logging to the in-game console and the engine log.
* Runtime Lua error reporting.

Example:

```lua
public number jumpStrength = 300
public AudioSource jumpSound = nil

local rigidbody

function Start()
    rigidbody = entity.rigidbody
    Debug.Print("Hello from " .. entity.name)
end

function Update()
    if Input.GetKeyDown(Key.Space) then
        rigidbody:AddImpulse(Vector3(0, jumpStrength, 0))
        if jumpSound ~= nil then jumpSound:Play() end
    end
end

function OnTriggerEnter(other)
    if other:HasTag("Pickup") then
        other:Destroy()
    end
end
```

The full scripting reference lives in [`docs/wiki`](docs/wiki/README.md), starting with [Getting Started](docs/wiki/GettingStarted.md). Ready-to-use example scripts are in [`docs/scripts`](docs/scripts/README.md).

---

## 🛠 Tech Stack

| Category                      | Technology                            |
| :---------------------------- | :------------------------------------ |
| **Language**                  | C++20                                 |
| **Graphics**                  | OpenGL 4.3, GLSL, GLAD                |
| **Framework**                 | SDL3                                  |
| **UI**                        | Dear ImGui                            |
| **Audio**                     | OpenAL                                |
| **Scripting**                 | Lua, sol2                             |
| **Profiling**                 | Tracy                                 |
| **Serialization**             | nlohmann/json                         |
| **Logging**                   | spdlog                                |
| **Assets**                    | SDL3_image, SDL3_ttf, FreeType, Assimp        |
| **Build System**              | CMake, Ninja                          |
| **Dependency Management**     | vcpkg                                 |
| **Exporter and Helper Tools** | C++20                                 |
| **Release Automation**        | GitHub Actions, GitHub Releases       |
| **Math**                      | Custom headers with SIMD intrinsics   |
| **Physics and Collision**     | Custom, lightweight, SIMD-accelerated |

---

## 🚀 Build Instructions

### Prerequisites

* A **C++20-compatible compiler**.
* **CMake**.
* **vcpkg** for dependency management. Dependencies are listed in `vcpkg.json` and installed automatically during configuration.
* Bundled in `External/`: `glad` and `ImGuiColorTextEdit` are part of the repository, while `imgui` and `tracy` are Git submodules.

### Building

```bash
# Clone the repository with its submodules
git clone --recursive https://github.com/BenBerke/Tilky-Engine.git
cd Tilky-Engine

# Generate and build
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg>/scripts/buildsystems/vcpkg.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target Tilky_All
```

Pass `-DTILKY_ENABLE_TRACY=OFF` to build without the Tracy profiler.

### CMake Targets

| Target             | Description                                             |
| :----------------- | :------------------------------------------------------ |
| `Tilky_Launcher`   | Creates, opens, versions, and launches Tilky projects.  |
| `Tilky_Engine`     | Main visual editor and development runtime.             |
| `Tilky_Standalone` | Standalone runtime used by exported projects.           |
| `Tilky_Exporter`   | Packages projects into distributable standalone builds. |
| `Tilky_All`        | Builds the complete Tilky toolchain.                    |

---

## 🎮 Editor Workflow

1. **Launch:** Open `Tilky_Launcher`.
2. **Create or Open:** Create a new `.tilky` project or select an existing project.
3. **Select an Engine Version:** Install or choose the engine version used by the project.
4. **Design:** Draw walls, construct sectors, add vertical levels, and place entities.
5. **Customize:** Assign textures, colors, slopes, components, scripts, and audio.
6. **Manage Assets:** Import, preview, rename, organize, and edit project files through the asset browser.
7. **Script:** Write Lua gameplay logic using the embedded script editor.
8. **Test:** Use **Save & Play** to launch the project immediately in the runtime.
9. **Profile:** Connect Tracy to inspect frame times and system bottlenecks.
10. **Export:** Package the project into a standalone distributable build.

### Hotkeys

| Key                        | Action                                                        |
| :------------------------- | :------------------------------------------------------------ |
| `Q`                        | Cycle editor modes                                            |
| `LMB`                      | Draw, select or move objects                                  |
| `Ctrl + LMB`               | Add to or remove from the selection                           |
| `Shift + LMB`              | Range select                                                  |
| `RMB`                      | Edit objects, or cancel the last vertex while drawing         |
| `E`                        | Create an entity at the cursor (Entity Mode)                  |
| `F`                        | Focus the camera on the selection                             |
| `WASD` / Arrow keys        | Move or step the editor camera                                |
| `MMB`                      | Pan the editor camera                                         |
| `Scroll` / `+` / `-`       | Zoom in or out                                                |
| `1` – `6`                  | Freehand, rectangle, polygon, circle, curve, staircase tool (Sector Mode) |
| `Enter` / `Space`          | Finish the current shape (Sector Mode)                        |
| `Backspace`                | Remove the last drawn point (Sector Mode)                     |
| `[` / `]`                  | Fewer or more polygon sides (Sector Mode)                     |
| `Alt`                      | Flip which end of a staircase is the bottom (Sector Mode)     |
| `Escape`                   | Cancel drawing (Sector Mode), or release the mouse in the runtime |
| `Ctrl + Z`                 | Undo action                                                   |

---

## 🗺 Roadmap

### 🏁 Completed Milestones

* [x] **Sector-Based 3D Renderer:** Implemented textured sector walls, floors, ceilings, sprites, and entities using an OpenGL shader pipeline.
* [x] **Multi-Level Sectors:** Added multiple independent floor and ceiling pairs inside a single sector.
* [x] **Sloped Surfaces:** Added directional floor and ceiling slopes with configurable strength.
* [x] **RGBA Rendering:** Added color tinting and functional alpha transparency for surfaces and sprites.
* [x] **Quaternion Transform Rotation:** Added unrestricted quaternion-based entity rotation.
* [x] **Lua Scripting Integration:** Embedded a custom gameplay scripting runtime using Lua and sol2.
* [x] **Embedded Lua Editor:** Added in-editor script editing, syntax highlighting, saving, focus handling, and asset-browser integration.
* [x] **Visual UI Editor:** Added in-engine canvas tools for creating and editing HUDs, menus, and text elements.
* [x] **Integrated Asset Browser:** Added asset navigation, previews, drag and drop, creation, renaming, and deletion.
* [x] **Custom Physics and Collision:** Added rigidbodies, gravity, friction, grounding, sector clamping, and multi-floor collision.
* [x] **Standalone Export Execution:** Implemented a pipeline for packaging game assets and runtime binaries into standalone builds.
* [x] **Engine Version Management:** Added launcher-based installation, project version selection, and engine version switching.
* [x] **Automated Release Pipeline:** Added GitHub Actions packaging, GitHub Release uploads, and launcher-readable version metadata.
* [x] **Localization Toolchain:** Developed a pipeline for generating and loading translator-friendly JSON localization assets.
* [x] **Tracy Profiler Instrumentation:** Integrated source-level profiling across rendering, physics, collision, and scripting systems.
* [x] **3D Model Support:** Added Assimp-based model loading for glTF, OBJ, FBX, and other common formats.
* [x] **Flipbook Animation:** Added `.fpk` flipbook assets, a Flipbook Editor, and animated sprites and UI sprites.
* [x] **Skyboxes:** Added cubemap, panoramic, and cylinder sky modes.
* [x] **Upper and Lower Wall Textures:** Added separate top and bottom textures for walls between sectors.
* [x] **Sector Scripting and Tags:** Added sector scripts, occupancy callbacks, the Sector API, and project-wide tags.
* [x] **Lua Autocomplete:** Added engine-API autocomplete to the embedded Lua editor.
* [x] **Advanced Drawing Tools:** Added rectangle, polygon, circle, curve, and staircase sector tools.
* [x] **Scripting Wiki:** Wrote a full scripting reference and a library of example scripts in `docs/`.

## 🔨 Future Milestones

### 🎨 Rendering Pipeline

* [ ] **Deferred Rendering Pipeline:** Transition the core shading pass to a G-buffer architecture using framebuffer objects. Geometry and lighting will be separated into albedo, normal, material, and depth channels to support a larger number of dynamic lights.
* [ ] **Vulkan Rendering Hardware Layer:** Implement a Vulkan rendering backend alongside the existing OpenGL pipeline.

### ⚙️ Tooling and Asset Management

* [ ] **Offline BSP Compiler and Map Processor:** Build a command-line utility that processes sector geometry into optimized Binary Space Partitioning trees.
* [ ] **Binary Asset Packer and Encoder:** Compress and serialize maps, textures, scripts, and audio files into a unified production asset format.

### 🌐 Networking and Multiplayer

* [ ] **Modular Networking Architecture:** Implement a customizable low-latency networking framework.
* [ ] **LAN Multiplayer Support:** Add local network discovery, matchmaking, and direct-IP connections.
* [ ] **Steam P2P Integration:** Add Steam peer-to-peer networking without requiring dedicated servers.

### 🛠️ Editor and Platform Integration

* [ ] **Advanced Editor Workspace:** Add docking presets, multi-view viewports, additional scene inspection tools, and further workflow improvements.

### 🚀 Production and Distribution

* [ ] **Steamworks Core API Integration:** Add native Steam SDK support for achievements, cloud saves, and overlay hooks.
* [ ] **Steam Workshop Content Pipeline:** Allow users to package and distribute custom maps and Lua script packs through Steam Workshop.

---

## 🤝 Contributing and Credits

Contributions are welcome. Bug reports, feature requests, documentation improvements, translations, and code contributions are appreciated.

**Author:** Berke Memioğlu

### Assets

* **Çağla Çıralı** — Logo

### Translations

* **Ilya Brezhnev** — Russian and Kazakh
* **ThatGuyMiki** — Polish
* **ZainX38** - Spanish
### Special Thanks

* **Roger Peterson** — Community management, bug testing, and feature suggestions.

## 📄 License

Distributed under the Apache License 2.0. See `LICENSE` for details.
