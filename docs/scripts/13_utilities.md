# 13 - Utilities and Patterns

The scripting environment has no coroutines, no `os.time`, and no `wait()`, so anything that
"happens later" is a variable that counts down in `Update`. These are the patterns that come up
again and again.

---

## Timers: run something later, or repeatedly

**Attach to:** any Entity (this one is a self-contained demo; copy the timer functions into
whichever script needs them).

`After(delay, fn)` runs `fn` once, `Every(interval, fn)` runs it forever, and `Cancel(id)` stops
a timer.

```lua
-- Scripts/Utils/TimerDemo.lua (entity script)
local timers = {}
local nextId = 1

local function Schedule(delay, callback, repeating)
    local id = nextId
    nextId = nextId + 1

    timers[#timers + 1] = {
        id = id,
        remaining = delay,
        interval = delay,
        callback = callback,
        repeating = repeating,
        cancelled = false,
    }

    return id
end

local function After(delay, callback) return Schedule(delay, callback, false) end
local function Every(interval, callback) return Schedule(interval, callback, true) end

local function Cancel(id)
    for _, timer in ipairs(timers) do
        if timer.id == id then timer.cancelled = true end
    end
end

local function Tick(dt)
    -- Walk backwards so removing an entry never skips one, and so timers that a callback
    -- schedules (they are appended at the end) wait until the next frame.
    for i = #timers, 1, -1 do
        local timer = timers[i]

        if timer.cancelled then
            table.remove(timers, i)
        else
            timer.remaining = timer.remaining - dt

            if timer.remaining <= 0 then
                if timer.repeating then
                    timer.remaining = timer.remaining + timer.interval
                else
                    table.remove(timers, i)
                end

                timer.callback()
            end
        end
    end
end

local ticks = 0
local ticker

function Start()
    After(2, function() Debug.Print("Two seconds have passed") end)

    ticker = Every(1, function()
        ticks = ticks + 1
        Debug.Print("tick " .. ticks)
    end)

    After(5.5, function()
        Cancel(ticker)
        Debug.Print("ticker stopped")
    end)
end

function Update()
    Tick(GameTime.deltaTime)
end
```

**Notes**

- For a one-off delay inside a state machine you don't need all this. A single `local timer = 2`
  that you count down in `Update` is simpler.
- `Every` drifts less than resetting to `interval` because it *adds* `interval`, which keeps any
  leftover time from the previous tick.

---

## Easing and tweening

**Attach to:** any prop that should glide back and forth.

Easing functions take progress `t` in `0..1` and return a reshaped `0..1`. Feed the result into
`mathT.Lerp` to move things with more character than a straight line.

```lua
-- Scripts/Utils/TweenMover.lua (entity script)
---@field offset Vector3 @ Move By
offset = Vector3(0, 0, 40)

---@field duration number @ One-way Duration (s)
duration = 1.5

---@field easing enum(Linear,SmoothStep,EaseIn,EaseOut,Bounce) @ Easing
easing = 1

local function Linear(t) return t end
local function SmoothStep(t) return t * t * (3 - 2 * t) end
local function EaseIn(t) return t * t end
local function EaseOut(t) return 1 - (1 - t) * (1 - t) end

local function Bounce(t)
    local n, d = 7.5625, 2.75

    if t < 1 / d then
        return n * t * t
    elseif t < 2 / d then
        t = t - 1.5 / d
        return n * t * t + 0.75
    elseif t < 2.5 / d then
        t = t - 2.25 / d
        return n * t * t + 0.9375
    end

    t = t - 2.625 / d
    return n * t * t + 0.984375
end

-- Indexed by the enum's number + 1, in the same order as the annotation above.
local EASINGS = { Linear, SmoothStep, EaseIn, EaseOut, Bounce }

local transform
local start
local clock = 0.0

function Start()
    transform = entity.transform
    start = transform.position
end

function Update()
    clock = clock + GameTime.deltaTime

    -- Ping-pong: 0 -> 1 -> 0 -> 1 ...
    local cycle = (clock / duration) % 2
    local t = cycle <= 1 and cycle or 2 - cycle

    local eased = EASINGS[easing + 1](t)

    transform.position = Vector3(
        mathT.Lerp(start.x, start.x + offset.x, eased),
        mathT.Lerp(start.y, start.y + offset.y, eased),
        mathT.Lerp(start.z, start.z + offset.z, eased)
    )
end
```

**Notes**

- A moving platform *sector* would use the same idea with `sector:GetFloor(1).floorHeight` as the
  value being eased. See [06_lifts_and_platforms.md](06_lifts_and_platforms.md).

---

## Event bus

**Attach to:** any Entity that always exists (a "Managers" object). One is enough. A second
copy quietly reuses the first one's bus.

Lets scripts talk **without knowing about each other**: one side calls `Emit("coinCollected", 5)`,
any number of other scripts are told. The bus lives in the shared `Scripts` table.

```lua
-- Scripts/Utils/EventBus.lua (entity script)

-- The bus is built when the script is loaded (before any script's Start), so every Start()
-- can already use Scripts.Events. Top-level code of *other* scripts can't rely on it, since
-- load order isn't guaranteed.
local bus = Scripts.Events

if bus == nil then
    bus = { listeners = {}, nextHandle = 1 }

    function bus.Subscribe(eventName, callback)
        local list = bus.listeners[eventName]
        if list == nil then
            list = {}
            bus.listeners[eventName] = list
        end

        local handle = bus.nextHandle
        bus.nextHandle = handle + 1
        list[handle] = callback

        return handle
    end

    function bus.Unsubscribe(eventName, handle)
        local list = bus.listeners[eventName]
        if list ~= nil then list[handle] = nil end
    end

    function bus.Emit(eventName, ...)
        local list = bus.listeners[eventName]
        if list == nil then return end

        -- Copy first: a listener may subscribe or unsubscribe while we're calling them.
        local snapshot = {}
        for _, callback in pairs(list) do snapshot[#snapshot + 1] = callback end

        for _, callback in ipairs(snapshot) do
            local ok, err = pcall(callback, ...)
            if not ok then Debug.LogError("Event '" .. eventName .. "' listener failed: " .. tostring(err)) end
        end
    end

    Scripts.Events = bus
end

-- Demo: listen for "greeting" and send one.
local handle

function Start()
    handle = bus.Subscribe("greeting", function(who)
        Debug.Print("Heard a greeting from " .. who)
    end)

    bus.Emit("greeting", entity.name)
end

function OnDestroy()
    bus.Unsubscribe("greeting", handle)   -- always unsubscribe when the listener goes away
end
```

**Using it from another script**

```lua
-- Any other script, from Start() onward:
local handle

function Start()
    handle = Scripts.Events.Subscribe("coinCollected", function(amount)
        Debug.Print("Collected " .. amount .. " coins")
    end)
end

function OnDestroy()
    Scripts.Events.Unsubscribe("coinCollected", handle)
end

-- ...and wherever the coin is picked up:
Scripts.Events.Emit("coinCollected", 5)
```

**Notes**

- Unsubscribe in `OnDestroy`. A listener left behind keeps its script's variables alive and still
  gets called after that script's Entity is gone.
- Each listener is wrapped in `pcall`, so one broken listener can't stop the others from running.

---

## Respawner

**Attach to:** a *separate* manager Entity. A disabled Entity stops running its own
scripts, so the script that revives something can't be on the thing itself.

Watches an Entity that was disabled (for example `Health` with **Destroy On Death** off) and
brings it back after a delay at its original spot.

```lua
-- Scripts/Utils/Respawner.lua (entity script)
---@field target Entity @ Target
target = nil

---@field respawnDelay number @ Respawn Delay (s)
respawnDelay = 5

local spawnPosition
local timer = 0.0
local waiting = false

local function Respawn()
    target.transform.position = spawnPosition

    -- Stop any leftover movement.
    local rb = target.rigidbody
    if rb ~= nil then rb.velocity = Vector3(0, 0, 0) end

    -- Give it full health again (see Health.Revive in 10_health_and_damage.md).
    local health = target:GetScript("Health")
    if health.isValid then health:Revive() end

    target.enabled = true
    waiting = false
end

function Start()
    if target == nil then
        Debug.LogWarning("Respawner has no target assigned")
        return
    end

    spawnPosition = target.transform.position
end

function Update()
    if target == nil or not target.isValid then return end

    if target.enabled then
        waiting = false
        return
    end

    if not waiting then
        waiting = true
        timer = respawnDelay
    end

    timer = timer - GameTime.deltaTime
    if timer <= 0 then Respawn() end
end
```

---

## Small helpers worth copying

A tour of functions that every game ends up needing. The script prints what each returns, so you
can try them.

```lua
-- Scripts/Utils/HelperTour.lua (entity script)

-- Moves `current` toward `target` by at most `maxDelta`, never overshooting.
local function MoveToward(current, target, maxDelta)
    if math.abs(target - current) <= maxDelta then return target end
    if target > current then return current + maxDelta end
    return current - maxDelta
end

-- Distance between two entities on the ground plane (ignores height).
local function DistanceXZ(a, b)
    local pa, pb = a.transform.position, b.transform.position
    local dx, dz = pb.x - pa.x, pb.z - pa.z
    return math.sqrt(dx * dx + dz * dz)
end

-- Wraps an angle in degrees into -180..180.
local function WrapAngle(degrees)
    return (degrees + 180) % 360 - 180
end

-- Rounds to the nearest integer (or to `places` decimals).
local function Round(value, places)
    local scale = 10 ^ (places or 0)
    return math.floor(value * scale + 0.5) / scale
end

-- Picks a key from a table of { name = weight }.
local function WeightedPick(weights)
    local total = 0
    for _, weight in pairs(weights) do total = total + weight end

    local roll = mathT.RandomF(0, total)

    for name, weight in pairs(weights) do
        roll = roll - weight
        if roll <= 0 then return name end
    end
end

function Start()
    Debug.Print("MoveToward(0, 10, 3) =", MoveToward(0, 10, 3))
    Debug.Print("WrapAngle(270) =", WrapAngle(270))
    Debug.Print("Round(3.14159, 2) =", Round(3.14159, 2))
    Debug.Print("Distance to self =", DistanceXZ(entity, entity))
    Debug.Print("Dice roll (1-6) =", mathT.Random(1, 6))
    Debug.Print("Loot drop =", WeightedPick({ common = 70, rare = 25, legendary = 5 }))
end
```

**Notes**

- `mathT.Random(a, b)` is inclusive on both ends. `mathT.Random(n)` gives `0` to `n-1`.
  `mathT.RandomF()` gives `0..1`, `RandomF(max)` gives `0..max`, and `RandomF(min, max)` a range.
  `mathT.RandomFast()` is a cheaper, lower-quality `0..1` that is fine for visual effects.
- Lua's standard `math` library is available too, including `math.sin`, `math.cos`, `math.atan`
  (with two arguments), `math.sqrt`, `math.abs`, `math.floor`, `math.min`, `math.max`,
  `math.pi`, and `math.huge`.
