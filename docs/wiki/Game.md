# Game

`Game` is the table for questions about the **level as a whole**: finding entities, finding the
sector at a point, and casting rays.

```lua
local player

function Start()
    player = Game.FindEntity("Player")
end
```

| Function | Returns | |
|---|---|---|
| [`FindEntity(name)`](#finding-entities) | `Entity` or `nil` | The first entity with exactly this name. |
| [`FindEntities(name)`](#finding-entities) | list of `Entity` | Every entity with exactly this name. |
| [`FindEntitiesWithTag(tag)`](#finding-entities) | list of `Entity` | Every entity with this tag. |
| [`GetEntity(id)`](#finding-entities) | `Entity` or `nil` | The entity with this ID. |
| [`GetEntities()`](#finding-entities) | list of `Entity` | Every entity in the level. |
| [`GetSectorAt(position)`](#getsectorat) | `Sector` or `nil` | The sector at a point on the map. |
| [`Raycast(origin, direction, length, ignoredEntityID, requireCollider)`](#raycast) | table or `nil` | What a line hits first. |
| [`LoadLevel(levelName)`](#loadlevel) | | Replaces the current level. Not ready for gameplay yet. |

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
---@field exit Sector
exit = nil

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

`transform.position` is at the **feet**. A "look" ray should start at eye height
(`position.y + playerController.eyeHeight`) and point along the [camera](Camera.md)'s `forward`.

```lua
-- Scripts/Shoot.lua (on the player): damage whatever is under the crosshair.
---@field damage number
damage = 20

function Update()
    if not Input.GetMouseButtonDown(Input.MouseLeft) then return end

    local p = entity.transform.position
    local eye = Vector3(p.x, p.y + entity.playerController.eyeHeight, p.z)
    local hit = Game.Raycast(eye, entity.camera.forward, 2000, entity.id, true)
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

Loads `levelName` from the project's `Levels` folder (with or without the `.bson` extension) and
replaces the current level with it.

> **Not ready for gameplay yet.** It swaps the level immediately, in the middle of the calling
> script, and doesn't start the new level's scripts or player controller. Use it for tools and
> experiments, not for moving the player to the next level.
