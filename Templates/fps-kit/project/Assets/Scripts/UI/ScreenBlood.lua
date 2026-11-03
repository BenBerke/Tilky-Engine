-- Scripts/UI/ScreenBlood.lua
--
-- ONE JOB: blood on the player's visor when they're hurt, fading away. It shows the HUD's
-- Screen Blood images. Other scripts call Global.ScreenBlood(damage).
--
-- Why this is a script: how thick it gets depends on the damage, and several hits add up.
-- The Gore setting's Off turns it off.

public string pictureName = "Screen Blood"  -- the images are this name plus 1, 2, 3 and 4
public int pictureCount = 4
public number fadePerSecond = 0.3           -- how fast it clears, as a share of solid
public number fullAtDamage = 40             -- one hit this big covers solidly
public number leastShown = 0.2              -- even a scratch shows this much
public number mostShown = 0.5               -- however bad it gets, you can still see through it

local pictures = {}

local function Show(picture)
    picture.entity.uiSprite.isActive = picture.alpha > 0
    picture.entity.uiSprite.color = Vector4(1, 1, 1, picture.alpha)
end

function Start()
    for index = 1, pictureCount do
        local found = Game.FindEntity(pictureName .. " " .. index)
        if found ~= nil then
            local picture = { entity = found, alpha = 0 }
            table.insert(pictures, picture)
            Show(picture)
        end
    end
end

function OnLevelLoaded()
    for _, picture in ipairs(pictures) do
        picture.alpha = 0
        Show(picture)
    end
end

function Global.ScreenBlood(damage)
    if #pictures == 0 or Global.Fx == nil or not Global.Fx.GoreOn() then return end
    local picture = pictures[mathT.Random(1, #pictures)]
    picture.alpha = math.min(mostShown, picture.alpha + math.max(leastShown, damage / fullAtDamage * mostShown))
    Show(picture)
end

function Update()
    for _, picture in ipairs(pictures) do
        if picture.alpha > 0 then
            picture.alpha = math.max(0, picture.alpha - fadePerSecond * GameTime.deltaTime)
            Show(picture)
        end
    end
end
