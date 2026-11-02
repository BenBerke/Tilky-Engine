# 4. Build Your First Room

Time to build. You'll start from a copy of the blank level, because it already has a player, the HUD
and the menus.

## Make your own level

1. In the Asset Browser, open the **Levels** folder.
2. Right-click **Blank Base Level** and choose **Duplicate**.
3. Rename the copy `My Level`, then double-click it to open it.

You'll see one small room with a player inside it and a comment pin.

Press **P** to try it. You can walk around the empty room. Press **Shift + F1** to come back to the editor.

## Change the room

1. **Right-click the room.** Its settings open in the inspector.
2. Find **Ceiling Height** and change it to `80`.
3. In the Asset Browser, open **Textures > Flats**. Drag any picture onto **Floor Texture**.
4. Press **P** to see the difference.

## Draw a second room

1. Press **R** for Sector Mode.
2. Press **2** for the **Rectangle Tool**.
3. Click on the top edge of your room, near one end. This is the first corner.
4. Move the mouse up and across, and click again for the opposite corner. The room appears.

The new room shares part of an edge with the first one, so that part becomes an opening. If you
draw it with a gap between the rooms, they won't connect. Press **Ctrl + Z** and try again.

The grid helps here: corners snap to it, so edges line up exactly. The **Drawing Tools** panel lists
the keys for the tool you're using. Hold **Shift** to draw a perfect square.

**Right-click the new room** and give it a floor, a ceiling and a texture, the same way as before.

## Texture the walls

1. **Right-click a wall.** Its settings open in the inspector.
2. In the Asset Browser, open **Textures > Walls** and drag a picture onto the wall's texture field.

To do many walls at once, hold **Ctrl** and right-click each one.

A wall between two rooms has separate **top** and **bottom** textures, for the piece above an
opening and the piece below it. Those only show when the two rooms have different heights.

## Make a staircase

Doing this once will make sectors click.

1. Draw a thin rectangle, 8 deep, touching your room.
2. **Right-click it** and set **Floor Height** to `4`.
3. Draw another thin one touching it, and set its floor to `8`.
4. Draw a third at `12`, then a room beyond it at `16`.
5. Make each ceiling high enough to walk under: at least 20 above that sector's floor.

The **Staircase Tool** (**6**) can draw a whole staircase in one go, but it's worth doing it by hand
the first time so you see how it works.

## Move the player

1. Press **E** for Entity Mode.
2. Click the player and drag it wherever you'd like the level to start.

Every level needs exactly one player.

## If something goes wrong

| What you see | What to check |
|---|---|
| You can't walk between two rooms | Check they really touch. Zoom in: they should share an edge, with no gap. |
| You can't climb a step | The step is taller than 6. |
| You bump your head and can't get through | The opening is too low. Raise the ceiling. |
| You fall into nothing or see no floor | The player was placed outside any sector. Drag it inside. |
| A wall is plain white or a flat colour | It has no texture yet. |

---

Next: [Prefabs: drag and drop your game](05-Prefabs.md)
