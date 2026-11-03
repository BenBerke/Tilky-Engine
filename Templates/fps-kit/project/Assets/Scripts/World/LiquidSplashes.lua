-- Scripts/World/LiquidSplashes.lua
--
-- ONE JOB: makes things splash when they land in or move through a liquid. It's on the
-- Player prefab and watches every moving thing near the player, a few times a second.
--
-- Why this is a script: no event fires for "something entered a liquid" that also says
-- how fast it was going.
--
-- To make a sector a liquid, give it the tag Liquid and one of Water, Nukage or Lava.
-- Fx.lua does the splash. Grenades tell it themselves, because they have no collider.

public number watchRadius = 420     -- things further from the player than this stay quiet
public number checksPerSecond = 10
public number wadeSpeed = 25        -- slower than this it makes no wading splashes
public number wadeGap = 0.35        -- seconds between wading splashes
public number surfaceReach = 1.5    -- this close above the floor counts as in it

local wait = 0
local wading = {}       -- entity id -> seconds until its next wading splash

function Update()
    local dt = GameTime.deltaTime
    wait = wait - dt
    if wait > 0 or dt <= 0 or Global.Fx == nil then return end
    local interval = 1 / checksPerSecond
    wait = interval

    local seen = {}
    for _, thing in ipairs(Game.OverlapSphere(entity.transform.position, watchRadius)) do
        local body = thing.rigidbody
        if body ~= nil and not body.isStatic then
            local position = thing.transform.position
            local sector = Game.GetSectorAt(position)

            if sector ~= nil and sector:HasTag("Liquid") and position.y <= sector:GetFloorHeightAt(position) + surfaceReach then
                seen[thing.id] = true
                local velocity = body.velocity
                local speed = math.sqrt(velocity.x * velocity.x + velocity.z * velocity.z)

                local left = wading[thing.id]
                if left == nil then
                    -- Just arrived.
                    Global.Fx.Splash(position, sector, true)
                    wading[thing.id] = wadeGap
                else
                    left = left - interval
                    if left <= 0 and speed >= wadeSpeed then
                        Global.Fx.Splash(position, sector, false)
                        left = wadeGap
                    end
                    wading[thing.id] = left
                end
            end
        end
    end

    -- Out of it, so that coming back splashes again.
    for id in pairs(wading) do
        if not seen[id] then wading[id] = nil end
    end
end
