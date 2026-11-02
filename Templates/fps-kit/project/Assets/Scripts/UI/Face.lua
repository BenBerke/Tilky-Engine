-- Scripts/UI/Face.lua
--
-- ONE JOB: the face on the status bar. It looks around, gets bloodier as health
-- drops, flinches when hit, grins at new weapons and snarls while you keep firing.
--
-- Why this is a script: choosing between this many pictures, with timers, would take
-- dozens of events and dispatches.
--
-- The pictures are in UI/Faces. Each kind has five versions, 0 (healthy) to 4 (nearly dead).

public string healthVariable = "Health"
public string invulnerableVariable = "Invulnerable"
public string pictureFolder = "UI/Faces/"

local LOOK_SECONDS = 0.6      -- how long it looks one way
local FLINCH_SECONDS = 0.45   -- a small hit
local OUCH_SECONDS = 1.0      -- a big hit
local OUCH_DAMAGE = 20
local GRIN_SECONDS = 1.5
local SNARL_AFTER = 2.0       -- seconds of holding the trigger

local lastHealth = nil
local holdPicture = nil       -- a reaction being shown
local holdTimer = 0
local lookTimer = 0
local look = 0
local firingTime = 0

local function Band(health)
    return math.min(4, math.floor((100 - math.min(100, health)) * 5 / 101))
end

local function Show(name)
    entity.uiSprite.textureIndex = pictureFolder .. name .. ".png"
end

local function React(name, seconds)
    holdPicture = name
    holdTimer = seconds
end

function Update()
    local dt = GameTime.unscaledDeltaTime
    local health = math.max(0, Game.GetVariable(healthVariable))
    local band = Band(health)

    if health <= 0 then Show("STFDEAD0") return end
    if Game.GetVariable(invulnerableVariable) ~= 0 then Show("STFGOD0") return end

    -- What just happened?
    if lastHealth ~= nil and health < lastHealth then
        if lastHealth - health >= OUCH_DAMAGE then
            React("STFOUCH" .. band, OUCH_SECONDS)
        else
            local side = math.random(2) == 1 and "STFTL" or "STFTR"
            React(side .. band .. "0", FLINCH_SECONDS)
        end
    end
    lastHealth = health

    if Global.FaceGrin then
        Global.FaceGrin = false
        React("STFEVL" .. band, GRIN_SECONDS)
    end

    if GameTime.timeScale > 0 and Input.GetMouseButton(Input.MouseLeft) then
        firingTime = firingTime + dt
    else
        firingTime = 0
    end

    -- A reaction first, then snarling, otherwise looking around.
    holdTimer = holdTimer - dt
    if holdTimer > 0 and holdPicture ~= nil then
        Show(holdPicture)
    elseif firingTime >= SNARL_AFTER then
        Show("STFKILL" .. band)
    else
        lookTimer = lookTimer - dt
        if lookTimer <= 0 then
            lookTimer = LOOK_SECONDS
            look = math.random(0, 2)
        end
        Show("STFST" .. band .. look)
    end
end
