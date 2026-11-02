# Pathfinder

**Inspector name:** Pathfinder · **Lua:** `entity.pathfinder` · **Public field type:** `Pathfinder`

A Pathfinder walks its entity to a point, or after another entity, the way Doom's monsters do: it
heads for the destination along one of **8 compass directions**, and when something is in the way
it picks another direction and keeps trying. There are no baked paths and no navigation mesh to
build. Place an enemy, tell it who to chase, and it goes.

It only walks. To have the entity notice the player and decide when to attack, add a
[Monster AI](MonsterAI.md), which drives the Pathfinder for you.

Like a Doom monster, it isn't a perfect route finder. It's good at getting around pillars, crates
and corners, and at following a target through open rooms. A long maze, or a dead end facing the
target, can keep it busy for a while.

## How it works

### Choosing a direction

Every frame the Pathfinder moves the entity a little in its current direction. It keeps that
direction for a short random time (between **Hold Steps Min** and **Hold Steps Max** "chase steps"
of **Chase Step** seconds, by default Doom's 1 to 16 of 3/35 of a second), then chooses again. It also chooses again at once when the current direction is blocked.

To choose, it tries these in order and takes the first one it can step into:

1. The diagonal straight toward the destination (for example north-east).
2. The two straight directions toward it, the longer distance first. Now and then
   (**Axis Swap %**) the two are swapped at random, which helps it get unstuck.
3. The direction it was already going.
4. Every other direction, swept from a random end, except straight back.
5. Straight back, as a last resort.

If none of them works it stands still and tries again next frame.

### Blocked steps

Before each step, the Pathfinder checks the square around the entity's [Collider](Collider.md),
moved one step ahead. The step is **blocked** when:

- part of that square is outside the map, or the collider would touch a solid wall, a wall with a
  solid middle texture, or a wall or enabled linedef with **Blocks Pathfinders**
  ticked (Doom's block-monsters line). Walls are checked directly, so a pillar thinner than the
  collider blocks it too,
- the floor there is more than the collider's **Step Size** above the floor it stands on (too high
  to climb),
- the floor there is more than **Step Size** below it, or the square would hang over a drop higher
  than **Step Size** (it won't walk off ledges, unless **Can Drop Off** is ticked),
- there isn't room for the collider's height under the ceiling.

With a [Rigidbody](Rigidbody.md) the entity moves by its velocity, so physics still stops it at
walls and other bodies. A step that physics cut to less than half its length also counts as
blocked.

Without a Rigidbody it moves the [Transform](Transform.md) directly and stays on the floor. Only
the checks above stop it: other entities don't. The inspector warns about this.

### Doors

With **Opens Doors** ticked (the default), a shut door in the way gets opened, as Doom's monsters
do: the sector's Door dispatch runs with the walker as the instigator, and it waits there until
there's room. A door that is closing reverses.

It never uses a closing type of door, and it leaves a locked door shut unless it has the required
tag, without playing the locked sound or showing the message. To keep Pathfinders out of a door
altogether, untick the door's **Pathfinders Can Open**.

Without a Collider the entity is treated as a point with the default Step Size of `15`.

### Arriving

It stops once it is close enough:

- **Normally:** within **Deadzone** of the destination (Doom's `10`), plus its collider's radius,
  plus the target's collider radius when following an entity. So a chaser stops next to the player
  instead of pushing into them.
- **Ranged entities:** within **Range** of the destination. For an air entity this is measured in
  3D. For one on the ground it's measured on the ground plane only. With **Keep Moving In Range**
  ticked (the default for new Pathfinders) that only counts as arriving: it keeps walking up to the
  destination as Doom's monsters do. Unticked, it stops there.

Each time it arrives, its scripts get [`OnPathfinderArrived()`](CallbackFunctions.md#onpathfinderarrived).
When it is following an entity that walks away, it sets off again on its own, and arriving again
calls `OnPathfinderArrived` again.

### Following an entity

With **Input** set to **Entity**, the destination is the target's position (its feet) plus
**Offset**, updated every frame. **Offset** is in world axes: `(0, 32, 0)` aims 32 units above
the target, which is useful for air entities.

- **Target disabled:** it keeps heading for where the target was when it was disabled.
- **Target destroyed:** it stops where it is. `target` reads `nil` from then on.

### Air entities

Tick **Air Entity** for things that fly. A flyer:

- ignores Step Size and drop-offs, so it floats over ledges and pits,
- rises or sinks at **Float Speed** toward the destination's height once the height difference
  times **Float Ratio** is more than the distance left (Doom's `140` and `3`), and on arriving,
  staying between the floor and ceiling. Heading toward a raised floor, it rises over it first,
- holds its height against gravity, so its Rigidbody can keep its normal Gravity Scale.

It still needs room for its collider's height between floor and ceiling.

### Facing

By default, while it moves, the entity turns to face its direction (yaw only), 45 degrees per chase
step as in Doom, so a directional [Sprite](Sprite.md) shows the right side. Standing still, it keeps
its last facing.

**Faces** can also be the destination (every frame) or nothing, which leaves it to your scripts.
**Turning** can be **At Once** instead of in steps.

### Pushes

With a Rigidbody, any velocity from something else, such as knockback from a script or dispatch, is
added on top of the walking. The push keeps **Push Friction** of its speed per 1/35 of a second (Doom's
`0.90625`), so it dies away quickly without you stopping the Pathfinder. It also works while the
entity stands still.

### When it runs

Pathfinders only move entities in the game, never in the editor. Each frame they run after every
script's `Update` and after [Flipbooks](Flipbook.md), and before physics. A destination set in
`Update` is used on the same frame. See [Order within one frame](CallbackFunctions.md#order-within-one-frame).

`speed` is in units per second and follows [`GameTime.timeScale`](GameTime.md): slowed in slow
motion, frozen while paused.

A disabled entity's Pathfinder is frozen. An entity can have several Pathfinders, but only the
**first** one moves it. The others keep their settings and do nothing, and the inspector says so.

## In the editor

| Field | Lua | Default | Notes |
|---|---|---|---|
| *(enable box in the header)* | `isActive` | on | Off: doesn't move the entity. |
| **Input** | `input` | Position | **Position** walks to a fixed point. **Entity** follows another entity. |
| **Destination** (X, Y, Z) | `destination` | `0, 0, 0` | Position input only. World position. |
| **Target** | `target` | none | Entity input only. Pick from the list, or drag an entity from the hierarchy or the map onto it. |
| **Offset** (X, Y, Z) | `offset` | `0, 0, 0` | Entity input only. Added to the target's position, in world axes. |
| **Speed** | `speed` | `64` | Units per second. |
| **Air Entity** | `isAirEntity` | off | Flies. See [Air entities](#air-entities). |
| **Ranged Entity** | `isRangedEntity` | off | Arrives within **Range**. |
| **Range** | `range` | `0` | Shown when Ranged Entity is ticked. |
| **Keep Moving In Range** | `keepMovingInRange` | on | Shown when Ranged Entity is ticked. Off stops in range. |
| **Can Drop Off** | `canDropOff` | off | Walks off ledges. |
| **Opens Doors** | `opensDoors` | on | See [Doors](#doors). |
| **Faces** | `facing` | Move Direction | Move Direction, Destination or Nothing. In Lua, `PathfinderFacing.MoveDirection`, `.Target` or `.None`. |
| **Turning** | `turning` | 45 Degree Steps | `PathfinderTurning.Steps45` or `.Snap`. |
| **Chase Step** | `chaseStep` | `0.0857` | Seconds per chase step (3/35). |
| **Hold Steps Min** | `holdMinSteps` | `1` | Chase steps it holds a direction at least. |
| **Hold Steps Max** | `holdMaxSteps` | `16` | And at most. Lower looks more direct, higher wanders more. |
| **Deadzone** | `deadzone` | `10` | This close on an axis counts as there. Also its reach for arriving. |
| **Axis Swap %** | `swapChance` | `21.5` | Chance to try the shorter axis first. |
| **Float Speed** | `floatSpeed` | `140` | Air only. Units per second up and down. |
| **Float Ratio** | `floatRatio` | `3` | Air only. `0` changes height at any distance. |
| **Push Friction** | `pushFriction` | `0.90625` | See [Pushes](#pushes). `0` = no pushes, `1` = never fades. |
| **Stuck Time** | `stuckTime` | `1` | Seconds without progress before it counts as stuck. `0` = never. |

A Pathfinder saved before these options existed keeps stopping in range, opening no doors and
turning at once, until you change those settings.

For a walker, also give the entity a [Collider](Collider.md) (its size and **Step Size** decide
where it fits) and a [Rigidbody](Rigidbody.md) (so walls, other bodies and gravity apply).

## Scripting

| Property | Type | | Description |
|---|---|---|---|
| `isValid` | boolean | read-only | `false` if the entity or its Pathfinder is gone. |
| `isActive` | boolean | read/write | `false` stops it where it is. |
| `hasArrived` | boolean | read-only | `true` while it is at its destination (within `range` when ranged). Always `false` while inactive. |
| `input` | PathfinderInput | read-only | `PathfinderInput.Position` or `PathfinderInput.Entity`. `SetDestination` and `SetTarget` switch it. |
| `destination` | Vector3 | read-only | Where it is heading. Following an entity, this is the target's position + `offset`, or its last known one while the target is disabled. |
| `target` | Entity? | read-only | The entity it follows. `nil` with Position input, or once the target is destroyed. |
| `offset` | Vector3 | read/write | Added to the target's position (Entity input only). |
| `speed` | number | read/write | Units per second. Negative values become `0`. |
| `range` | number | read/write | Ranged entities stop this close. Negative values become `0`. |
| `isAirEntity` | boolean | read/write | Flies. |
| `isRangedEntity` | boolean | read/write | Arrives within `range`. |
| `isMoving` | boolean | read-only | `true` while it is walking. |
| `isStuck` | boolean | read-only | `true` once it has made no progress for `stuckTime`. |
| `moveDirection` | Vector3 | read-only | The way it is walking, one of 8 compass directions, or zero when it isn't. |

The options in the table above can be read and written too. Negative numbers become `0`.

| Method | Description |
|---|---|
| `SetDestination(position)` | Walks to a world position (`Vector3`). Switches `input` to Position. |
| `SetTarget(entity, offset?)` | Follows `entity`, keeping up as it moves. Switches `input` to Entity. `offset` is optional, and when left out the current one is kept. An entity can't follow itself. |
| `Stop()` | Stops moving: `isActive = false`. If it was walking, its Rigidbody's horizontal velocity is zeroed. An air entity's vertical velocity is zeroed too, and it then falls under gravity unless its Gravity Scale is `0`. |
| `Resume()` | Moves again: `isActive = true`. |
| `Pause(seconds)` | Holds still for this long, keeping an air entity's height and any push, then carries on. `0` carries on now. Good for pain or an attack. |
| `PickNewDirection()` | Chooses a direction again at its next update. |

`SetDestination` and `SetTarget` to a different place reset `hasArrived`, so `OnPathfinderArrived`
fires again when it gets there. Sending the place it is already heading for changes nothing.

```lua
function OnPathfinderArrived()
    -- this entity's Pathfinder just got to its destination
end
```

`OnPathfinderArrived` is called on **every enabled script on the entity** that defines it, after
every Pathfinder has moved that frame.

It also has [`OnPathfinderBlocked`, `OnPathfinderStuck`, `OnPathfinderStartedMoving` and
`OnPathfinderStoppedMoving`](CallbackFunctions.md#other-pathfinder-callbacks), and events of the same
names.

Changing `rigidbody.velocity` from a script counts as a [push](#pushes): it adds to the walking and
fades. It doesn't take over the movement. To move a walking enemy yourself, `Stop()` it first and
`Resume()` it later.

## Examples

### Chase the player

```lua
-- Scripts/AI/Chaser.lua (enemy with a Collider, Rigidbody and Pathfinder)
public Entity player = nil

function Start()
    if player ~= nil then
        entity.pathfinder:SetTarget(player)
    end
end

function OnPathfinderArrived()
    Debug.Print(entity.name .. " caught up!")
end
```

Setting the Pathfinder's **Input** to **Entity** and picking the player as **Target** in the
inspector does the same without a script.

### Patrol between points

```lua
-- Scripts/AI/Patrol.lua (entity with a Pathfinder)
-- Walks a loop of points, pausing at each one.
public Vector3 pointA = Vector3(0, 0, 0)
public Vector3 pointB = Vector3(0, 0, 0)
public Vector3 pointC = Vector3(0, 0, 0)
public number waitTime = 1.5

local points
local index = 1

local function GoToNext()
    entity.pathfinder:SetDestination(points[index])
    index = index % #points + 1
end

function Start()
    points = {pointA, pointB, pointC}
    GoToNext()
end

function OnPathfinderArrived()
    Invoke(GoToNext, waitTime)
end
```

### Ranged enemy: keep its distance and shoot

For Doom's own monster behaviour, use a [Monster AI](MonsterAI.md) instead of this script.

```lua
-- Scripts/AI/Gunner.lua (enemy with a Collider, Rigidbody and Pathfinder)
-- Closes to within range, stops, and fires while the player stays in range.
public Entity player = nil
public number shootRange = 160
public number fireInterval = 1.0

local cooldown = 0

function Start()
    if player == nil then return end
    local pf = entity.pathfinder
    pf.isRangedEntity = true
    pf.keepMovingInRange = false
    pf.range = shootRange
    pf:SetTarget(player)
end

function Update()
    cooldown = cooldown - GameTime.deltaTime
    if entity.pathfinder.hasArrived and cooldown <= 0 then
        cooldown = fireInterval
        Debug.Print("Bang!")  -- spawn a projectile or cast a ray here
    end
end
```

### Flying enemy that hovers above the player

```lua
-- Scripts/AI/Floater.lua (enemy with a Collider, Rigidbody and Pathfinder)
public Entity player = nil
public number hoverHeight = 24

function Start()
    if player == nil then return end
    local pf = entity.pathfinder
    pf.isAirEntity = true
    pf:SetTarget(player, Vector3(0, hoverHeight, 0))
end
```

### Stop when stunned

```lua
-- Scripts/AI/Stun.lua
-- Another script calls enemy:GetScript("Stun"):Stun(2)
public function Stun(seconds)
    entity.pathfinder:Pause(seconds)
end
```
