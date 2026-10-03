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

- A [sprite](Sprite.md) is drawn upward from it.
- A sphere [collider](Collider.md) sits on it: its centre is one radius above `position`.
- The [player camera](PlayerController.md) is `eyeHeight` above it.

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

### Rotation and forward

There are two separate "facing" values:

- `rotation` is a quaternion. It turns [models](Model.md) and **static** [sprites](Sprite.md). The
  inspector shows it as X/Y/Z angles in degrees. From Lua it is a `Vector4` `(x, y, z, w)`.
- `forward` is a flat `Vector2` direction on the map `(x, z)`. It is the way a 4- or 8-direction
  sprite faces, which picks which of its images you see, and the way an
  [Audio Source's](AudioSource.md#sound-cone) sound cone points.

Neither one moves the camera. The camera has its own `yaw` and `pitch` (see [Camera](Camera.md)).

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
| `forward` | Vector2 | read/write | Facing on the map, used by directional sprites and the Audio Source sound cone. |
| `relativeHeight` | number | read/write | Height above the floor. Physics overwrites it every frame on physics bodies. |
| `sectorIndex` | integer | read-only | Internal index of the current sector, `-1` outside the map. Use `entity:GetSector()` instead: it returns the sector itself and survives map edits. |
| `isDirty` | boolean | read/write | Engine flag meaning "moved this frame". You shouldn't need it. |

| Method | Description |
|---|---|
| `addPosition(offset)` | Moves by a `Vector3` offset. Same as `position = position + offset`. |

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

There are no quaternion functions in `mathT`. For a rotation around the vertical axis only
(turning left and right), the quaternion is:

```lua
local function YawRotation(degrees)
    local half = mathT.DegToRad(degrees) * 0.5
    return Vector4(0, mathT.Sin(half), 0, mathT.Cos(half))
end

entity.transform.rotation = YawRotation(90)
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

    local half = mathT.DegToRad(t * spinSpeed) * 0.5
    entity.transform.rotation = Vector4(0, mathT.Sin(half), 0, mathT.Cos(half))
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
    local d = target - here
    if d.length > 0.001 then entity.transform.forward = Vector2(d.x, d.z).normalized end

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
