-- Scripts/Game/GameRules.lua
--
-- Helpers that many scripts share: dealing damage, making noise that wakes
-- enemies up, and showing Doom-style sprite frames.
--
-- There is no `require` in Tilky scripts, so shared code lives in the
-- `Global` table, which every script can read. Code at the top of a file
-- runs when the level loads, before any script's Start(), so everything
-- defined here is ready by the time other scripts use it.
--
-- This script is on the Player prefab, so every level with a player has it.

-- A clock that keeps counting across the whole level, in seconds.
Global.time = Global.time or 0

-- Scripts that can be hurt. Each one has a public TakeDamage(amount, source)
-- function. Add your own script's name here to make it damageable.
local DAMAGEABLE_SCRIPTS = { "Player", "Enemy", "Barrel" }

-- Hurts `target` (an Entity) by `amount`. `source` is the entity that caused
-- it, or nil. Returns true if the target could take damage.
function Global.Damage(target, amount, source)
    if target == nil or not target.isValid then return false end

    for _, scriptName in ipairs(DAMAGEABLE_SCRIPTS) do
        local script = target:GetScript(scriptName)
        if script.isValid then
            script:TakeDamage(amount, source)
            return true
        end
    end

    return false
end

-- True if `target` has a script that can take damage.
function Global.IsDamageable(target)
    if target == nil or not target.isValid then return false end

    for _, scriptName in ipairs(DAMAGEABLE_SCRIPTS) do
        if target:HasScriptNamed(scriptName) then return true end
    end

    return false
end

-- Gunfire is loud. Enemies that hear it within their hearing range wake up
-- (see Enemy.lua). Real Doom spreads sound through open doorways; this
-- simpler version just uses distance.
function Global.MakeNoise(position)
    Global.noisePosition = position
    Global.noiseTime = Global.time
end

-- Shows one animation frame of a Doom-style sprite.
--
-- Sprite folders hold one image per frame letter and angle: "A1.png" to
-- "A8.png" for a frame drawn from 8 sides, or "A0.png" for one that looks
-- the same from everywhere (death frames, items, effects).
--
-- Doom numbers the angles going one way round the monster (1 = its face),
-- and a Tilky 8-sided sprite numbers its slots the other way, so angle N
-- goes into slot (9 - N) % 8.
function Global.ShowFrame(sprite, folder, frame, directional)
    if sprite == nil or not sprite.isValid then return end

    if directional then
        sprite.sideCount = 1 -- 8-sided
        for angle = 1, 8 do
            sprite:SetTextureFileName((9 - angle) % 8, folder .. "/" .. frame .. angle .. ".png")
        end
    else
        sprite.sideCount = 0 -- single image
        sprite:SetTextureFileName(0, folder .. "/" .. frame .. "0.png")
    end
end

-- Doom's "tics" are 1/35 of a second. Frame timings below are written in
-- tics because that's how they're usually documented for this kind of game.
function Global.Tics(count)
    return count / 35
end

function Update()
    Global.time = Global.time + GameTime.deltaTime
end
