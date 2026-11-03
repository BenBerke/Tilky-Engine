-- Scripts/Weapons/WeaponView.lua
--
-- ONE JOB: the picture of the weapon in the player's hands. It sits on the empty
-- Weapon View image in the HUD layout, and Weapon.lua tells it what to show.
--
-- Move or resize Weapon View in the UI Editor to move every weapon. The image has
-- Fill When Empty off, so it draws nothing until a weapon gives it a picture.

local restingPosition = nil
local hiddenFor = 0

-- ── Used by Weapon.lua ──────────────────────────────────────────────────────

public function Hide()
    entity.uiSprite.isActive = false
end

-- Shows the picture, `offset` pixels from where the UI Editor put it.
public function ShowAt(offset)
    entity.uiSprite.isActive = hiddenFor <= 0
    entity.uiTransform.position = restingPosition + offset
end

-- Keeps the picture away for a while: QuickActions.lua, during a kick or a throw.
-- The weapon shows itself again on the next ShowAt after that.
public function HideFor(seconds)
    hiddenFor = seconds
    Hide()
end

public function GetSize()
    return entity.uiTransform.resolvedSize
end

public function PlayFlipbook(flipbook)
    entity.flipbook:Play(flipbook, true)
end

public function IsShowingFlipbook(flipbook)
    return entity.flipbook.flipbookFileName == flipbook
end

-- ── Callbacks ───────────────────────────────────────────────────────────────

function Start()
    restingPosition = entity.uiTransform.position
    Global.WeaponView = entity
end

function Update()
    if hiddenFor > 0 then hiddenFor = hiddenFor - GameTime.deltaTime end
end

-- The HUD came along from the last level, but its weapons didn't.
function OnLevelLoaded()
    hiddenFor = 0
    Hide()
end
