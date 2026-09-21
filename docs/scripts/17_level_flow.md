# 17 - Level Flow

Scripts that tie a level together: getting from A to B, saving progress, and unlocking the exit.
They communicate through the shared `Scripts` table, as described in
[05_doors.md](05_doors.md#switch-and-channel-door).

---

## Teleporter

**Attach to:** a pad GameObject. Set `destination` to another GameObject, either a plain marker or
another teleporter for a two-way pair.

```lua
-- Scripts/Flow/Teleporter.lua (entity script)
---@field player GameObject @ Player
player = nil

---@field destination GameObject @ Destination
destination = nil

---@field radius number @ Trigger Radius
radius = 16

local function Teleport()
    -- Positions are measured at the feet, so the player lands where the destination stands.
    local d = destination.transform.position
    player.transform.position = Vector3(d.x, d.y, d.z)

    local rb = player.rigidbody
    if rb ~= nil then rb.velocity = Vector3(0, 0, 0) end

    -- Tell the pad we arrived at not to send us straight back.
    Scripts.teleportArrival = destination.id
end

function Update()
    if player == nil or destination == nil or not player.isValid or not destination.isValid then return end

    local p = player.transform.position
    local me = gameObject.transform.position
    local dx, dz = p.x - me.x, p.z - me.z
    local inside = math.sqrt(dx * dx + dz * dz) <= radius

    -- We are the pad the player just arrived at: stay quiet until they step off it.
    if Scripts.teleportArrival == gameObject.id then
        if not inside then Scripts.teleportArrival = nil end
        return
    end

    if inside then Teleport() end
end
```

**Notes**

- Without the `teleportArrival` handshake, two linked pads would bounce the player between them
  forever.
- The destination's `y` is used as-is, so put the marker on the floor you want the player to
  arrive on.

---

## Checkpoints and a kill plane

Two scripts. Touching a **checkpoint** remembers where the player was. The **kill plane** puts the
player back at the last checkpoint (or where they started) if they fall out of the world.

### Checkpoint

**Attach to:** a marker GameObject.

```lua
-- Scripts/Flow/Checkpoint.lua (entity script)
---@field player GameObject @ Player
player = nil

---@field radius number @ Trigger Radius
radius = 20

local reached = false

function Update()
    if reached or player == nil or not player.isValid then return end

    local p = player.transform.position
    local me = gameObject.transform.position
    local dx, dz = p.x - me.x, p.z - me.z

    if math.sqrt(dx * dx + dz * dz) <= radius then
        reached = true
        Scripts.checkpoint = p   -- the player's own position, so respawn heights are consistent
        Debug.Print("Checkpoint reached")
    end
end
```

### Kill plane

**Attach to:** the player.

```lua
-- Scripts/Flow/KillPlane.lua (entity script)
---@field killHeight number @ Kill Below Height
killHeight = -200

local transform, rb
local startPosition

function Start()
    transform = gameObject.transform
    rb = gameObject.rigidbody
    startPosition = transform.position
end

function Update()
    if transform.position.y >= killHeight then return end

    transform.position = Scripts.checkpoint or startPosition
    if rb ~= nil then rb.velocity = Vector3(0, 0, 0) end

    Debug.Print("You fell out of the world")
end
```

---

## Kill counter that unlocks the exit

Enemies report their deaths to a counter, and the counter turns on a channel when enough have
fallen. Any [ChannelDoor](05_doors.md#switch-and-channel-door) on that channel then opens.

### The counter

**Attach to:** a manager GameObject.

```lua
-- Scripts/Flow/KillCounter.lua (entity script)
---@field killsRequired int @ Kills Required
killsRequired = 3

---@field channel string @ Channel To Turn On
channel = "exit"

-- Called by an enemy's Health script (its "Death Listener" field points at this script).
function OnDeath(self, who)
    Scripts.kills = (Scripts.kills or 0) + 1
    Debug.Print(who.name .. " down (" .. Scripts.kills .. "/" .. killsRequired .. ")")

    if Scripts.kills >= killsRequired then
        Scripts.channels = Scripts.channels or {}
        Scripts.channels[channel] = true
        Debug.Print("The way out is open")
    end
end
```

**Setup:** on every enemy's `Health` script, drag the manager's `KillCounter` script into the
**Death Listener** field ([10_health_and_damage.md](10_health_and_damage.md)).

---

## Level exit

**Attach to:** an exit marker GameObject.

Loads another level when the player reaches it. It can also require a channel to be on first, for
example the `exit` channel the kill counter above turns on.

> **Experimental:** `Game.LoadLevel` swaps the running level's data, and the engine's own code
> carries a TODO about verifying this in a running game. Test it in your build before relying on
> it. The script guards against calling it twice.

```lua
-- Scripts/Flow/LevelExit.lua (entity script)
---@field player GameObject @ Player
player = nil

---@field nextLevel string @ Next Level Name
nextLevel = ""

---@field requiredChannel string @ Required Channel (empty = none)
requiredChannel = ""

---@field radius number @ Trigger Radius
radius = 20

local loading = false

function Update()
    if loading or player == nil or not player.isValid or nextLevel == "" then return end

    if requiredChannel ~= "" then
        local unlocked = Scripts.channels ~= nil and Scripts.channels[requiredChannel] == true
        if not unlocked then return end
    end

    local p = player.transform.position
    local me = gameObject.transform.position
    local dx, dz = p.x - me.x, p.z - me.z

    if math.sqrt(dx * dx + dz * dz) <= radius then
        loading = true
        Debug.Print("Loading " .. nextLevel .. "...")
        Game.LoadLevel(nextLevel)
    end
end
```
