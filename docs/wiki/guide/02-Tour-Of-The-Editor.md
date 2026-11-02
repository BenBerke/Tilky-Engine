# 2. A Tour of the Editor

The big area in the middle of the screen is the **map**: your level seen from above, like a floor
plan. You build levels here. Open **Level 1 - Outpost** now so you have something to look at.

## Moving around the map

| Key | What it does |
|---|---|
| **W A S D** | Move the view |
| **+** and **-** | Zoom in and out |
| **J** | Show the whole map |
| **F** | Jump to whatever you have selected |

## The three modes

The map has three modes. Each one decides what your clicks affect, so if a click seems to do
nothing, check the mode first.

| Key | Mode | Use it to |
|---|---|---|
| **R** | **Sector Mode** | Draw rooms, and pick them to change their floors, ceilings and light |
| **G** | **Geometry Mode** | Move walls and corners to reshape rooms |
| **E** or **T** | **Entity Mode** | Place and move things: enemies, pickups, the player |

**Q** switches to the next mode.

## The inspector

The **inspector** is the panel that shows the settings of whatever you've selected.

**Right-click anything on the map to edit it:** a room, a wall or an entity. Its settings open in
the inspector. This works in every mode.

## The Asset Browser

This is the panel that lists your project's files. Three things you'll do with it again and again:

- **Drag a prefab onto the map** to place it.
- **Drag a texture into a texture field** in the inspector to use it on a floor, ceiling or wall.
- **Double-click a level** to open it.

Right-click a file to copy, duplicate or delete it.

## The most useful keys

| Key | What it does |
|---|---|
| **P** | Save and play |
| **Shift + F1** | Stop playing and come back to the editor |
| **Ctrl + Z** | Undo. Use it freely, nearly everything can be undone. |
| **Delete** | Delete what's selected |
| **C** | Add a comment pin, a note to yourself on the map |
| **M** | Change what the map shows: floor textures, ceiling textures, heights |
| **N** | Turn snapping to the grid on or off |
| **,** and **.** | Make the grid smaller or larger |

You can change any key in **User Settings > Key Bindings**.

## The other windows

You won't need these yet, but it helps to know where they are:

- **World Settings** holds what belongs to the whole level: its sky, its music, and which UI layouts
  it shows.
- **Project Settings** holds what belongs to the whole game: the list of levels, the tags and the
  exporter.
- The **UI Editor** (**U**) is where menus and the HUD are made.
- The **Runtime Editor** (**V**) lets you fly through the level in 3D and adjust things while seeing
  the result.

---

Next: [Sectors: how maps work](03-Sectors.md)
