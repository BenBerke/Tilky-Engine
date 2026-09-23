# 05 - Doors

A door is just a **sector whose ceiling moves**. Build a small sector in the doorway, give it a
floor at the normal floor height and a ceiling at (or just above) the floor, and attach one of
these **sector scripts** to it. Raising `ceilingHeight` opens the door, lowering it closes it.

All of these scripts share the same building blocks:

```lua
local door = sector:GetFloor(1)         -- the sector's first floor/ceiling interval
local current = door.ceilingHeight      -- read the current ceiling
door.ceilingHeight = current + 1        -- move it (throws if it would go below the floor)
```

Things to know:

- The scripts always treat "closed" as floor + `MIN_GAP`, so the door starts closing itself
  in play mode even if the sector was drawn with a tall ceiling. Keep `openHeight` above that.
- A ceiling must stay **above** the floor, so a closed door rests `MIN_GAP` (0.01) above the
  floor. It can't be exactly zero.
- With several floor intervals in one sector, the ceiling also must not rise past the next
  interval's floor. That is why every script writes the ceiling through `pcall`: an invalid value
  is reported once and the door stops, instead of erroring every frame.
- Scripts with a **player** field need the player assigned in the inspector. There is no "find the
  player" call.
- The distance helper below works on the sector's outline, so it works for a door of any shape.
  It returns `0` while the player is inside the door sector.

---

## Automatic door

**Attach to:** the door sector.

Opens when the player comes near, closes when they leave.

```lua
-- Scripts/Doors/AutoDoor.lua (sector script)
---@field player Entity @ Player
player = nil

---@field openHeight number @ Open Height (above the floor)
openHeight = 40

---@field speed number @ Open/Close Speed
speed = 60

---@field triggerDistance number @ Trigger Distance
triggerDistance = 30

local MIN_GAP = 0.01

local door
local closedCeiling, openCeiling
local broken = false

local function MoveToward(current, target, maxDelta)
    if math.abs(target - current) <= maxDelta then return target end
    if target > current then return current + maxDelta end
    return current - maxDelta
end

-- Distance from (px, pz) to the sector's outline; 0 when the point is inside.
local function DistanceToSector(px, pz)
    local n = sector.vertexCount
    local best = math.huge
    local inside = false
    local prev = sector:GetVertex(n)

    for i = 1, n do
        local cur = sector:GetVertex(i)

        -- Closest point on the edge prev -> cur.
        local ex, ey = cur.x - prev.x, cur.y - prev.y
        local lenSq = ex * ex + ey * ey
        local t = 0
        if lenSq > 0 then
            t = ((px - prev.x) * ex + (pz - prev.y) * ey) / lenSq
            t = math.max(0, math.min(1, t))
        end
        local dx, dz = px - (prev.x + ex * t), pz - (prev.y + ey * t)
        best = math.min(best, math.sqrt(dx * dx + dz * dz))

        -- Even-odd test for "is the point inside the polygon".
        if (cur.y > pz) ~= (prev.y > pz)
            and px < (prev.x - cur.x) * (pz - cur.y) / (prev.y - cur.y) + cur.x then
            inside = not inside
        end

        prev = cur
    end

    if inside then return 0 end
    return best
end

local function SetCeiling(height)
    height = math.max(height, door.floorHeight + MIN_GAP)

    local ok, err = pcall(function() door.ceilingHeight = height end)
    if not ok then
        broken = true
        Debug.LogError("AutoDoor (sector " .. sector.id .. "): " .. tostring(err))
    end
end

function Start()
    if player == nil then
        Debug.LogWarning("AutoDoor in sector " .. sector.id .. " has no player assigned")
    end

    door = sector:GetFloor(1)
    -- Closed is always "just above the floor", whatever ceiling the sector was authored with.
    -- (Using the authored ceiling broke doors built tall: "open" ended up lower than "closed".)
    closedCeiling = door.floorHeight + MIN_GAP
    openCeiling = door.floorHeight + openHeight
end

function Update()
    if broken or player == nil or not player.isValid then return end

    local p = player.transform.position
    local wanted = closedCeiling
    if DistanceToSector(p.x, p.z) <= triggerDistance then wanted = openCeiling end

    local current = door.ceilingHeight
    if current ~= wanted then
        SetCeiling(MoveToward(current, wanted, speed * GameTime.deltaTime))
    end
end
```

**Notes**

- `openHeight` is measured above the floor, so the script keeps working if you later move the
  door sector's floor in the editor.
- The door doesn't close on the player because the distance is `0` while they're inside it.

---

## Use-key door (with optional key and auto-close)

**Attach to:** the door sector.

Press a key near the door to open or close it. Leave `requiredKey` empty for an unlocked door, or
give it a name like `red` to lock it until a matching key pickup
([11_pickups.md](11_pickups.md)) has been collected.

```lua
-- Scripts/Doors/UseDoor.lua (sector script)
---@field player Entity @ Player
player = nil

---@field openHeight number @ Open Height (above the floor)
openHeight = 40

---@field speed number @ Open/Close Speed
speed = 60

---@field useDistance number @ Use Distance
useDistance = 28

---@field useKey string @ Use Key
useKey = "E"

---@field requiredKey string @ Required Key (empty = unlocked)
requiredKey = ""

---@field autoCloseDelay number @ Auto-close After (s, 0 = never)
autoCloseDelay = 4

local MIN_GAP = 0.01

local door
local closedCeiling, openCeiling
local isOpen = false
local openTimer = 0.0
local broken = false

local function MoveToward(current, target, maxDelta)
    if math.abs(target - current) <= maxDelta then return target end
    if target > current then return current + maxDelta end
    return current - maxDelta
end

local function DistanceToSector(px, pz)
    local n = sector.vertexCount
    local best = math.huge
    local inside = false
    local prev = sector:GetVertex(n)

    for i = 1, n do
        local cur = sector:GetVertex(i)
        local ex, ey = cur.x - prev.x, cur.y - prev.y
        local lenSq = ex * ex + ey * ey
        local t = 0
        if lenSq > 0 then
            t = ((px - prev.x) * ex + (pz - prev.y) * ey) / lenSq
            t = math.max(0, math.min(1, t))
        end
        local dx, dz = px - (prev.x + ex * t), pz - (prev.y + ey * t)
        best = math.min(best, math.sqrt(dx * dx + dz * dz))

        if (cur.y > pz) ~= (prev.y > pz)
            and px < (prev.x - cur.x) * (pz - cur.y) / (prev.y - cur.y) + cur.x then
            inside = not inside
        end

        prev = cur
    end

    if inside then return 0 end
    return best
end

-- Keys are collected into the shared Scripts table by the Pickup script.
local function HasKey()
    if requiredKey == "" then return true end
    return Scripts.keys ~= nil and Scripts.keys[requiredKey] == true
end

local function SetCeiling(height)
    height = math.max(height, door.floorHeight + MIN_GAP)

    local ok, err = pcall(function() door.ceilingHeight = height end)
    if not ok then
        broken = true
        Debug.LogError("UseDoor (sector " .. sector.id .. "): " .. tostring(err))
    end
end

function Start()
    door = sector:GetFloor(1)
    -- Closed is always "just above the floor", whatever ceiling the sector was authored with.
    -- (Using the authored ceiling broke doors built tall: "open" ended up lower than "closed".)
    closedCeiling = door.floorHeight + MIN_GAP
    openCeiling = door.floorHeight + openHeight
end

function Update()
    if broken or player == nil or not player.isValid then return end

    local dt = GameTime.deltaTime
    local p = player.transform.position
    local distance = DistanceToSector(p.x, p.z)

    if Input.GetKeyDown(useKey) and distance <= useDistance then
        if isOpen then
            isOpen = false
        elseif HasKey() then
            isOpen = true
            openTimer = autoCloseDelay
        else
            Debug.Print("This door needs the " .. requiredKey .. " key")
        end
    end

    -- Close by itself, but never while the player is standing in the doorway.
    if isOpen and autoCloseDelay > 0 and distance > 0 then
        openTimer = openTimer - dt
        if openTimer <= 0 then isOpen = false end
    end

    local wanted = isOpen and openCeiling or closedCeiling
    local current = door.ceilingHeight
    if current ~= wanted then
        SetCeiling(MoveToward(current, wanted, speed * dt))
    end
end
```

---

## Switch and channel door

Two scripts that talk through the shared `Scripts` table. A **switch** flips a named *channel*,
and any **channel door** listening on that channel follows it. Several doors can share a channel,
and one switch can drive them all.

### The switch

**Attach to:** an Entity near a wall (a lever sprite, a button).

```lua
-- Scripts/Doors/Switch.lua (entity script)
---@field player Entity @ Player
player = nil

---@field channel string @ Channel
channel = "door1"

---@field useDistance number @ Use Distance
useDistance = 24

---@field useKey string @ Use Key
useKey = "E"

function Start()
    Scripts.channels = Scripts.channels or {}
end

function Update()
    if player == nil or not player.isValid then return end
    if not Input.GetKeyDown(useKey) then return end

    local p = player.transform.position
    local s = entity.transform.position
    local dx, dz = p.x - s.x, p.z - s.z

    if math.sqrt(dx * dx + dz * dz) <= useDistance then
        Scripts.channels[channel] = not Scripts.channels[channel]
        Debug.Print("Switch '" .. channel .. "' is now " .. (Scripts.channels[channel] and "ON" or "OFF"))
    end
end
```

### The door

**Attach to:** the door sector.

```lua
-- Scripts/Doors/ChannelDoor.lua (sector script)
---@field channel string @ Channel
channel = "door1"

---@field openHeight number @ Open Height (above the floor)
openHeight = 40

---@field speed number @ Open/Close Speed
speed = 60

local MIN_GAP = 0.01

local door
local closedCeiling, openCeiling
local broken = false

local function MoveToward(current, target, maxDelta)
    if math.abs(target - current) <= maxDelta then return target end
    if target > current then return current + maxDelta end
    return current - maxDelta
end

function Start()
    door = sector:GetFloor(1)
    -- Closed is always "just above the floor", whatever ceiling the sector was authored with.
    -- (Using the authored ceiling broke doors built tall: "open" ended up lower than "closed".)
    closedCeiling = door.floorHeight + MIN_GAP
    openCeiling = door.floorHeight + openHeight
end

function Update()
    if broken then return end

    local isOn = Scripts.channels ~= nil and Scripts.channels[channel] == true
    local wanted = isOn and openCeiling or closedCeiling
    local current = door.ceilingHeight

    if current ~= wanted then
        local height = math.max(MoveToward(current, wanted, speed * GameTime.deltaTime), door.floorHeight + MIN_GAP)

        local ok, err = pcall(function() door.ceilingHeight = height end)
        if not ok then
            broken = true
            Debug.LogError("ChannelDoor (sector " .. sector.id .. "): " .. tostring(err))
        end
    end
end
```

**Notes**

- `Scripts` is shared by every script and cleared each time the level starts, so channels always
  begin "off".
- The switch and door never reference each other. Any script can flip a channel:
  [17_level_flow.md](17_level_flow.md) uses one to open an exit once enough enemies are dead.
- `Scripts.channels[channel] = not Scripts.channels[channel]` works even the first time, because
  a missing key is `nil` and `not nil` is `true`.
