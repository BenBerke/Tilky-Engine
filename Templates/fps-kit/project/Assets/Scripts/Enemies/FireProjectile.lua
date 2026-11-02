-- Scripts/Enemies/FireProjectile.lua
--
-- ONE JOB: makes an enemy fire projectiles. Only enemies that shoot something you
-- can dodge use it. Biting and bullet enemies need no script at all: open the
-- Zombie prefab to see how its events do it.
--
-- The enemy's Monster AI decides WHEN to attack and calls OnMissileReady. This
-- script turns that into shots.

public Entity projectilePrefab = nil
public int shots = 1              -- several at once, fanned out
public number fanDegrees = 0      -- how far apart they are
public number delay = 0.3         -- seconds from the decision to the shot, to match the animation
public number muzzleHeight = 8    -- height above its feet the shot starts from
public string fireSound = ""

local function Fire(target)
    -- It may have died or lost its target while winding up.
    if not entity.isValid or entity.monsterAI.state == MonsterAIState.Dead then return end
    if target == nil or not target.isValid then return end

    local start = entity.transform.position + Vector3(0, muzzleHeight, 0)
    local aim = target.transform.position + Vector3(0, muzzleHeight * 0.75, 0)
    local toTarget = mathT.Vector3Normalize(aim - start)

    if fireSound ~= "" then Audio.PlayAt(fireSound, start) end

    for i = 1, shots do
        -- Fan the shots out evenly around the direction to the target.
        local angle = (i - (shots + 1) * 0.5) * fanDegrees
        local direction = mathT.QuaternionRotate(mathT.QuaternionAngleAxis(Vector3(0, 1, 0), angle), toTarget)

        local shot = Game.CreateEntity(projectilePrefab)
        shot.transform.position = start + direction * 4 - Vector3(0, shot.transform.scale.z * 0.5, 0)
        Global.LaunchOrders = Global.LaunchOrders or {}
        Global.LaunchOrders[shot.id] = { owner = entity, direction = direction, target = target }
    end
end

function OnMissileReady(target)
    Invoke(Fire, delay, target)
end
