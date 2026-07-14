# 🛠️ Tilky Engine

[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-20-red.svg)](https://en.cppreference.com/w/cpp/20)

**Tilky Engine** is a custom **sector-based 3D game engine and editor** built in C++20.

Inspired by classic 90s engines like *Doom* and *Build Engine*, Tilky Engine uses a specialized 3D rendering pipeline where maps are built from topological sectors with independent height data, textured walls, floors, decals, sprites, and entities.

The project provides a full end-to-end toolchain: a project launcher, visual level editor, OpenGL runtime, Lua scripting, OpenAL audio, localization tooling, profiling, and standalone export support.

## 🕹️ Editor & Engine Workflow
This 80-second walkthrough demonstrates the UI layout, texture and sector workflows, asset pipelines, and real-time editor-to-runtime testing.

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

Unlike modern triangle-mesh engines, Tilky Engine builds levels from connected sectors. Each sector can define its own floor and ceiling heights, allowing classic room-based level design with verticality.

This architecture enables:

* **Sector-Based Worlds:** Maps are built from walls, sectors, floors, ceilings, and entities.
* **True 3D Perspective:** Walls, floors, decals, billboards, and sprites are rendered in a 3D scene.
* **Classic Aesthetic:** Designed for retro 3D games, sprite-heavy worlds, and Doom-like level layouts.
* **Modern Pipeline:** Uses OpenGL shaders and SSBO-backed geometry data for efficient rendering.

### Integrated Toolchain

Tilky Engine is built as a complete editor/runtime workflow rather than just a renderer.

* **Tilky Launcher:** Manages `.tilky` project files, assets, project settings, and localization data.
* **Visual Editor:** A Dear ImGui-powered editor with tools for Wall, Sector, and Entity editing.
* **Runtime:** Loads projects, runs scripts, updates entities, handles audio, collision, and rendering.
* **ECS:** A data-oriented entity component system using entity IDs and type-specific component arrays.
* **Lua Scripting:** Attach Lua scripts to entities for custom gameplay behavior.
* **Audio System:** Positional 3D audio powered by OpenAL.
* **Standalone Exporting:** Rust-based exporter for packaging projects into standalone builds.
* **Localization Tooling:** Rust-based translator tool that generates the required JSON files automatically.
* **Performance Profiling:** Tracy integration for frame, rendering, physics, collision, and script profiling.

## 🛠 Tech Stack

| Category                     | Library                               |
|:-----------------------------|:--------------------------------------|
| **Language**                 | C++20, Rust                           |
| **Graphics**                 | OpenGL, GLSL, GLAD                    |
| **Framework**                | SDL3                                  |
| **UI**                       | Dear ImGui                            |
| **Audio**                    | OpenAL                                |
| **Scripting**                | Lua, sol2                             |
| **Profiling**                | Tracy                                 |
| **Data**                     | nlohmann/bson                         |
| **Logging**                  | spdlog                                |
| **Assets**                   | SDL3_image, SDL3_ttf, FreeType        |
| **Build System**             | CMake                                 |
| **Helper Tools & Exporting** | Rust                                  |
| **Math**                     | Custom Headers (with SIMD Intrinsics) |
| **Physics & Collisions**	   | Custom, Lightweight, SIMD-Accelerated |

---

## 🚀 Build Instructions

### Prerequisites

* A **C++20** compatible compiler.
* **CMake**.
* **vcpkg** for dependency management.
* **Rust/Cargo** for helper tools and exporting.
* External libraries cloned into `External/`: `glad`, `imgui`, `tracy`.

### Building

```bash
# Clone the repository
git clone https://github.com/your-username/TilkyEngine.git
cd TilkyEngine

# Initialize submodules
git submodule update --init --recursive

# Generate and build
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

### CMake Targets

| Target           | Description                                                        |
| :--------------- | :----------------------------------------------------------------- |
| `Tilky_Launcher` | Manages projects, project settings, assets, and localisation data. |
| `Tilky_Engine`   | The main editor and runtime environment.                           |
| `Tilky_All`      | Builds the full Tilky toolchain for development convenience.       |

---

## 🎮 Editor Workflow

1. **Launch:** Open `Tilky_Launcher` and create or open a `.tilky` project.
2. **Design:** Use the editor modes to draw walls, define sectors, and place entities.
3. **Customize:** Add textures, components, scripts, and audio settings.
4. **Script:** Attach Lua scripts to entities for custom gameplay logic.
5. **Test:** Use **Save & Play** to jump directly into the runtime.
6. **Profile:** Connect Tracy to inspect frame times and system bottlenecks.
7. **Export:** Package the project into a standalone build.

### Hotkeys

| Key        | Action                   |
| :--------- | :----------------------- |
| `Q`        | Cycle Modes              |
| `LMB`      | Place / Move New Objects |
| `RMB`      | Edit Objects             |
| `MMB`      | Pan Editor Camera        |
| `Scroll`   | Zoom In / Out            |
| `Ctrl + Z` | Undo Action              |
| `Escape`   | Release mouse in Runtime |

---

## 🗺 Roadmap

### 🏁 Completed Milestones
* [x] **Lua Scripting Integration:** Embedded custom gameplay scripting runtime via Lua for decoupled game logic.
* [x] **Visual UI Editor:** Designed in-engine canvas tools for real-time creation and editing of HUDs, menus, and text elements.
* [x] **Standalone Export Execution:** Implemented a pipeline to package game assets and runtime binaries into optimized, standalone distribution builds.
* [x] **Rust Localization Toolchain:** Developed an external Rust-based pipeline for compiling translator-friendly JSON localization assets.
* [x] **Tracy Profiler Instrumentation:** Integrated deep source-level profiling across rendering, physics, collision, and scripting systems.

## 🔨 Future Milestones

### 🎨 Rendering Pipeline
* [ ] **Deferred Rendering Pipeline (G-Buffer):** Transition the core shading pass to a deferred architecture using Framebuffer Objects (FBOs). This will separate geometry from lighting, outputting Albedo, Normals, and Screen-Space Depth channels to support high-density dynamic light rendering.
* [ ] **Vulkan Rendering Hardware Layer:** Implement a modern Vulkan abstraction layer alongside the existing pipeline to improve hardware efficiency and cross-platform performance.

### ⚙️ Tooling & Asset Management
* [ ] **Offline BSP Compiler & Map Processor:** Build a dedicated command-line utility to parse raw sector geometry, execute recursive hyper-plane splitting, and output optimized Binary Space Partitioning (BSP) trees for zero-overdraw rendering.
* [ ] **Binary Asset Packer & Encoder:** Develop an offline utility to compress and serialize maps, textures, and audio files into a unified, lightweight binary format optimized for production exports.

### 🌐 Networking & Multiplayer
* [ ] **Modular Networking Architecture:** Implement a highly customizable, low-latency networking framework designed to scale from local to online play.
* [ ] **LAN Multiplayer Support:** Enable local network discovery, matchmaking, and direct IP connection handling.
* [ ] **Steam P2P Integration:** Implement Steam's peer-to-peer networking infrastructure to allow seamless player-to-player connectivity without relying on dedicated servers.

### 🛠️ Editor & Platform Integration
* [ ] **UX / Editor Overhaul:** Redesign the Dear ImGui workspace layout to support docking, multi-view viewports, and an improved asset-browsing workflow.

### 🚀 Production & Distribution Deployment
* [ ] **Steamworks Core API Integration:** Native C++ Steam SDK bindings for achievements, cloud saves, and overlay hooks.
* [ ] **Steam Workshop Content Pipeline:** Allowing users to package and distribute custom maps and Lua script packs directly via Steam.

## 🤝 Contributing & Credits

Contributions are welcome. Bug reports, feature requests, documentation improvements, translations, and code contributions are all appreciated.

**Author:** Berke Memioğlu

### Assets
* **Çağla Çıralı** — Logo 
 
### Translations

* **Ilya Brezhnev** — Russian & Kazakh
* **ThatGuyMiki** — Polish

---

### Special Thanks

* **Roger Peterson** for community management, bug testing, and suggesting ideas.

## 📄 License

Distributed under the Apache License 2.0. See `LICENSE` for details.
