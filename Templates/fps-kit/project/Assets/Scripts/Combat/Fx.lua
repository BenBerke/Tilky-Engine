-- Scripts/Combat/Fx.lua
--
-- ONE JOB: the blood, chunks, marks and splashes, in one place, so that every weapon
-- and explosion gets the same ones and the Gore setting reaches them all.
--
-- It lives on the Player prefab and puts its functions in the shared Global table:
--     Global.Fx.Blood(position, direction, amount)
--     Global.Fx.BulletHit(from, direction, hit)
--     Global.Fx.Gib(target)
--     Global.Fx.Pool(target)
--     Global.Fx.Blast(centre, radius)
--     Global.Fx.Splash(position, sector, big)
--     Global.Fx.Flash(position, amount)
--
-- Why this is a script: the amount changes with the Gore setting, and a splash has to
-- find out what it landed in.
--
-- The effects themselves are prefabs in Prefabs/Effects holding Particle Emitters. Open
-- one and change its numbers to change how much blood there is, how far it flies, or
-- how big the stains are. Nothing is spawned: Game.EmitParticles fires a prefab's
-- emitters directly, so a hundred shots cost almost nothing.

public string goreVariable = "GoreLevel"    -- 0 Off, 1 Normal, 2 Extra
public number extraGoreScale = 2            -- how much more blood Extra throws

public Entity bloodPrefab = nil
public Entity bloodBurstPrefab = nil
public Entity gibPrefab = nil
public Entity puffPrefab = nil
public Entity debrisPrefab = nil
public Entity splashWaterPrefab = nil
public Entity splashNukagePrefab = nil
public Entity splashLavaPrefab = nil

public string gibSound = "Sounds/Player/gib.wav"
public string waterSound = "Sounds/FX/splash water.wav"
public string nukageSound = "Sounds/FX/splash goo.wav"
public string lavaSound = "Sounds/FX/splash lava.wav"

public number holeSize = 1.5
public number flashSeconds = 0.14           -- how long a muzzle flash takes to fade

local HOLE_PICTURE = "Sprites/FX/Marks/Bullet Hole.png"
local SCORCH_PICTURES = { "Sprites/FX/Marks/Scorch 1.png", "Sprites/FX/Marks/Scorch 2.png", "Sprites/FX/Marks/Scorch 3.png" }
local STAIN_PICTURES = { "Sprites/FX/Blood/Stain 1.png", "Sprites/FX/Blood/Stain 2.png", "Sprites/FX/Blood/Stain 3.png",
                         "Sprites/FX/Blood/Stain 4.png", "Sprites/FX/Blood/Stain 5.png" }
local WHITE = Vector4(1, 1, 1, 1)
local NO_STAINS_ON = "Liquid"

local LIQUIDS = {
    { tag = "Water", prefab = function() return splashWaterPrefab end, sound = function() return waterSound end,
      ripple = "Sprites/FX/Splash/Water Ripple.png" },
    { tag = "Nukage", prefab = function() return splashNukagePrefab end, sound = function() return nukageSound end,
      ripple = "Sprites/FX/Splash/Nukage Ripple.png" },
    { tag = "Lava", prefab = function() return splashLavaPrefab end, sound = function() return lavaSound end,
      ripple = "Sprites/FX/Splash/Lava Ripple.png" },
}

local function GoreScale()
    local level = Game.GetVariable(goreVariable)
    if level <= 0 then return 0 end
    return level >= 2 and extraGoreScale or 1
end

local function Pick(list)
    return list[mathT.Random(1, #list)]
end

Global.Fx = {}

function Global.Fx.GoreOn()
    return GoreScale() > 0
end

-- `direction` is the way the hit was going, so blood carries on past the target and
-- stains what's behind it. `amount` multiplies the emitter's burst numbers.
function Global.Fx.Blood(position, direction, amount)
    local scale = GoreScale() * (amount or 1)
    if scale <= 0 or bloodPrefab == nil then return end
    Game.EmitParticles(bloodPrefab, position, direction, scale)
end

-- A shot that reached a wall, floor or ceiling, or something that doesn't bleed.
function Global.Fx.BulletHit(from, direction, hit)
    if puffPrefab ~= nil then Game.EmitParticles(puffPrefab, hit.position + hit.normal * 0.6, hit.normal) end
    if hit.entity == nil then
        Game.StainSurface(from, direction, hit.distance + 1, HOLE_PICTURE, holeSize, WHITE, 0, NO_STAINS_ON)
    end
end

-- Enough damage to burst it. The entity stays, as an unseen corpse, so that kill
-- counts and drops work the same as any other death.
function Global.Fx.Gib(target)
    local scale = GoreScale()
    if scale <= 0 then return false end

    local position = target.transform.position
    local centre = position + Vector3(0, target.transform.scale.z * 0.5, 0)
    if bloodBurstPrefab ~= nil then Game.EmitParticles(bloodBurstPrefab, centre, nil, scale) end
    if gibPrefab ~= nil then Game.EmitParticles(gibPrefab, centre, nil, scale) end
    Game.StainSurface(centre, Vector3(0, -1, 0), 200, Pick(STAIN_PICTURES), target.transform.scale.x * 1.6, WHITE, 0, NO_STAINS_ON)
    Audio.PlayAt(gibSound, position)

    if target.sprite ~= nil then target.sprite.isActive = false end

    -- Close enough to get some on your visor.
    local away = (position - entity.transform.position).length
    if away < 40 and Global.ScreenBlood ~= nil then Global.ScreenBlood(50) end
    return true
end

-- A body's pool of blood on the floor under it.
function Global.Fx.Pool(target)
    if GoreScale() <= 0 or not target:HasTag("Bleeds") then return end
    local above = target.transform.position + Vector3(0, 4, 0)
    Game.StainSurface(above, Vector3(0, -1, 0), 400, Pick(STAIN_PICTURES), target.transform.scale.x * 0.9, WHITE, 0, NO_STAINS_ON)
end

-- What an explosion leaves: burnt surfaces on every side in reach, and flying debris.
function Global.Fx.Blast(centre, radius)
    if debrisPrefab ~= nil then Game.EmitParticles(debrisPrefab, centre) end
    local size = radius * 0.7
    for _, direction in ipairs({ Vector3(0, -1, 0), Vector3(1, 0, 0), Vector3(-1, 0, 0), Vector3(0, 0, 1), Vector3(0, 0, -1), Vector3(0, 1, 0) }) do
        Game.StainSurface(centre, direction, radius, Pick(SCORCH_PICTURES), size, WHITE, 0, NO_STAINS_ON)
    end
end

-- Something landed in or moved through a liquid. Does nothing if the sector isn't one:
-- tag a sector Liquid plus Water, Nukage or Lava to make it splash.
function Global.Fx.Splash(position, sector, big)
    if sector == nil or not sector:HasTag(NO_STAINS_ON) then return end
    for _, liquid in ipairs(LIQUIDS) do
        if sector:HasTag(liquid.tag) then
            local surface = Vector3(position.x, sector:GetFloorHeightAt(position), position.z)
            local prefab = liquid.prefab()
            if prefab ~= nil then Game.EmitParticles(prefab, surface + Vector3(0, 0.5, 0), nil, big and 1 or 0.35) end
            Game.StainSurface(surface + Vector3(0, 3, 0), Vector3(0, -1, 0), 8, liquid.ripple, big and 12 or 6, WHITE, 0.7)
            Audio.PlayAt(liquid.sound(), position, big and 1 or 0.45)
            return
        end
    end
end

-- A gun's flash lights the room it's in for a moment. Sectors with their own light
-- effect go on flickering over it. `amount` is added to the sector's red, green and blue.
local flashingFrom = {}
function Global.Fx.Flash(position, amount)
    local sector = Game.GetSectorAt(position)
    if sector == nil or amount <= 0 then return end

    local rest = flashingFrom[sector.id]
    if rest == nil or not sector:IsLightFading() then
        rest = sector.light
        flashingFrom[sector.id] = rest
    end
    sector.light = Vector3(math.min(255, rest.x + amount), math.min(255, rest.y + amount), math.min(255, rest.z + amount * 0.8))
    sector:FadeLight(rest, flashSeconds)
end
