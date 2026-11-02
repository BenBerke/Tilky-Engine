-- Scripts/UI/MenuCursor.lua
--
-- ONE JOB: the blinking skull beside a menu, as in Doom. The mouse moves it by
-- hovering, the Up and Down keys move it too, and Enter picks what it points at.
--
-- Why this is a script: buttons have no keyboard control, and no dispatch can keep
-- one picture lined up with whichever button is under the cursor.
--
-- HOW TO USE IT: put this on a picture, and give that picture and the menu's buttons
-- the same tag, named in Menu Tag. Enter fires the Lua Signal "Menu Pick <button name>",
-- so a button needs a Lua Signal event with that name as well as its click event.

public string menuTag = "Pause Menu"
public string moveSound = "Sounds/World/lift stop.wav"
public string pictureOne = "UI/Pictures/M_SKULL1.png"
public string pictureTwo = "UI/Pictures/M_SKULL2.png"
public number gap = 0.008         -- space between the skull and the button, as a share of the screen

local BLINK_SECONDS = 0.23

local items = nil
local selected = 1
local blink = 0
local showingFirst = true

-- The menu's enabled buttons, top to bottom.
local function FindItems()
    items = {}
    for _, candidate in ipairs(Game.FindEntitiesWithTag(menuTag)) do
        if candidate.hasUIButton then table.insert(items, candidate) end
    end
    table.sort(items, function(a, b) return a.uiTransform.anchorMin.y < b.uiTransform.anchorMin.y end)
end

local function Select(index)
    if index ~= selected then Audio.Play2D(moveSound) end
    selected = index
end

function Update()
    if items == nil then FindItems() end
    if #items == 0 then return end

    for index, item in ipairs(items) do
        if item.uiButton.isHovered then Select(index) end
    end
    if Input.GetKeyDown(Key.Down) then Select(selected % #items + 1) end
    if Input.GetKeyDown(Key.Up) then Select((selected - 2) % #items + 1) end
    if Input.GetKeyDown(Key.Enter) then Game.Signal("Menu Pick " .. items[selected].name) end

    -- Beside the selected button, keeping the skull's own size.
    local own = entity.uiTransform
    local width = own.anchorMax.x - own.anchorMin.x
    local height = own.anchorMax.y - own.anchorMin.y
    local target = items[selected].uiTransform
    local x = target.anchorMin.x - gap - width
    local y = (target.anchorMin.y + target.anchorMax.y - height) * 0.5
    own.anchorMin = Vector2(x, y)
    own.anchorMax = Vector2(x + width, y + height)

    blink = blink + GameTime.unscaledDeltaTime
    if blink >= BLINK_SECONDS then
        blink = 0
        showingFirst = not showingFirst
        entity.uiSprite.textureIndex = showingFirst and pictureOne or pictureTwo
    end
end
