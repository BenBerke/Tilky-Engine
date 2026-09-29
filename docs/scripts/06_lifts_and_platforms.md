# 06 - Lifts and Platforms

Lifts, crushers, and floods are **sector scripts** that move a sector's *floor* or *ceiling*.

```lua
local f = sector:GetFloor(1)
local low = f.floorHeight       -- read
f.floorHeight = low + 1         -- write
local high = f.ceilingHeight    -- read
f.ceilingHeight = high + 1      -- write
```

Both setters enforce that the floor stays below the ceiling (and doesn't overlap another interval
in the same sector), so give lifts a ceiling comfortably above their highest stop, and write the
values through `pcall` like the door scripts do.

For the common case of "move this floor up to the ceiling" (or the ceiling down to the floor) you
don't have to write the movement yourself: see [Built-in floor and ceiling movement](#built-in-floor-and-ceiling-movement)
below.

To know whether the player is standing in the sector, ask the sector which entities are inside:

```lua
local playerIsInside = false

for i = 1, sector.entityCount do
    if sector:GetEntity(i).id == player.id then playerIsInside = true end
end
```

---

## Built-in floor and ceiling movement

`Sector` has built-in functions that move one floor interval's floor or ceiling toward the other
surface for you. You call them once, and the engine moves the surface a bit every frame until it
arrives, so there is no `Update` code and no `pcall` needed.

```lua
sector:MoveFloorToCeiling(floorIndex, speed, gap)          -- speed in units/s
sector:MoveCeilingToFloor(floorIndex, speed, gap)
sector:MoveFloorToCeilingOverTime(floorIndex, seconds, gap) -- arrives after `seconds`
sector:MoveCeilingToFloorOverTime(floorIndex, seconds, gap)

sector:IsMoving(floorIndex)   -- true while that floor or ceiling is still moving
sector:StopMoving(floorIndex) -- stops both where they are
```

| Parameter | Meaning |
|-----------|---------|
| `floorIndex` | Which floor interval of the sector to move, 1-based like `GetFloor` |
| `speed` | Units per second. Must be above 0 |
| `seconds` | How long the move takes, measured from the heights when you call it. `0` snaps on the next frame |
| `gap` | Optional, default `0`. How far short of the other surface to stop. `MoveFloorToCeiling(1, 30, 10)` leaves 10 units between floor and ceiling |

Lua has no overloading on "a number that means speed" vs "a number that means seconds", so the
timed versions have their own `...OverTime` names. `gap` can be left out of all four.

**How they behave**

- The target follows the other surface. The floor stops `gap` below the ceiling's *current*
  height, so it still stops in the right place if something else moves the ceiling meanwhile.
- If the surfaces are already closer than `gap`, the surface moves *away* until the gap is
  reached. `sector:MoveCeilingToFloor(1, 60, 40)` on a closed door opens it to 40 units.
- A gap below `0.01` is raised to `0.01`: a floor can never touch its ceiling. A negative gap,
  a speed of 0 or less, or a negative time raises an error.
- The floor and the ceiling of an interval move independently, so both can move at once. Starting
  a new move on the same surface replaces the old one.
- A surface never moves into the interval above or below it in the same sector.
- Child sectors follow the move, same as when you write `floorHeight` yourself.
- Writing `floorHeight`/`ceilingHeight` directly doesn't cancel a move. Call `StopMoving` first.

### One-shot lift with the built-in movement

**Attach to:** the lift sector.

When the player steps on, the floor rises to 16 units below the ceiling over two seconds.

```lua
-- Scripts/Platforms/OneShotLift.lua (sector script)
---@field player Entity @ Player
player = nil

---@field travelTime number @ Travel Time (s)
travelTime = 2

---@field headroom number @ Space Left Under Ceiling
headroom = 16

local started = false

local function PlayerIsInside()
    for i = 1, sector.entityCount do
        if sector:GetEntity(i).id == player.id then return true end
    end
    return false
end

function Update()
    if started or player == nil or not player.isValid then return end

    if PlayerIsInside() then
        started = true
        sector:MoveFloorToCeilingOverTime(1, travelTime, headroom)
    end
end
```

### Door with the built-in movement

**Attach to:** the door sector. Its ceiling starts closed (down at the floor).

Press the use key inside the sector to open the door, and again to close it.

```lua
-- Scripts/Doors/BuiltinDoor.lua (sector script)
---@field player Entity @ Player
player = nil

---@field openHeight number @ Open Height
openHeight = 40

---@field speed number @ Speed
speed = 60

---@field useKey string @ Use Key
useKey = "E"

local open = false

local function PlayerIsInside()
    for i = 1, sector.entityCount do
        if sector:GetEntity(i).id == player.id then return true end
    end
    return false
end

function Update()
    if player == nil or not player.isValid then return end
    if not Input.GetKeyDown(useKey) or not PlayerIsInside() then return end

    open = not open

    -- A gap larger than the current opening moves the ceiling up (away
    -- from the floor), a gap of 0 closes it.
    sector:MoveCeilingToFloor(1, speed, open and openHeight or 0)
end
```

---

## Automatic lift

**Attach to:** the lift sector. The sector's ceiling must be higher than the floor's *top* stop.

Rises while the player is standing on it and sinks back when they step off.

```lua
-- Scripts/Platforms/Lift.lua (sector script)
---@field player Entity @ Player
player = nil

---@field rise number @ Rise Height
rise = 32

---@field speed number @ Lift Speed
speed = 30

local MIN_GAP = 0.5

local lift
local bottom, top
local broken = false

local function MoveToward(current, target, maxDelta)
    if math.abs(target - current) <= maxDelta then return target end
    if target > current then return current + maxDelta end
    return current - maxDelta
end

local function PlayerIsInside()
    for i = 1, sector.entityCount do
        if sector:GetEntity(i).id == player.id then return true end
    end
    return false
end

function Start()
    lift = sector:GetFloor(1)
    bottom = lift.floorHeight
    -- Never rise into the ceiling.
    top = math.min(bottom + rise, lift.ceilingHeight - MIN_GAP)
end

function Update()
    if broken or player == nil or not player.isValid then return end

    local wanted = PlayerIsInside() and top or bottom
    local current = lift.floorHeight

    if current ~= wanted then
        local height = MoveToward(current, wanted, speed * GameTime.deltaTime)

        local ok, err = pcall(function() lift.floorHeight = height end)
        if not ok then
            broken = true
            Debug.LogError("Lift (sector " .. sector.id .. "): " .. tostring(err))
        end
    end
end
```

---

## Call-button elevator

**Attach to:** the elevator sector.

Stands still until the player is aboard and presses a key, then travels to the other stop.

```lua
-- Scripts/Platforms/Elevator.lua (sector script)
---@field player Entity @ Player
player = nil

---@field travel number @ Travel Distance
travel = 64

---@field speed number @ Speed
speed = 40

---@field callKey string @ Call Key
callKey = "E"

local MIN_GAP = 0.5

local lift
local lowStop, highStop
local atTop = false
local moving = false
local broken = false

local function MoveToward(current, target, maxDelta)
    if math.abs(target - current) <= maxDelta then return target end
    if target > current then return current + maxDelta end
    return current - maxDelta
end

local function PlayerIsInside()
    for i = 1, sector.entityCount do
        if sector:GetEntity(i).id == player.id then return true end
    end
    return false
end

function Start()
    lift = sector:GetFloor(1)
    lowStop = lift.floorHeight
    highStop = math.min(lowStop + travel, lift.ceilingHeight - MIN_GAP)
end

function Update()
    if broken or player == nil or not player.isValid then return end

    if not moving and Input.GetKeyDown(callKey) and PlayerIsInside() then
        atTop = not atTop
        moving = true
        Debug.Print(atTop and "Going up" or "Going down")
    end

    if moving then
        local wanted = atTop and highStop or lowStop
        local height = MoveToward(lift.floorHeight, wanted, speed * GameTime.deltaTime)

        local ok, err = pcall(function() lift.floorHeight = height end)
        if not ok then
            broken = true
            Debug.LogError("Elevator (sector " .. sector.id .. "): " .. tostring(err))
            return
        end

        if height == wanted then moving = false end
    end
end
```

**Notes**

- While `moving` is true, extra presses are ignored, so the player can't reverse mid-ride.
- Add a message when the elevator arrives by printing at the spot where `moving` is cleared.

---

## Crusher

**Attach to:** a sector with a ceiling well above its floor (a corridor trap).

The ceiling repeatedly slams down and rises again. Anything with a `Health` script standing under
it takes damage while it's crushing (see [10_health_and_damage.md](10_health_and_damage.md)).

```lua
-- Scripts/Platforms/Crusher.lua (sector script)
---@field crushedGap number @ Gap When Down
crushedGap = 8

---@field downSpeed number @ Down Speed
downSpeed = 120

---@field upSpeed number @ Up Speed
upSpeed = 30

---@field pauseTop number @ Pause At Top (s)
pauseTop = 1.5

---@field pauseBottom number @ Pause At Bottom (s)
pauseBottom = 0.5

---@field damage number @ Damage Per Hit
damage = 25

---@field hitInterval number @ Seconds Between Hits
hitInterval = 0.5

local MIN_GAP = 0.01
local GOING_DOWN, WAIT_BOTTOM, GOING_UP, WAIT_TOP = 1, 2, 3, 4

local crusher
local topCeiling, bottomCeiling
local state = WAIT_TOP
local timer = 0.0
local hitTimer = 0.0
local broken = false

local function MoveToward(current, target, maxDelta)
    if math.abs(target - current) <= maxDelta then return target end
    if target > current then return current + maxDelta end
    return current - maxDelta
end

local function HurtEveryoneInside()
    for i = 1, sector.entityCount do
        local entity = sector:GetEntity(i)

        if entity:HasScriptNamed("Health") then
            entity:GetScript("Health"):TakeDamage(damage)
        end
    end
end

function Start()
    crusher = sector:GetFloor(1)
    topCeiling = crusher.ceilingHeight
    bottomCeiling = crusher.floorHeight + math.max(crushedGap, MIN_GAP)
    timer = pauseTop
end

function Update()
    if broken then return end

    local dt = GameTime.deltaTime
    local height = crusher.ceilingHeight

    if state == WAIT_TOP then
        timer = timer - dt
        if timer <= 0 then state = GOING_DOWN end

    elseif state == GOING_DOWN then
        height = MoveToward(height, bottomCeiling, downSpeed * dt)
        if height == bottomCeiling then
            state = WAIT_BOTTOM
            timer = pauseBottom
        end

    elseif state == WAIT_BOTTOM then
        timer = timer - dt
        if timer <= 0 then state = GOING_UP end

    elseif state == GOING_UP then
        height = MoveToward(height, topCeiling, upSpeed * dt)
        if height == topCeiling then
            state = WAIT_TOP
            timer = pauseTop
        end
    end

    -- Crushing happens while the ceiling is down (or on its way).
    if state == GOING_DOWN or state == WAIT_BOTTOM then
        hitTimer = hitTimer - dt
        if hitTimer <= 0 then
            hitTimer = hitInterval
            HurtEveryoneInside()
        end
    end

    if height ~= crusher.ceilingHeight then
        local ok, err = pcall(function() crusher.ceilingHeight = height end)
        if not ok then
            broken = true
            Debug.LogError("Crusher (sector " .. sector.id .. "): " .. tostring(err))
        end
    end
end
```

**Notes**

- `entity:GetScript("Health")` returns a reference to that entity's script, and
  `:TakeDamage(damage)` calls the function of that name inside it.
- Damage lands only while the ceiling is descending or resting at the bottom. To damage only when
  the gap is smaller than the target's height, compare
  `crusher.ceilingHeight - crusher.floorHeight` against it before calling `HurtEveryoneInside`.

---

## Rising lava / flood

**Attach to:** the sector that floods. Its ceiling must be above `maxHeight`.

After a delay the floor rises slowly to a maximum height. Combine with the damage zone from
[10_health_and_damage.md](10_health_and_damage.md) on the same sector so the flood actually hurts.

```lua
-- Scripts/Platforms/RisingFloor.lua (sector script)
---@field startDelay number @ Start Delay (s)
startDelay = 10

---@field riseSpeed number @ Rise Speed (units/s)
riseSpeed = 2

---@field maxHeight number @ Max Height
maxHeight = 30

local MIN_GAP = 0.5

local flood
local timer = 0.0
local broken = false

function Start()
    flood = sector:GetFloor(1)
    timer = startDelay
    maxHeight = math.min(maxHeight, flood.ceilingHeight - MIN_GAP)
end

function Update()
    if broken then return end

    local dt = GameTime.deltaTime

    if timer > 0 then
        timer = timer - dt
        return
    end

    local height = flood.floorHeight
    if height >= maxHeight then return end

    height = math.min(height + riseSpeed * dt, maxHeight)

    local ok, err = pcall(function() flood.floorHeight = height end)
    if not ok then
        broken = true
        Debug.LogError("RisingFloor (sector " .. sector.id .. "): " .. tostring(err))
    end
end
```
