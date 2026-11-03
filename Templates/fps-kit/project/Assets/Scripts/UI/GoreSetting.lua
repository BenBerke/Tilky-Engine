-- Scripts/UI/GoreSetting.lua
--
-- ONE JOB: the Options menu's Gore row: which setting is shown, changing it, and keeping
-- it for next time you play.
--
-- Why this is a script: a dispatch can't step through three values and save the result.
--
-- It sits on an always-enabled entity, because a hidden menu's entities don't run. The
-- Gore row's Go dispatch calls CycleGore here through the Change Gore dispatch.
-- Fx.lua and ScreenBlood.lua read the variable.

public string variable = "GoreLevel"    -- 0 Off, 1 Normal, 2 Extra
public string saveFile = "options"
public string saveName = "gore"
public string pictureOff = "Gore Off"
public string pictureNormal = "Gore Normal"
public string pictureExtra = "Gore Extra"

local pictures = {}
local shown = -1

function Start()
    pictures[0] = Game.FindEntity(pictureOff)
    pictures[1] = Game.FindEntity(pictureNormal)
    pictures[2] = Game.FindEntity(pictureExtra)

    if Saving.HasKey(saveFile, saveName) then
        Game.SetVariable(variable, Saving.LoadNumber(saveFile, saveName))
    end
end

function CycleGore()
    local level = (Game.GetVariable(variable) + 1) % 3
    Game.SetVariable(variable, level)
    Saving.SaveNumber(saveFile, saveName, level)
end

function Update()
    local level = Game.GetVariable(variable)
    if level == shown then return end
    shown = level
    for index, picture in pairs(pictures) do
        picture.uiSprite.isActive = index == level
    end
end
