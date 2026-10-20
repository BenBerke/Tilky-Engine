-- Scripts/UI/ScaleWithScreen.lua
--
-- UI Transform sizes and positions are in pixels, so on a bigger window
-- an image stays the same size and looks smaller. Put this on a UI image
-- to give it a size and offset measured at a 1080-pixel-tall window; it is
-- rescaled every frame to match the real window height.
--
-- Text doesn't need this: UI Text font sizes already scale with the window.

public Vector2 sizeAt1080 = Vector2(64, 64)
public Vector2 positionAt1080 = Vector2(0, 0)

local screen

function Start()
    -- Any full-screen UI element tells us the window size.
    screen = Game.FindEntity("HUD Hurt Flash")
end

function Update()
    if screen == nil or not screen.isValid then return end
    local height = screen.uiTransform.resolvedSize.y
    if height <= 0 then return end

    local k = height / 1080
    entity.uiTransform.scale = sizeAt1080 * k
    entity.uiTransform.position = positionAt1080 * k
end
