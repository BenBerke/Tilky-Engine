# 🛠️ Tilky Engine 

[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-20-red.svg)](https://en.cppreference.com/w/cpp/20)

**Tilky Engine** is a custom **Sector-Based 3D game engine** built in C++20. Inspired by the classic architecture of 90s legends like *Doom* and *Build Engine*, it utilizes a specialized 3D rendering pipeline where the world is defined by topological sectors with independent height data.

The project provides a complete end-to-end toolchain: from a project launcher and visual level editor to a high-performance OpenGL runtime.

<p align="center">
  <img src="https://github.com/user-attachments/assets/76ba6573-6561-4394-96f5-809ba7211b12" width="48%" alt="Editor Screenshot 1" />
  <img src="https://github.com/user-attachments/assets/552930c9-3a2f-4785-8e4d-3f1520b710de" width="48%" alt="Editor Screenshot 2" />
</p>

---

## 🏗️ Core Architecture

### **Sector-Based 3D Rendering**
Unlike modern triangle-mesh engines, Tilky defines the world through sectors. This architecture enables:
* **True 3D Perspective:** Full 3D wall projection, depth handling, and textured surfaces.
* **Dynamic Sector Heights:** Independent floor and ceiling altitudes per sector for complex verticality.
* **Classic Aesthetic:** Authentic 90s-style sprite rendering and wall decals.
* **Modern Pipeline:** Powered by OpenGL shaders and **SSBO-based geometry buffers** for high-performance data throughput.

### **Integrated Toolchain**
* **Tilky Launcher:** A centralized hub to manage `.tilky` project manifests, assets, and localized settings.
* **Visual Editor:** A Dear ImGui-powered workspace with dedicated modes for Dot, Wall, Sector, and Entity manipulation.
* **ECS (Entity Component System):** A data-oriented backend designed for cache-friendly performance, featuring components like `Transform`, `Sprite`, and `Decal`
* **Lua Scripting:** Make custom scripts with Lua, easy-to-use and powerful at the same time!

---

## 🛠 Tech Stack

| Category | Library |
| :--- | :--- |
| **Language** | C++20 |
| **Graphics** | OpenGL (GLAD) |
| **Framework** | SDL3 |
| **UI** | Dear ImGui |
| **Data/JSON** | nlohmann/json |
| **Assets** | SDL3_image, SDL3_ttf, FreeType |

---

## 🚀 Build Instructions

### Prerequisites
* A **C++20** compatible compiler (MSVC, GCC, or Clang).
* **CMake** 3.16 or higher.
* **vcpkg** (Recommended for dependency management).

### Building
# Clone the repository
git clone [https://github.com/your-username/TilkyEngine.git](https://github.com/your-username/TilkyEngine.git)
cd TilkyEngine

# Generate and build
```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

# CMake Targets
* Tilky_Launcher: Manages project files and localisation.
* Tilky_Engine: The core Editor and Runtime environment.
* Tilky_All: Builds the entire suite for development convenience.

# 🎮 Editor Workflow
* Launch: Open the Tilky_Launcher and create a new project.
* Design: Use the Editor modes to draw walls and define sectors.
* Test: Use the Save & Play function to jump directly into the 3D runtime.

## HotkeysKeyAction
* Q Cycle Modes (Wall, Sector, Entity)
* LMB Place / Select / Edit
* MMBPan Editor CameraScrollZoom In / Out
* Ctrl + Z Undo Action

# 🗺 Roadmap
[ ] Visual UI Editor: In-engine tools for creating HUDs and menus.

[ ] Standalone Export: Capability to package projects into a single executable.

[ ] Networking: Initial support for multiplayer sector synchronization.

# 🤝 Contributing & Credits
Contributions are welcome! Whether it's bug fixes, feature requests, or documentation, feel free to open an issue or submit a pull request.

* Author: Berke Memioğlu

# Special Thanks:

## Translations
* Ilya Brezhnev (Russian & Kazakh)
* ThatGuyMiki (Polish).

# License 
Distributed under the Apache License 2.0. See LICENSE for details.
