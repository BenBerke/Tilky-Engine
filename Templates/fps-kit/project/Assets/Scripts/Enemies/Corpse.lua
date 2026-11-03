-- Scripts/Enemies/Corpse.lua
--
-- ONE JOB: keeps a dead monster's body on the floor, without it getting in the way.
--
-- Why this is a script: nothing should bump into or shoot a corpse, so its collider
-- becomes a trigger. But a trigger doesn't stand on floors, so physics would let the
-- body fall out of the level. This does the falling itself, and follows the floor if
-- it moves, so a flying monster drops to the ground and a body on a lift rides it.
--
-- The monster's Die dispatch calls BecomeCorpse with a Call Function dispatch.

local FALL_ACCELERATION = 300 -- units per second, each second

local dead = false
local fallSpeed = 0

function BecomeCorpse()
    dead = true
    entity.collider.isTrigger = true
    entity.rigidbody.gravityScale = 0
    entity.rigidbody:Stop()
    if Global.Fx ~= nil then Global.Fx.Pool(entity) end
end

function Update()
    if not dead then return end

    entity.rigidbody:Stop() -- a last shot's knockback mustn't slide it along

    local position = entity.transform.position
    local sector = Game.GetSectorAt(position)
    if sector == nil then return end

    local floor = sector:GetFloorHeightAt(position)
    if position.y > floor then
        fallSpeed = fallSpeed + FALL_ACCELERATION * GameTime.deltaTime
        position.y = math.max(floor, position.y - fallSpeed * GameTime.deltaTime)
    else
        fallSpeed = 0
        position.y = floor
    end
    entity.transform.position = position
end
