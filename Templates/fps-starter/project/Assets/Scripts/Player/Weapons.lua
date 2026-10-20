-- Scripts/Player/Weapons.lua
--
-- The player's guns: switching, firing hitscan bullets, ammo, and drawing
-- the gun at the bottom of the screen.
--
-- Controls: left mouse button (or Left Ctrl) fires, 1 and 2 pick a weapon.
--
-- The gun is a UI Image ("HUD Weapon") with a second one on top for the
-- muzzle flash ("HUD Muzzle Flash"). Both are moved and resized every frame
-- to match the window, the way a classic 320x200 game would draw them.

public number range = 2000
public Entity puffPrefab = nil   -- spawned where a bullet hits a wall
public Entity bloodPrefab = nil  -- spawned where a bullet hits an enemy

-- Every weapon the game knows about. To add one: export its frames into a
-- folder, add an entry here, and give the player a pickup for it.
--
-- fire: the frames played for one shot, as { image, tics, action }. A tic
-- is 1/35 s. The "shoot" action fires the bullets on that frame.
-- flash: muzzle flash frames, started together with "shoot".
-- canvas: where the frame images sit on a 320x200 screen (x, y, width,
-- height), so the gun lines up the same way on any window size.
local WEAPONS = {
    pistol = {
        slot = 1, label = "PISTOL", folder = "Sprites/Weapons/Pistol",
        ammo = "bullets", ammoPerShot = 1, pellets = 1,
        spread = 0, refireSpread = 3, damageMin = 5, damageMax = 15,
        sound = "Sounds/pistol",
        fire = { { "A", 4 }, { "B", 6, "shoot" }, { "C", 4 }, { "B", 5 } },
        flash = { { "FlashA", 7 } },
        canvas = { 124, 100, 86, 100 },
    },
    shotgun = {
        slot = 2, label = "SHOTGUN", folder = "Sprites/Weapons/Shotgun",
        ammo = "shells", ammoPerShot = 1, pellets = 7,
        spread = 5.5, refireSpread = 5.5, damageMin = 5, damageMax = 15,
        sound = "Sounds/shotgun",
        fire = { { "A", 3 }, { "A", 7, "shoot" }, { "B", 5 }, { "C", 5 }, { "D", 4 },
                 { "C", 5 }, { "B", 5 }, { "A", 3 }, { "A", 7 } },
        flash = { { "FlashA", 4 }, { "FlashB", 3 } },
        canvas = { 45, 57, 145, 143 },
    },
}
local SLOT_KEYS = { [Key.Alpha1] = "pistol", [Key.Alpha2] = "shotgun" }
for name, weapon in pairs(WEAPONS) do weapon.id = name end

local SWITCH_TIME = 0.2   -- seconds to lower or raise a gun
local LOWERED = 120       -- how far a lowered gun drops, in 320x200 pixels

local player              -- this entity's Player script
local gunSound            -- the AudioSource guns play on
local weaponImage, flashImage, screen, ammoText

local current             -- the weapon table in use
local pending             -- a weapon waiting to be raised after a switch
local lowerAmount = LOWERED  -- 0 = fully up, LOWERED = fully down
local frames, frameIndex, frameTimer = nil, 0, 0   -- firing animation
local flashFrames, flashIndex, flashTimer = nil, 0, 0
local refiring = false    -- holding the trigger: later shots spread more
local bobTime = 0

-- ── Helpers ────────────────────────────────────────────────────────────

local function Inventory() return Global.inventory end

local function Direction(yaw, pitch)
    local y, p = mathT.DegToRad(yaw), mathT.DegToRad(pitch)
    return Vector3(mathT.Sin(y) * mathT.Cos(p), mathT.Sin(p), mathT.Cos(y) * mathT.Cos(p))
end

local function HasAmmoFor(weapon)
    return Inventory()[weapon.ammo] >= weapon.ammoPerShot
end

local function SpawnEffect(prefab, position)
    if prefab == nil then return end
    local effect = Game.CreateEntity(prefab)
    -- Sprites stand on their position; lower it so the effect is centred.
    effect.transform.position = position - Vector3(0, effect.transform.scale.z * 0.5, 0)
end

-- One bullet: a ray from the eye, slightly off-centre by `spread` degrees.
local function FireBullet(weapon, spread)
    local camera = entity.camera
    local yaw = camera.yaw + mathT.RandomF(-spread, spread)
    local pitch = camera.pitch + mathT.RandomF(-spread, spread) * 0.3
    local direction = Direction(yaw, pitch)
    local eye = camera:GetEyePosition()

    -- requireCollider = true: only solid things stop bullets, so pickups
    -- and decorations without a collider are shot straight through.
    local hit = Game.Raycast(eye, direction, range, entity.id, true)
    if hit == nil then return end

    local backOff = hit.position - direction * 0.5
    if hit.entity ~= nil and Global.IsDamageable(hit.entity) then
        Global.Damage(hit.entity, mathT.Random(weapon.damageMin, weapon.damageMax), entity)
        SpawnEffect(hit.entity:HasScriptNamed("Enemy") and bloodPrefab or puffPrefab, backOff)
    else
        SpawnEffect(puffPrefab, backOff)
    end
end

local function Shoot(weapon)
    local inv = Inventory()
    inv[weapon.ammo] = inv[weapon.ammo] - weapon.ammoPerShot

    local spread = refiring and weapon.refireSpread or weapon.spread
    for _ = 1, weapon.pellets do FireBullet(weapon, spread) end

    gunSound.soundFileName = weapon.sound
    gunSound:Play()
    Global.MakeNoise(entity.transform.position)

    flashFrames, flashIndex, flashTimer = weapon.flash, 1, Global.Tics(weapon.flash[1][2])
end

-- Picks `name` as the next weapon; the current one lowers first.
local function SwitchTo(name)
    local weapon = WEAPONS[name]
    if weapon == nil or weapon == current or not Inventory().weapons[name] then return end
    pending = weapon
end

-- Out of ammo: change to any weapon that still has some.
local function SwitchToLoadedWeapon()
    for name, weapon in pairs(WEAPONS) do
        if Inventory().weapons[name] and HasAmmoFor(weapon) then
            SwitchTo(name)
            return
        end
    end
end

-- ── Called by pickups ──────────────────────────────────────────────────

-- Adds ammo up to the carrying limit. Returns false when already full.
function AddAmmo(self, kind, amount)
    local inv = Inventory()
    local limitKey = kind == "bullets" and "maxBullets" or "maxShells"
    if inv[kind] >= inv[limitKey] then return false end
    inv[kind] = math.min(inv[limitKey], inv[kind] + amount)
    return true
end

-- Gives a weapon (switching to it if it's new) and some ammo for it.
function GiveWeapon(self, name, ammoAmount)
    local weapon = WEAPONS[name]
    if weapon == nil then return false end

    local inv = Inventory()
    local isNew = not inv.weapons[name]
    inv.weapons[name] = true
    local gotAmmo = AddAmmo(nil, weapon.ammo, ammoAmount)

    if isNew then SwitchTo(name) end
    return isNew or gotAmmo
end

-- ── Engine callbacks ───────────────────────────────────────────────────

function Start()
    player = entity:GetScript("Player")
    gunSound = entity:GetComponents(Component.AudioSource)[2]

    weaponImage = Game.FindEntity("HUD Weapon")
    flashImage = Game.FindEntity("HUD Muzzle Flash")
    ammoText = Game.FindEntity("HUD Ammo")
    -- A full-screen UI element; its size tells us how big the window is.
    screen = Game.FindEntity("HUD Hurt Flash")

    current = WEAPONS[Inventory().current] or WEAPONS.pistol
    lowerAmount = LOWERED -- raise the gun at the start of the level
end

local function UpdateAnimation(dt)
    -- Muzzle flash runs on its own timer.
    if flashFrames ~= nil then
        flashTimer = flashTimer - dt
        while flashFrames ~= nil and flashTimer <= 0 do
            flashIndex = flashIndex + 1
            if flashIndex > #flashFrames then
                flashFrames = nil
            else
                flashTimer = flashTimer + Global.Tics(flashFrames[flashIndex][2])
            end
        end
    end

    if frames == nil then return end
    frameTimer = frameTimer - dt
    while frames ~= nil and frameTimer <= 0 do
        frameIndex = frameIndex + 1
        if frameIndex > #frames then
            frames = nil -- shot finished, back to ready
            -- Still holding the trigger? The next shot is less accurate.
            refiring = true
        else
            local frame = frames[frameIndex]
            frameTimer = frameTimer + Global.Tics(frame[2])
            if frame[3] == "shoot" then Shoot(current) end
        end
    end
end

local function UpdateInput()
    for key, name in pairs(SLOT_KEYS) do
        if Input.GetKeyDown(key) then SwitchTo(name) end
    end

    local trigger = Input.GetMouseButton(Input.MouseLeft) or Input.GetKey(Key.LCtrl)
    if not trigger then refiring = false end

    -- Start a shot only when the gun is up and the last one has finished.
    if trigger and frames == nil and pending == nil and lowerAmount == 0 then
        if HasAmmoFor(current) then
            frames, frameIndex, frameTimer = current.fire, 1, Global.Tics(current.fire[1][2])
            if current.fire[1][3] == "shoot" then Shoot(current) end
        else
            SwitchToLoadedWeapon()
        end
    end
end

local function UpdateSwitching(dt)
    local speed = LOWERED / SWITCH_TIME * dt
    if pending ~= nil and frames == nil then
        lowerAmount = math.min(LOWERED, lowerAmount + speed)
        if lowerAmount >= LOWERED then
            current = pending
            pending = nil
            Inventory().current = current.id
        end
    else
        lowerAmount = math.max(0, lowerAmount - speed)
    end
end

-- Places an image so a `canvas` (in 320x200 pixels) lands in the same spot
-- on the real window, plus an offset (also in 320x200 pixels).
local function PlaceImage(image, canvas, offsetX, offsetY)
    local size = screen.uiTransform.resolvedSize
    if size.y <= 0 then return end

    -- Old games stretched 320x200 to a 4:3 screen, so their pixels are a
    -- little taller than wide: 1.2 times.
    local sy = size.y / 200
    local sx = sy / 1.2
    local ui = image.uiTransform
    ui.anchorMin = Vector2(0, 0)
    ui.anchorMax = Vector2(0, 0)
    ui.pivot = Vector2(0, 0)
    ui.position = Vector2(size.x * 0.5 + (canvas[1] - 160 + offsetX) * sx, (canvas[2] + offsetY) * sy)
    ui.scale = Vector2(canvas[3] * sx, canvas[4] * sy)
end

local function UpdateImages(dt)
    if weaponImage == nil or flashImage == nil or screen == nil then return end

    -- Bob the gun while walking.
    local v = entity.rigidbody.velocity
    local speed = Vector2(v.x, v.z).length
    local bob = math.min(1, speed / 46)
    bobTime = bobTime + dt * 7
    local bobX = mathT.Cos(bobTime) * 6 * bob
    local bobY = math.abs(mathT.Sin(bobTime)) * 5 * bob
    if frames ~= nil then bobX, bobY = 0, 0 end

    local frameName = frames ~= nil and frames[frameIndex][1] or "A"
    weaponImage.uiSprite.textureIndex = current.folder .. "/" .. frameName .. ".png"
    PlaceImage(weaponImage, current.canvas, bobX, bobY + lowerAmount)

    local showFlash = flashFrames ~= nil
    flashImage.uiSprite.isActive = showFlash
    if showFlash then
        flashImage.uiSprite.textureIndex = current.folder .. "/" .. flashFrames[flashIndex][1] .. ".png"
        PlaceImage(flashImage, current.canvas, bobX, bobY + lowerAmount)
    end

    if ammoText ~= nil then
        ammoText.uiText.text = current.label .. "  " .. Inventory()[current.ammo]
    end
end

function Update()
    local dt = GameTime.deltaTime
    local busy = player.isValid and (player.dead or player.won)

    if busy then
        frames, flashFrames = nil, nil
        lowerAmount = math.min(LOWERED, lowerAmount + LOWERED * dt)
    else
        UpdateInput()
        UpdateAnimation(dt)
        UpdateSwitching(dt)
    end

    UpdateImages(dt)
end
