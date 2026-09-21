# 07 - Lighting

Every sector has a `light` property: a `Vector3` of red, green, blue in the range **0-255**
(`255, 255, 255` is full white, `0, 0, 0` is dark). It returns a copy, so to change it, build a new
`Vector3` and assign it back.

```lua
local base = sector.light                     -- copy
sector.light = Vector3(base.x * 0.5, base.y * 0.5, base.z * 0.5)   -- half brightness
```

All scripts on this page are **sector scripts**.

---

## Flicker

**Attach to:** a sector with a broken light.

Holds a random brightness for a random short time, then picks another.

```lua
-- Scripts/Lighting/Flicker.lua (sector script)
---@field minBrightness number @ Min Brightness (0-1)
minBrightness = 0.3

---@field maxBrightness number @ Max Brightness (0-1)
maxBrightness = 1.0

---@field minInterval number @ Min Hold Time (s)
minInterval = 0.03

---@field maxInterval number @ Max Hold Time (s)
maxInterval = 0.15

local base
local timer = 0.0

function Start()
    base = sector.light   -- remember the light the designer painted
end

function Update()
    timer = timer - GameTime.deltaTime
    if timer > 0 then return end

    timer = mathT.RandomF(minInterval, maxInterval)
    local k = mathT.RandomF(minBrightness, maxBrightness)
    sector.light = Vector3(base.x * k, base.y * k, base.z * k)
end

function OnDisable()
    if base ~= nil then sector.light = base end   -- restore
end
```

---

## Smooth pulse

**Attach to:** a sector with a glowing thing: a reactor room, a heartbeat.

```lua
-- Scripts/Lighting/Pulse.lua (sector script)
---@field period number @ Period (s)
period = 2.0

---@field minBrightness number @ Min Brightness (0-1)
minBrightness = 0.35

---@field maxBrightness number @ Max Brightness (0-1)
maxBrightness = 1.0

local base
local clock = 0.0

function Start()
    base = sector.light
end

function Update()
    clock = clock + GameTime.deltaTime

    -- Sine wave remapped from -1..1 to 0..1, then to minBrightness..maxBrightness.
    local wave = 0.5 + 0.5 * math.sin(clock / period * 2 * math.pi)
    local k = mathT.Lerp(minBrightness, maxBrightness, wave)

    sector.light = Vector3(base.x * k, base.y * k, base.z * k)
end
```

---

## Alarm strobe

**Attach to:** a sector that should flash red while an alarm is on.

The alarm is a channel in the shared `Scripts` table (the same trick the doors use), so any
script can trigger it: `Scripts.channels = Scripts.channels or {}` then
`Scripts.channels["alarm"] = true`.

```lua
-- Scripts/Lighting/AlarmStrobe.lua (sector script)
---@field channel string @ Channel
channel = "alarm"

---@field flashesPerSecond number @ Flashes Per Second
flashesPerSecond = 2

---@field alarmColor Vector3 @ Alarm Color
alarmColor = Vector3(255, 0, 0)

local base
local clock = 0.0
local wasActive = false

function Start()
    base = sector.light
end

function Update()
    local active = Scripts.channels ~= nil and Scripts.channels[channel] == true

    if not active then
        if wasActive then
            sector.light = base   -- alarm just ended: restore the normal light
            wasActive = false
        end
        return
    end

    if not wasActive then
        wasActive = true
        clock = 0.0   -- restart the flash pattern each time the alarm begins
    end

    clock = clock + GameTime.deltaTime

    -- Two half-cycles per flash: even = alarm color, odd = dark.
    local lit = math.floor(clock * flashesPerSecond * 2) % 2 == 0

    if lit then
        sector.light = alarmColor
    else
        sector.light = Vector3(base.x * 0.15, base.y * 0.15, base.z * 0.15)
    end
end
```

**Notes**

- Every strobe sector restarts its clock on the frame the alarm switches on, so sectors that share
  a channel flash in sync without needing a shared timer.
- A `Vector3` public field (`alarmColor`) can be assigned straight to `sector.light`.

---

## Day / night cycle

**Attach to:** an outdoor sector (or one script per outdoor sector).

Blends through a list of colors over a day.

```lua
-- Scripts/Lighting/DayNight.lua (sector script)
---@field dayLength number @ Day Length (s)
dayLength = 120

---@field startTime number @ Start Time (0-1, 0.5 = noon)
startTime = 0.5

-- One entry per key moment of the day, evenly spaced from midnight (0) around to midnight (1).
local KEYFRAMES = {
    {40, 50, 90},      -- midnight
    {255, 170, 110},   -- dawn
    {255, 255, 255},   -- noon
    {255, 130, 80},    -- dusk
    {40, 50, 90},      -- midnight again (so it loops)
}

local clock = 0.0

local function SampleDay(t)
    local segments = #KEYFRAMES - 1
    local scaled = (t % 1) * segments
    local index = math.floor(scaled)
    local blend = scaled - index

    local a = KEYFRAMES[index + 1]
    local b = KEYFRAMES[index + 2]

    return Vector3(
        mathT.Lerp(a[1], b[1], blend),
        mathT.Lerp(a[2], b[2], blend),
        mathT.Lerp(a[3], b[3], blend)
    )
end

function Start()
    clock = startTime
end

function Update()
    clock = (clock + GameTime.deltaTime / dayLength) % 1
    sector.light = SampleDay(clock)
end
```

**Notes**

- `%` is Lua's modulo, so `(t % 1)` keeps the time inside `0..1`.
- The keyframe table is plain Lua data. Add more entries to add more moods, and the math adapts
  because it uses `#KEYFRAMES`.

---

## Light switch

**Attach to:** the sector whose lights you want to toggle.

Press a key while standing in the sector to toggle its lights.

```lua
-- Scripts/Lighting/LightSwitch.lua (sector script)
---@field player GameObject @ Player
player = nil

---@field toggleKey string @ Toggle Key
toggleKey = "F"

---@field offBrightness number @ Brightness When Off (0-1)
offBrightness = 0.1

local base
local lightsOn = true

local function PlayerIsInside()
    for i = 1, sector.entityCount do
        if sector:GetEntity(i).id == player.id then return true end
    end
    return false
end

function Start()
    base = sector.light
end

function Update()
    if player == nil or not Input.GetKeyDown(toggleKey) or not PlayerIsInside() then return end

    lightsOn = not lightsOn

    if lightsOn then
        sector.light = base
    else
        local k = offBrightness
        sector.light = Vector3(base.x * k, base.y * k, base.z * k)
    end
end
```
