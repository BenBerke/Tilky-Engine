-- Scripts/Enemies/Projectile.lua
--
-- A slow-moving shot, like the imp's fireball. Whoever spawns it sets its
-- position and turns its Transform to face where it should fly
-- (transform:LookDirection(direction, false)); this script does the rest.
--
-- Projectiles don't use physics. Each frame the script casts a short ray
-- along the path it's about to travel: if the ray hits something, the
-- projectile explodes there, so it can never skip through a thin wall.

public number speed = 110
public int damageMin = 3
public int damageMax = 24
public number lifetime = 6

public string spriteFolder = "Sprites/Effects/Fireball"
public string flyFrames = "AB"
public string explodeFrames = "CDE"
public number frameTime = 0.1
public string explodeSound = "Sounds/fireball_explode"

local direction
local exploded = false
local age, frameTimer, frameIndex = 0, 0, 1

local function Center()
    return entity.transform.position + Vector3(0, entity.transform.scale.z * 0.5, 0)
end

local function ShowFrame(frames, index)
    Global.ShowFrame(entity.sprite, spriteFolder, frames:sub(index, index), false)
end

local function Explode(hit)
    exploded = true
    frameIndex, frameTimer = 1, frameTime
    ShowFrame(explodeFrames, 1)

    if hit ~= nil then
        -- Step back from the wall so the explosion isn't drawn inside it.
        local center = hit.position - direction * 2
        entity.transform.position = center - Vector3(0, entity.transform.scale.z * 0.5, 0)
        if hit.entity ~= nil then
            Global.Damage(hit.entity, mathT.Random(damageMin, damageMax), entity)
        end
    end

    local audio = entity.audioSource
    if audio ~= nil and explodeSound ~= "" then
        audio.soundFileName = explodeSound
        audio:Play()
    end
end

function Start()
    direction = entity.transform.forward
    ShowFrame(flyFrames, 1)
end

function Update()
    local dt = GameTime.deltaTime
    age = age + dt

    -- Animation: loop the flying frames, play the explosion once and vanish.
    frameTimer = frameTimer - dt
    if frameTimer <= 0 then
        frameTimer = frameTime
        frameIndex = frameIndex + 1
        if exploded then
            if frameIndex > #explodeFrames then
                entity:Destroy()
                return
            end
            ShowFrame(explodeFrames, frameIndex)
        else
            if frameIndex > #flyFrames then frameIndex = 1 end
            ShowFrame(flyFrames, frameIndex)
        end
    end

    if exploded then return end
    if age > lifetime then
        Explode(nil)
        return
    end

    local step = speed * dt
    local hit = Game.Raycast(Center(), direction, step + 2, entity.id, true)
    if hit ~= nil then
        Explode(hit)
    else
        entity.transform.position = entity.transform.position + direction * step
    end
end
