# 6. Add a Door

A door is a thin sector between two rooms. Its ceiling sits on its floor, so it's shut. When you
press **E** on it, the ceiling rises.

```
   Seen from above:

   +-----------+---+-----------+
   |           | D |           |
   |  Room A   | O |  Room B   |
   |           | O |           |
   |           | R |           |
   +-----------+---+-----------+
```

## Build one

**You'll need two rooms with a gap between them.** Make the gap 8 wide.

1. Press **R** for Sector Mode, then **2** for the Rectangle Tool.
2. Draw a thin rectangle that fills part of the gap. It should touch both rooms, and be about 32 wide
   so the player fits through easily.
3. **Right-click the new sector.**
4. In the inspector, find the **Name** field and call it `My Door`. A name makes it easy to find later.
5. Set its **Floor Height** to match the rooms on both sides.
6. Click **Make Door...**
7. A small window asks for two numbers:
   - **Lip**: how far below the rooms' ceilings the door stops when it opens. `2` looks right.
   - **Wait**: how many seconds it stays open. `3` is the usual choice.
8. Click **Make Door**. The sector's ceiling drops onto its floor, so the door starts shut.

Press **P**, walk up to it and press **E**. It opens, waits, and closes again.

## Make it look like a door

Now it works, so give it the right look. Each wall of the door sector can have its own texture.

1. **Right-click the wall facing Room A.** Drag a door picture onto its **top** texture. In
   `Textures/Walls`, look for `DOOR1`, `BIGDOOR1` or `BIGDOOR2`.
2. Do the same for the wall facing Room B.
3. Give the two short walls at the sides `DOORTRAK`, the classic door track.

To see it in Level 1, right-click **Door 1**.

## Add sound

1. **Right-click the door sector.**
2. In the inspector, open its **Dispatches** and find the **Door** dispatch.
3. For **Open Sound** and **Close Sound**, choose `Sounds/World/door open.wav` and
   `Sounds/World/door close.wav`.

While you're there, have a look at what else is in that dispatch: speeds, whether enemies can open
it, and what type of door it is. Each field explains itself if you hover over it.

## Lock it with a key

This takes one minute, and then you've got the core of Doom's level design.

1. **Right-click the door sector** and open its **Door** dispatch.
2. Tick **Locked**.
3. For **Required Tag**, choose `RedKey`.
4. For **Locked Message**, type `You need a red key to open this door.`
5. For **Locked Sound**, choose `Sounds/Player/no way.wav`.
6. From `Prefabs/Pickups/Keys`, drag **Red Key** somewhere the player has to go first.

Press **P**. The door refuses to open until you've got the key, and the key appears on the HUD when
you take it.

The tag names for the other keys are `BlueKey` and `YellowKey`. **Level 2 - Keys** is a full example.

## Other kinds of door

The **Door Type** setting in the Door dispatch has more options:

| Type | What it does |
|---|---|
| **Open, Wait, Close** | The normal door |
| **Open and Stay** | Opens once and never closes |
| **Close, Wait, Open** | Shuts an open door for a while |
| **Close and Stay** | Shuts an open door for good |
| **Toggle** | Opens if it's shut, closes if it's open |

Level 3's monster closets use **Open and Stay**. See [page 9](09-Events-And-Dispatches.md) for how to
open a door with a switch.

## If something goes wrong

| What you see | What to check |
|---|---|
| Nothing happens when you press E | Get closer, within about 20 units, and look straight at the door. Something else in front of the door also blocks the press, so move any decorations out of the way. |
| The door opens only a tiny bit | Its neighbours have low ceilings. A door opens up to the lowest ceiling next to it, so check both rooms. |
| You can't fit through when it's open | Make it wider, or raise the ceilings on both sides. |
| Enemies open it when you don't want them to | Untick **Pathfinders Can Open** in the Door dispatch. |

---

Next: [Add a lift](07-Lifts.md)
