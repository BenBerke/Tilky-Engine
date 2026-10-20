-- Scripts/Player/Player.lua
--
-- The player's health, armor, keys, HUD text and death.
--
-- Health, armor and ammo carry over to the next level through
-- Global.inventory (the Global table is the only thing that survives
-- Game.LoadLevel). Keys belong to one level and start empty in each.
--
-- Other scripts talk to this one through its functions:
--   player:GetScript("Player"):TakeDamage(10, enemyEntity)
--   player:GetScript("Player"):Heal(25, 100)
-- Functions called that way take `self` first (see the Script wiki page).

public int startHealth = 100
public number hurtFlashTime = 0.25
public number pickupFlashTime = 0.15
public number messageTime = 3
public string painSound = "Sounds/player_pain"
public string deathSound = "Sounds/player_death"
-- Where "play again" goes after the last level.
public string firstLevel = "Level 1 - Outpost"

-- Readable by other scripts (no `local`).
dead = false
won = false

local keys = {}
local voice            -- the AudioSource used for the player's own sounds
local hud = {}         -- HUD entities, found by name in Start
local hurtTimer, pickupTimer, messageTimer = 0, 0, 0

-- A fresh inventory, used at the very start of the game.
local function NewInventory()
    return {
        health = startHealth,
        armor = 0,
        bullets = 50, maxBullets = 200,
        shells = 0, maxShells = 50,
        weapons = { pistol = true },
        current = "pistol",
    }
end

local function CopyTable(t)
    local copy = {}
    for k, v in pairs(t) do
        copy[k] = type(v) == "table" and CopyTable(v) or v
    end
    return copy
end

local function PlaySound(path)
    if voice == nil or path == "" then return end
    voice.soundFileName = path
    voice:Play()
end

local function SetText(entityHandle, text)
    if entityHandle ~= nil and entityHandle.isValid then entityHandle.uiText.text = text end
end

local function SetShown(entityHandle, shown)
    if entityHandle ~= nil and entityHandle.isValid then entityHandle.uiSprite.isActive = shown end
end

function Start()
    -- First level of a new game: make an inventory. Later levels reuse the
    -- one the previous level left behind.
    if Global.inventory == nil then Global.inventory = NewInventory() end
    -- Remember how we came into this level, so dying can restart it fairly.
    Global.levelStartInventory = CopyTable(Global.inventory)
    Global.kills = Global.kills or 0
    Global.levelStartKills = Global.kills

    voice = entity:GetComponents(Component.AudioSource)[1]

    -- The HUD is made of UI entities in the level. Finding them by name keeps
    -- the Player prefab free of level-specific references.
    hud.health = Game.FindEntity("HUD Health")
    hud.armor = Game.FindEntity("HUD Armor")
    hud.message = Game.FindEntity("HUD Message")
    hud.center = Game.FindEntity("HUD Center Text")
    hud.blueKey = Game.FindEntity("HUD Blue Key")
    hud.redKey = Game.FindEntity("HUD Red Key")
    hud.hurt = Game.FindEntity("HUD Hurt Flash")
    hud.pickup = Game.FindEntity("HUD Pickup Flash")

    SetShown(hud.blueKey, false)
    SetShown(hud.redKey, false)
    SetShown(hud.hurt, false)
    SetShown(hud.pickup, false)
    SetText(hud.center, "")

    ShowMessage(nil, Game.levelName)
end

-- ── Called by other scripts ────────────────────────────────────────────

function TakeDamage(self, amount, source)
    if dead or won then return end
    local inv = Global.inventory

    -- Armor soaks up a third of the damage while it lasts.
    local saved = math.min(inv.armor, math.floor(amount / 3))
    inv.armor = inv.armor - saved
    inv.health = inv.health - (amount - saved)

    hurtTimer = hurtFlashTime
    if inv.health <= 0 then
        inv.health = 0
        Die()
    else
        PlaySound(painSound)
    end
end

-- Adds health up to `limit`. Returns false (and changes nothing) when the
-- player is already at the limit, so the pickup stays on the floor.
function Heal(self, amount, limit)
    local inv = Global.inventory
    if inv.health >= limit then return false end
    inv.health = math.min(limit, inv.health + amount)
    return true
end

function AddArmor(self, amount, limit)
    local inv = Global.inventory
    if inv.armor >= limit then return false end
    inv.armor = math.min(limit, inv.armor + amount)
    return true
end

function GiveKey(self, color)
    keys[color] = true
    if color == "blue" then SetShown(hud.blueKey, true) end
    if color == "red" then SetShown(hud.redKey, true) end
    return true
end

function HasKey(self, color)
    return keys[color] == true
end

function ShowMessage(self, text)
    SetText(hud.message, text)
    messageTimer = messageTime
end

-- Gold flash on the screen when something is picked up.
function FlashPickup(self)
    pickupTimer = pickupFlashTime
end

-- The last exit calls this instead of loading another level.
function Win(self)
    if dead then return end
    won = true
    Freeze()
    SetText(hud.center, "YOU WIN!\nKills: " .. (Global.kills or 0) .. "\n\nPress E to play again")
end

-- ── Internal ───────────────────────────────────────────────────────────

-- Stops the player moving and looking around. Switching the Player
-- Controller off would also do that, but it would drop the camera to the
-- player's feet, so the controller stays on with everything set to zero.
-- Loading a level puts the saved values back.
function Freeze()
    local pc = entity.playerController
    pc.speed, pc.runningSpeed, pc.jumpPower = 0, 0, 0
    pc.sensitivityX, pc.sensitivityY = 0, 0
end

function Die()
    dead = true
    PlaySound(deathSound)
    -- Stop walking and looking around; Update sinks the camera to the floor.
    Freeze()
    SetText(hud.center, "YOU DIED\n\nPress E to try again")
end

function Update()
    local dt = GameTime.deltaTime
    local inv = Global.inventory

    SetText(hud.health, "HEALTH " .. inv.health .. "%")
    SetText(hud.armor, "ARMOR " .. inv.armor .. "%")

    hurtTimer = math.max(0, hurtTimer - dt)
    pickupTimer = math.max(0, pickupTimer - dt)
    SetShown(hud.hurt, hurtTimer > 0 or dead)
    SetShown(hud.pickup, pickupTimer > 0)

    if messageTimer > 0 then
        messageTimer = messageTimer - dt
        if messageTimer <= 0 then SetText(hud.message, "") end
    end

    if dead then
        local pc = entity.playerController
        pc.eyeHeight = math.max(2, pc.eyeHeight - 20 * dt)

        if Input.GetKeyDown(Key.E) or Input.GetMouseButtonDown(Input.MouseLeft) then
            -- Restart this level with what we had when we arrived.
            Global.inventory = CopyTable(Global.levelStartInventory)
            Global.kills = Global.levelStartKills
            Game.LoadLevel(Game.levelName)
        end
    elseif won then
        if Input.GetKeyDown(Key.E) then
            Global.inventory = nil
            Global.kills = 0
            Game.LoadLevel(firstLevel)
        end
    end
end
