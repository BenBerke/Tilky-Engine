# Sector

A sector is a **room**. On the map it is a 2D shape, a polygon you draw in the map editor. In the
game it becomes a space with a floor at one height and a ceiling at another. Its outline becomes
[walls](Wall.md): solid where nothing is on the other side, and openings where another sector
shares the edge. A step, a window, a doorway and a lift are all just two sectors next to each other
with different floor and ceiling heights.

Sectors also hold a **light colour**, **tags** and **scripts**. Sector scripts are how you build
doors, lifts, crushers, traps and flickering lights.

This page explains what sectors are and what every setting does. The complete list of sector
functions, with full parameter details, is in [`docs/sector.md`](../sector.md).

---

## How a sector works

### Floors and ceilings

Every sector has at least one **floor interval**: a floor surface at the bottom and a ceiling
surface at the top. The space between them is where things can stand. A new sector's floor is at
height `0` and its ceiling at `40`.

A sector can have **several** intervals stacked on top of each other (**Add Floor** in the
inspector). That makes a multi-storey room: a balcony above a hall, or a bridge over a pit. The
intervals are numbered from `1` at the bottom. Each one must sit entirely above the one below it:
a floor has to stay below its own ceiling and can't reach into the interval under it.

Entities stand in whichever interval fits them best at their height. Physics keeps them between
that interval's floor and ceiling.

### Surfaces

Each floor and each ceiling is a **surface** with its own settings:

| Setting | Meaning |
|---|---|
| Height | World height of the surface. |
| Texture | Image drawn on the surface. Empty means no texture, just the colour. |
| Color | Tint, `0..1` per channel. |
| Slope Direction / Strength | Tilts the surface. See below. |
| Texture Offset / Scale / Flip X / Flip Y | How the texture is placed on the surface. |

### Slopes

A surface can be tilted so that it rises along one axis: **+X**, **-X**, **+Z** or **-Z**. The
**Strength** sets how steep it is. The surface rises by `strength × 0.01745` units for every unit
you move along the slope direction. So a strength of `45` rises about 0.79 units per unit, a
little under 45°. The rise is measured from the edge of the sector's bounding rectangle on the
"low" side. So the **Height** setting is the height at that edge, and the surface climbs from
there.

Physics, rendering and `sector:GetFloorHeightAt(point)` all use the same slope maths, so what you
see is what entities stand on.

### Light

`light` is the sector's light colour, `0..255` per channel. `255, 255, 255` means fully lit and
lower values darken everything in the sector: walls, floors, ceilings, and the sprites and models
of entities standing in it. Tinted light like `255, 80, 80` gives a red room.

### Parent and child sectors

A sector can have a **Parent Sector** (in the inspector). Its children then **follow** it: when you
change the parent's heights, colours, slopes, texture offsets or light, every child changes **by the
same amount**. For example, raising a parent's floor from 10 to 20 takes a child floor from 5 to 15. Texture
names, slope directions and flips are copied over when the parent's change. This works for edits
in the inspector, for script changes and for moves started by scripts. It's how a pillar or
platform inside a room rises with the room's floor.

Separately from that setting, a sector drawn completely inside another one is cut out of it
geometrically. The inner sector is a hole in the outer one's floor, like an island or a pillar.
An entity standing on the inner sector is inside the **inner** sector, not the outer one.

### Occupancy

The engine always knows which entities are inside each sector:

- An entity is inside the sector that contains its `(x, z)` position. **Height is ignored**, so all
  storeys of a multi-storey sector count as the same sector.
- An entity is inside **exactly one** sector: the innermost one.
- Entities without a [Transform](Transform.md) (UI entities) are never inside any sector.
- Membership updates every frame for every entity that moved, however it moved.

This drives `sector:ContainsEntity`, `sector:GetEntities`, `entity:GetSector()`, and the
`OnEntityEnter` / `OnEntityExit` / `OnSectorChange` callbacks.

---

## In the editor

| Section | Field | Lua | Notes |
|---|---|---|---|
| | **Name** | `name` | Free text, handy for finding the sector in scripts and logs. |
| **Parent Sector** | **Parent** | | The logical parent. See [Parent and child sectors](#parent-and-child-sectors). |
| **Floor** (per interval) | **Floor Height** / **Ceiling Height** | `GetFloor(i).floorHeight` / `ceilingHeight` | |
| | **Floor Texture** / **Ceiling Texture** | `floorTexture` / `ceilingTexture` | |
| | **Floor Color** / **Ceiling Color** | `floorColor` / `ceilingColor` | `Vector4`, `0..1`. |
| | **Floor Slope** / **Ceiling Slope** (Direction, Strength) | | Editor only. |
| | **Texture Offset / Scale / Flip** | | Editor only. |
| | **Add Floor** / **Remove Floor** | | Adds or removes an interval. |
| **Lighting** | **Light Color** | `light` | `Vector3`, `0..255`. |
| **Tags** | | `HasTag`, `GetTag`, `tagCount` | Read-only from scripts. |
| **Scripts** | **Add Script** / **Remove Script** | | Attach any number of Lua scripts. |

---

## Scripting

### Getting a sector

| Where from | How |
|---|---|
| A script attached to the sector | the global `sector` |
| A public field | `public Sector door` |
| A point on the map | `Game.GetSectorAt(position)` |
| An entity | `entity:GetSector()` |
| A raycast hit | `hit.sector` |
| A neighbour | `sector:GetNeighbor(i)` |

A `Sector` value is a safe handle. If the sector is deleted, `isValid` becomes `false` and any other
use raises an error.

### Properties

| Property | Type | | Description |
|---|---|---|---|
| `id` | integer | read-only | Stable ID, the `#` number shown in the editor. |
| `isValid` | boolean | read-only | `false` once the sector no longer exists. |
| `name` | string | read/write | |
| `floorHeight` | number | read/write | Floor height of interval 1. Same as `GetFloor(1).floorHeight`. |
| `light` | Vector3 | read/write | `0..255`. Children follow. Setting it cancels a running `FadeLight`. |
| `floorCount` | integer | read-only | Number of floor intervals. |
| `vertexCount` | integer | read-only | Corners of the outline. |
| `wallCount` | integer | read-only | Walls on the outline. |
| `entityCount` | integer | read-only | Entities inside right now. |
| `neighborCount` | integer | read-only | Sectors sharing a wall with this one. |
| `tagCount` | integer | read-only | |

### Functions

| Group | Functions |
|---|---|
| Floors | `GetFloor(i)` returns a `SectorFloor` with read/write `floorHeight`, `ceilingHeight`, `floorColor`, `ceilingColor`, `floorTexture`, `ceilingTexture`, plus `clearFloorTexture()` and `clearCeilingTexture()`. `GetFloorHeightAt(point[, i])` and `GetCeilingHeightAt(point[, i])` include slopes. |
| Movement | `MoveFloorToCeiling(i, speed[, gap])`, `MoveCeilingToFloor(i, speed[, gap])`, their `...OverTime(i, seconds[, gap])` versions, `MoveFloorTo(i, height, speed)`, `MoveCeilingTo(i, height, speed)`, their `...OverTime(i, height, seconds)` versions, `IsMoving(i)`, `IsFloorMoving(i)`, `IsCeilingMoving(i)`, `StopMoving(i)` |
| Light | `FadeLight(color, seconds)`, `IsLightFading()` |
| Occupancy | `ContainsEntity(e)`, `ContainsEntityWithTag(tag)`, `GetEntities()`, `GetEntitiesWithTag(tag)`, `CountEntities([tag])`, `IsEmpty()`, `GetEntity(i)` |
| Shape | `GetArea()`, `GetCenter()`, `GetBounds()` (returns two values), `RandomPointInside([i])` |
| Geometry | `GetVertex(i)`, `GetWall(i)`, `GetNeighbor(i)` |
| Tags | `HasTag(tag)`, `GetTag(i)` |

> **Any image in `Assets` can be switched to.** When the level loads, the engine packs every
> `.png`, `.jpg` and `.jpeg` file under `Assets` into the texture atlas, so a script can switch to
> an image the level doesn't use yet. An image added to `Assets` while the game is running is only
> picked up the next time the level loads.

Every move is **fire-and-forget**: call it once and the engine moves the surface a little each
frame until it arrives. No `Update` code is needed. Starting another move on the same surface
replaces the old one. A floor never passes its ceiling, and neither passes into the next interval.

Full parameters, rules and more examples: [`docs/sector.md`](../sector.md).

### Callbacks

Sector scripts can define these, on top of `Start`, `Update` and the other
[lifecycle callbacks](CallbackFunctions.md):

| Callback | Called when |
|---|---|
| `OnEntityEnter(entity)` | An entity moved into this sector, at the end of that frame. |
| `OnEntityExit(entity)` | An entity moved out, left the map or was destroyed while inside. |

Entities already inside when the level starts don't trigger `OnEntityEnter`.

Entity scripts have the opposite view: `OnSectorChange(sector)` runs on an entity when **it**
changes sector. See [Callback Functions](CallbackFunctions.md#onsectorchange).

### Things to know

- In a sector script, `entity` exists but is an empty placeholder (`entity.isValid == false`).
  Use `sector`.
- Other scripts can't reach a sector script through `GetScript`. Share state through the `Global`
  table, or have the sector script look up the entities it needs.
- Setting a height that would put a floor at or above its ceiling, or into another interval, raises
  an error. Wrap uncertain writes in `pcall`.

---

## Examples

### Door that opens when the player walks up

```lua
-- Scripts/Doors/ProximityDoor.lua (sector script on the door sector)
-- Draw the door as a thin sector whose ceiling starts at its floor (closed).
public number openHeight = 40
public number speed = 80
public number range = 48
public number stayOpen = 3

local player
local openTimer = 0

function Start()
    player = Game.FindEntity("Player")
end

function Update()
    if player == nil or not player.isValid then return end

    local center = sector:GetCenter()
    local p = player.transform.position
    local near = Vector2(p.x - center.x, p.z - center.y).length < range

    if near then
        openTimer = stayOpen
        sector:MoveCeilingTo(1, sector.floorHeight + openHeight, speed)
    elseif openTimer > 0 then
        openTimer = openTimer - GameTime.deltaTime
        if openTimer <= 0 then sector:MoveCeilingToFloor(1, speed) end
    end
end
```

### Lift that rides up when stepped on

```lua
-- Scripts/Lifts/StepLift.lua (sector script on the lift platform)
public number topHeight = 64
public number speed = 30
public number wait = 2

local bottom
local timer = 0

function Start()
    bottom = sector.floorHeight
end

function OnEntityEnter(e)
    if e.hasPlayerController then sector:MoveFloorTo(1, topHeight, speed) end
end

function Update()
    -- Once it's at the top and empty, wait, then go back down.
    if sector:IsFloorMoving(1) or sector.floorHeight == bottom then return end

    if sector:IsEmpty() then
        timer = timer + GameTime.deltaTime
        if timer >= wait then
            timer = 0
            sector:MoveFloorTo(1, bottom, speed)
        end
    else
        timer = 0
    end
end
```

### Flickering light

```lua
-- Scripts/Lights/Flicker.lua (sector script)
public Vector3 onColor = Vector3(255, 240, 200)
public Vector3 offColor = Vector3(40, 35, 30)

local timer = 0

function Update()
    timer = timer - GameTime.deltaTime
    if timer > 0 then return end

    timer = mathT.RandomF(0.05, 0.4)
    sector:FadeLight(mathT.RandomBool() and onColor or offColor, 0.05)
end
```

### Spawn points spread across a room

```lua
-- Scripts/Scatter.lua (sector script)
-- Moves every entity tagged "pickup" to a random spot in this sector.
function Start()
    for _, e in ipairs(Game.FindEntitiesWithTag("pickup")) do
        e.transform.position = sector:RandomPointInside()
    end
end
```
