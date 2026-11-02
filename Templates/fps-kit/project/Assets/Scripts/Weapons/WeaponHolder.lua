-- Scripts/Weapons/WeaponHolder.lua
--
-- ONE JOB: looks after the weapons the player carries. It picks which one is in
-- the player's hands, and asks that one to fire. Firing itself is in Weapon.lua.
--
-- ADDING A WEAPON TO THE GAME, WITHOUT CODE:
--   1. Duplicate a prefab in Prefabs/Weapons/In Hand and change its numbers.
--   2. Drag the new prefab onto a free Weapon field below, on the Player prefab.
--   3. Duplicate a pickup in Prefabs/Weapons/Pickups so that it sets the new
--      weapon's "owned" variable to 1.
--
-- The player owns a weapon when its Owned Variable isn't 0. Pickups set that
-- variable with an ordinary dispatch, and this script notices and switches to it.

public Entity weapon1 = nil
public Entity weapon2 = nil
public Entity weapon3 = nil
public Entity weapon4 = nil
public Entity weapon5 = nil
public Entity weapon6 = nil
public Entity weapon7 = nil
public Entity weapon8 = nil
public Entity weapon9 = nil
public Entity weapon10 = nil
public Entity weapon11 = nil
public Entity weapon12 = nil

public Key alternateFireKey = Key.LCtrl
public string healthVariable = "Health"
public string shownAmmoVariable = "CurrentAmmo"   -- what the HUD's big ammo number shows

-- Number row keys, 1 to 9, indexed by slot.
local SLOT_KEYS = { Key.Alpha1, Key.Alpha2, Key.Alpha3, Key.Alpha4, Key.Alpha5,
                    Key.Alpha6, Key.Alpha7, Key.Alpha8, Key.Alpha9 }

local spawned = {}      -- the weapon entities, until their scripts are running
local weapons = {}      -- every weapon, as its Weapon.lua script
local wasOwned = {}     -- weapon script -> did the player own it last frame?
local current = nil     -- the weapon in the player's hands
local pending = nil     -- the one to bring up once `current` has gone down
local started = false
local waitForRelease = true  -- a click that closed a menu or started the level mustn't also fire

-- ── Helpers ─────────────────────────────────────────────────────────────────

local function Owns(weapon)
    local variable = weapon.ownedVariable
    return variable == "" or Game.GetVariable(variable) ~= 0
end

-- Starts changing to `weapon`: the current one lowers, then that one rises.
local function SwitchTo(weapon)
    if weapon == current and pending == nil then return end
    pending = weapon
    if current ~= nil then current:Lower() end
end

-- The next weapon the player owns in a slot, so pressing 3 twice goes from the
-- shotgun to the super shotgun.
local function NextInSlot(slot)
    local afterCurrent, first = nil, nil
    local passedCurrent = false
    for _, weapon in ipairs(weapons) do
        if weapon == current then passedCurrent = true end
        if weapon.slot == slot and Owns(weapon) then
            first = first or weapon
            if passedCurrent and weapon ~= current and afterCurrent == nil then afterCurrent = weapon end
        end
    end
    return afterCurrent or first
end

-- The best weapon that still has ammo: the last in the list wins, so put strong
-- weapons in the higher fields.
local function BestWeaponWithAmmo()
    local best = nil
    for _, weapon in ipairs(weapons) do
        if weapon.useAsFallback and Owns(weapon) and weapon:HasAmmo() then best = weapon end
    end
    return best
end

-- ── Callbacks ───────────────────────────────────────────────────────────────

function Start()
    -- Prefab fields hold the prefab itself. Spawning makes the real UI entity.
    for _, prefab in ipairs({ weapon1, weapon2, weapon3, weapon4, weapon5, weapon6,
                              weapon7, weapon8, weapon9, weapon10, weapon11, weapon12 }) do
        if prefab ~= nil then
            table.insert(spawned, Game.CreateEntity(prefab))
        end
    end
end

function Update()
    -- A new entity's scripts start a frame after it is created, and can't be
    -- reached until then. Wait until every weapon's script is running.
    if not started then
        for _, weaponEntity in ipairs(spawned) do
            if not weaponEntity:GetScript("Weapon").isValid then return end
        end
        for _, weaponEntity in ipairs(spawned) do
            table.insert(weapons, weaponEntity:GetScript("Weapon"))
        end
        started = true
        for _, weapon in ipairs(weapons) do
            weapon:SetOwner(entity)
            wasOwned[weapon] = Owns(weapon)
        end
        SwitchTo(BestWeaponWithAmmo())
    end

    -- Dead, or a menu is open: leave the weapon alone.
    if GameTime.timeScale == 0 or Game.GetVariable(healthVariable) <= 0 then
        waitForRelease = true
        if current ~= nil and Game.GetVariable(healthVariable) <= 0 then current:Lower() end
        return
    end

    -- Picked up something new? Change to it, as Doom does.
    for _, weapon in ipairs(weapons) do
        local owns = Owns(weapon)
        if owns and not wasOwned[weapon] then
            SwitchTo(weapon)
            Global.FaceGrin = true -- Face.lua: a new gun deserves a grin
        end
        wasOwned[weapon] = owns
    end

    -- Number keys pick a slot.
    for slot, key in ipairs(SLOT_KEYS) do
        if Input.GetKeyDown(key) then
            local weapon = NextInSlot(slot)
            if weapon ~= nil then SwitchTo(weapon) end
        end
    end

    -- Once the old weapon is down, the new one comes up.
    if pending ~= nil and (current == nil or current:IsDown()) then
        current = pending
        pending = nil
        current:Raise()
    end

    if current == nil then return end

    if current.ammoVariable ~= "" then
        Game.SetVariable(shownAmmoVariable, Game.GetVariable(current.ammoVariable))
    else
        Game.SetVariable(shownAmmoVariable, 0)
    end

    -- Hold to keep firing. Out of ammo: change to something that has some.
    local triggerHeld = Input.GetMouseButton(Input.MouseLeft) or Input.GetKey(alternateFireKey)
    if not triggerHeld then waitForRelease = false end
    if triggerHeld and not waitForRelease and pending == nil then
        if current:HasAmmo() then
            current:TryFire()
        elseif current:IsReady() then
            SwitchTo(BestWeaponWithAmmo())
        end
    end
end
