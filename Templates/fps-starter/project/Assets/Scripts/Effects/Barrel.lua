-- Scripts/Effects/Barrel.lua
--
-- An explosive barrel. Shoot it (or blow up the one next to it) and it
-- explodes, hurting everything nearby: enemies, the player, other barrels.
--
-- Needs: Sprite, solid sphere Collider, Audio Source.

public int health = 20
public number blastRadius = 32
public int blastDamage = 120    -- at the centre; less further out
public string spriteFolder = "Sprites/Effects/Barrel"
public string idleFrames = "AB"
public string explosionFolder = "Sprites/Effects/Explosion"
public string explosionFrames = "ABCDE"
public Vector3 explosionScale = Vector3(15, 15, 13)
public number frameTime = 0.15
public string explodeSound = "Sounds/barrel_explode"

local exploding = false
local index, timer = 1, 0

local function Blast(source)
    local center = entity.transform.position
    for _, other in ipairs(Game.GetEntities()) do
        if other.id ~= entity.id and other.hasTransform and Global.IsDamageable(other) then
            local distance = other.transform:DistanceTo(center)
            -- Walls shield you from the blast.
            if distance < blastRadius and Game.LineOfSight(center + Vector3(0, 4, 0), other.transform.position + Vector3(0, 4, 0)) then
                local damage = math.floor(blastDamage * (1 - distance / blastRadius))
                if damage > 0 then Global.Damage(other, damage, source) end
            end
        end
    end
end

local function Explode(source)
    exploding = true
    index, timer = 1, frameTime
    entity.collider.isActive = false
    entity.transform.scale = explosionScale
    Global.ShowFrame(entity.sprite, explosionFolder, explosionFrames:sub(1, 1), false)

    local audio = entity.audioSource
    if audio ~= nil then
        audio.soundFileName = explodeSound
        audio:Play()
    end

    -- Barrels damage each other, so this can set off a chain reaction.
    Blast(source)
end

function TakeDamage(self, amount, source)
    if exploding then return end
    health = health - amount
    if health <= 0 then Explode(source) end
end

function Update()
    timer = timer - GameTime.deltaTime
    if timer > 0 then return end
    timer = frameTime

    if exploding then
        index = index + 1
        if index > #explosionFrames then
            entity:Destroy()
        else
            Global.ShowFrame(entity.sprite, explosionFolder, explosionFrames:sub(index, index), false)
        end
    else
        index = index % #idleFrames + 1
        Global.ShowFrame(entity.sprite, spriteFolder, idleFrames:sub(index, index), false)
    end
end
