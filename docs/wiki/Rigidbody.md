# Rigidbody

**Inspector name:** Rigidbody · **Lua:** `entity.rigidbody` · **Public field type:** `Rigidbody`

A Rigidbody lets physics move an entity. It holds a **velocity**, and every frame the engine applies
gravity to it and moves the entity by it. Pair it with a [Collider](Collider.md) so the entity lands
on floors and stops at walls. The player needs one, and so does anything that should fall, be
pushed or be launched.

## How it works

Every frame, for every Rigidbody:

1. **Gravity.** If the entity is inside a sector **and** above the floor, its vertical velocity is
   reduced by `level gravity × gravityScale × deltaTime`. Standing on the floor, no gravity is
   added.
2. **Friction.** The horizontal velocity (`x` and `z`) is slowed toward zero by `friction` units per
   second, every second. Vertical velocity isn't affected.
3. **Move.** The entity's [Transform](Transform.md) moves by `velocity × deltaTime`.
4. **Collide.** If the entity has an active sphere [Collider](Collider.md), physics pushes it out of
   walls, other entities, the floor and the ceiling, removes the velocity going into them, and
   updates `isGrounded`.

An entity can have several Rigidbodies, but only the **first** one simulates. The others are kept
with their settings and do nothing, and the inspector says so. To switch, drag another one to the
top of the component list, or remove the first one from a script.

### Gravity

The level's **Gravity** (the Physics section of the level settings, default `9.8`) is multiplied by the
Rigidbody's **Gravity Scale** (default `9.8`). So a default body falls with an acceleration of about
**96 units/s²**. To make one thing float or fall slowly, lower its Gravity Scale. `0` means no gravity.

Gravity only applies inside a sector and while the body is above the floor. Physics tracks the
height above the floor for bodies with a sphere Collider, so give falling things a collider.

### Static bodies

**Is Static** freezes the body: no gravity, and physics never moves it. Other bodies still collide
with it. An entity with a Collider and **no** Rigidbody behaves the same way. Use static bodies for
props that should block the player but never move.

### Grounded

`isGrounded` is `true` when physics found the body resting on a floor this frame (its feet within a
small distance of the floor and not moving away from it), or when it just climbed a step. The
[Player Controller](PlayerController.md) only jumps when it is `true`. It is recalculated every
frame, so writing it from a script has no lasting effect.

### Mass

**Mass** is **not used** by the physics simulation yet. Collisions between two moving bodies always
push each one by half, whatever their masses. The only thing that reads it is `AddImpulse`, which
moves a heavy body less than a light one.

## In the editor

| Field | Lua | Default | Notes |
|---|---|---|---|
| **Is Static** | `isStatic` | off | Frozen in place. |
| **Mass** | `mass` | `1` | Only used by `AddImpulse`. |
| **Gravity Scale** | `gravityScale` | `9.8` | Multiplies the level's gravity. |
| **Friction** | `friction` | `1` | Horizontal slow-down, units/s per second. |

`velocity` isn't shown in the inspector. It starts at zero.

## Scripting

| Property | Type | | Description |
|---|---|---|---|
| `isValid` | boolean | read-only | `false` if the entity or its Rigidbody is gone. |
| `velocity` | Vector3 | read/write | Units per second. |
| `isGrounded` | boolean | read/write | Resting on a floor this frame. Recalculated by physics every frame. |
| `isStatic` | boolean | read/write | |
| `mass` | number | read/write | Only used by `AddImpulse`. |
| `gravityScale` | number | read/write | |
| `friction` | number | read/write | |

| Method | Description |
|---|---|
| `AddVelocity(v)` | Adds a `Vector3` to the velocity. Every body gets the same change, whatever its mass. |
| `AddImpulse(impulse)` | Adds `impulse / mass` to the velocity, so heavier bodies move less. Good for explosions and knockback that should push a crate further than a boulder. A mass of `0` or less counts as `1`. |
| `Stop()` | Sets the velocity to zero. |

`rigidbody.velocity.y = 50` changes just the vertical speed. A velocity stored in a variable is a
copy, so assign it back after changing it.

On the **player**, the [Player Controller](PlayerController.md#movement) pulls the horizontal
velocity back toward what the movement keys ask for every frame, at its Acceleration /
Deceleration rate, so horizontal pushes fade out. Vertical changes (jump pads, launches) work
normally.

## Examples

### Jump pad

```lua
-- Scripts/JumpPad.lua (entity with a trigger Collider)
public number launchSpeed = 200

function OnTriggerEnter(other)
    local body = other.rigidbody
    if body == nil then return end

    local v = body.velocity
    body.velocity = Vector3(v.x, launchSpeed, v.z)
end
```

### Explosion

```lua
-- Scripts/Barrel.lua (entity with a Collider)
-- Call barrel:GetScript("Barrel"):Explode() from another script.
public number radius = 96
public number force = 250

function Explode(self)
    local center = entity.transform.position

    for _, e in ipairs(Game.GetEntities()) do
        local body = e.rigidbody
        if e.id ~= entity.id and body ~= nil and not body.isStatic then
            local offset = e.transform.position - center
            local distance = offset.length

            if distance < radius and distance > 0.001 then
                local strength = force * (1 - distance / radius)
                body:AddImpulse(offset.normalized * strength + Vector3(0, strength * 0.5, 0))
            end
        end
    end

    entity:Destroy()
end
```

(Against the player only the upward part has an effect, as explained above.)

### Low gravity room

```lua
-- Scripts/LowGravity.lua (on anything with a Rigidbody)
-- Floaty physics in sectors tagged "lowgravity".
public number lowScale = 2

local normalScale

function Start()
    normalScale = entity.rigidbody.gravityScale
end

function OnSectorChange(sector)
    local low = sector ~= nil and sector:HasTag("lowgravity")
    entity.rigidbody.gravityScale = low and lowScale or normalScale
end
```

### Drop a crate when shot

```lua
-- Scripts/HangingCrate.lua (entity with a Collider and a static Rigidbody, placed in the air)
-- Another script calls crate:GetScript("HangingCrate"):Drop()
function Drop(self)
    entity.rigidbody.isStatic = false
end
```

A static body floating in the air starts falling as soon as it's made non-static. It lands on the
floor below, because its sphere Collider keeps track of the floor under it.
