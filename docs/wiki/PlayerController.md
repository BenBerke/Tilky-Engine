# Player Controller

**Inspector name:** Player Controller · **Lua:** `entity.playerController` · **Public field type:** `PlayerController`

The Player Controller is built-in first-person movement: WASD to walk, the mouse to look, Space to
jump. Add it to your player entity and you have a playable character without writing any code.
Everything it does could also be written as a Lua script. It's there so you don't have to.

## Setting up a player

The player entity needs:

| Component | Why |
|---|---|
| [Transform](Transform.md) | Where the player is (at the feet). |
| [Rigidbody](Rigidbody.md) | **Required.** The controller moves the player by setting the Rigidbody's velocity. Without one the controller is skipped and an error is logged. Leave **Is Static** off. |
| [Collider](Collider.md), Sphere | Needed to stand on floors, bump into walls and climb steps. Give it a **Step Size** (e.g. `8`) so the player can walk up stairs. |
| [Camera](Camera.md) | What you see through. It is normally on the same entity, so the view sits at **Eye Height** above the feet. |
| Player Controller | Tick **Is Active**. |

## How it works

### Which controller is used

Only one Player Controller runs: the one with **Is Active** ticked.

- **Ticking a controller's Is Active switches control to it** and unticks every other controller,
  including others on the same entity, in the inspector or from a script
  (`playerController.isActive = true`).
- The camera does **not** switch with it. To see through the new player too, also set its
  `camera.isActive = true`.
- When control leaves a player, its horizontal velocity is set to `0`, so it stops instead of
  sliding on.
- Unticking the active controller leaves none active, which pauses player control.
- Adding a Player Controller in the editor ticks it and its Camera, so it takes over. A copied
  entity's controller starts unticked.
- If several controllers were saved ticked, the first one keeps Is Active when the level starts.

### Movement

Every frame, while it is active:

| Input | Action |
|---|---|
| **W** / **S** | Walk forward / backward. |
| **A** / **D** | Strafe left / right. |
| **Left Shift + W** | Run at **Running Speed**. It only works while moving forward. |
| **Space** | Jump, if the Rigidbody is on the ground. |
| **V** | Toggle **No Clip**. |
| Mouse | Look around. |

- "Forward" is the direction the player's **own** [Camera](Camera.md) faces (its `yaw`), flattened
  onto the ground, even while another entity's camera is in use. A player without a Camera of its
  own uses the active camera's instead.
- The controller sets the Rigidbody's **horizontal** velocity directly to `speed` (or
  `runningSpeed`) in the direction of the keys. When no movement key is held, the horizontal
  velocity is set to `0`: the player stops instantly, with no sliding.
- This happens every frame, so **anything else that changes the player's horizontal velocity is
  overwritten**. Knockback or conveyor belts pushing the player sideways need to move the Transform
  instead, or turn the controller off for a moment.
- **Vertical** velocity is left alone, apart from jumping. Gravity, falling, jump pads and
  launches all work normally.

### Jumping

Pressing Space sets the Rigidbody's vertical velocity to **Jump Strength**, but only while the
Rigidbody is **grounded** (`rigidbody.isGrounded`). A press shortly before landing still counts:
it is remembered for **Jump Buffer Milliseconds**, and the jump happens as soon as the player
touches down.

How high a jump goes depends on Jump Strength and gravity. Gravity is the level's **Gravity**
setting × the Rigidbody's **Gravity Scale**, about 96 units/s² with the defaults. With the default
Jump Strength of 100, a jump peaks at roughly `100² / (2 × 96) ≈ 52` units.

### Looking

Moving the mouse turns the player's own camera, **only while it is the active camera**. While
another entity's camera is in use, the mouse does nothing. Horizontal movement changes `yaw` by
`mouse × Sensitivity X` and vertical movement changes `pitch` by `mouse × Sensitivity Y`. Pitch is
kept between **Min Pitch** and **Max Pitch**. Yaw wraps around to stay within `0..360`, then is
kept between **Min Yaw** and **Max Yaw** (the defaults allow a full turn).

### No Clip

**No Clip** switches the player's collider off, so they walk through walls. Press **V** to toggle
it while playing. It's meant for debugging. There's no fly up/down control.

While a Player Controller is active it sets its entity's `collider.isActive` every frame to match
No Clip. A script can't turn the player's collider on or off separately.

### Sound

The audio **listener**, the "ears" for every [Audio Source](AudioSource.md), follows the active
[Camera](Camera.md), not the controller. When the player's own camera is active, that puts it at
the player's eye position, facing where they look.

## In the editor

| Field | Lua | Default | Notes |
|---|---|---|---|
| **Is Active** | `isActive` | off | Must be ticked for the controller to do anything. |
| **Speed** | `speed` | `46` | Walking speed, units per second. |
| **Running Speed** | `runningSpeed` | `90` | Speed while holding Left Shift + W. |
| **Jump Strength** | `jumpPower` | `100` | Upward speed at the start of a jump. |
| **Jump Buffer Milliseconds** | | `5` | How early before landing a jump press is kept. Editor only. |
| **Eye Height** | `eyeHeight` | `12` | Camera height above the feet. |
| **Friction** | `friction` | `0.8` | **Not used** by the built-in movement, which always stops instantly. |
| **Sensitivity X** / **Y** | `sensitivityX` / `sensitivityY` | `0.5` | Degrees per mouse unit. |
| **Min Pitch** / **Max Pitch** | | `-89` / `89` | Editor only. |
| **Min Yaw** / **Max Yaw** | | `0` / `360` | Editor only. |
| **No Clip** | `noClip` | off | Walk through everything. |

## Scripting

| Property | Type | | Description |
|---|---|---|---|
| `isValid` | boolean | read-only | `false` if the entity or its Player Controller is gone. |
| `isActive` | boolean | read/write | `true` switches control to this controller and unticks the others. `false` pauses movement and mouse look. |
| `speed` | number | read/write | |
| `runningSpeed` | number | read/write | |
| `jumpPower` | number | read/write | |
| `eyeHeight` | number | read/write | Takes effect on the next frame. |
| `friction` | number | read/write | Unused. See above. |
| `sensitivityX` | number | read/write | |
| `sensitivityY` | number | read/write | |
| `noClip` | boolean | read/write | |
| `currentSpeed` | number | read-only | `speed` or `runningSpeed`, whichever was used this frame. |
| `velocity` | Vector3 | read-only | Always `0,0,0`. Read `entity.rigidbody.velocity` for the real velocity. |
| `currentEyeHeight` | number | read-only | Always `0`. Use `eyeHeight`. |

Whatever you aim "from the player's eyes" (raycasts, projectiles) should start at
`transform.position.y + playerController.eyeHeight`, not at the Transform position.

## Examples

### Crouch

```lua
-- Scripts/Player/Crouch.lua (on the player)
public number crouchEyeHeight = 6
public number crouchSpeed = 20

local standEye, standSpeed

function Start()
    local pc = entity.playerController
    standEye, standSpeed = pc.eyeHeight, pc.speed
end

function Update()
    local pc = entity.playerController
    local crouching = Input.GetKey(Key.LCtrl)

    local wantedEye = crouching and crouchEyeHeight or standEye
    pc.eyeHeight = mathT.MoveTowards(pc.eyeHeight, wantedEye, 40 * GameTime.deltaTime)
    pc.speed = crouching and crouchSpeed or standSpeed
end
```

### Double jump

```lua
-- Scripts/Player/DoubleJump.lua (on the player)
public number airJumpPower = 90

local usedAirJump = false

function Update()
    local body = entity.rigidbody

    if body.isGrounded then
        usedAirJump = false
        return
    end

    -- The controller already handles the jump from the ground; this adds one in the air.
    if Input.GetKeyDown(Key.Space) and not usedAirJump then
        usedAirJump = true
        local v = body.velocity
        body.velocity = Vector3(v.x, airJumpPower, v.z)
    end
end
```

### Freeze the player during a cutscene

```lua
-- Scripts/Cutscene.lua (on the player)
-- Other scripts call player:GetScript("Cutscene"):Play(3)
local timeLeft = 0

function Play(self, seconds)
    timeLeft = seconds
    entity.playerController.isActive = false -- also stops the player's walking velocity
end

function Update()
    if timeLeft <= 0 then return end

    timeLeft = timeLeft - GameTime.deltaTime
    if timeLeft <= 0 then entity.playerController.isActive = true end
end
```

### Speed zone

```lua
-- Scripts/Player/SpeedZone.lua (on the player)
-- Walk faster in sectors tagged "fast".
local baseSpeed

function Start()
    baseSpeed = entity.playerController.speed
end

function OnSectorChange(sector)
    local fast = sector ~= nil and sector:HasTag("fast")
    entity.playerController.speed = fast and baseSpeed * 2 or baseSpeed
end
```

More player scripts (jump pads, sprint stamina and more) are in
[`docs/scripts/03_player.md`](../scripts/03_player.md).
