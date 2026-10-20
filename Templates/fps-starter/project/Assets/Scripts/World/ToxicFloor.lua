-- Scripts/World/ToxicFloor.lua  (sector script)
--
-- Glowing green slime: the floor texture cycles through a few images, and
-- standing in it hurts.

public string textures = "Textures/Flats/nukage1.png,Textures/Flats/nukage2.png,Textures/Flats/nukage3.png"
public number frameTime = 0.25
public int damage = 5
public number damageInterval = 1

local frames = {}
local index, frameTimer, damageTimer = 1, 0, 0
local player

function Start()
    -- Split the comma-separated list into a table.
    for path in string.gmatch(textures, "[^,]+") do frames[#frames + 1] = path end
    player = Game.FindEntity("Player")
end

function Update()
    local dt = GameTime.deltaTime

    frameTimer = frameTimer - dt
    if frameTimer <= 0 and #frames > 0 then
        frameTimer = frameTime
        index = index % #frames + 1
        sector:GetFloor(1).floorTexture = frames[index]
    end

    -- Only feet touching the slime count, not jumping over it.
    if player ~= nil and player.isValid and sector:ContainsEntity(player)
        and player.transform.position.y <= sector.floorHeight + 1 then
        damageTimer = damageTimer - dt
        if damageTimer <= 0 then
            damageTimer = damageInterval
            Global.Damage(player, damage, nil)
        end
    else
        damageTimer = 0
    end
end
