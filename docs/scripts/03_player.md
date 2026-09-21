# 03 - Player

The built-in `PlayerController` already handles walking, mouse look, jumping, sprinting
(`LShift` + `W`) and no-clip (`V`). These scripts tweak it by reading and writing its properties, or
by nudging the player's `Rigidbody`.

In the usual setup the player GameObject carries the `PlayerController`, `Rigidbody`, and
`Camera` components, so an entity script on it can reach them through
`gameObject.playerController`, `gameObject.rigidbody`, and `gameObject.camera`. Each of these is
`nil` if the component is missing.

---

## Crouch

**Attach to:** the player.

Hold a key to walk slowly with a lower eye height.

```lua
-- Scripts/Player/Crouch.lua (entity script)
---@field crouchKey string @ Crouch Key
crouchKey = "LCtrl"

---@field crouchSpeed number @ Crouch Speed
crouchSpeed = 22

---@field crouchEyeHeight number @ Crouch Eye Height
crouchEyeHeight = 6

local pc
local standingSpeed, standingRunSpeed, standingEye

function Start()
    pc = gameObject.playerController

    if pc == nil then
        Debug.LogError("Crouch: " .. gameObject.name .. " has no PlayerController")
        return
    end

    standingSpeed = pc.speed
    standingRunSpeed = pc.runningSpeed
    standingEye = pc.eyeHeight
end

function Update()
    if pc == nil then return end

    if Input.GetKey(crouchKey) then
        pc.speed = crouchSpeed
        pc.runningSpeed = crouchSpeed   -- no sprinting while crouched
        pc.eyeHeight = crouchEyeHeight
    else
        pc.speed = standingSpeed
        pc.runningSpeed = standingRunSpeed
        pc.eyeHeight = standingEye
    end
end
```

**Notes**

- The standing values are captured in `Start` so the script respects whatever you set in the
  inspector.
- Nothing here checks for a low ceiling above the player, so standing up can clip into a ceiling.
- Other scripts that write `speed`, `runningSpeed` or `eyeHeight` (like the speed zone below, or
  the head bob in [16_camera_effects.md](16_camera_effects.md)) will fight this one.

---

## Double jump

**Attach to:** the player.

The controller handles the first jump from the ground. This script adds extra jumps in the air by
overwriting the vertical velocity.

```lua
-- Scripts/Player/DoubleJump.lua (entity script)
---@field extraJumps int @ Extra Air Jumps
extraJumps = 1

---@field jumpKey string @ Jump Key
jumpKey = "Space"

local rb, pc
local jumpsLeft = 0

function Start()
    rb = gameObject.rigidbody
    pc = gameObject.playerController
    jumpsLeft = extraJumps
end

function Update()
    if rb == nil or pc == nil then return end

    if rb.isGrounded then
        jumpsLeft = extraJumps
    elseif jumpsLeft > 0 and Input.GetKeyDown(jumpKey) then
        local v = rb.velocity
        rb.velocity = Vector3(v.x, pc.jumpPower, v.z)
        jumpsLeft = jumpsLeft - 1
    end
end
```

**Notes**

- `rb.velocity` returns a copy, so build a new `Vector3` and assign it back. Keeping `v.x` and
  `v.z` preserves the controller's horizontal movement.
- Make the extra jump stronger or weaker than the first by using a different number instead of
  `pc.jumpPower`.

---

## Speed zone

**Attach to:** a sector.

While the player stands inside this sector their speed is multiplied. It goes back to normal when
they leave.

```lua
-- Scripts/Player/SpeedZone.lua (sector script)
---@field player GameObject @ Player
player = nil

---@field multiplier number @ Speed Multiplier
multiplier = 1.6

local pc
local baseSpeed, baseRunSpeed
local boosted = false

local function PlayerIsInside()
    for i = 1, sector.entityCount do
        if sector:GetEntity(i).id == player.id then return true end
    end
    return false
end

local function Restore()
    if boosted then
        pc.speed = baseSpeed
        pc.runningSpeed = baseRunSpeed
        boosted = false
    end
end

function Start()
    if player == nil then
        Debug.LogWarning("SpeedZone in sector " .. sector.id .. " has no player assigned")
        return
    end

    pc = player.playerController
    if pc == nil then return end

    baseSpeed = pc.speed
    baseRunSpeed = pc.runningSpeed
end

function Update()
    if pc == nil then return end

    local inside = PlayerIsInside()

    if inside and not boosted then
        pc.speed = baseSpeed * multiplier
        pc.runningSpeed = baseRunSpeed * multiplier
        boosted = true
    elseif not inside then
        Restore()
    end
end

function OnDisable()
    if pc ~= nil then Restore() end
end
```

**Notes**

- `sector.entityCount` and `sector:GetEntity(i)` list the entities the engine currently places
  inside the sector, so no geometry math is needed.
- The `OnDisable` hook makes sure the player never keeps the bonus if the script is switched off
  while they're standing in the zone.

---

## Jump pad

**Attach to:** a sector.

Launches the player upward when they stand on the sector.

```lua
-- Scripts/Player/JumpPad.lua (sector script)
---@field player GameObject @ Player
player = nil

---@field launchSpeed number @ Launch Speed
launchSpeed = 160

local rb

local function PlayerIsInside()
    for i = 1, sector.entityCount do
        if sector:GetEntity(i).id == player.id then return true end
    end
    return false
end

function Start()
    if player ~= nil then rb = player.rigidbody end
end

function Update()
    if rb == nil then return end

    if rb.isGrounded and PlayerIsInside() then
        local v = rb.velocity
        rb.velocity = Vector3(v.x, launchSpeed, v.z)
        Debug.Print("Boing!")
    end
end
```

**Notes**

- Because it only fires while `isGrounded`, holding still on the pad launches once per landing.
- Stronger pads (more `launchSpeed`) may need a taller sector ceiling so the player doesn't hit
  their head.
