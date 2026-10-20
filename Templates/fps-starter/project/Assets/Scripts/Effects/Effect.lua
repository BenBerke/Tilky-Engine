-- Scripts/Effects/Effect.lua
--
-- A short animation that plays once and removes itself: bullet puffs,
-- blood splats. Spawn it from another script with
-- Game.CreateEntity(prefab) and set its position.

public string spriteFolder = "Sprites/Effects/Puff"
public string frames = "ABCD"
public number frameTime = 0.1
public number riseSpeed = 4   -- units per second; smoke drifts upward

local index, timer = 1, 0

function Start()
    timer = frameTime
    Global.ShowFrame(entity.sprite, spriteFolder, frames:sub(1, 1), false)
end

function Update()
    local dt = GameTime.deltaTime
    entity.transform.position = entity.transform.position + Vector3(0, riseSpeed * dt, 0)

    timer = timer - dt
    if timer > 0 then return end
    timer = frameTime
    index = index + 1

    if index > #frames then
        entity:Destroy()
    else
        Global.ShowFrame(entity.sprite, spriteFolder, frames:sub(index, index), false)
    end
end
