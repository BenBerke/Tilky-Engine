# 16 - Camera Effects

These scripts go on the Entity that holds the `Camera` component, which is normally the
**player**. A `Camera` component is reached with `entity.camera` and has:

| Member | Notes |
|--------|-------|
| `fov` | Field of view |
| `yaw`, `pitch` | Look angles in degrees. The `PlayerController` changes these from the mouse every frame |
| `forward` | Read-only `Vector3`, the direction the camera faces |
| `nearPlane`, `farPlane`, `aspectRatio` | Projection settings |
| `isActive` | Whether this camera is the one rendering |

---

## Sprint FOV kick and zoom

Widens the view slightly while sprinting and narrows it while the right mouse button is held.
The change eases in and out instead of snapping.

```lua
-- Scripts/Camera/FovEffects.lua (entity script)
---@field sprintFovBonus number @ Sprint FOV Bonus
sprintFovBonus = 10

---@field zoomScale number @ Zoom FOV Multiplier (0.5 = 2x zoom)
zoomScale = 0.5

---@field blendSpeed number @ Blend Speed
blendSpeed = 8

local cam
local baseFov = 60.0

function Start()
    cam = entity.camera

    if cam == nil then
        Debug.LogError("FovEffects: " .. entity.name .. " has no Camera")
        return
    end

    baseFov = cam.fov   -- whatever the designer set is "normal"
end

function Update()
    if cam == nil then return end

    local target = baseFov

    if Input.GetMouseButton(Input.MouseRight) then
        target = baseFov * zoomScale
    elseif Input.GetKey("LShift") and Input.GetKey("W") then
        target = baseFov + sprintFovBonus   -- same condition the PlayerController sprints on
    end

    -- Frame-rate independent easing toward the target.
    local k = 1 - math.exp(-blendSpeed * GameTime.deltaTime)
    cam.fov = mathT.Lerp(cam.fov, target, k)
end
```

---

## Head bob

Adds a gentle vertical sway while walking on the ground.

```lua
-- Scripts/Camera/HeadBob.lua (entity script)
---@field bobHeight number @ Bob Height
bobHeight = 0.6

---@field stepsPerUnit number @ Bob Cycles Per Unit Walked
stepsPerUnit = 0.04

---@field blendSpeed number @ Fade Speed
blendSpeed = 10

local pc, rb
local baseEye = 12.0
local phase = 0.0
local amount = 0.0

function Start()
    pc = entity.playerController
    rb = entity.rigidbody

    if pc == nil or rb == nil then
        Debug.LogError("HeadBob: " .. entity.name .. " needs a PlayerController and a Rigidbody")
        return
    end

    baseEye = pc.eyeHeight
end

function Update()
    if pc == nil or rb == nil then return end

    local dt = GameTime.deltaTime
    local v = rb.velocity
    local speed = math.sqrt(v.x * v.x + v.z * v.z)
    local walking = rb.isGrounded and speed > 1

    if walking then
        phase = phase + speed * dt * stepsPerUnit * 2 * math.pi
    end

    -- Fade the bob in and out so it doesn't jolt when starting or stopping.
    amount = mathT.Lerp(amount, walking and bobHeight or 0.0, 1 - math.exp(-blendSpeed * dt))

    pc.eyeHeight = baseEye + math.sin(phase) * amount
end
```

**Notes**

- It works by nudging the controller's `eyeHeight`, so it will fight the crouch script in
  [03_player.md](03_player.md), which writes the same property. Use one or the other, or
  fold the crouch height into `baseEye`.

---

## Screen shake

Shakes the view by nudging yaw and pitch. Other scripts trigger it through a public `Shake`
function, so an explosion, a landing, or a crusher can all rattle the camera.

```lua
-- Scripts/Camera/ScreenShake.lua (entity script)
---@field testKey string @ Test Key (empty = none)
testKey = "T"

local cam
local strength = 0.0
local timeLeft = 0.0
local totalTime = 1.0
local offsetYaw, offsetPitch = 0.0, 0.0

-- Public: shakerScript:Shake(degrees, seconds)
function Shake(self, degrees, seconds)
    strength = degrees
    timeLeft = seconds
    totalTime = math.max(seconds, 0.001)
end

function Start()
    cam = entity.camera

    if cam == nil then
        Debug.LogError("ScreenShake: " .. entity.name .. " has no Camera")
    end
end

function Update()
    if cam == nil then return end

    if testKey ~= "" and Input.GetKeyDown(testKey) then Shake(nil, 2.0, 0.4) end

    -- The mouse look keeps adding to yaw/pitch, so take back last frame's shake first.
    cam.yaw = cam.yaw - offsetYaw
    cam.pitch = cam.pitch - offsetPitch
    offsetYaw, offsetPitch = 0.0, 0.0

    if timeLeft > 0 then
        timeLeft = timeLeft - GameTime.deltaTime

        -- Fades out as the shake runs down.
        local k = strength * math.max(0, timeLeft) / totalTime
        offsetYaw = mathT.RandomF(-k, k)
        offsetPitch = mathT.RandomF(-k, k)
    end

    cam.yaw = cam.yaw + offsetYaw
    cam.pitch = cam.pitch + offsetPitch
end
```

**Calling it from another script**

```lua
---@field shaker Behaviour @ Screen Shake Script
shaker = nil

-- ...when something explodes:
if shaker ~= nil then shaker:Shake(3.0, 0.5) end
```

**Notes**

- The shake is an *offset* that is removed and re-applied every frame. Writing random values
  straight into `yaw`/`pitch` without undoing them would slowly drag the player's aim around.
