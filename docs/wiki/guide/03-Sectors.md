# 3. Sectors: How Maps Work

Everything in a Doom-style map comes down to one idea, and it's a simple one.

**A sector is a shape on the floor plan, with a floor height and a ceiling height.**

That's a room. You draw its outline from above, and you type in how high its floor and ceiling are.
The engine builds the 3D walls for you.

```
   Seen from above:              Seen from the side:

   +-----------+                 ceiling 48  -------------
   |           |                             |           |
   |  a sector |                             |  a sector |
   |           |                             |           |
   +-----------+                 floor 0     -------------
```

## Each sector has

| Setting | What it does |
|---|---|
| **Floor Height** | How high the floor is |
| **Ceiling Height** | How high the ceiling is |
| **Floor Texture**, **Ceiling Texture** | The pictures on them |
| **Light Color** | How bright the room is. Lower numbers make it darker. |

## How big is everything?

These sizes are used all through the kit, so they're worth remembering:

| Thing | Size |
|---|---|
| The player's eyes | About 10 above the floor |
| The player's height | 14 |
| A room with a low ceiling | 48 |
| A big room | 72 to 96 |
| The tallest step the player can climb | 6 |
| A door opening | 32 wide |

## Where two sectors touch

This is where it gets interesting. When two sectors share an edge, that edge is an opening you can
walk through. The engine works out what wall to draw from the difference in heights.

```
   A step up: the second room's floor is higher.
   The engine draws a wall piece in the gap.

                       ------------------
   ---------------
                       ------------------
   floor 0         |   floor 4
   ---------------     (the step you see)
```

If the difference is 6 or less, the player can walk up it. That's a stair. Higher than that and it's
a ledge the player can't climb.

Almost everything in a Doom map is this trick:

| You want | Make it with |
|---|---|
| **Stairs** | Several thin sectors, each floor a little higher than the last |
| **A window** | A sector with a raised floor and a lowered ceiling. You can see through but not walk through. |
| **A pool** | A sector inside a room, with a lower floor |
| **A pillar** | A sector inside a room with its floor and ceiling at the same height, so nothing fits |
| **A door** | A thin sector whose ceiling starts on its floor, and rises when you press Use |
| **A lift** | A sector whose floor moves |
| **Outdoors** | A sector whose ceiling is invisible, so the sky shows through |

**A sector with its floor and ceiling at the same height is solid.** Nothing fits inside it. You'll
see this in Level 3: the building is a solid block with a room drawn inside it.

## Things that catch people out

**Rooms overlap by joining, not by stacking.** Drawing a sector inside another cuts a hole in the
outer one and puts the new sector in the hole. The outer room becomes a ring around it.

**Everything outside your sectors is solid.** There's no ground outside your map. The edge of a
sector with nothing beyond it is a solid wall.

**Keep ceilings above floors.** If you lower a ceiling below a floor, the inspector marks it as
invalid.

---

Next: [Build your first room](04-Your-First-Room.md)
