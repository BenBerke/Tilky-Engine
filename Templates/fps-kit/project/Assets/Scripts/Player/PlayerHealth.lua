-- Scripts/Player/PlayerHealth.lua
--
-- ONE JOB: takes damage away from the player, armour first.
--
-- Health, Armor and everything else the player carries are GLOBAL EVENT
-- VARIABLES (Project Settings > Event Variables), not script values. That way
-- the HUD shows them without any code, pickups change them with a plain Change
-- Variable dispatch, and they stay the same from level to level.
--
-- What happens when health reaches 0 isn't here either. Open the Player prefab:
-- its "Health <= 0" event plays the death sound, lowers the camera and calls the
-- death screen.
--
-- There are two ways to hurt the player:
--   1. From a script:  player:GetScript("PlayerHealth"):TakeDamage(20)
--   2. With no code at all, from any event:
--        - a Change Variable dispatch sets IncomingDamage to the amount,
--        - then the event calls the player's "Take Damage" dispatch.
--      That's how melee and bullet enemies, and toxic floors, do it.

public string healthVariable = "Health"
public string armorVariable = "Armor"
public string armorTypeVariable = "ArmorType"     -- 0 none, 1 green, 2 blue
public string incomingVariable = "IncomingDamage"
public string godModeVariable = "Invulnerable"    -- not 0: nothing hurts

public number greenArmorAbsorbs = 0.333           -- share of each hit armour takes
public number blueArmorAbsorbs = 0.5

public string painSound = "Sounds/Player/pain.wav"
public number painSoundGap = 0.5                  -- seconds between pain sounds

local lastPainSound = -100

-- Called by scripts. amount is how much the hit does before armour.
public function TakeDamage(amount)
    if Game.GetVariable(godModeVariable) ~= 0 then return end

    local health = Game.GetVariable(healthVariable)
    if health <= 0 then return end -- already dead

    -- Armour takes its share of the hit, for as long as it lasts.
    local armor = Game.GetVariable(armorVariable)
    local armorType = Game.GetVariable(armorTypeVariable)
    if armor > 0 and armorType > 0 then
        local share = armorType >= 2 and blueArmorAbsorbs or greenArmorAbsorbs
        local absorbed = math.min(math.floor(amount * share), armor)
        armor = armor - absorbed
        amount = amount - absorbed
        Game.SetVariable(armorVariable, armor)
        if armor <= 0 then Game.SetVariable(armorTypeVariable, 0) end
    end

    Game.SetVariable(healthVariable, math.max(0, health - amount))

    -- The HUD's red flash listens for this signal (Lua Signal event).
    Game.Signal("PlayerHurt", entity)
    if Global.ScreenBlood ~= nil then Global.ScreenBlood(amount) end

    if GameTime.time - lastPainSound >= painSoundGap then
        lastPainSound = GameTime.time
        Audio.Play2D(painSound)
    end
end

-- Called by the player's "Take Damage" dispatch (a Call Function dispatch), so
-- events can hurt the player without any code. See the top of this file.
function TakeIncomingDamage()
    TakeDamage(Game.GetVariable(incomingVariable))
end
