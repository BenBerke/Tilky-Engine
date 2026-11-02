-- Scripts/UI/PictureNumber.lua
--
-- ONE JOB: shows a variable's value using number pictures, like Doom's status bar.
--
-- Why this is a script: UI Text can show a variable by itself, but only in a font,
-- and Doom's numbers are shaded pictures. A dispatch can't pick a picture by digit.
--
-- HOW TO USE IT: place one UI image per digit and name them "<Digit Name> 1" for the
-- ones, "<Digit Name> 2" for the tens, and so on. Then fill in the fields below.
-- See "Health Number" in the HUD layout.

public string variable = "Health"
public string digitPictures = "UI/Digits/Big "  -- the digit and ".png" are added to this
public string digitName = "Health Digit"
public int digitCount = 3
public int minimumDigits = 1                     -- 2 shows 5 as 05, for seconds on a clock

local digits = nil
local shownValue = nil

local function FindDigits()
    digits = {}
    for place = 1, digitCount do
        digits[place] = Game.FindEntity(digitName .. " " .. place)
    end
end

function Update()
    if digits == nil then FindDigits() end

    local value = math.floor(math.max(0, Game.GetVariable(variable)))
    if value == shownValue then return end
    shownValue = value

    for place, digit in pairs(digits) do
        local remaining = math.floor(value / 10 ^ (place - 1))
        if remaining > 0 or place <= minimumDigits then
            digit.uiSprite.textureIndex = digitPictures .. (remaining % 10) .. ".png"
            digit.uiSprite.isActive = true
        else
            digit.uiSprite.isActive = false
        end
    end
end
