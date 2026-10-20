-- Scripts/Pickups/Pickup.lua
--
-- Anything the player collects by walking over it: health, armor, ammo,
-- weapons and keys. Every pickup prefab (Prefabs/Pickups) uses this script
-- with different settings.
--
-- Needs a Collider with Is Trigger ticked, so the player walks through it
-- and this script hears about the overlap.

public enum(Health, Armor, Bullets, Shells, Weapon, Key) kind = Health
public int amount = 10
public int limit = 100          -- Health/Armor: won't go above this
public string item = ""         -- Weapon: "pistol"/"shotgun". Key: "blue"/"red"
public string message = "Picked up a stimpack."
public string sound = "Sounds/item_pickup"

-- Optional looping animation, e.g. a blinking key card.
public string spriteFolder = ""
public string frames = ""
public number frameTime = 0.25

local index, timer = 1, 0

-- Tries to give this pickup to the player. Returns true if it was used.
local function Give(player)
    local stats = player:GetScript("Player")
    local weapons = player:GetScript("Weapons")
    if not stats.isValid or stats.dead then return false end

    if kind == 0 then return stats:Heal(amount, limit) end
    if kind == 1 then return stats:AddArmor(amount, limit) end
    if kind == 2 then return weapons:AddAmmo("bullets", amount) end
    if kind == 3 then return weapons:AddAmmo("shells", amount) end
    if kind == 4 then return weapons:GiveWeapon(item, amount) end
    if kind == 5 then return stats:GiveKey(item) end
    return false
end

-- OnTrigger runs every frame the player stands on the pickup, not just the
-- first one, so a medikit you walked over at full health is still picked
-- up once you're hurt and step on it again.
function OnTrigger(other)
    if not other.hasPlayerController then return end
    if not Give(other) then return end

    local stats = other:GetScript("Player")
    stats:ShowMessage(message)
    stats:FlashPickup()

    -- The pickup is about to be destroyed, so play the sound on the player.
    local voice = other:GetComponents(Component.AudioSource)[1]
    if voice ~= nil and sound ~= "" then
        voice.soundFileName = sound
        voice:Play()
    end

    entity:Destroy()
end

function Update()
    if frames == "" or spriteFolder == "" then return end
    timer = timer - GameTime.deltaTime
    if timer > 0 then return end
    timer = frameTime
    index = index % #frames + 1
    Global.ShowFrame(entity.sprite, spriteFolder, frames:sub(index, index), false)
end
