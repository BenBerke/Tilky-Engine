-- Scripts/World/LightEffect.lua  (sector script)
--
-- Makes a room's light flicker, glow or strobe. The sector's own light in
-- the editor is the brightest it gets.

public enum(Flicker, Glow, Strobe) mode = Flicker
public number dimLevel = 0.45   -- 0..1, how dark it gets compared to full
public number speed = 1         -- higher is faster

local bright
local timer = 0
local t = 0

function Start()
    bright = sector.light
end

function Update()
    local dt = GameTime.deltaTime * speed
    t = t + dt

    if mode == 0 then
        -- Flicker: jump between bright and dim at random moments.
        timer = timer - dt
        if timer <= 0 then
            timer = mathT.RandomF(0.05, 0.6)
            sector.light = mathT.RandomBool() and bright or bright * dimLevel
        end
    elseif mode == 1 then
        -- Glow: fade smoothly up and down.
        local k = mathT.Lerp(dimLevel, 1, (mathT.Sin(t * 2) + 1) * 0.5)
        sector.light = bright * k
    else
        -- Strobe: a short bright blink once a second.
        sector.light = (t % 1) < 0.12 and bright or bright * dimLevel
    end
end
