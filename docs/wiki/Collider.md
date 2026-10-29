# Collider

**Inspector name:** Collider · **Lua:** `entity.collider` · **Public field type:** `Collider`

A Collider gives an entity a physical shape. With one, the entity can stand on floors, bump into
walls and ceilings, climb steps and push against other entities. A collider can also be a
**trigger**: a volume that things pass through, which tells your scripts when something enters or
leaves it.

The collider **is** the body. Every physics check (walls, floors, ceilings, slopes, steps,
grounding, other entities, triggers) uses the collider's shape and offset. The
[Transform](Transform.md)'s position is only the point the collider hangs off. Its `scale`
multiplies the collider's size and offset (see [Size and scale](#size-and-scale)).

A collider only **moves** if its entity also has a non-static [Rigidbody](Rigidbody.md). Without
one it is an immovable obstacle that other bodies bump into.

## Shapes

| Type | Lua | Size | Status |
|---|---|---|---|
| **Capsule** | `ColliderType.Capsule` | **Radius** (`scale.x`) and **Height** (`scale.y`) | Fully working. The default. |
| **Sphere** | `ColliderType.Sphere` | **Radius** (`scale.x`) | Fully working. |
| **AABB** (box) | `ColliderType.Box` | **Scale** (`scale.x`, `.y`, `.z`), the box size | Fully working. |

A **capsule** is an upright cylinder with rounded ends. It never tilts, even when the entity
rotates. Its **Height** is the total height from bottom to top, rounded ends included, and is never
less than twice the radius. Use capsules for characters: they can be tall and thin, which a sphere
can't. Use spheres for balls and projectiles.

An **AABB** (axis-aligned bounding box, `ColliderType.Box` in Lua) is a box whose faces always line
up with the world's X, Y and Z axes. It never turns, even when the entity rotates (only its
`offset` turns with the entity). It is centred on its position in X and Z and stands on it: the
bottom is at the position, the top **Scale Y** above. Use boxes for crates, pillars, blocks and
square trigger volumes.

### Size and scale

Sizes are multiplied by the [Transform](Transform.md)'s `scale`: a sphere's or capsule's radius by
`scale.x`, a capsule's height by `scale.y`, and a box by all three axes. The `offset` is scaled
per axis the same way. **Step Size** is always in world units.

### Where the collider sits

The collider's **bottom** is at the [Transform](Transform.md) position plus its `offset` (turned
with the entity's rotation). Every shape rests its bottom on the floor, so with the default
offset of `0, 0, 0` the entity's position stays on the floor. A sphere's centre is one radius
above its bottom; a capsule's top is **Height** above it, a box's top **Scale Y** above it.

An offset of `0, 4, 0` puts the collider's bottom 4 units above the position. The collider still
stands on the floor, so the position ends up 4 units **below** the floor, and sprites and models
drawn from it sink in. Keep the offset at `0, 0, 0` for anything that stands on the floor.

### Several on one entity

An entity can have several Colliders. Together they make **one compound shape**:

- Each collider's bottom sits at the Transform position plus its `offset`, as above.
- Each one is resolved against walls, other entities, the floor and the ceiling on its own, and
  every push moves the whole entity.
- The **base collider** is the active, non-trigger collider (any shape) whose bottom is lowest. It
  owns the Transform's `relativeHeight` and the step down onto a lower floor (with its own Step
  Size).
- Other entities collide with any of its active, non-trigger colliders, and triggers are touched
  by any of its active colliders.
- Collision and trigger callbacks still fire **once per pair of entities**, however many of their
  colliders touch.
- Each collider has its own Step Size; ray casts test every collider.
- Floor and ceiling checks use the sector under each collider's bottom.

`entity.collider` is the first one; `entity:GetComponents(Component.Collider)` lists them all.

## How it works

Each frame, after scripts and the [Player Controller](PlayerController.md) have set velocities and
the Rigidbodies have moved, physics resolves every **active, non-trigger collider on an entity with
a non-static Rigidbody**:

1. **Other entities.** It is pushed out of every other active, non-trigger collider nearby, whatever
   the two shapes are. If the other entity is static (no Rigidbody, or a static one), only this
   entity moves. If both are moving bodies, each is pushed half the overlap.
2. **Walls.** It is pushed out of the solid parts of nearby [walls](Wall.md), and its velocity into
   the wall is removed so it slides along instead of sticking. A push out of one wall can go into
   another (a corner), so walls are checked again until nothing moves it.
3. **Floor and ceiling.** It is kept between the floor and ceiling of the sector it stands in: its
   bottom above the floor, its top below the ceiling. On a sloped floor or ceiling the real shape
   touches the slope: a sphere or capsule with its rounded end, so a wider one stops sooner under a
   ceiling sloping down; a box with its flat bottom or top, resting on the highest point of the
   slope under it (its downhill side hovers, like a real crate). Where the gap gets shorter than
   the collider, it is pushed back to where it fits, like a wall. If its bottom ends up on a floor
   no steeper than **Max Slope**, the Rigidbody is marked **grounded**.

Step 1 runs for every body before steps 2 and 3 run for any, so walls, floors and ceilings always
get the last word: a body shoved into a wall by another one ends up back out of it.

"Nearby" means in the sector the entity was in at the start of the frame, the one its collider is
over now, or a neighbour of either. A very large collider can reach past its neighbours, and things
beyond them aren't tested.

Bodies don't **stand** on other entities: one landing on top of another is pushed up out of it,
but it isn't grounded and keeps gathering falling speed, so it soon sinks through. Stand things on
floors.

### Fast bodies

A body that moves too far in one step could get past a wall before physics sees it. Two things stop
that:

- **Sub-steps.** On a frame where some body moves more than about a quarter of its own width (or a
  quarter of its height, going up or down), the whole frame's movement and physics run in several
  smaller steps. The most per frame is the level's **Max Physics Sub-steps** (World Settings >
  Physics, default `8`; `1` turns it off). Normal frames take one step and cost nothing extra.
- **Swept walls.** If a body's centre still went past a wall's line during a step, it is put back
  on the side it came from, touching the wall. This holds however fast it moves.

A body wedged into a corner too narrow for it stays where it was before the step, instead of being
squeezed out through one of the walls.

Only movement from **velocity** is swept. Setting `transform.position` from a script moves the
entity there directly, past walls too, so teleporters work.

### Max Slope

**Max Slope** is the steepest floor, in degrees, the collider can stand on (default `45`). On a
steeper floor the body isn't grounded: gravity keeps pulling, it slides down the slope, and the
[Player Controller](PlayerController.md) can't jump off it. A steep slope is a wall to walk into:
moving uphill on it gains no height, while moving across or down it still works.

### Step Size

**Step Size** lets a body walk up small ledges instead of being stopped by them. When the body walks
into the solid lower part of a wall (the face of a step) whose top is no more than Step Size above
the collider's bottom, it is lifted onto the step. The same limit applies going **down**: walking off a ledge
no taller than Step Size snaps the body down onto the lower floor instead of making it fall.

`0` turns stepping off. For a player, `8` to `12` feels right with the default scale. The
[Camera](Camera.md#smooth-stepping)'s Smooth Stepping hides the sudden height change.

### Triggers

A collider with **Is Trigger** ticked isn't solid. Nothing is pushed out of it and it isn't pushed
by anything. Instead, the engine reports overlaps each frame:

- A trigger of any shape overlaps any other active collider nearby, trigger or not, of any shape,
  that it intersects.
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

An inactive collider is ignored completely. It also keeps its setting when the level is saved and
loaded. The entity passes through walls and other entities,
floors and ceilings no longer hold it in, and triggers don't notice it. A moving body whose
colliders are all inactive falls through the floor.

## In the editor

| Field | Lua | Default | Notes |
|---|---|---|---|
| **Is Active** | `isActive` | on | |
| **Collider Type** | `type` | Capsule | Sphere, AABB or Capsule. |
| **Is Trigger** | `isTrigger` | off | See [Triggers](#triggers). |
| **Radius** (sphere, capsule) / **Height** (capsule) / **Scale** (AABB) | `scale` | `0.1875, 0.5, 0.1875` | Radius is `scale.x`, a capsule's height `scale.y`, a box's size all three. Multiplied by the Transform's scale: `6, 16, 6` at the default scale of 32. See [Size and scale](#size-and-scale). |
| **Step Size** | `stepSize` | `15` | See [Step Size](#step-size). |
| **Max Slope** | `maxSlope` | `45` | Degrees, `0` to `90`. See [Max Slope](#max-slope). |
| **Offset** | `offset` | `0, 0, 0` | Where the collider's bottom sits. See [Where the collider sits](#where-the-collider-sits). |

The default capsule is 16 tall, taller than the [Player Controller](PlayerController.md)'s default
Eye Height of 12, so the camera stays inside the body.

## Scripting

| Property | Type | | Description |
|---|---|---|---|
| `isValid` | boolean | read-only | `false` if the entity or its Collider is gone. |
| `type` | ColliderType | read/write | `ColliderType.Capsule`, `ColliderType.Sphere` or `ColliderType.Box`. |
| `isActive` | boolean | read/write | |
| `isTrigger` | boolean | read/write | |
| `scale` | Vector3 | read/write | Radius is `scale.x`; a capsule's height is `scale.y`; a box's size is all three. Multiplied by the Transform's scale. |
| `stepSize` | number | read/write | |
| `maxSlope` | number | read/write | Degrees, clamped to `0`..`90`. |
| `offset` | Vector3 | read/write | Position of the collider's bottom relative to the Transform, turned with its rotation. See [Where the collider sits](#where-the-collider-sits). |

## Examples

### Damage zone

```lua
-- Scripts/DamageZone.lua (entity with a trigger Collider)
-- Hurts anything with a Health script while it stands inside.
public number damagePerSecond = 20

function OnTrigger(other)
    local health = other:GetScript("Health")
    if health.isValid then health:TakeDamage(damagePerSecond * GameTime.deltaTime) end
end
```

`Health` is the script from [Getting Started](GettingStarted.md#10-talking-to-other-scripts).

### Door trigger

```lua
-- Scripts/DoorTrigger.lua (entity with a trigger Collider in front of a door)
public Sector door = nil

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
public number growPerSecond = 4

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
    if Input.GetKeyDown(Key.F) then
        entity.collider.isActive = not entity.collider.isActive
    end
end
```
