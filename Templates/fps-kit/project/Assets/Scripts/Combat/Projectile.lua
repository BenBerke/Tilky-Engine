-- Scripts/Combat/Projectile.lua
--
-- ONE JOB: flies a fireball, rocket or plasma bolt until it hits something.
-- Every prefab in Prefabs/Projectiles runs this script with different numbers.
--
-- Why this is a script: no dispatch can aim a shot at something that moves.
--
-- Whoever fires it spawns the prefab, then leaves it its orders in a shared table,
-- because a new entity's scripts don't start until the next frame:
--     Global.LaunchOrders[shot.id] = { owner = owner, direction = direction }
-- (see Weapon.lua and Enemies/FireProjectile.lua). Target is optional, only
-- Homing uses it.
--
-- It doesn't use physics. Each frame it casts a short ray along the way it's about
-- to go, so even a very fast rocket can't skip through a thin wall.

public number speed = 87
public int damageMin = 3
public int damageMax = 24
public number lifetime = 8          -- seconds before it disappears by itself
public number homing = 0            -- degrees per second it turns toward its target
public Entity impactPrefab = nil    -- spawned where it hits: a flash or an explosion

local owner = nil
local target = nil
local direction = Vector3(0, 0, 1)
local age = 0

-- Picks up the orders left by whoever fired it.
function Start()
    local orders = Global.LaunchOrders and Global.LaunchOrders[entity.id]
    if orders == nil then return end
    Global.LaunchOrders[entity.id] = nil
    owner = orders.owner
    target = orders.target
    direction = mathT.Vector3Normalize(orders.direction)
end

-- The middle of the sprite: the Transform's position is at the sprite's bottom.
local function Centre()
    return entity.transform.position + Vector3(0, entity.transform.scale.z * 0.5, 0)
end

local function Explode(hit)
    local at = Centre()

    if hit ~= nil then
        -- Back out of the wall, so the flash isn't drawn inside it.
        at = hit.position - direction * 2

        -- Monsters of one kind never hurt each other with missiles, as in Doom.
        local ownerAlive = owner ~= nil and owner.isValid
        local sameKind = hit.entity ~= nil and ownerAlive and hit.entity.name == owner.name
        if hit.entity ~= nil and not sameKind then
            Global.ApplyDamage(hit.entity, mathT.Random(damageMin, damageMax), owner)
        end
    end

    if impactPrefab ~= nil then
        local impact = Game.CreateEntity(impactPrefab)
        impact.transform.position = at - Vector3(0, impact.transform.scale.z * 0.5, 0)

        -- An explosion wants to know who to blame for what it hits.
        Global.BlastAttackers = Global.BlastAttackers or {}
        Global.BlastAttackers[impact.id] = owner
    end

    entity:Destroy()
end

function Update()
    local dt = GameTime.deltaTime
    age = age + dt
    if age > lifetime then
        Explode(nil)
        return
    end

    -- Homing: bend a little toward the target each frame.
    if homing > 0 and target ~= nil and target.isValid then
        local wanted = mathT.Vector3Normalize(target.transform.position + Vector3(0, 8, 0) - Centre())
        local turn = math.min(1, mathT.DegToRad(homing) * dt)
        direction = mathT.Vector3Normalize(direction + (wanted - direction) * turn)
    end

    local step = speed * dt
    local ignored = owner ~= nil and owner.isValid and owner.id or 0
    local hit = Game.Raycast(Centre(), direction, step + 1, ignored, true, RayType.Bullet)
    if hit ~= nil then
        Explode(hit)
    else
        entity.transform.position = entity.transform.position + direction * step
    end
end
