# Camera

**Inspector name:** Camera · **Lua:** `entity.camera` · **Public field type:** `Camera`

The Camera is the eye the game is rendered through. It decides where the view is, which way it
looks and how wide it sees. A level **must** have at least one camera to play. Without one the
level refuses to start, and no scripts run.

The usual setup puts the Camera on the player entity, next to a
[Player Controller](PlayerController.md), which turns it with the mouse. A camera can also sit on
any other entity: a security camera, a cutscene viewpoint, a spectator.

## How it works

### Only one camera renders

A level can contain many cameras, but only **one** is used at a time: the **first camera whose
Is Active box is ticked**.

When the level starts:

1. The first camera with **Is Active** ticked becomes the active camera, and **Is Active is
   switched off on every other camera**.
2. If no camera has Is Active ticked, the first camera in the level is used.
3. If the level has no camera at all, the level doesn't start.

To switch cameras while playing, turn the current one off and the new one on (see
[Switching cameras](#switching-cameras)). If more than one is active, the first one in the level
wins, which may not be the one you just turned on.

### Where the camera is

The view is placed at the camera entity's [Transform](Transform.md) position:

- If the **same entity** also has an active [Player Controller](PlayerController.md), the view is
  raised by the controller's **Eye Height**. Transform positions are at the feet, so this puts
  the eye at head height.
- Otherwise the view is **exactly** at the Transform position. For a standalone camera, put its
  Transform at the height you want to see from.

The Transform's rotation is **not** used. The camera has its own direction.

### Which way it looks

The direction comes from two angles, in degrees:

- **`yaw`** turns left and right around the vertical axis. At `0` the camera looks along **+z**;
  in general it looks along `x = sin(yaw)`, `z = cos(yaw)`.
- **`pitch`** tilts up and down. `0` is level, positive looks up and negative looks down.

From those the engine computes `forward`, a unit `Vector3` pointing where the camera looks:
`(sin(yaw)·cos(pitch), sin(pitch), cos(yaw)·cos(pitch))`. Use it to aim raycasts, projectiles and
"what am I looking at" checks.

While a [Player Controller](PlayerController.md) is active, the mouse changes the **active
camera's** `yaw` and `pitch` every frame, whichever entity that camera is on. The mouse **adds** to
the current angles, so a script can still nudge them (recoil, screen shake) and the mouse carries on
from there.

### Lens

- **`fov`** is the **vertical** field of view in degrees. The horizontal view follows from the
  window's shape. Bigger values see more but make things look smaller and more stretched at the
  edges. 60 to 100 is the useful range.
- **`nearPlane`** / **`farPlane`** are the closest and furthest distances that get drawn. Anything
  nearer than `nearPlane` or further than `farPlane` is cut off. Keep `nearPlane` small (the default
  0.1 is fine) so walls don't vanish when you press your face against them.
- **`aspectRatio`** is width divided by height. The engine sets it from the window size **every
  frame**, so changing it has no lasting effect.

### Smooth stepping

When a physics body walks up a step, physics lifts it onto the step in a single frame. Without
smoothing, the view would jump up. With **Smooth Stepping** on, the view glides up to the new
height instead, and does the same when stepping down.

- It only applies to a camera whose entity has a [Collider](Collider.md) with a **Step Size**
  above 0 and a grounded [Rigidbody](Rigidbody.md), which is how a player is normally set up.
- **Smoothing Strength** sets how fast the view catches up. Each second it closes about
  `1 - e^(-strength)` of the remaining gap, so `1` is a slow, floaty glide, `10` is quick and `20`
  and above is nearly instant.
- Jumping and falling are never smoothed. Only step changes are.

## In the editor

| Field | Lua | Default | Notes |
|---|---|---|---|
| **Is Active** | `isActive` | on | See [Only one camera renders](#only-one-camera-renders). |
| **FOV** | `fov` | `90` | Vertical field of view, degrees. |
| **Aspect Ratio** | `aspectRatio` | `1.75` | Overwritten from the window size while playing. |
| **Near Plane** | `nearPlane` | `0.1` | |
| **Far Plane** | `farPlane` | `10000` | |
| **Smooth Stepping** | | on | Editor only. |
| **Smoothing Strength** | | `1` | Editor only. |

`yaw` and `pitch` aren't shown in the inspector. They start at `0` (looking along +z, level) unless
a script sets them.

## Scripting

| Property | Type | | Description |
|---|---|---|---|
| `isValid` | boolean | read-only | `false` if the entity or its Camera is gone. |
| `isActive` | boolean | read/write | Whether this camera may render. |
| `yaw` | number | read/write | Degrees, left/right. |
| `pitch` | number | read/write | Degrees, up/down. |
| `fov` | number | read/write | Vertical field of view, degrees. |
| `aspectRatio` | number | read/write | Overwritten every frame. Treat it as read-only. |
| `nearPlane` | number | read/write | |
| `farPlane` | number | read/write | |
| `forward` | Vector3 | read-only | Unit look direction, updated once per rendered frame. |
| `target` | Vector3 | read-only | Not kept up to date. Use `forward`. |

`forward` is updated when the frame is drawn, which happens after scripts run. If you change `yaw`
or `pitch` in `Update` and read `forward` straight away, you get last frame's direction. Compute
it yourself if you need it at once:

```lua
local function Forward(yaw, pitch)
    local y, p = mathT.DegToRad(yaw), mathT.DegToRad(pitch)
    return Vector3(mathT.Sin(y) * mathT.Cos(p), mathT.Sin(p), mathT.Cos(y) * mathT.Cos(p))
end
```

## Examples

### Zoom while holding the right mouse button

```lua
-- Scripts/Camera/Zoom.lua (on the player)
---@field zoomFov number @ Zoomed FOV
zoomFov = 40

---@field speed number @ Zoom Speed
speed = 10

local normalFov

function Start()
    normalFov = entity.camera.fov
end

function Update()
    local camera = entity.camera
    local wanted = Input.GetMouseButton(Input.MouseRight) and zoomFov or normalFov
    camera.fov = mathT.Lerp(camera.fov, wanted, 1 - mathT.Exp(-speed * GameTime.deltaTime))
end
```

### Screen shake

```lua
-- Scripts/Camera/Shake.lua (on the player)
-- Other scripts call entity:GetScript("Shake"):Shake(strength, seconds).
local strength = 0
local timeLeft = 0
local lastYaw, lastPitch = 0, 0

function Shake(self, amount, seconds)
    strength = amount
    timeLeft = seconds
end

function Update()
    local camera = entity.camera

    -- Undo last frame's offset so the shake never drifts the view.
    camera.yaw = camera.yaw - lastYaw
    camera.pitch = camera.pitch - lastPitch
    lastYaw, lastPitch = 0, 0

    if timeLeft <= 0 then return end
    timeLeft = timeLeft - GameTime.deltaTime

    lastYaw = mathT.RandomF(-strength, strength)
    lastPitch = mathT.RandomF(-strength, strength)
    camera.yaw = camera.yaw + lastYaw
    camera.pitch = camera.pitch + lastPitch
end
```

### Switching cameras

```lua
-- Scripts/Camera/CameraSwitcher.lua
-- Press C to cycle through every entity tagged "camera".
local cameras = {}
local current = 1

function Start()
    for _, e in ipairs(Game.FindEntitiesWithTag("camera")) do
        if e.hasCamera then cameras[#cameras + 1] = e.camera end
    end
end

local function Use(index)
    for i, camera in ipairs(cameras) do camera.isActive = (i == index) end
    current = index
end

function Update()
    if #cameras > 0 and Input.GetKeyDown("C") then
        Use(current % #cameras + 1)
    end
end
```

The Player Controller always steers with the **active** camera. After switching, the mouse turns
the new camera, and WASD moves the player relative to where the new camera looks. For a
cutscene or security-camera view, set the player's `playerController.isActive = false` while it is
showing, and turn it back on when you switch back.

### Security camera that sweeps side to side

```lua
-- Scripts/Camera/Sweep.lua (on an entity with a Camera, placed high on a wall)
---@field centerYaw number @ Center Yaw
centerYaw = 0

---@field range number @ Sweep Degrees
range = 45

---@field period number @ Seconds Per Sweep
period = 6

local t = 0

function Start()
    entity.camera.pitch = -20
end

function Update()
    t = t + GameTime.deltaTime
    entity.camera.yaw = centerYaw + mathT.Sin(t / period * mathT.Tau) * range
end
```

More camera effects (FOV kick, head bob) are in
[`docs/scripts/16_camera_effects.md`](../scripts/16_camera_effects.md).
