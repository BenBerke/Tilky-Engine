-- Scripts/World/Animate.lua
--
-- Loops a sprite through a few frames forever: flickering lamps, torches.

public string spriteFolder = "Sprites/Decorations/TechLamp"
public string frames = "ABCD"
public number frameTime = 0.15

local index, timer = 1, 0

function Update()
    timer = timer - GameTime.deltaTime
    if timer > 0 then return end
    timer = frameTime
    index = index % #frames + 1
    Global.ShowFrame(entity.sprite, spriteFolder, frames:sub(index, index), false)
end
