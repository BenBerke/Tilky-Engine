-- Scripts/UI/ActionView.lua
--
-- ONE JOB: the picture of the boot or the throwing hand. It sits on the
-- empty Action View image in the HUD layout, and QuickActions.lua tells it what to play.
--
-- Move or resize Action View in the UI Editor to move them. It has Fill When Empty off,
-- and hides itself when the action is over.

local showFor = 0

public function Play(flipbook, seconds)
    showFor = seconds
    entity.uiSprite.isActive = true
    entity.flipbook:Play(flipbook, true)
end

function Start()
    Global.ActionView = entity
    entity.uiSprite.isActive = false
end

-- The HUD came along from the last level.
function OnLevelLoaded()
    showFor = 0
    entity.uiSprite.isActive = false
end

function Update()
    if showFor <= 0 then return end
    showFor = showFor - GameTime.deltaTime
    if showFor <= 0 then entity.uiSprite.isActive = false end
end
