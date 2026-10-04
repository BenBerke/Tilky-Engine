# Transform

**Inspector name:** Transform · **Lua:** `entity.transform` · **Public field type:** `Transform`

The Transform says **where an entity is**: its position, rotation and size in the world. Every
world entity gets one when it is created, and almost every other component depends on it. Sprites
and models are drawn at it, physics moves it, audio plays from it, and the camera looks out from
it. [UI entities](Entity.md#world-entities-and-ui-entities) use a [UI Transform](UITransform.md)
instead.

## How it works

### Position is at the feet

`position` is the point at the **bottom** of the entity, where it touches the floor, not its
centre:

- A sprite is drawn upward from it.
- A sphere collider sits on it: its centre is one radius above `position`.
- The player camera is `eyeHeight` above it.

So an entity standing on a floor at height 0 has `position.y == 0`.

### Sector membership

Every frame, any entity whose Transform changed is assigned to the [sector](Sector.md) that
contains its `(x, z)` position. This drives `entity:GetSector()`, the sector's occupancy lists and
the `OnSectorChange` / `OnEntityEnter` / `OnEntityExit` callbacks. It doesn't matter what moved the
entity: physics, the player controller or a script.

`relativeHeight` is the height of the feet above the current floor. Physics keeps it up to date for
entities with a Rigidbody and a Collider, and uses it to decide whether gravity applies.

### Scale

`scale` means something different to each component that reads it:

| Used by | How |
|---|---|
| [Sprite](Sprite.md) | `scale.x` is the sprite's **width**, `scale.z` its **height**. `scale.y` is not used. |
| [Model](Model.md) | Multiplies the model on each axis. |
| Physics | `scale.y` is the body height used for ceiling collision (at least the collider's diameter). |

New entities start at `32, 32, 32`.

### Rotation

`rotation` is a quaternion. The inspector shows it as X/Y/Z angles in degrees. From Lua it is a
`Vector4` `(x, y, z, w)`.

An entity **faces its local +Z**. With rotation `0, 0, 0` it faces +Z on the map, and rotation Y
turns it left and right the same way as camera `yaw`, so Y `90` faces +X. The rotation is used by:

- [Models](Model.md) and **static** [sprites](Sprite.md), which turn with it fully.
- 4- and 8-direction [sprites](Sprite.md#directions), which use the facing flattened onto
  the map to pick which of their images you see.
- The [Audio Source's](AudioSource.md#sound-cone) sound cone, which points along the facing,
  tilted up or down too.

It does not move the camera. The camera has its own `yaw` and `pitch` (see [Camera](Camera.md)).

## In the editor

| Field | Lua | Default | Notes |
|---|---|---|---|
| **Position** (X, Y, Z) | `position` | `0, 0, 0` | World position of the feet. |
| **Rotation** (X, Y, Z) | `rotation` | `0, 0, 0` | Degrees in the inspector, a quaternion in Lua. |
| **Scale** (X, Y, Z) | `scale` | `32, 32, 32` | See [Scale](#scale). |

## Scripting

| Property | Type | | Description |
|---|---|---|---|
| `isValid` | boolean | read-only | `false` if the entity or its Transform is gone. |
| `position` | Vector3 | read/write | Feet position. Setting it updates sector membership this frame. |
| `rotation` | Vector4 | read/write | Quaternion `(x, y, z, w)`. Normalised when written. |
| `scale` | Vector3 | read/write | |
| `relativeHeight` | number | read/write | Height above the floor. Physics overwrites it every frame on physics bodies. |
| `sectorIndex` | integer | read-only | Internal index of the current sector, `-1` outside the map. Use `entity:GetSector()` instead: it returns the sector itself and survives map edits. |
| `isDirty` | boolean | read/write | Engine flag meaning "moved this frame". You shouldn't need it. |

| Method | Description |
|---|---|
| `addPosition(offset)` | Moves by a `Vector3` offset. Same as `position = position + offset`. |
| `lookAt(point, [yawOnly])` | Turns so the entity faces a world `Vector3` point. Does nothing if the point is at the entity's position. |
| `lookDirection(direction, [yawOnly])` | Turns so the entity faces along a `Vector3` direction. Does nothing for a zero direction. |

`yawOnly` defaults to `true`: the entity only turns left and right, and the height of the point or
direction is ignored. Pass `false` to tilt up and down as well (never any roll). Upright things
such as sprites and characters usually want the default.

**Vectors are copies.** `entity.transform.position.x = 5` changes a temporary copy and has no effect.
Build a new vector and assign it:

```lua
local p = entity.transform.position
entity.transform.position = Vector3(5, p.y, p.z)
```

### Moving things that have a Rigidbody

Setting `position` teleports the entity. On an entity with a [Rigidbody](Rigidbody.md) that is fine
for teleports and respawns. For normal movement, set the Rigidbody's `velocity` so physics can do
the moving and stop it at walls. A position set by a script is not checked against walls until
physics runs later in the frame, and a big jump can pass straight through a thin wall.

### Rotation helpers

[`mathT`](mathT.md#quaternions) has the quaternion functions. Angles are in degrees, the same as
the inspector:

```lua
-- Face +X (rotation Y 90 in the inspector).
entity.transform.rotation = mathT.QuaternionFromEuler(0, 90, 0)

-- Read the angles back.
local angles = mathT.QuaternionToEuler(entity.transform.rotation)

-- Turn a further 45 degrees to the right.
local turn = mathT.QuaternionAngleAxis(Vector3(0, 1, 0), 45)
entity.transform.rotation = mathT.QuaternionMultiply(turn, entity.transform.rotation)

-- The direction the entity faces, as a unit Vector3.
local facing = mathT.QuaternionRotate(entity.transform.rotation, Vector3(0, 0, 1))
```

## Examples

### Bob up and down, and spin

```lua
-- Scripts/Movement/BobAndSpin.lua (pickup with a Model)
---@field bobHeight number @ Bob Height
bobHeight = 4

---@field bobSpeed number @ Bobs Per Second
bobSpeed = 0.5

---@field spinSpeed number @ Degrees Per Second
spinSpeed = 90

local base
local t = 0

function Start()
    base = entity.transform.position
end

function Update()
    t = t + GameTime.deltaTime

    local y = base.y + (mathT.Sin(t * bobSpeed * mathT.Tau) + 1) * 0.5 * bobHeight
    entity.transform.position = Vector3(base.x, y, base.z)

    entity.transform.rotation = mathT.QuaternionFromEuler(0, t * spinSpeed, 0)
end
```

### Patrol between two points

```lua
-- Scripts/Movement/Patrol.lua (no Rigidbody needed)
---@field pointA Vector3 @ Point A
pointA = Vector3(0, 0, 0)

---@field pointB Vector3 @ Point B
pointB = Vector3(128, 0, 0)

---@field speed number @ Speed
speed = 30

local target

function Start()
    entity.transform.position = pointA
    target = pointB
end

function Update()
    local here = entity.transform.position
    local moved = mathT.Vector3MoveTowards(here, target, speed * GameTime.deltaTime)
    entity.transform.position = moved

    -- Face the way we're walking (for 4/8-direction sprites).
    entity.transform:lookAt(target)

    if mathT.Vector3Distance(moved, target) < 0.01 then
        target = (target == pointB) and pointA or pointB
    end
end
```

### Keep an entity on the floor

```lua
-- Scripts/Movement/SnapToFloor.lua
-- For entities without a Rigidbody that are moved by script over sloped or stepped floors.
function Update()
    local sector = entity:GetSector()
    if sector == nil then return end

    local p = entity.transform.position
    entity.transform.position = Vector3(p.x, sector:GetFloorHeightAt(p), p.z)
end
```
