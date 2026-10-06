# Game

`Game` is the table for the **level as a whole**: creating and finding entities, finding the
sector at a point, casting rays, and moving to another level.

```lua
local player

function Start()
    player = Game.FindEntity("Player")
end
```

| Function | Returns | |
|---|---|---|
| [`CreateEntity(isUIEntity)`](#createentity) | `Entity` | Adds a new entity to the level. |
| [`FindEntity(name)`](#finding-entities) | `Entity` or `nil` | The first entity with exactly this name. |
| [`FindEntities(name)`](#finding-entities) | list of `Entity` | Every entity with exactly this name. |
| [`FindEntitiesWithTag(tag)`](#finding-entities) | list of `Entity` | Every entity with this tag. |
| [`GetEntity(id)`](#finding-entities) | `Entity` or `nil` | The entity with this ID. |
| [`GetEntities()`](#finding-entities) | list of `Entity` | Every entity in the level. |
| [`GetSectorAt(position)`](#getsectorat) | `Sector` or `nil` | The sector at a point on the map. |
| [`Raycast(origin, direction, length, ignoredEntityID, requireCollider)`](#raycast) | table or `nil` | What a line hits first. |
| [`LoadLevel(levelName)`](#loadlevel) | | Switches to another level at the end of this frame. |

| Property | Type | | |
|---|---|---|---|
| [`levelName`](#levelname) | string | read-only | The current level's name. |

---

## CreateEntity

```lua
Game.CreateEntity()          -- world entity
Game.CreateEntity(true)      -- UI entity
```

| Parameter | Type | |
|---|---|---|
| `isUIEntity` | boolean, optional | `true` makes a [UI entity](Entity.md#world-entities-and-ui-entities). Leave it out (or pass `false`) for a world entity. |

Adds a new entity to the level right away and returns it. It is the same as placing a new entity in
the editor:

- It is named `"Entity"`, has no tags and is enabled.
- A world entity gets a [Transform](Transform.md) at the origin. A UI entity gets a
  [UI Transform](UITransform.md) instead. It has no other components.
- It gets a new `id`, so `Game.FindEntity`, `Game.GetEntities` and the other lookups find it from
  now on.

Give it more components with [`AddComponent`](Entity.md#adding-and-removing-components), for
example `"Sprite"` to draw it or `"Collider"` to make it solid. Scripts can't be added, so a created
entity can't run a script of its own.

Entities created while the game runs are **not** saved into the level. Stopping the game in the
editor removes them.

```lua
-- Drop a visible marker where the player is standing.
function Update()
    if not Input.GetKeyDown(Key.M) then return end

    local marker = Game.CreateEntity()
    marker.name = "Waypoint"
    marker.transform.position = entity.transform.position
    marker:AddComponent(Component.Sprite).northTextureFileName = "Textures/flag.png"
end
```

---

## Finding entities

```lua
Game.FindEntity(name)            -- Entity or nil
Game.FindEntities(name)          -- { Entity, ... }
Game.FindEntitiesWithTag(tag)    -- { Entity, ... }
Game.GetEntity(id)               -- Entity or nil
Game.GetEntities()               -- { Entity, ... }
```

- Names and tags must match **exactly**, including upper and lower case.
- The lists are ordinary 1-based Lua tables. They are empty, never `nil`, when nothing matches.
- The result is a snapshot. Entities created or destroyed later don't appear in or disappear from
  a list you already have, so check `e.isValid` before using an entity you kept.
- Every call loops over the whole level. Call them once in `Start` and keep the result instead
  of calling them every frame.
- `GetEntity` takes the number from `entity.id` or from a raycast's `entityID`.

```lua
-- Count the enemies left and open the exit when none remain.
public Sector exit = nil

function Update()
    for _, e in ipairs(Game.FindEntitiesWithTag("enemy")) do
        if e.isValid then return end
    end

    exit:MoveCeilingTo(1, exit.floorHeight + 40, 60)
end
```

(For a real level, find the enemies once in `Start` instead of every frame.)

---

## GetSectorAt

```lua
Game.GetSectorAt(position)   -- Sector or nil
```

| Parameter | Type | |
|---|---|---|
| `position` | `Vector2` or `Vector3` | A `Vector2` is a map position (`.y` is world `z`). For a `Vector3`, only `x` and `z` are used. |

Returns the [sector](Sector.md) that contains the point, or `nil` if the point is outside the map.
Height is ignored, and if sectors are nested the innermost one wins. It is the same rule that
decides which sector an entity is in, so `Game.GetSectorAt(e.transform.position)` gives the same
answer as `e:GetSector()`.

```lua
-- Only drop a pickup where there is floor.
local spot = entity.transform.position + Vector3(mathT.RandomF(-32, 32), 0, mathT.RandomF(-32, 32))
local where = Game.GetSectorAt(spot)
if where ~= nil then pickup.transform.position = Vector3(spot.x, where.floorHeight, spot.z) end
```

---

## Raycast

```lua
Game.Raycast(origin, direction, length, ignoredEntityID, requireCollider)   -- table or nil
```

| Parameter | Type | |
|---|---|---|
| `origin` | `Vector3` | Where the ray starts. |
| `direction` | `Vector3` | Which way it goes. It doesn't need length 1. A zero vector never hits anything. |
| `length` | number | How far it reaches. |
| `ignoredEntityID` | integer | An entity the ray passes through, usually the caster's own `entity.id`. Pass `0` to ignore nothing. |
| `requireCollider` | boolean | See below. |

All five arguments are required.

The ray stops at the **first** thing it hits: a wall, an entity, a floor or a ceiling. If it hits
nothing within `length`, the result is `nil`. Otherwise it is a table:

| Field | Type | |
|---|---|---|
| `type` | string | `"Entity"`, `"Wall"`, `"SectorFloor"` or `"SectorCeiling"`. |
| `typeID` | integer | The same as a number: `1` Entity, `2` Wall, `3` SectorFloor, `4` SectorCeiling. |
| `position` | `Vector3` | The point that was hit. |
| `distance` | number | From `origin` to `position`. |
| `entity` | `Entity` or `nil` | Set when an entity was hit. |
| `wall` | [`Wall`](Wall.md) or `nil` | Set when a wall was hit. |
| `sector` | [`Sector`](Sector.md) or `nil` | Set when a floor or ceiling was hit. |
| `entityID`, `wallID`, `sectorID` | integer | The IDs of the above. When nothing of that kind was hit, they hold a large invalid number, so test `hit.entity ~= nil` rather than the ID. |

### What the ray can hit

- **Walls**: every solid part of a wall, including the steps above and below an opening.
  The open part of a doorway lets the ray through.
- **Floors and ceilings** of every interval of every sector. Slopes are **not** taken into
  account: a sloped surface is hit as if it were flat at its **Height** setting.
- **Entities**, depending on `requireCollider`:
  - `true`: only entities with an active, non-trigger [Collider](Collider.md). Sphere and box
    shapes are both used.
  - `false`: every entity with a [Transform](Transform.md), using a box the size of its
    `scale`, standing on its position. Colliders are ignored. This also hits things like
    invisible markers, so `true` is usually what you want.

For a ray from a camera through the crosshair, use
[`camera:Raycast([length], [requireCollider])`](Camera.md#scripting). It starts at the eye
(`transform.position` is at the **feet**), follows the camera's current `yaw` and `pitch`, ignores
the camera's own entity, and returns the same table. `camera:ScreenToRay(x, y)` gives the
`origin, direction` for any other point on screen.

```lua
-- Scripts/Shoot.lua (on the player): damage whatever is under the crosshair.
public number damage = 20

function Update()
    if not Input.GetMouseButtonDown(Input.MouseLeft) then return end

    local hit = entity.camera:Raycast(2000, true)
    if hit == nil then return end

    if hit.entity ~= nil then
        local health = hit.entity:GetScript("Health")
        if health.isValid then health:TakeDamage(damage) end
    elseif hit.wall ~= nil then
        Debug.Print("Hit a wall at", hit.position)
    end
end
```

---

## LoadLevel

```lua
Game.LoadLevel(levelName)
```

| Parameter | Type | |
|---|---|---|
| `levelName` | string | A level's file name, with or without `.bson`: `"Level2"` or `"Level2.bson"`. The file can be in any folder under `Assets`. |

Switches the game to another level. The level is loaded from its **saved file**, so in the editor,
unsaved changes to that level aren't included. Save it before pressing Play if you want them.

**When it happens.** The call only queues the switch, the same way
[`entity:Destroy()`](Entity.md#destroying-entities) is queued. The rest of the frame carries on in
the old level: code after the call still runs, and `Game.levelName` is still the old name. Once the
frame is over:

1. Every script in the old level gets `OnDestroy`.
2. The new level replaces the old one. Its entities, sectors, sounds and background are all its
   own, exactly as saved.
3. The new level starts just like the first one does: its camera and Player Controller are picked,
   and every script gets `OnEnable` and `Start`.

If `LoadLevel` is called several times in one frame, the last call wins. Calls made from
`OnDestroy` during a switch are ignored. Calls from the new level's `Start` are fine: they switch
again after the new level's first frame.

**What carries over.** Only the [`Global`](Global.md) table. Everything else belongs to the level.
Each level has its own player entity, placed in the editor. Entities, sectors and walls you kept in
variables or in `Global` don't carry over; see [Global](Global.md#rules).

**Errors.** A name that isn't a string, a level that doesn't exist, or a name that more than one
level file has, raises an error at the call, and nothing is queued. If the file exists but can't be read, the error is logged and the game stays
on the current level.

**In the editor.** Pressing **Stop** always puts you back on the level you pressed **Play** on,
whatever level the game ended on.

Loading happens in one go, so the game may freeze for a moment while a big level loads.

```lua
-- Scripts/Flow/Exit.lua (trigger at the end of the level)
public string nextLevel = "Level2"

function OnTriggerEnter(other)
    if other.hasPlayerController then Game.LoadLevel(nextLevel) end
end
```

```lua
-- Restart the current level when the player falls out of the map.
function Update()
    if entity:GetSector() == nil then Game.LoadLevel(Game.levelName) end
end
```

---

## levelName

```lua
Game.levelName   -- "Level2"
```

The current level's name: its file name in the `Levels` folder, without `.bson`. It changes when
the new level starts, not when `LoadLevel` is called. Assigning to it raises an error.
