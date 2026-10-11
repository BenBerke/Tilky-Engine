# What's New Since 1.1.0

Everything added to Tilky Engine between version 1.1.0 (2 Sep 2026) and 10 Oct 2026.

---

## Major Additions

### Lua Scripting Overhaul
- **Public fields:** declare variables in a script (`public number speed = 15`) and edit them in the inspector. They support ranges, dropdowns (`enum`), vectors, and references to entities, walls, sectors, components and other scripts. You can fill reference fields by dragging objects from the hierarchy.
- **Sector scripts:** attach scripts to sectors as well as entities.
- **Large new API:** move floors and ceilings, fade lights, detect entities entering or leaving sectors, raycast, look at targets, apply impulses, control sounds, and more.
- **Smarter script editor:** autocomplete, better highlighting, and shortcuts such as Ctrl+/ to comment a line and Ctrl+D to duplicate it.

### Staircase Tool
Drag one rectangle to generate a whole staircase. Set the step height, the target height or the step count, and the tool works out the rest. You can also choose the direction and whether ceilings rise with the steps.

### Skybox
Each level can have its own sky: a cubemap, a panorama, or a classic DOOM-style cylinder. You can rotate it, tint it and set it to spin slowly while the game runs.

### Flipbook Animations
A new Flipbook editor for creating sprite animations, with 8 directions per frame, a live preview, loop modes and per-frame timing. Frames can also call functions in your scripts, for example to play a sound on each footstep. Flipbooks work on both world sprites and UI sprites.

### Multiple Components per Entity
An entity can now have several components of the same type, such as two sprites, several colliders or multiple sound sources. Each one has its own offset, and you can drag components to reorder them.

### Two Textures per Wall
Walls now have separate **Top** and **Bottom** textures. You can also choose how each texture is anchored, so doors and lifts carry their textures with them and neighbouring walls line up.

### Sector Parenting
Give a sector a parent, and it follows every change made to that parent: heights, lighting and textures all move together. This is useful for platforms, pillars and lifts.

### Tags
Create tags for your project and attach them to sectors, walls and entities. Scripts can then find and react to anything with a given tag.

### 3D Models 
Place 3D models (`.obj`, `.fbx`, `.gltf`, ...) in your levels. Their textures are imported automatically.

### Extensive Wiki
New documentation in `docs/`: a full wiki page for every scripting type, plus step-by-step scripting tutorials.

---

## Smaller Additions

### Editor
- Multi-select walls, sectors and entities, then edit them together.
- New toolbar with icon buttons, and a reorganised top menu.
- **User Settings** window where you can customise editor colours and camera speed.
- WASD camera movement in the map editor.
- Hold Ctrl to ignore grid snapping, and press 1–6 to switch drawing tools.
- Right-click undoes the last vertex while you draw a sector.
- Left-click moves walls; right-click opens them in the inspector.
- Name your walls and sectors.
- Floor and ceiling textures can be offset, scaled and flipped.
- Walls have UV scale and flip options.
- **Open File** button on script components.
- Double-click a `.txt`, `.md` or `.json` file to edit it in the built-in script editor, as plain text without highlighting. Right-click any script or text file for **Open in Text Editor**.
- New **Create File → Text (.txt)** option; the new file opens straight away.

### 3D Editor
- Drag textures from the Asset Browser straight onto walls, floors and ceilings.
- Left-drag on a surface to slide its texture into place.

### UI
- Custom fonts, font sizes and text colours for UI text.
- UI entities now appear in the hierarchy.
- Sprites and UI sprites can be toggled on and off.

### Gameplay
- The Player Controller now has acceleration, deceleration and air control.

### Assets
- JPG textures are supported.
- Sounds can be previewed in the Asset Browser.

### Languages
- Spanish translation (thanks to **ZainX38**!).

---

## Fixes
- Fixed sectors with holes.
- Fixed camera stepping on stairs.
- Fixed floor and sector lighting.
- Fixed texture previews in the 3D editor.
- Fixed the Player Controller and Camera not updating correctly.
