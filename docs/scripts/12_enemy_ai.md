# 12 - Enemy AI

Both enemies here move using the entity's **Rigidbody** if it has one (so they collide with the
world and obey gravity), and fall back to moving the transform directly if not.

Both use the same small `Steer(dx, dz, dist, speed)` helper: it takes a direction on the ground
plane and sets the horizontal velocity, keeping the current vertical velocity so gravity still
works.

---

## Chase and attack

**Attach to:** the enemy. Give it a `Collider` and, ideally, a `Rigidbody`. Assign the player as
`target`. The player needs a `Health` script ([10_health_and_damage.md](10_health_and_damage.md))
to take damage.

A small state machine:

```
Idle --(sees target)--> Chase --(in range)--> Attack
  ^                       |  ^                   |
  |  (lost sight for      |  |  (target moved    |
  +-- loseSightTime) -----+  +-- out of range) --+
```

```lua
-- Scripts/AI/Enemy.lua (entity script)
---@field target Entity @ Target (the player)
target = nil

---@field sightRange number @ Sight Range
sightRange = 220

---@field attackRange number @ Attack Range
attackRange = 24

---@field moveSpeed number @ Move Speed
moveSpeed = 30

---@field attackDamage number @ Attack Damage
attackDamage = 10

---@field attackCooldown number @ Attack Cooldown (s)
attackCooldown = 1.2

---@field loseSightTime number @ Give Up After (s)
loseSightTime = 3

---@field eyeHeight number @ Eye Height (above the enemy's feet)
eyeHeight = 8

---@field aimHeight number @ Aim Height (above the target's feet)
aimHeight = 8

local IDLE, CHASE, ATTACK = "Idle", "Chase", "Attack"

local state = IDLE
local transform, rb
local targetHealth
local attackTimer = 0.0
local sinceSeen = 0.0

-- Sets horizontal movement toward (dx, dz). Pass dist = 0 to stand still.
local function Steer(dx, dz, dist, speed)
    local vx, vz = 0.0, 0.0

    if dist > 0.001 then
        vx, vz = dx / dist * speed, dz / dist * speed
        transform.forward = Vector2(dx / dist, dz / dist)
    end

    if rb ~= nil then
        local v = rb.velocity
        rb.velocity = Vector3(vx, v.y, vz)   -- keep v.y so gravity still works
    else
        local dt = GameTime.deltaTime
        transform:addPosition(Vector3(vx * dt, 0, vz * dt))
    end
end

-- Line of sight: shoot a ray from the enemy's eyes at the target's chest. If a wall (or anything
-- else that isn't the target) is hit first, the target is hidden. Positions are at the feet, so
-- both ends are raised by an eye/aim height.
local function CanSee(dist)
    local me = transform.position
    local t = target.transform.position
    local origin = Vector3(me.x, me.y + eyeHeight, me.z)
    local aim = Vector3(t.x, t.y + aimHeight, t.z)

    local direction = Vector3(aim.x - origin.x, aim.y - origin.y, aim.z - origin.z).normalized
    local hit = Game.Raycast(origin, direction, dist, entity.id, true)

    return hit == nil or hit.entityID == target.id
end

local function Attack()
    attackTimer = attackCooldown

    if targetHealth ~= nil and targetHealth.isValid then
        targetHealth:TakeDamage(attackDamage)
    end
end

function Start()
    transform = entity.transform
    rb = entity.rigidbody

    if target == nil then
        Debug.LogWarning("Enemy " .. entity.name .. " has no target assigned")
        return
    end

    targetHealth = target:GetScript("Health")
end

function Update()
    if target == nil or not target.isValid then
        Steer(0, 0, 0, 0)
        return
    end

    local dt = GameTime.deltaTime
    attackTimer = math.max(0, attackTimer - dt)

    local me = transform.position
    local t = target.transform.position
    local dx, dz = t.x - me.x, t.z - me.z
    local dist = math.sqrt(dx * dx + dz * dz)

    local sees = dist > 0.001 and dist <= sightRange and CanSee(dist)
    if sees then sinceSeen = 0.0 else sinceSeen = sinceSeen + dt end

    if state == IDLE then
        Steer(0, 0, 0, 0)
        if sees then
            state = CHASE
            Debug.Print(entity.name .. " spotted you")
        end

    elseif state == CHASE then
        if sinceSeen > loseSightTime then
            state = IDLE
        elseif dist <= attackRange then
            state = ATTACK
        else
            Steer(dx, dz, dist, moveSpeed)
        end

    elseif state == ATTACK then
        Steer(0, 0, 0, 0)

        -- 1.2x gives some slack so the enemy doesn't flicker between chasing and attacking.
        if dist > attackRange * 1.2 then
            state = CHASE
        elseif attackTimer <= 0 then
            Attack()
        end
    end
end
```

**Notes**

- While chasing, the enemy keeps heading for the target even in the moments after losing sight of
  it (until `loseSightTime` runs out). It doesn't remember the last spot it saw the target, so
  it's a little psychic. To fix that, store the target's position each time `sees` is true and
  steer to that instead.
- `direction` is built with `.normalized`, which makes it a unit-length `Vector3`, as
  `Game.Raycast` expects.
- Everything is measured on the ground plane (`x`, `z`), so a target on a balcony is treated as
  right above the enemy.

---

## Wander

**Attach to:** an ambient creature: a critter, a civilian.

Picks a random spot near home, walks to it, stands around for a moment, and repeats.

```lua
-- Scripts/AI/Wander.lua (entity script)
---@field wanderRadius number @ Wander Radius
wanderRadius = 60

---@field moveSpeed number @ Move Speed
moveSpeed = 15

---@field minPause number @ Min Pause (s)
minPause = 1

---@field maxPause number @ Max Pause (s)
maxPause = 4

---@field maxWalkTime number @ Give Up Walking After (s)
maxWalkTime = 6

local transform, rb
local homeX, homeZ = 0.0, 0.0
local goalX, goalZ = 0.0, 0.0
local pause = 0.0
local walkTimer = 0.0
local walking = false

local function Steer(dx, dz, dist, speed)
    local vx, vz = 0.0, 0.0

    if dist > 0.001 then
        vx, vz = dx / dist * speed, dz / dist * speed
        transform.forward = Vector2(dx / dist, dz / dist)
    end

    if rb ~= nil then
        local v = rb.velocity
        rb.velocity = Vector3(vx, v.y, vz)
    else
        local dt = GameTime.deltaTime
        transform:addPosition(Vector3(vx * dt, 0, vz * dt))
    end
end

local function PickGoal()
    -- Random point in a square around home; good enough for ambient wandering.
    goalX = homeX + mathT.RandomF(-wanderRadius, wanderRadius)
    goalZ = homeZ + mathT.RandomF(-wanderRadius, wanderRadius)
    walkTimer = maxWalkTime
    walking = true
end

function Start()
    transform = entity.transform
    rb = entity.rigidbody

    local p = transform.position
    homeX, homeZ = p.x, p.z
    pause = mathT.RandomF(minPause, maxPause)
end

function Update()
    if not walking then
        Steer(0, 0, 0, 0)
        pause = pause - GameTime.deltaTime
        if pause <= 0 then PickGoal() end
        return
    end

    local p = transform.position
    local dx, dz = goalX - p.x, goalZ - p.z
    local dist = math.sqrt(dx * dx + dz * dz)

    walkTimer = walkTimer - GameTime.deltaTime

    -- Arrived, or stuck (against a wall, say) for too long: stop and pick a new goal later.
    if dist < 3 or walkTimer <= 0 then
        walking = false
        pause = mathT.RandomF(minPause, maxPause)
        Steer(0, 0, 0, 0)
    else
        Steer(dx, dz, dist, moveSpeed)
    end
end
```

**Notes**

- The chosen goal may be inside or behind a wall. The creature keeps pushing toward it until
  `maxWalkTime` runs out, then rests and picks a new goal, so it can never get stuck forever.
