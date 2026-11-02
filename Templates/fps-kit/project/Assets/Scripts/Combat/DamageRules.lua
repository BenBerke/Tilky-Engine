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

-- Top level, not inside Start: this runs before any Start callback in the level.
function Global.ApplyDamage(target, amount, attacker)
    if target == nil or not target.isValid or amount <= 0 then return end

    -- Enemies and barrels: the Monster AI subtracts the health, rolls for pain
    -- and turns on the attacker (monster infighting comes from this).
    if target.monsterAI ~= nil then
        target.monsterAI:Hurt(attacker, amount)
        return
    end

    -- The player: armour rules are in PlayerHealth.lua.
    local health = target:GetScript("PlayerHealth")
    if health.isValid then health:TakeDamage(amount) end
end
