-- Scripts/Combat/Explosion.lua
--
-- ONE JOB: hurts and shoves everything near it the moment it appears, more the closer
-- it is, and shakes the view. The burn marks and debris are in Fx.lua. The picture, the
-- sound and removing it afterwards are done by the prefab's own Flipbook and events.
--
-- Why this is a script: dispatches can't find what's nearby or vary the damage
-- with distance.

public number radius = 32       -- how far it reaches, in map units
public number damage = 128      -- what something right at the centre takes
public number knockback = 170   -- how hard it throws something at the centre, in units per second
public number lift = 0.45       -- how much of that throw goes upward
public number shakeStrength = 1 -- how hard it shakes the view, up close
public number shakeReach = 4    -- felt out to this many times its radius

local function Blast(attacker)
    -- The Transform's position is at the sprite's bottom, so go up to its middle.
    local centre = entity.transform.position + Vector3(0, entity.transform.scale.z * 0.5, 0)

    for _, victim in ipairs(Game.OverlapSphere(centre, radius)) do
        local aim = victim.transform.position + Vector3(0, 6, 0)
        -- Walls and shut doors shelter whatever is behind them. Triggers (pickups,
        -- switches) and corpses have nothing to hurt.
        if victim.id ~= entity.id and not victim.collider.isTrigger and Game.LineOfSight(centre, aim) then
            local away = (aim - centre).length
            local closeness = math.max(0, 1 - away / radius)
            Global.ApplyDamage(victim, math.floor(damage * closeness), attacker)

            -- A static body stays put, whatever hits it.
            local body = victim.rigidbody
            if body ~= nil and not body.isStatic and away > 0 then
                local throw = (aim - centre) * (knockback * closeness / away)
                body.velocity = body.velocity + throw + Vector3(0, knockback * closeness * lift, 0)
            end
        end
    end
end

-- A rocket leaves its owner here, so monsters know who to blame.
function Start()
    local attackers = Global.BlastAttackers
    Blast(attackers and attackers[entity.id])
    if attackers then attackers[entity.id] = nil end

    local centre = entity.transform.position + Vector3(0, entity.transform.scale.z * 0.5, 0)
    if Global.Fx ~= nil then Global.Fx.Blast(centre, radius) end

    -- Felt by whichever player is nearest.
    local viewer = Game.FindNearestWithTag("Player", centre)
    if viewer ~= nil and Global.ShakeView ~= nil then
        local reach = radius * shakeReach
        local away = (viewer.transform.position - centre).length
        if away < reach then Global.ShakeView(shakeStrength * (1 - away / reach)) end
    end
end
