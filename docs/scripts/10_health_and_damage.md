# 10 - Health and Damage

Tilky has no built-in health system, so this is a script. It also shows the most useful pattern in
the scripting API: **one script calling functions and reading fields of another**.

```lua
local health = target:GetScript("Health")   -- a Behaviour (reference to that script)
if health.isValid then
    health:TakeDamage(10)                   -- calls TakeDamage inside that script
    Debug.Print(health.currentHealth)       -- reads a variable from that script
end
```

- `GetScript("Health")` matches the script by **file name**, ignoring folders.
- A Behaviour reference has three built-in members: `isValid`, `gameObject`, `enabled`. Any other
  name is looked up in the target script's own variables and functions.
- Functions you want others to call take `self` first, because the call uses a colon.

---

## Health

**Attach to:** anything that can be hurt: the player, enemies, breakable props.

```lua
-- Scripts/Combat/Health.lua (entity script)
---@field maxHealth number @ Max Health
maxHealth = 100

---@field currentHealth number @ Current Health
currentHealth = 100

---@field destroyOnDeath bool @ Destroy On Death
destroyOnDeath = true

---@field invulnerableTime number @ Invulnerability After A Hit (s)
invulnerableTime = 0.0

---@field deathListener Behaviour @ Death Listener (optional)
deathListener = nil

local dead = false
local invulnerable = 0.0

local function Die()
    dead = true
    Debug.Print(gameObject.name .. " died")

    -- Tell the listener script about it, if it has an OnDeath function.
    if deathListener ~= nil and deathListener.isValid and type(deathListener.OnDeath) == "function" then
        deathListener:OnDeath(gameObject)
    end

    if destroyOnDeath then
        gameObject:Destroy()
    else
        gameObject.enabled = false
    end
end

-- Public API used by other scripts ----------------------------------------

function TakeDamage(self, amount)
    if dead or invulnerable > 0 then return end

    currentHealth = math.max(0, currentHealth - amount)
    invulnerable = invulnerableTime

    if currentHealth <= 0 then Die() end
end

function Heal(self, amount)
    if dead then return end
    currentHealth = math.min(maxHealth, currentHealth + amount)
end

function IsDead(self)
    return dead
end

-- Brings a dead (disabled) GameObject back to full health. See the respawner in 13_utilities.md.
function Revive(self)
    dead = false
    invulnerable = 0.0
    currentHealth = maxHealth
end

function GetHealthFraction(self)
    return currentHealth / maxHealth
end

-- Lifecycle ----------------------------------------------------------------

function Start()
    currentHealth = math.min(currentHealth, maxHealth)
end

function Update()
    if invulnerable > 0 then invulnerable = invulnerable - GameTime.deltaTime end
end
```

**Notes**

- `gameObject:Destroy()` is queued and happens at the end of the frame, so it's safe to call in the
  middle of an `Update`.
- With `destroyOnDeath` off, the GameObject is disabled instead. That's a good choice for the
  player, so you can still read `IsDead()` afterwards from another script.
- `currentHealth` is a public field, so it can be set per-instance in the inspector, and other
  scripts can read it directly (`health.currentHealth`).
- `deathListener` is the hook for reacting to a death. See the kill counter in
  [17_level_flow.md](17_level_flow.md).

---

## Damage zone (lava, acid, radiation)

**Attach to:** a sector.

Every `interval` seconds, hurts everything inside the sector that has a `Health` script. Pair it
with `RisingFloor` from [06_lifts_and_platforms.md](06_lifts_and_platforms.md) for a flood that
hurts.

```lua
-- Scripts/Combat/DamageZone.lua (sector script)
---@field damage number @ Damage Per Tick
damage = 5

---@field interval number @ Seconds Between Ticks
interval = 0.5

---@field requiredTag string @ Only Hurt Entities With Tag (empty = all)
requiredTag = ""

local timer = 0.0

function Update()
    timer = timer - GameTime.deltaTime
    if timer > 0 then return end

    timer = interval

    for i = 1, sector.entityCount do
        local entity = sector:GetEntity(i)

        local tagOk = requiredTag == "" or entity:HasTag(requiredTag)

        if tagOk and entity:HasScriptNamed("Health") then
            entity:GetScript("Health"):TakeDamage(damage)
        end
    end
end
```

**Notes**

- `HasScriptNamed("Health")` guards against entities that just happen to be in the sector (props,
  pickups) and have no Health script. Without it, `GetScript` would return an invalid reference.
- Set `requiredTag` to `Player` to hurt only the player, or `Enemy` to hurt only enemies.

---

## Health regeneration

**Attach to:** the same GameObject as `Health`. It is a separate script that talks to it.

Heals a little each second, but only after not having been hurt for a while.

```lua
-- Scripts/Combat/HealthRegen.lua (entity script)
---@field regenPerSecond number @ Regen Per Second
regenPerSecond = 3

---@field delayAfterHit number @ Delay After A Hit (s)
delayAfterHit = 5

local health
local lastHealth = 0.0
local sinceHit = 0.0

function Start()
    health = gameObject:GetScript("Health")

    if not health.isValid then
        Debug.LogError("HealthRegen needs a Health script on " .. gameObject.name)
        return
    end

    lastHealth = health.currentHealth
end

function Update()
    if health == nil or not health.isValid then return end

    local dt = GameTime.deltaTime
    local now = health.currentHealth

    -- Losing health restarts the delay.
    if now < lastHealth then sinceHit = 0.0 else sinceHit = sinceHit + dt end
    lastHealth = now

    if sinceHit >= delayAfterHit and now < health.maxHealth and not health:IsDead() then
        health:Heal(regenPerSecond * dt)
        lastHealth = health.currentHealth
    end
end
```

**Notes**

- If the `Health` script isn't found, `health.isValid` is `false`, and this script quietly stops
  instead of erroring every frame.
- Because `Heal` is called with `regenPerSecond * dt`, the healing amount is frame-rate
  independent.
