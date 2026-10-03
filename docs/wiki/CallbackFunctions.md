# Callback Functions

A callback is a function **you** define in a script and **the engine** calls at the right moment.
Every callback is optional: define only the ones you need. The names are reserved, so you can't use
them for public fields.

| Callback | Entity scripts | Sector scripts | When |
|---|:---:|:---:|---|
| [`Start()`](#start) | ✓ | ✓ | Once, the first time the script is active |
| [`Update()`](#update) | ✓ | ✓ | Every frame |
| [`FixedUpdate()`](#fixedupdate) | ✓ | ✓ | On a fixed 60 Hz step |
| [`OnEnable()`](#onenable--ondisable) | ✓ | ✓ | When the script becomes active |
| [`OnDisable()`](#onenable--ondisable) | ✓ | ✓ | When the script stops being active |
| [`OnDestroy()`](#ondestroy) | ✓ | ✓ | When the script is torn down |
| [`OnCollisionEnter(other)`](#oncollisionenter--oncollision--oncollisionexit) | ✓ | | Another solid collider starts touching this entity |
| [`OnCollision(other)`](#oncollisionenter--oncollision--oncollisionexit) | ✓ | | Every frame while they touch |
| [`OnCollisionExit(other)`](#oncollisionenter--oncollision--oncollisionexit) | ✓ | | They stop touching |
| [`OnTriggerEnter(other)`](#ontriggerenter--ontrigger--ontriggerexit) | ✓ | | A collider starts overlapping a trigger |
| [`OnTrigger(other)`](#ontriggerenter--ontrigger--ontriggerexit) | ✓ | | Every frame while they overlap |
| [`OnTriggerExit(other)`](#ontriggerenter--ontrigger--ontriggerexit) | ✓ | | They stop overlapping |
| [`OnSectorChange(sector)`](#onsectorchange) | ✓ | | This entity moved into a different sector |
| [`OnEntityEnter(entity)`](#onentityenter--onentityexit) | | ✓ | An entity entered this sector |
| [`OnEntityExit(entity)`](#onentityenter--onentityexit) | | ✓ | An entity left this sector |

If a callback is defined on the wrong kind of script (for example `OnEntityEnter` on an entity
script), it is silently ignored.

If a callback raises an error, the message is printed in red in the in-game console and written to
the log. The script keeps running, and the callback is called again next time as normal.

---

## Active, enabled and started

Most callbacks only run while a script is **active**:

- **Entity scripts** are active when the script's own **Enabled** box is ticked **and** its entity
  is enabled (`entity.enabled`).
- **Sector scripts** are active when the script's **Enabled** box is ticked. Sectors can't be
  disabled.

Scripts can switch both flags at run time: `entity.enabled = false` on an entity, and
`behaviour.enabled = false` on a script reference (see [Script](Script.md#behaviour-references)).
Disabling the entity doesn't change each script's own flag, so re-enabling the entity brings back
exactly the scripts that were enabled before.

---

## Start

```lua
function Start() end
```

Runs **once per script instance**, the first time the script is active.

- At level start, every script in the level is loaded before any `Start` runs. So inside `Start`
  every other script already exists and all public fields hold their inspector values. It's the
  right place to look things up (`Game.FindEntity`, `GetScript`) and to set up state.
- Entity scripts start before sector scripts.
- A script that is inactive when the level starts doesn't run `Start` then. It runs `Start` the
  first frame it becomes active, right after `OnEnable`.
- Code at the very top of the file (outside any function) runs even earlier, when the script is
  loaded, and **before** the inspector values are applied. Public fields still hold their inline
  defaults there. Put setup code in `Start`.

```lua
local player

function Start()
    player = Game.FindEntity("Player")
    if player == nil then Debug.LogWarning(entity.name .. ": no Player in the level") end
end
```

---

## Update

```lua
function Update() end
```

Runs **every frame** while the script is active. `GameTime.deltaTime` is the length of the last
frame in seconds. Multiply anything that changes over time by it.

```lua
---@field speed number
speed = 20

function Update()
    entity.transform:addPosition(Vector3(0, 0, speed * GameTime.deltaTime))
end
```

---

## FixedUpdate

```lua
function FixedUpdate() end
```

Runs on a fixed step of **1/60 s**, independent of the frame rate. `GameTime.fixedDeltaTime` is
the step length.

- The engine adds each frame's time to a counter and runs `FixedUpdate` once for every full 1/60 s
  in it. A 30 fps frame runs it twice; a 144 fps frame often runs it zero times.
- It runs after every script's `Update` in the same frame.
- At most **5 steps** run per frame, so a long stall doesn't cause a burst of hundreds of calls.
- The engine's physics does **not** use this fixed step yet. It still steps once per frame.
  `FixedUpdate` is best for logic that must tick at a steady rate, like counters, AI decisions or
  anything that should behave identically at any frame rate.

---

## OnEnable / OnDisable

```lua
function OnEnable() end
function OnDisable() end
```

- `OnEnable` runs when the script becomes active: at level start (just before `Start`), and
  whenever it becomes active again later.
- `OnDisable` runs when the script stops being active, because its own Enabled flag or its
  entity's `enabled` was switched off.
- The switch is noticed when the engine next reaches that script in the `Update` pass: later in
  the same frame if that script hasn't run yet, otherwise next frame. It never happens in the middle
  of your own call.

```lua
-- A light that only hums while its entity is enabled.
function OnEnable()
    if entity.audioSource ~= nil then entity.audioSource.looping = true end
end

function OnDisable()
    if entity.audioSource ~= nil then entity.audioSource.looping = false end
end
```

---

## OnDestroy

```lua
function OnDestroy() end
```

Runs once when a script instance is torn down:

- its entity was destroyed with `entity:Destroy()`;
- the script was removed from its entity or sector, or the sector was deleted;
- the level stops (leaving play mode).

`OnDestroy` only runs if `Start` ran first. A script that was never active has nothing to tear
down, so it gets no `OnDestroy`.

`entity:Destroy()` is **deferred**. The entity keeps working normally until the end of the
current frame, then every script on it gets `OnDestroy`, then it is removed.

---

## OnCollisionEnter / OnCollision / OnCollisionExit

```lua
function OnCollisionEnter(other) end
function OnCollision(other) end
function OnCollisionExit(other) end
```

| Parameter | Type | |
|---|---|---|
| `other` | [`Entity`](Entity.md) | The entity this one collided with. |

Called on entity scripts when this entity's [Collider](Collider.md) and another entity's collider
are pushed apart by physics.

- `OnCollisionEnter` runs on the first frame of contact.
- `OnCollision` runs **every frame** the two are in contact, **including** the first frame.
- `OnCollisionExit` runs on the first frame they are no longer in contact.
- Both entities get the call, each receiving the other as `other`.
- They fire at the end of the frame, after all movement and physics.

What counts as a collision:

- Both colliders are **Sphere** colliders, active, and **not** triggers. Box colliders don't
  collide yet.
- At least one of the two has a non-static [Rigidbody](Rigidbody.md). Two static things never
  collide with each other.
- Walls, floors and ceilings are not entities, so they never cause these callbacks.
- Two bodies resting against each other keep a tiny overlap, so `OnCollision` keeps firing while
  they touch, even when nothing moves.

If `other` is destroyed while touching, the next frame calls `OnCollisionExit` with an entity
whose `isValid` is `false`. Check `other.isValid` before using it in `OnCollisionExit`.

```lua
-- Scripts/BouncePad.lua: launch whatever bumps into this entity straight up.
-- Give the pad a Sphere Collider (not a trigger) and no Rigidbody.
---@field launchSpeed number
launchSpeed = 180

function OnCollisionEnter(other)
    local body = other.rigidbody
    if body == nil then return end

    local v = body.velocity
    body.velocity = Vector3(v.x, launchSpeed, v.z)
end
```

Pushing the **player** sideways this way doesn't work: the built-in
[Player Controller](PlayerController.md#movement) sets the player's horizontal velocity itself every
frame. Upward velocity is kept, so launches and jump pads work.

---

## OnTriggerEnter / OnTrigger / OnTriggerExit

```lua
function OnTriggerEnter(other) end
function OnTrigger(other) end
function OnTriggerExit(other) end
```

| Parameter | Type | |
|---|---|---|
| `other` | [`Entity`](Entity.md) | The entity overlapping the trigger, or the trigger itself when this script is on the other entity. |

A trigger is a [Collider](Collider.md) with **Is Trigger** ticked. Nothing collides with it: things
pass straight through, and the engine reports the overlap instead.

- `OnTriggerEnter` runs on the first frame of overlap, `OnTrigger` **every frame** of overlap
  (including the first), and `OnTriggerExit` on the first frame they no longer overlap.
- Both entities get the call. The script on the trigger learns who came in, and the script on the
  visitor learns which trigger it entered.
- A trigger does **not** need a Rigidbody. A static trigger volume is the usual setup.
- Two overlapping triggers call each other too.
- A pair involving a trigger only ever calls the `OnTrigger*` callbacks, never `OnCollision*`.
- Only Sphere colliders are checked. The trigger's entity must be inside a sector.
- They fire at the end of the frame, just after the collision callbacks.

```lua
-- Scripts/Checkpoint.lua: a trigger that saves where the player last was.
function OnTriggerEnter(other)
    if not other.hasPlayerController then return end

    Global.checkpoint = entity.transform.position
    Debug.Print("Checkpoint reached")
end
```

```lua
-- Scripts/HealingPool.lua: heal whoever stands in it, a bit every frame.
---@field healPerSecond number
healPerSecond = 10

function OnTrigger(other)
    local health = other:GetScript("Health")
    if health.isValid then
        health.health = math.min(health.maxHealth, health.health + healPerSecond * GameTime.deltaTime)
    end
end
```

---

## OnSectorChange

```lua
function OnSectorChange(sector) end
```

| Parameter | Type | |
|---|---|---|
| `sector` | [`Sector`](Sector.md) or `nil` | The sector the entity is in now, or `nil` if it left the map. |

Called on entity scripts when their entity ends up in a different sector.

- It fires at the end of the frame the entity crossed over, right after the sectors'
  `OnEntityExit` / `OnEntityEnter`.
- The sector the entity starts the level in does not count as a change.
- Which sector an entity is "in" works exactly like `entity:GetSector()`: only the `(x, z)`
  position matters, and the innermost sector wins. See [Sector](Sector.md#occupancy).

```lua
-- Scripts/Footsteps.lua: change footstep sound by floor type.
function OnSectorChange(sector)
    local audio = entity.audioSource
    if audio == nil or sector == nil then return end

    -- Paths are inside Assets, with the extension.
    if sector:HasTag("water") then audio.soundFileName = "Sounds/Footsteps/splash.wav"
    elseif sector:HasTag("metal") then audio.soundFileName = "Sounds/Footsteps/clank.wav"
    else audio.soundFileName = "Sounds/Footsteps/step.wav" end
end
```

---

## OnEntityEnter / OnEntityExit

```lua
function OnEntityEnter(entity) end
function OnEntityExit(entity) end
```

| Parameter | Type | |
|---|---|---|
| `entity` | [`Entity`](Entity.md) | The entity that came in or went out. |

Called on sector scripts when an entity crosses into or out of their sector.

- They fire once, at the end of the frame the entity crossed the boundary.
- Entities already inside when the level starts **don't** trigger `OnEntityEnter`. Use
  `sector:GetEntities()` in `Start` if you need them.
- Moving from one sector to another fires the old sector's `OnEntityExit` first, then the new
  sector's `OnEntityEnter`.
- An entity that is destroyed inside, or leaves the map, also fires `OnEntityExit`. After a
  destroy, `entity.isValid` is `false`.
- The parameter is named `entity` here, which hides the script's own `entity` global inside the
  function. On a sector script that global is an empty placeholder anyway.

```lua
-- Scripts/TrapRoom.lua (sector script): lock the door behind the player
-- and open it once every enemy in the room is gone.
---@field door Sector
door = nil

function OnEntityEnter(e)
    if e.hasPlayerController and door ~= nil then door:MoveCeilingToFloor(1, 200) end
end

function OnEntityExit(e)
    if door ~= nil and sector:CountEntities("enemy") == 0 then door:MoveFloorToCeiling(1, 60) end
end
```

---

## Order within one frame

| # | Step | Callbacks |
|---|---|---|
| 1 | Scripts | For each script in turn: `OnEnable`/`Start` or `OnDisable` if its active state changed, then `Update`. Then all `FixedUpdate` steps. |
| 2 | Sector movement | Floor/ceiling moves and light fades advance. |
| 3 | Player controller | WASD movement, jumping, mouse look. |
| 4 | Physics | Gravity, velocity, collisions. |
| 5 | Sector membership | Every moved entity is assigned to the sector it's in now. |
| 6 | Sector events | `OnEntityExit`, `OnEntityEnter` (sector scripts), then `OnSectorChange` (entity scripts). |
| 7 | Contact events | `OnCollisionExit`, `OnCollisionEnter`, `OnCollision`, then `OnTriggerExit`, `OnTriggerEnter`, `OnTrigger`. |
| 8 | Destroys | Entities queued with `Destroy()` run `OnDestroy` and are removed. |

In step 1, entity scripts run before sector scripts. Something a script does in
`Update` (moving an entity, starting a door) is already reflected in the events at the end of the
same frame.
