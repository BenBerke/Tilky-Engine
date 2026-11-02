# 1. Create Your Project

## Make the project

1. Open the **Tilky Engine Launcher**.
2. Click **Create Project**.
3. Choose **Classic FPS Kit** from the list of templates.
4. Type a name for your game in **Project Name**. Anything works, and you can rename it later.
5. Click **Create**.
6. Your project now appears in the **Projects** list. Select it and click **Open Project**.

The editor opens with the project loaded.

## Play the example game first

Before you build anything, play what's there. It shows you what the kit can do, and every level is
an example you can copy.

1. The editor opens on one of the levels. Press **P**, or click **Save & Play**.
2. If you start on the main menu, click **New Game**.

| Control | What it does |
|---|---|
| **W A S D** | Walk |
| **Mouse** | Look |
| **Left click** or **Left Ctrl** | Shoot |
| **E** | Use: opens doors, presses switches |
| **1** to **9** | Change weapon |
| **Esc** | Pause menu |

To stop playing and go back to the editor, press **Shift + F1**. **Quit Game** in the pause menu does the same
while you're working in the editor.

## What's in the project

Look at the **Asset Browser**, the panel that lists your project's files.

| Folder | What's inside |
|---|---|
| **Levels** | The levels of the game |
| **Prefabs** | Everything you can drag into a level. You'll use this one the most. |
| **UI Layouts** | The HUD, the menus and the screen between levels |
| **Textures** | Pictures for walls (`Walls`) and for floors and ceilings (`Flats`) |
| **Sprites**, **Animations**, **Sounds** | The art and sound that prefabs use |
| **Scripts** | The few bits of code the kit needs, all explained in comments. You don't have to open them. |
| **Freedoom** | The licence for the art. Keep it in your project, see below. |

### The levels

| Level | What it teaches |
|---|---|
| **Main Menu** | The first screen of the game |
| **Level 1 - Outpost** | Rooms, a door, stairs, a lift, an exit switch |
| **Level 2 - Keys** | Keys and locked doors, toxic floors, a teleporter, rising stairs, a secret |
| **Level 3 - Courtyard** | Outdoors, buildings, a monster trap |
| **Level 4 - The Tripod** | A boss fight, and an exit that opens when the boss dies |
| **Blank Base Level** | One empty room with a player. Copy this to start your own level. |
| **Prefab Gallery** | Every prefab in the kit, in rows |

Open any level from the **Levels** folder in the Asset Browser. You'll see **yellow comment pins**
on the map. Click them to read what each part of the level does and how it was made. They only
exist in the editor, and players never see them.

## Using the art in your game

The art and sound come from [Freedoom](https://freedoom.github.io/), a free project with a BSD
licence. You may use them in your own game, even a commercial one, as long as you keep the licence
text with it. That's why the **Freedoom** folder is there: leave it in your project.

---

Next: [A tour of the editor](02-Tour-Of-The-Editor.md)
