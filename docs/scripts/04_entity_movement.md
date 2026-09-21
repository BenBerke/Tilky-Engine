# 04 - Entity Movement

Scripts for moving props, platforms, and characters around. Position is a `Vector3` where `x` and
`z` are the ground plane and `y` is height. Move something by reading `transform.position`,
building a new `Vector3`, and assigning it back (or use `transform:addPosition(...)` for relative
moves).

Two ways to move something:

- **Teleport-style** (`transform.position = ...`): simple and precise, but ignores walls and
  colliders. Good for props, pickups, and scripted platforms.
- **Physics-style** (`rigidbody.velocity = ...`): the entity collides with the world and is
  affected by gravity. Good for characters. The enemy examples in [12_enemy_ai.md](12_enemy_ai.md)
  use this when a Rigidbody is present.

---

## Patrol between two points

**Attach to:** the moving GameObject. Create two empty GameObjects as waypoints and assign them.

```lua
-- Scripts/Movement/Patrol.lua (entity script)
---@field pointA GameObject @ Point A
pointA = nil

---@field pointB GameObject @ Point B
pointB = nil

---@field speed number @ Speed
speed = 30

---@field waitTime number @ Wait At Ends (s)
waitTime = 1.0

local transform
local goingToB = true
local waiting = 0.0

function Start()
    transform = gameObject.transform
end

function Update()
    if pointA == nil or pointB == nil then return end

    local dt = GameTime.deltaTime

    if waiting > 0 then
        waiting = waiting - dt
        return
    end

    local target = goingToB and pointB or pointA
    local goal = target.transform.position
    local p = transform.position

    local dx, dz = goal.x - p.x, goal.z - p.z
    local dist = math.sqrt(dx * dx + dz * dz)
    local step = speed * dt

    if dist <= step then
        -- Arrived: snap to the point, turn around, and pause.
        transform.position = Vector3(goal.x, p.y, goal.z)
        goingToB = not goingToB
        waiting = waitTime
    else
        transform.position = Vector3(p.x + dx / dist * step, p.y, p.z + dz / dist * step)
        transform.forward = Vector2(dx / dist, dz / dist)   -- face the way we're going
    end
end
```

**Notes**

- Movement stays on the ground plane and keeps the entity's own `y`.
- `transform.forward` is a `Vector2` (`x`, `z`). Directional sprites use it to pick which side to
  show.
- Want a longer route? Replace the two points with a table of waypoints and an index.

---

## Orbit

**Attach to:** the orbiting GameObject.

```lua
-- Scripts/Movement/Orbit.lua (entity script)
---@field center GameObject @ Center (optional)
center = nil

---@field radius number @ Radius
radius = 40

---@field degreesPerSecond number @ Degrees Per Second
degreesPerSecond = 90

local transform
local angle = 0.0
local cx, cz = 0.0, 0.0

function Start()
    transform = gameObject.transform

    -- Orbit the assigned GameObject, or the spot where this entity starts.
    local c = (center ~= nil) and center.transform.position or transform.position
    cx, cz = c.x, c.z
end

function Update()
    angle = angle + mathT.DegToRad(degreesPerSecond) * GameTime.deltaTime

    local p = transform.position
    transform.position = Vector3(cx + math.cos(angle) * radius, p.y, cz + math.sin(angle) * radius)
end
```

**Notes**

- With no `center` assigned, the orbit is centered on the starting position, so the entity jumps
  `radius` units on the first frame. Place it where you want the *center* to be.

---

## Bob and spin

**Attach to:** a pickup, a floating item, a decoration. It should not have a Rigidbody, or gravity
will fight the bobbing.

```lua
-- Scripts/Movement/BobAndSpin.lua (entity script)
---@field bobHeight number @ Bob Height
bobHeight = 3

---@field bobSpeed number @ Bob Speed
bobSpeed = 2

---@field spinDegreesPerSecond number @ Spin (deg/s)
spinDegreesPerSecond = 120

local transform
local baseY = 0.0
local phase = 0.0
local yaw = 0.0

function Start()
    transform = gameObject.transform
    baseY = transform.position.y
    phase = mathT.RandomF(0, 6.28)   -- so a row of pickups doesn't bob in lockstep
end

function Update()
    local dt = GameTime.deltaTime

    phase = phase + bobSpeed * dt
    yaw = yaw + math.rad(spinDegreesPerSecond) * dt

    local p = transform.position
    transform.position = Vector3(p.x, baseY + math.sin(phase) * bobHeight, p.z)

    -- Rotation is a quaternion (x, y, z, w). Rotating about the Y axis by `yaw`:
    transform.rotation = Vector4(0, math.sin(yaw / 2), 0, math.cos(yaw / 2))
end
```

**Notes**

- Billboard sprites always face the camera, so the spin only matters for entities that render
  with their rotation.

---

## Smooth follow

**Attach to:** the follower.

Eases toward a target instead of snapping, using exponential smoothing so the feel is the same at
any frame rate.

```lua
-- Scripts/Movement/Follow.lua (entity script)
---@field target GameObject @ Target
target = nil

---@field followSpeed number @ Follow Speed (higher = snappier)
followSpeed = 4

---@field offset Vector3 @ Offset
offset = Vector3(0, 0, 0)

local transform

function Start()
    transform = gameObject.transform
end

function Update()
    if target == nil or not target.isValid then return end

    -- 1 - e^(-k*dt) is a frame-rate independent blend factor.
    local k = 1 - math.exp(-followSpeed * GameTime.deltaTime)

    local p = transform.position
    local t = target.transform.position

    transform.position = Vector3(
        mathT.Lerp(p.x, t.x + offset.x, k),
        mathT.Lerp(p.y, t.y + offset.y, k),
        mathT.Lerp(p.z, t.z + offset.z, k)
    )
end
```

---

## Keyboard mover

**Attach to:** any prop you want to push around with the arrow keys.

```lua
-- Scripts/Movement/KeyboardMover.lua (entity script)
---@field speed number @ Speed
speed = 40

local transform

function Start()
    transform = gameObject.transform
end

function Update()
    local dx, dz = 0.0, 0.0

    if Input.GetKey("Left") then dx = dx - 1 end
    if Input.GetKey("Right") then dx = dx + 1 end
    if Input.GetKey("Up") then dz = dz + 1 end
    if Input.GetKey("Down") then dz = dz - 1 end

    if dx == 0 and dz == 0 then return end

    -- Normalise so diagonals aren't faster.
    local len = math.sqrt(dx * dx + dz * dz)
    local step = speed * GameTime.deltaTime

    transform:addPosition(Vector3(dx / len * step, 0, dz / len * step))
    transform.forward = Vector2(dx / len, dz / len)
end
```

**Notes**

- `Input.GetKey` is true while held, `GetKeyDown` only on the frame it was pressed, and `GetKeyUp`
  only on the frame it was released. Key names: letters `A`-`Z`, digits `0`-`9`, `Space`,
  `Escape`, `Enter`, `Tab`, `Backspace`, `Left`/`Right`/`Up`/`Down`, `LShift`/`RShift`,
  `LCtrl`/`RCtrl`, `LAlt`/`RAlt`. An unknown name simply returns `false`.
