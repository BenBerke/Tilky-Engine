-- Scripts/Combat/Grenade.lua
--
-- ONE JOB: a thrown grenade: it flies, bounces off walls, floors and monsters, rolls to
-- a stop, and goes off when its fuse runs out.
--
-- Why this is a script: physics bodies don't bounce. Each frame it casts a short ray
-- along the way it's about to go, so it can't skip through a thin wall.
--
-- Whoever throws it spawns the prefab and leaves its orders in a shared table, because a
-- new entity's scripts don't start until the next frame (see QuickActions.lua):
--     Global.LaunchOrders[grenade.id] = { owner = owner, velocity = velocity }

public number fuse = 2.5            -- seconds until it goes off
public number gravity = 260         -- units per second, each second
public number bounciness = 0.45     -- share of its speed off a surface it keeps
public number rollFriction = 0.7    -- share of its speed along a surface it keeps
public number stopSpeed = 16        -- slower than this on a floor, it stops
public number radius = 1.2
public string bounceSound = "Sounds/FX/grenade bounce.wav"
public Entity explosionPrefab = nil

local owner = nil
local velocity = Vector3(0, 0, 0)
local age = 0
local resting = false

function Start()
    local orders = Global.LaunchOrders and Global.LaunchOrders[entity.id]
    if orders == nil then return end
    Global.LaunchOrders[entity.id] = nil
    owner = orders.owner
    velocity = orders.velocity
end

-- The Transform's position is at the sprite's bottom.
local function Centre()
    return entity.transform.position + Vector3(0, entity.transform.scale.z * 0.5, 0)
end

local function PutCentreAt(point)
    entity.transform.position = point - Vector3(0, entity.transform.scale.z * 0.5, 0)
end

local function Explode()
    if explosionPrefab ~= nil then
        local blast = Game.CreateEntity(explosionPrefab)
        PutCentreAt(Centre())
        blast.transform.position = Centre() - Vector3(0, blast.transform.scale.z * 0.5, 0)

        -- An explosion wants to know who to blame for what it hits.
        Global.BlastAttackers = Global.BlastAttackers or {}
        Global.BlastAttackers[blast.id] = owner
    end
    entity:Destroy()
end

local function Bounce(hit)
    local speed = velocity.length
    local normal = hit.normal
    local into = velocity.x * normal.x + velocity.y * normal.y + velocity.z * normal.z

    -- Split the speed into straight off the surface and along it, and lose some of each.
    local along = velocity - normal * into
    velocity = along * rollFriction - normal * (into * bounciness)
    PutCentreAt(hit.position + normal * radius)

    if speed > 25 then Audio.PlayAt(bounceSound, hit.position, math.min(1, speed / 120)) end

    -- In a liquid it splashes and stays down.
    local isFloor = hit.type == "SectorFloor"
    local sector = isFloor and hit.sector or nil
    if sector ~= nil and sector:HasTag("Liquid") then
        if Global.Fx ~= nil then Global.Fx.Splash(hit.position, sector, true) end
        velocity = Vector3(0, 0, 0)
        resting = true
        return
    end

    if isFloor and velocity.length < stopSpeed then
        velocity = Vector3(0, 0, 0)
        resting = true
    end
end

function Update()
    local dt = GameTime.deltaTime
    age = age + dt
    if age >= fuse then
        Explode()
        return
    end

    if resting then
        -- Rides a floor that moves, and falls if the floor drops away.
        local position = entity.transform.position
        local sector = Game.GetSectorAt(position)
        if sector ~= nil then
            local floor = sector:GetFloorHeightAt(position)
            if position.y > floor + 0.5 then
                resting = false
            else
                position.y = floor
                entity.transform.position = position
            end
        end
        return
    end

    velocity = velocity - Vector3(0, gravity * dt, 0)
    local step = velocity.length * dt
    if step <= 0 then return end

    local ignored = owner ~= nil and owner.isValid and owner.id or entity.id
    local hit = Game.Raycast(Centre(), velocity, step + radius, ignored, true)
    if hit ~= nil and (hit.entity == nil or hit.entity.id ~= entity.id) then
        Bounce(hit)
    else
        entity.transform.position = entity.transform.position + velocity * dt
    end
end
