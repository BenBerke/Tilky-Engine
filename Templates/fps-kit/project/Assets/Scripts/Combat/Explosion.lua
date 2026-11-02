-- Scripts/Combat/Explosion.lua
--
-- ONE JOB: hurts everything near it the moment it appears, more the closer it is.
-- The picture, the sound and removing it afterwards are done by the prefab's own
-- Flipbook and events.
--
-- Why this is a script: dispatches can't find what's nearby or vary the damage
-- with distance.

public number radius = 32       -- how far it reaches, in map units
public number damage = 128      -- what something right at the centre takes

local function Blast(attacker)
    -- The Transform's position is at the sprite's bottom, so go up to its middle.
    local centre = entity.transform.position + Vector3(0, entity.transform.scale.z * 0.5, 0)

    for _, victim in ipairs(Game.OverlapSphere(centre, radius)) do
        local aim = victim.transform.position + Vector3(0, 6, 0)
        -- Walls and shut doors shelter whatever is behind them. Triggers (pickups,
        -- switches) and corpses have nothing to hurt.
        if victim.id ~= entity.id and not victim.collider.isTrigger and Game.LineOfSight(centre, aim) then
            local away = (aim - centre).length
            local amount = math.floor(damage * math.max(0, 1 - away / radius))
            Global.ApplyDamage(victim, amount, attacker)
        end
    end
end

-- A rocket leaves its owner here, so monsters know who to blame.
function Start()
    local attackers = Global.BlastAttackers
    Blast(attackers and attackers[entity.id])
    if attackers then attackers[entity.id] = nil end
end
