# 🛠️ Tilky Engine

**Tilky Engine** is a custom **sector-based 3D game engine, editor, and project toolchain** built in C++20.

Inspired by classic 90s engines such as *Doom* and *Build Engine*, Tilky Engine uses a specialized 3D rendering pipeline where maps are constructed from topological sectors, textured walls, floors, ceilings, slopes, sprites, and entities.

Tilky Engine is designed as a complete development environment rather than only a  rendering library. It includes a project launcher, visual level editor, asset browser, embedded Lua editor, OpenGL runtime, custom physics, OpenAL audio, engine version management, automated releases, localization tooling, profiling, and standalone project exporting.

## 🚀 Download Tilky Engine

The recommended way to install and use Tilky Engine is through the **Tilky Launcher**. The launcher handles project creation, engine installation, version switching, and opening projects with the correct engine version. No manual compilation is required!

[**Download the Tilky Launcher from GitHub Releases →**](https://github.com/BenBerke/Tilky-Engine-Launcher/releases)

The source build instructions below are intended for contributors and developers modifying the engine itself.

## 🕹️ Editor & Engine Workflow

This 80-second walkthrough demonstrates the UI layout, texture and sector workflows, asset pipeline, and real-time editor-to-runtime testing.

<p align="center">
  <a href="https://www.youtube.com/watch?v=_2Y0dR1GkY8">
    <img src="https://img.youtube.com/vi/_2Y0dR1GkY8/maxresdefault.jpg" alt="Editor Workflow" width="700">
  </a>
</p>

## 🔬 Systems Optimization & Performance Showcase

This demonstration showcases the physics and collision stress tests, proving sub-linear time complexity under heavy load.

<p align="center">
  <a href="https://www.youtube.com/watch?v=cR_EaVp9ymU">
    <img src="https://img.youtube.com/vi/cR_EaVp9ymU/maxresdefault.jpg" alt="Collision System Performance" width="700">
  </a>
</p>

<p align="center">
  <img src="https://github.com/user-attachments/assets/76ba6573-6561-4394-96f5-809ba7211b12" width="48%" alt="Editor Screenshot 1" />
  <img src="https://github.com/user-attachments/assets/552930c9-3a2f-4785-8e4d-3f1520b710de" width="48%" alt="Editor Screenshot 2" />
</p>

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
* **Modern GPU Pipeline:** OpenGL shaders and SSBO-backed scene data are used for efficient rendering.

### Entity Component System

Tilky Engine uses a data-oriented entity component architecture based on entity IDs and type-specific component arrays.

Available systems include:

* Transform components with position, scale, and quaternion rotation.
* Sprite rendering components.
* Rigidbody and collision components.
* Audio related components.
* Lua script components.
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
* Switch projects between installed engine versions.
* Launch projects using the correct editor build.
* Access releases without requiring users to manually browse or download files from GitHub.

### Visual Level Editor

The Dear ImGui-powered editor provides dedicated editing modes for sectors, walls, entities, and project content.

* Draw walls and construct sectors visually.
* Add multiple floor and ceiling levels to sectors.
* Configure floor and ceiling heights.
* Create sloped floors and ceilings.
* Place and edit entities.
* Add and modify entity components.
* Edit position, scale, and unrestricted quaternion-based rotation.
* Configure sprite direction modes and static rotation.
* Edit RGBA colors and transparency.
* Assign textures, scripts, and audio assets.
* Test the current project directly inside the runtime.
* Use a runtime editor and free camera for in-game inspection.

### Asset Browser

The integrated asset browser provides project-level file management without leaving the editor.

* Navigate project directories.
* Create files and folders.
* Rename and delete assets.
* Use file and folder context menus.
* Preview supported image assets.
* Drag and drop assets into compatible editor fields.
* Open Lua scripts directly inside the editor.
* Validate asset paths against the project root.

### Embedded Lua Editor

Lua scripts can be edited directly inside Tilky Engine.

* Lua syntax highlighting.
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

Gameplay logic can be attached to entities using Lua scripts through sol2.

The scripting environment supports:

* Entity-owned scripts.
* Startup and per-frame update callbacks.
* Public script variables.
* Vector and mathematical types.
* Input handling.
* Component property access.
* Physics and collision functions.
* Logging functions for information, warnings, errors, and critical failures.
* Runtime Lua error reporting.

Example:

```lua
function Start()
    Debug.Info("Script started")
end

function Update(deltaTime)
    local transform = Self:GetTransform()
    local rigidbody = Self:GetRigidbody()

    if Input.IsKeyDown("W") then
        rigidbody:AddVelocity(Vector3(0.0, 0.0, 5.0))
    end
end
```

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
| **Assets**                    | SDL3_image, SDL3_ttf, FreeType        |
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
* **vcpkg** for dependency management.
* External libraries cloned into `External/`:

  * `glad`
  * `imgui`
  * `tracy`

### Building

```bash
# Clone the repository
git clone https://github.com/your-username/TilkyEngine.git
cd TilkyEngine

# Initialize submodules
git submodule update --init --recursive

# Generate and build
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

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

| Key        | Action                           |
| :--------- | :------------------------------- |
| `Q`        | Cycle editor modes               |
| `LMB`      | Place or move objects            |
| `RMB`      | Edit objects                     |
| `MMB`      | Pan the editor camera            |
| `Scroll`   | Zoom in or out                   |
| `Ctrl + Z` | Undo action                      |
| `Escape`   | Release the mouse in the runtime |

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

### Special Thanks

* **Roger Peterson** — Community management, bug testing, and feature suggestions.

## 📄 License

Distributed under the Apache License 2.0. See `LICENSE` for details.
