# Sector API

Everything a Lua script can do with a sector.

## Getting a sector

| Where from | How |
|---|---|
| A script attached to the sector | the global `sector` |
| A public field | `---@field door Sector` |
| A point in the map  | `Game.GetSectorAt(position)` |
| An entity  | `entity:GetSector()` |
| A raycast hit | `hit.sector` |
| A neighbour | `sector:GetNeighbor(i)` |

## Conventions

- **Indices are 1-based**: floors, vertices, walls, entities, neighbours and tags.
- **Map space.** A `Vector2` position is `(x, z)` on the map: `.x` is world x and `.y` is world z.
  Functions that take a position accept a `Vector3` too and use only its `x` and `z`.
- **`floorIndex`** picks one floor/ceiling interval of a multi-storey sector. Most sectors have
  only one, `1`. When it's optional, it defaults to `1`.
- **Bad arguments raise a Lua error.** Examples: a floor index out of range, a speed of 0 or a
  negative time. Wrap the call in `pcall` if the values come from somewhere you don't control.
- **Every sector handle stays safe.** If the sector no longer exists, `isValid` is `false` and any
  other call raises an error.

---

## Game.GetSectorAt

```lua
Game.GetSectorAt(position) -> Sector?
```

| Parameter | Type | |
|---|---|---|
| `position` | `Vector2` or `Vector3` | Map position. Height is ignored. |

Returns the sector containing `position`, or `nil` if the point is outside every sector. It uses
the same lookup that decides which sector an entity is in (see [Occupancy](#occupancy)).

```lua
local here = Game.GetSectorAt(entity.transform.position)
if here ~= nil and here:HasTag("water") then Debug.Print("splash") end
```

## entity:GetSector

```lua
entity:GetSector() -> Sector?
```

Returns the sector the entity is standing in. It returns `nil` if the entity is outside the map
or has no Transform (UI entities, for example). This is the same membership `ContainsEntity` and
`OnEntityEnter` use.

---

## Properties

| Property | Type | | Notes |
|---|---|---|---|
| `id` | integer | read-only | Stable sector ID (the `#` number in the editor). |
| `isValid` | boolean | read-only | `false` once the sector no longer exists. |
| `name` | string | read/write | Set in the sector inspector. Empty if unnamed. |
| `floorHeight` | number | read/write | Floor height of floor 1. Same as `GetFloor(1).floorHeight`. |
| `light` | Vector3 | read/write | Light colour, 0-255 per channel. Changes reach child sectors. Setting it cancels a running `FadeLight`. |
| `floorCount` | integer | read-only | Number of floor/ceiling intervals. |
| `vertexCount` | integer | read-only | |
| `wallCount` | integer | read-only | |
| `entityCount` | integer | read-only | Entities inside right now. Same as `CountEntities()`. |
| `neighborCount` | integer | read-only | |
| `tagCount` | integer | read-only | Tags are set in the editor; scripts can't add or remove them. |

---

## Occupancy

### How "inside" is decided

- An entity is inside the sector that contains its `(x, z)` position. **Height is ignored**, so
  on a multi-storey sector every storey counts as the same sector.
- An entity is inside **exactly one** sector: the innermost one. An entity standing on a pillar
  or platform that is a child sector is inside the child, **not** the parent.
- Entities without a Transform are never inside a sector.
- Membership updates every frame for every entity that moved, whether a Rigidbody, the player
  controller or a script moved it.

### ContainsEntity

```lua
sector:ContainsEntity(entity) -> boolean
```

| Parameter | Type | |
|---|---|---|
| `entity` | `Entity` | The entity to look for. |

Returns `true` if `entity` is inside this sector.

```lua
---@field player Entity
player = nil

function Update()
    if sector:ContainsEntity(player) then sector:FadeLight(Vector3(255, 80, 80), 0.5) end
end
```

### ContainsEntityWithTag 

```lua
sector:ContainsEntityWithTag(tag) -> boolean
```

| Parameter | Type | |
|---|---|---|
| `tag` | string | Tag name, as created in Project Settings. |

Returns `true` if at least one entity inside this sector has `tag`. An unknown tag returns
`false`.

```lua
if not sector:ContainsEntityWithTag("enemy") then OpenExitDoor() end
```

### GetEntities 

```lua
sector:GetEntities() -> Entity[]
```

Every entity inside this sector, as a Lua list. The order isn't meaningful.

### GetEntitiesWithTag 

```lua
sector:GetEntitiesWithTag(tag) -> Entity[]
```

Every entity inside this sector that has `tag`.

```lua
for _, e in ipairs(sector:GetEntitiesWithTag("burnable")) do
    e:GetScript("Health"):TakeDamage(5)
end
```

### CountEntities 

```lua
sector:CountEntities() -> integer
sector:CountEntities(tag) -> integer
```

| Parameter | Type | |
|---|---|---|
| `tag` | string, optional | Count only entities with this tag. |

The number of entities inside, or only those with `tag`.

### IsEmpty 

```lua
sector:IsEmpty() -> boolean
```

Returns `true` if no entity is inside this sector.

### GetEntity

```lua
sector:GetEntity(index) -> Entity
```

The `index`-th entity inside (1-based, up to `entityCount`). This is the old way to walk the list.
`GetEntities()` is usually simpler.

### OnEntityEnter / OnEntityExit 

These are callbacks. You **define** them in a script attached to a sector and the engine calls
them.

```lua
function OnEntityEnter(entity) end
function OnEntityExit(entity) end
```

| Parameter | Type | |
|---|---|---|
| `entity` | `Entity` | The entity that came in or went out. |

- They fire once, at the end of the frame the entity crossed the boundary, after all movement and
  physics for that frame.
- Entities already inside when the level starts **do not** trigger `OnEntityEnter`. Use
  `sector:GetEntities()` in `Start` if you need them.
- Moving from one sector to another fires the old sector's `OnEntityExit` first, then the new
  sector's `OnEntityEnter`.
- An entity that is destroyed while inside, or that leaves the map entirely, also fires
  `OnEntityExit`. After a destroy, `entity.isValid` is `false`, so check it before using the
  entity.
- They only run on **sector** scripts, and only while the script is enabled. On an entity script
  they are ignored.
- `OnEntityEnter` and `OnEntityExit` are reserved names: you can't use them for public fields.

```lua
-- Trap room: slam the door behind the player, reopen it when the room is clear.
---@field door Sector
door = nil

function OnEntityEnter(entity)
    if entity.hasPlayerController then door:MoveCeilingToFloor(1, 200) end
end

function OnEntityExit(entity)
    if sector:CountEntities("enemy") == 0 then door:MoveFloorToCeiling(1, 60) end
end
```

---

## Floors

### GetFloor

```lua
sector:GetFloor(floorIndex) -> SectorFloor
```

One floor/ceiling interval. `SectorFloor` has `floorHeight`, `ceilingHeight`, `floorColor`,
`ceilingColor`, `floorTexture` and `ceilingTexture` (all read/write), plus `index`, `isValid`,
`clearFloorTexture()` and `clearCeilingTexture()`. Writing a height raises an error if the floor
would reach its ceiling or overlap another interval.

### GetFloorHeightAt 

```lua
sector:GetFloorHeightAt(position) -> number
sector:GetFloorHeightAt(position, floorIndex) -> number
```

| Parameter | Type | |
|---|---|---|
| `position` | `Vector2` or `Vector3` | Map position (x, z). |
| `floorIndex` | integer, optional | Default `1`. |

The floor height at `position`, **including slope**. On a flat floor this is just
`GetFloor(i).floorHeight`. On a sloped one, it's the height where something standing at
`position` would actually be. It uses the same slope math as physics and rendering. Points
outside the sector are extrapolated along the slope.

```lua
local p = Vector2(12, 4)
spawned.transform.position = Vector3(p.x, sector:GetFloorHeightAt(p), p.y)
```

### GetCeilingHeightAt 

```lua
sector:GetCeilingHeightAt(position) -> number
sector:GetCeilingHeightAt(position, floorIndex) -> number
```

Same as `GetFloorHeightAt`, for the ceiling.

---

## Movement

Every move below is **fire-and-forget**. You call it once and the engine moves the surface a bit
each frame until it arrives. No `Update` code is needed.

- Starting a new move on the same surface replaces the old one.
- A floor never passes its ceiling and a ceiling never passes its floor (they stop 0.01 apart).
  Neither passes into the interval above or below.
- Moves pass on to child sectors, the same as an edit in the inspector.

### MoveFloorToCeiling / MoveCeilingToFloor

```lua
sector:MoveFloorToCeiling(floorIndex, speed)
sector:MoveFloorToCeiling(floorIndex, speed, gap)
sector:MoveCeilingToFloor(floorIndex, speed)
sector:MoveCeilingToFloor(floorIndex, speed, gap)
```

| Parameter | Type | |
|---|---|---|
| `floorIndex` | integer | Which interval. |
| `speed` | number | Units per second, above 0. |
| `gap` | number, optional | Stop this far short of the opposite surface. Default 0 (really 0.01). Not negative. |

Moves the floor up to its ceiling, or the ceiling down to its floor. The target follows the
opposite surface, so it still stops at the right place if that surface is moving too.

### MoveFloorToCeilingOverTime / MoveCeilingToFloorOverTime

```lua
sector:MoveFloorToCeilingOverTime(floorIndex, seconds)
sector:MoveFloorToCeilingOverTime(floorIndex, seconds, gap)
sector:MoveCeilingToFloorOverTime(floorIndex, seconds)
sector:MoveCeilingToFloorOverTime(floorIndex, seconds, gap)
```

The same moves, but arriving after `seconds` (not negative; `0` snaps next frame) instead of at a
set speed.

### MoveFloorTo / MoveCeilingTo 

```lua
sector:MoveFloorTo(floorIndex, height, speed)
sector:MoveCeilingTo(floorIndex, height, speed)
```

| Parameter | Type | |
|---|---|---|
| `floorIndex` | integer | Which interval. |
| `height` | number | Absolute world height to move to. Can be above or below the current height. |
| `speed` | number | Units per second, above 0. |

Moves the floor or ceiling to an exact height, up or down. Use these for multi-stop lifts,
crushers that stop partway and rising water. If `height` is past the opposite surface, the move
stops just short of it.

```lua
-- Three-stop lift.
local stops = {0, 64, 128}
local current = 1

function GoTo(stop)
    current = stop
    sector:MoveFloorTo(1, stops[stop], 40)
end
```

### MoveFloorToOverTime / MoveCeilingToOverTime 

```lua
sector:MoveFloorToOverTime(floorIndex, height, seconds)
sector:MoveCeilingToOverTime(floorIndex, height, seconds)
```

`MoveFloorTo` / `MoveCeilingTo`, arriving after `seconds` (not negative).

### IsMoving

```lua
sector:IsMoving(floorIndex) -> boolean
```

Returns `true` while this interval's floor **or** ceiling is still moving.

### IsFloorMoving / IsCeilingMoving

```lua
sector:IsFloorMoving(floorIndex) -> boolean
sector:IsCeilingMoving(floorIndex) -> boolean
```

The same check for just one surface. For example, a door can wait for its ceiling to finish
opening before starting its close timer:

```lua
local waiting = false

function Open()
    sector:MoveCeilingTo(1, 128, 80)
    waiting = true
end

function Update()
    if waiting and not sector:IsCeilingMoving(1) then
        waiting = false
        -- fully open: start the close timer here
    end
end
```

### StopMoving

```lua
sector:StopMoving(floorIndex)
```

Stops this interval's floor and ceiling where they are.

---

## Light

### FadeLight

```lua
sector:FadeLight(color, seconds)
```

| Parameter | Type | |
|---|---|---|
| `color` | `Vector3` | Target light, 0-255 per channel. |
| `seconds` | number | Fade length. Not negative; `0` snaps next frame. |

Fades `light` from its current value to `color` over `seconds`, in a straight line. It runs by
itself each frame and child sectors follow along. Starting another fade replaces this one, and
setting `sector.light` directly cancels it.

```lua
-- Power cut
sector:FadeLight(Vector3(20, 20, 30), 2.0)
```

### IsLightFading

```lua
sector:IsLightFading() -> boolean
```

Returns `true` while a `FadeLight` is running.

---

## Shape

All shape values are in map space. Child sectors cut out of this one (pillars, islands) are
**not** part of its area.

### GetArea

```lua
sector:GetArea() -> number
```

Floor area in square units.

### GetCenter

```lua
sector:GetCenter() -> Vector2
```

The area-weighted centre, `(x, z)`. For L-shaped or ring-shaped sectors this point can fall
**outside** the sector, like any centroid. Use `RandomPointInside` when you need a point that is
guaranteed to be inside.

### GetBounds

```lua
sector:GetBounds() -> Vector2, Vector2
```

Returns **two** values: the minimum and maximum corner of the sector's bounding rectangle,
`(x, z)`.

```lua
local min, max = sector:GetBounds()
local width, depth = max.x - min.x, max.y - min.y
```

### RandomPointInside

```lua
sector:RandomPointInside() -> Vector3
sector:RandomPointInside(floorIndex) -> Vector3
```

| Parameter | Type | |
|---|---|---|
| `floorIndex` | integer, optional | Which floor to stand the point on. Default `1`. |

Returns a random point inside the sector, evenly spread over its area and never inside a child
sector. `y` is the floor height at that spot, slope included, so the point is ready to use as an
entity position. It uses the same generator as `mathT.Random*`, so `mathT.RandomSeed` makes it
repeatable.

```lua
for i = 1, 5 do
    local e = Game.FindEntity("Coin" .. i)
    if e ~= nil then e.transform.position = sector:RandomPointInside() end
end
```

---

## Geometry and links

| Function | Returns | |
|---|---|---|
| `GetVertex(index)` | `Vector2` | Outer boundary corner, 1-based. |
| `GetWall(index)` | `Wall` | A wall on this sector's boundary, 1-based. |
| `GetNeighbor(index)` | `Sector` | A sector sharing a wall with this one, 1-based. |

## Tags

| Function | Returns | |
|---|---|---|
| `HasTag(tag)` | boolean | `true` if this sector has `tag`. |
| `GetTag(index)` | string | The `index`-th tag, 1-based. |

Tags are assigned in the editor. Scripts can read them but not change them.
