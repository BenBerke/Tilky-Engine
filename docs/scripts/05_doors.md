# 05 - Doors

A door is just a **sector whose ceiling moves**. Build a small sector in the doorway, give it a
floor at the normal floor height and a ceiling at (or just above) the floor, and attach one of
these **sector scripts** to it.

All of these scripts are built on one call, the sector's built-in ceiling mover
([06_lifts_and_platforms.md](06_lifts_and_platforms.md) covers the whole family):

```lua
sector:MoveCeilingToFloor(1, speed, openHeight)  -- open: ceiling heads to `openHeight` above the floor
sector:MoveCeilingToFloor(1, speed)              -- close: ceiling heads down to the floor
```

The engine runs the move by itself every frame, so a script only starts it when the door should
change state. Starting a new move replaces the old one, so a door can reverse halfway.

Things to know:

- The `gap` (third argument) is measured from the floor, and the ceiling moves *away* from the
  floor if it is closer than `gap`. That is why the same call both opens a closed door and closes
  an open one.
- The scripts close the door in `Start`, so it closes itself in play mode even if the sector was
  drawn with a tall ceiling.
- A ceiling can never touch its floor, so a closed door rests 0.01 above the floor.
- With several floor intervals in one sector, the engine stops the ceiling at the next interval's
  floor instead of erroring.
- A `speed` of 0 or less raises an error.
- Scripts with a **player** field need the player assigned in the inspector. You could look it up
  with `Game.FindEntity("Player")` in `Start` instead, but then renaming the entity breaks it.
- `sector:DistanceToSector(player)` measures to the sector's outline, so it works for a door of
  any shape. It returns `0` while the player is inside the door sector.

---

## Automatic door

**Attach to:** the door sector.

Opens when the player comes near, closes when they leave.

```lua
-- Scripts/Doors/AutoDoor.lua (sector script)
public Entity player = nil
public number openHeight = 40
public number speed = 60
public number triggerDistance = 30

local isOpen = false

-- Starts the ceiling moving; the engine finishes the move on its own.
local function SetOpen(open)
    isOpen = open
    sector:MoveCeilingToFloor(1, speed, open and openHeight or 0)
end

function Start()
    if player == nil then
        Debug.LogWarning("AutoDoor in sector " .. sector.id .. " has no player assigned")
    end

    -- Close whatever ceiling height the sector was authored with.
    SetOpen(false)
end

function Update()
    if player == nil or not player.isValid then return end

    local near = sector:DistanceToSectorSquared(player) <= triggerDistance * triggerDistance
    if near ~= isOpen then SetOpen(near) end
end
```

**Notes**

- `openHeight` is measured above the floor, so the script keeps working if you later move the
  door sector's floor in the editor.
- The move is only started when `near` changes, not every frame.
- The door doesn't close on the player because the distance is `0` while they're inside it.

---

## Use-key door (with optional key and auto-close)

**Attach to:** the door sector.

Press a key near the door to open or close it. Leave `requiredKey` empty for an unlocked door, or
give it a name like `red` to lock it until a matching key pickup
([11_pickups.md](11_pickups.md)) has been collected.

```lua
-- Scripts/Doors/UseDoor.lua (sector script)
public Entity player = nil
public number openHeight = 40
public number speed = 60
public number useDistance = 28
public Key useKey = Key.E
public string requiredKey = ""
public number autoCloseDelay = 4

local isOpen = false
local openTimer = 0.0

-- Keys are collected into the shared Global table by the Pickup script.
local function HasKey()
    if requiredKey == "" then return true end
    return Global.keys ~= nil and Global.keys[requiredKey] == true
end

-- Starts the ceiling moving; the engine finishes the move on its own.
local function SetOpen(open)
    isOpen = open
    sector:MoveCeilingToFloor(1, speed, open and openHeight or 0)
end

function Start()
    -- Close whatever ceiling height the sector was authored with.
    SetOpen(false)
end

function Update()
    if player == nil or not player.isValid then return end

    local distance = sector:DistanceToSector(player)

    if Input.GetKeyDown(useKey) and distance <= useDistance then
        if isOpen then
            SetOpen(false)
        elseif HasKey() then
            SetOpen(true)
            openTimer = autoCloseDelay
        else
            Debug.Print("This door needs the " .. requiredKey .. " key")
        end
    end

    -- Close by itself, but never while the player is standing in the doorway.
    if isOpen and autoCloseDelay > 0 and distance > 0 then
        openTimer = openTimer - GameTime.deltaTime
        if openTimer <= 0 then SetOpen(false) end
    end
end
```

---

## Switch and channel door

Two scripts that talk through the shared `Global` table. A **switch** flips a named *channel*,
and any **channel door** listening on that channel follows it. Several doors can share a channel,
and one switch can drive them all.

### The switch

**Attach to:** an Entity near a wall (a lever sprite, a button).

```lua
-- Scripts/Doors/Switch.lua (entity script)
public Entity player = nil
public string channel = "door1"
public number useDistance = 24
public Key useKey = Key.E

function Start()
    Global.channels = Global.channels or {}
end

function Update()
    if player == nil or not player.isValid then return end
    if not Input.GetKeyDown(useKey) then return end

    local p = player.transform.position
    local s = entity.transform.position
    local dx, dz = p.x - s.x, p.z - s.z

    if math.sqrt(dx * dx + dz * dz) <= useDistance then
        Global.channels[channel] = not Global.channels[channel]
        Debug.Print("Switch '" .. channel .. "' is now " .. (Global.channels[channel] and "ON" or "OFF"))
    end
end
```

### The door

**Attach to:** the door sector.

```lua
-- Scripts/Doors/ChannelDoor.lua (sector script)
public string channel = "door1"
public number openHeight = 40
public number speed = 60

local isOpen = false

-- Starts the ceiling moving; the engine finishes the move on its own.
local function SetOpen(open)
    isOpen = open
    sector:MoveCeilingToFloor(1, speed, open and openHeight or 0)
end

function Start()
    -- Close whatever ceiling height the sector was authored with.
    SetOpen(false)
end

function Update()
    local isOn = Global.channels ~= nil and Global.channels[channel] == true
    if isOn ~= isOpen then SetOpen(isOn) end
end
```

**Notes**

- `Global` is shared by every script and starts empty each time the game starts, so channels always
  begin "off".
- The switch and door never reference each other. Any script can flip a channel:
  [17_level_flow.md](17_level_flow.md) uses one to open an exit once enough enemies are dead.
- `Global.channels[channel] = not Global.channels[channel]` works even the first time, because
  a missing key is `nil` and `not nil` is `true`.
