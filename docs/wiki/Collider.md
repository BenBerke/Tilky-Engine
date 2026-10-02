# Collider

**Inspector name:** Collider · **Lua:** `entity.collider` · **Public field type:** `Collider`

A Collider gives an entity a physical shape. With one, the entity can stand on floors, bump into
walls, climb steps and push against other entities. A collider can also be a **trigger**: a volume
that things pass through, which tells your scripts when something enters or leaves it.

A collider only **moves** if its entity also has a non-static [Rigidbody](Rigidbody.md). Without
one it is an immovable obstacle that other bodies bump into.

## Shapes

| Type | Lua | Size | Status |
|---|---|---|---|
| **Sphere** | `ColliderType.Sphere` | **Radius** (`scale.x`) | Fully working. |
| **AABB** (box) | `ColliderType.Box` | **Scale** (`scale.x`, `.y`, `.z`), the box size | **Not simulated yet.** Box colliders don't collide with anything and don't fire trigger or collision callbacks. |

Use spheres for now.

### Where the sphere sits

The sphere rests **on** the entity's feet. Its centre is one radius above the
[Transform](Transform.md) position, so an entity with `position.y == 0` and a radius of 8 has a
sphere touching the floor at 0 with its centre at 8.

## How it works

Each frame, after scripts and the [Player Controller](PlayerController.md) have set velocities and
the Rigidbodies have moved, physics resolves every **active, non-trigger sphere on an entity with a
non-static Rigidbody**:

1. **Other entities.** It is pushed out of every other active, non-trigger sphere nearby. If the
   other entity is static (no Rigidbody, or a static one), only this entity moves. If both are
   moving bodies, each is pushed half the overlap.
2. **Walls.** It is pushed out of the solid parts of nearby [walls](Wall.md), and its velocity into
   the wall is removed so it slides along instead of sticking.
3. **Floor and ceiling.** It is kept between the floor and ceiling of the sector it stands in. The
   body's height for this is the larger of the Transform's `scale.y` and the sphere's diameter. If
   its feet end up on the floor, the Rigidbody is marked **grounded**.

"Nearby" means in the entity's own sector or a neighbouring one. A very large sphere can reach
past its neighbours, and things beyond them aren't tested.

### Step Size

**Step Size** lets a body walk up small ledges instead of being stopped by them. When the body walks
into the solid lower part of a wall (the face of a step) whose top is no more than Step Size above
its feet, it is lifted onto the step. The same limit applies going **down**: walking off a ledge
no taller than Step Size snaps the body down onto the lower floor instead of making it fall.

`0` turns stepping off. For a player, `8` to `12` feels right with the default scale. The
[Camera](Camera.md#smooth-stepping)'s Smooth Stepping hides the sudden height change.

### Triggers

A collider with **Is Trigger** ticked isn't solid. Nothing is pushed out of it and it isn't pushed
by anything. Instead, the engine reports overlaps each frame:

- A trigger overlaps any other active sphere collider nearby, trigger or not, that its sphere
  intersects.
- A trigger doesn't need a Rigidbody. A static trigger volume is the normal setup.
- Its entity must stand inside a sector.

Overlaps fire `OnTriggerEnter`, `OnTrigger` and `OnTriggerExit` on scripts of **both** entities
(see [Callback Functions](CallbackFunctions.md#ontriggerenter--ontrigger--ontriggerexit)).

### Collision callbacks

Whenever step 1 pushes two entities apart, both get `OnCollisionEnter`, `OnCollision` and
`OnCollisionExit` (see
[Callback Functions](CallbackFunctions.md#oncollisionenter--oncollision--oncollisionexit)). Walls
and floors don't fire these callbacks: only entity-to-entity contacts do.

### Is Active

An inactive collider is ignored completely. The entity passes through walls and other entities,
floors and ceilings no longer hold it in, and triggers don't notice it. The
[Player Controller](PlayerController.md) uses this for No Clip, and it sets the player's
`isActive` every frame.

## In the editor

| Field | Lua | Default | Notes |
|---|---|---|---|
| **Is Active** | `isActive` | on | |
| **Collider Type** | `type` | Sphere | Sphere or AABB. |
| **Is Trigger** | `isTrigger` | off | See [Triggers](#triggers). |
| **Radius** (sphere) / **Scale** (AABB) | `scale` | `1, 1, 1` | Sphere radius is `scale.x`. |
| **Step Size** | `stepSize` | `0` | See [Step Size](#step-size). |

The default radius of `1` is tiny next to the default Transform scale of 32. Give characters a
radius of about 6 to 10.

## Scripting

| Property | Type | | Description |
|---|---|---|---|
| `isValid` | boolean | read-only | `false` if the entity or its Collider is gone. |
| `type` | ColliderType | read/write | `ColliderType.Sphere` or `ColliderType.Box`. |
| `isActive` | boolean | read/write | |
| `isTrigger` | boolean | read/write | |
| `scale` | Vector3 | read/write | Sphere radius is `scale.x`. |
| `stepSize` | number | read/write | |

## Examples

### Damage zone

```lua
-- Scripts/DamageZone.lua (entity with a trigger Collider)
-- Hurts anything with a Health script while it stands inside.
---@field damagePerSecond number @ Damage Per Second
damagePerSecond = 20

function OnTrigger(other)
    local health = other:GetScript("Health")
    if health.isValid then health:TakeDamage(damagePerSecond * GameTime.deltaTime) end
end
```

`Health` is the script from [Getting Started](GettingStarted.md#10-talking-to-other-scripts).

### Door trigger

```lua
-- Scripts/DoorTrigger.lua (entity with a trigger Collider in front of a door)
---@field door Sector @ Door
door = nil

local inside = 0

function OnTriggerEnter(other)
    if door == nil or not other.hasPlayerController then return end
    inside = inside + 1
    door:MoveCeilingTo(1, door.floorHeight + 40, 80)
end

function OnTriggerExit(other)
    if door == nil or not other.isValid or not other.hasPlayerController then return end
    inside = inside - 1
    if inside == 0 then door:MoveCeilingToFloor(1, 80) end
end
```

### Grow a collider with the sprite

```lua
-- Scripts/Grow.lua (entity with a Sprite and a sphere Collider)
---@field growPerSecond number
growPerSecond = 4

function Update()
    local s = entity.transform.scale
    local grown = s + Vector3(1, 1, 1) * (growPerSecond * GameTime.deltaTime)
    entity.transform.scale = grown

    -- Keep the collider radius at a quarter of the sprite's width.
    entity.collider.scale = Vector3(grown.x * 0.25, 1, 1)
end
```

### Toggle a force field

```lua
-- Scripts/ForceField.lua (entity with a sphere Collider)
-- Solid while "on", passable while "off". Press F to switch.
function Update()
    if Input.GetKeyDown("F") then
        entity.collider.isActive = not entity.collider.isActive
    end
end
```
