-- Scripts/Combat/DamageRules.lua
--
-- ONE JOB: decides how damage reaches whatever got hit.
--
-- It lives on the Player prefab and puts one function in the shared Global
-- table, so bullets, projectiles and explosions can all say:
--
--     Global.ApplyDamage(target, amount, attacker)
--
-- Why this is a script: dispatches can only take away a fixed number, and how
-- much a bullet does changes from shot to shot.
--
-- To make a new kind of thing shootable, you normally don't need to touch this
-- file: give the thing a Monster AI component and it can be hurt (that's how
-- the Barrel works, and it never chases anybody).

-- Doom's rule for bursting into pieces: a hit that takes a monster below minus its
-- starting health. 1 is Doom's. Lower bursts them more easily. See Fx.lua for the rest.
public number gibOverkill = 1

local startingHealth = {}

-- Top level, not inside Start: this runs before any Start callback in the level.
function Global.ApplyDamage(target, amount, attacker)
    if target == nil or not target.isValid or amount <= 0 then return end

    -- Enemies and barrels: the Monster AI subtracts the health, rolls for pain
    -- and turns on the attacker (monster infighting comes from this).
    if target.monsterAI ~= nil then
        local health = target.monsterAI.health
        startingHealth[target.id] = startingHealth[target.id] or health
        target.monsterAI:Hurt(attacker, amount)

        -- Only what bleeds bursts, and only on the blow that killed it.
        if health > 0 and amount - health >= startingHealth[target.id] * gibOverkill
           and target:HasTag("Bleeds") and Global.Fx ~= nil then
            Global.Fx.Gib(target)
        end
        return
    end

    -- The player: armour rules are in PlayerHealth.lua.
    local health = target:GetScript("PlayerHealth")
    if health.isValid then health:TakeDamage(amount) end
end
