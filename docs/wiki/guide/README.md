# Make a Doom-Style Game: The Beginner's Guide

This guide takes you from an empty launcher to a level you built yourself, with rooms, enemies,
a door and a lift. You don't need to know how to program, and you don't need to have used a game
engine before.

You'll use the **Classic FPS Kit**, a project template that comes with the engine. It already has
16 enemies, 9 weapons, pickups, keys, switches, menus and a HUD. Your job is the fun part: drawing
maps and dragging things into them.

Read the pages in order the first time. Each one takes about ten minutes.

| # | Page | What you'll do |
|---|---|---|
| 1 | [Create your project](01-Create-Your-Project.md) | Make the project and play the example game |
| 2 | [A tour of the editor](02-Tour-Of-The-Editor.md) | Learn the screen, the modes and the keys |
| 3 | [Sectors: how maps work](03-Sectors.md) | Understand the one idea all maps are made of |
| 4 | [Build your first room](04-Your-First-Room.md) | Draw a room, add a player and walk around in it |
| 5 | [Prefabs: drag and drop your game](05-Prefabs.md) | Add enemies, weapons and pickups |
| 6 | [Add a door](06-Doors.md) | Connect two rooms with a door, then lock it with a key |
| 7 | [Add a lift](07-Lifts.md) | Build a raised ledge and a lift up to it |
| 8 | [Components: what things are made of](08-Components.md) | See inside a prefab and change how it behaves |
| 9 | [Events and dispatches: making things happen](09-Events-And-Dispatches.md) | Make a switch that opens a door, with no code |
| 10 | [Finish your game](10-Finish-Your-Game.md) | Add levels, change the menus and export the game |

## Words you'll meet

| Word | Plain meaning |
|---|---|
| **Level** | One map of your game. Doom's E1M1 is a level. |
| **Sector** | A room, drawn from above as a flat shape. Every level is made of them. |
| **Wall** | One edge of a sector. |
| **Entity** | One thing placed in a level: an enemy, a lamp, the player. |
| **Component** | One ability an entity has, such as "has a picture" or "is solid". |
| **Prefab** | A ready-made entity saved as a file, which you can drag into any level. |
| **Event** | Something that happens: "the player touched me". |
| **Dispatch** | Something to do about it: "add 10 to Health". |
| **Asset** | Any file your game uses: a texture, a sound, a level, a prefab. |
