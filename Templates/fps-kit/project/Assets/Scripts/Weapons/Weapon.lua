-- Scripts/Weapons/Weapon.lua
--
-- ONE JOB: one weapon, on screen and firing. Every prefab in
-- Prefabs/Weapons/In Hand runs this same script with different numbers, so a new
-- weapon is a duplicated prefab with new values in the inspector, not new code.
--
-- Why this is a script: no dispatch can fire a ray with random spread.
--
-- WeaponHolder.lua (on the player) decides when this weapon is up and when to
-- fire. This script does the rest:
--   TryFire  -> plays Fire Flipbook, and after Shot Delay fires the shot.
--   The shot -> takes ammo, then rays (Bullets, Melee) or a spawned Projectile.

public enum(Bullets, Melee, Projectile) attackType = Bullets
public int slot = 1                 -- which number key picks it
public string ownedVariable = ""    -- 1 = the player has it. Empty: always has it
public string ammoVariable = ""     -- Empty: needs no ammo
public int ammoPerShot = 1
public bool useAsFallback = true    -- may be picked automatically when ammo runs out

public number shotDelay = 0.1       -- seconds from the trigger to the shot
public number fireTime = 0.5        -- seconds before it can fire again
public FlipbookAsset readyFlipbook = nil
public FlipbookAsset fireFlipbook = nil
public string triggerSound = ""     -- played when the trigger is pulled (a charge-up)
public string fireSound = ""        -- played when the shot goes off
public string hitSound = ""         -- Melee only, played when it connects

-- Bullets and Melee.
public int pellets = 1
public number spread = 0            -- degrees either side of the crosshair
public int damageMin = 5
public int damageMax = 15
public number range = 2000
public number berserkMultiplier = 1 -- damage multiplier while Berserk isn't 0
public Entity puffPrefab = nil      -- spawned where a shot hits a wall
public Entity bloodPrefab = nil     -- spawned where a shot hits an enemy

-- Projectile.
public Entity projectilePrefab = nil

-- How it looks while moving.
public number raiseTime = 0.2       -- seconds to come up or go down
public number bobAmount = 0.018     -- sway while walking, as a share of the screen height

local owner = nil
local holder = 0        -- 0 = down, 1 = up
local wanted = 0        -- which way it's heading
local cooldown = 0
local shotTimer = -1    -- seconds until the queued shot, below 0 when there's none
local bobTime = 0

-- ── Used by WeaponHolder.lua ────────────────────────────────────────────────

public function SetOwner(newOwner) owner = newOwner end
public function Raise() wanted = 1 end
public function Lower() wanted = 0 end
public function IsDown() return holder <= 0 end

-- Up, and not in the middle of a shot.
public function IsReady() return holder >= 1 and cooldown <= 0 end

public function HasAmmo()
    return ammoVariable == "" or Game.GetVariable(ammoVariable) >= ammoPerShot
end

public function TryFire()
    if not IsReady() or not HasAmmo() then return end

    cooldown = fireTime
    shotTimer = shotDelay
    if triggerSound ~= "" then Audio.Play2D(triggerSound) end
    if fireFlipbook ~= "" then entity.flipbook:Play(fireFlipbook, true) end
end

-- ── The shot ────────────────────────────────────────────────────────────────

-- A direction from the view, turned by some degrees.
local function ViewDirection(yawOffset, pitchOffset)
    local yaw = mathT.DegToRad(owner.camera.yaw + yawOffset)
    local pitch = mathT.DegToRad(owner.camera.pitch + pitchOffset)
    return Vector3(math.sin(yaw) * math.cos(pitch), math.sin(pitch), math.cos(yaw) * math.cos(pitch))
end

-- Effect prefabs stand on their position, so lower them to centre them on the hit.
local function SpawnEffect(prefab, position)
    if prefab == nil then return end
    local effect = Game.CreateEntity(prefab)
    effect.transform.position = position - Vector3(0, effect.transform.scale.z * 0.5, 0)
end

local function FireRay()
    local direction = ViewDirection(mathT.RandomF(-spread, spread), mathT.RandomF(-spread, spread) * 0.3)

    -- RayType.Bullet also fires any Receive Ray (Bullet) events, so a wall can be
    -- a shootable switch. Only things with a solid collider stop a shot.
    local hit = Game.Raycast(owner.camera:GetEyePosition(), direction, range, owner.id, true, RayType.Bullet)
    if hit == nil then return false end

    local atHit = hit.position - direction
    if hit.entity ~= nil and hit.entity.monsterAI ~= nil then
        local damage = mathT.Random(damageMin, damageMax)
        if Game.GetVariable("Berserk") ~= 0 then damage = damage * berserkMultiplier end
        Global.ApplyDamage(hit.entity, damage, owner)
        SpawnEffect(hit.entity:HasTag("Bleeds") and bloodPrefab or puffPrefab, atHit)
    else
        SpawnEffect(puffPrefab, atHit)
    end
    return true
end

local function FireProjectile()
    local direction = ViewDirection(0, 0)
    local start = owner.camera:GetEyePosition() + direction * 6
    local shot = Game.CreateEntity(projectilePrefab)
    shot.transform.position = start - Vector3(0, shot.transform.scale.z * 0.5, 0)
    Global.LaunchOrders = Global.LaunchOrders or {}
    Global.LaunchOrders[shot.id] = { owner = owner, direction = direction }
end

local function Shoot()
    if ammoVariable ~= "" then
        Game.SetVariable(ammoVariable, Game.GetVariable(ammoVariable) - ammoPerShot)
    end
    if fireSound ~= "" then Audio.Play2D(fireSound) end

    -- Monsters that can hear this come looking for the player.
    Game.NoiseAlert(owner)

    if attackType == 2 then
        FireProjectile()
    else
        local connected = false
        for _ = 1, pellets do connected = FireRay() or connected end
        if connected and hitSound ~= "" then Audio.Play2D(hitSound) end
    end
end

-- ── Each frame ──────────────────────────────────────────────────────────────

function Update()
    local dt = GameTime.deltaTime

    -- Up and down: a menu or a lowered weapon means nothing is drawn.
    local step = raiseTime > 0 and dt / raiseTime or 1
    holder = wanted > holder and math.min(wanted, holder + step) or math.max(wanted, holder - step)
    entity.uiSprite.isActive = holder > 0 and GameTime.timeScale > 0

    cooldown = cooldown - dt
    if shotTimer >= 0 then
        shotTimer = shotTimer - dt
        if shotTimer < 0 and owner ~= nil then Shoot() end
    end

    -- Back to the resting frame once the firing frames are done.
    if cooldown <= 0 and readyFlipbook ~= "" and entity.flipbook.flipbookFileName ~= readyFlipbook then
        entity.flipbook:Play(readyFlipbook, true)
    end

    -- Sink while lowered, sway while walking. UI positions are in pixels, so
    -- everything is measured against the size the weapon currently has.
    local size = entity.uiTransform.resolvedSize
    local offset = Vector2(0, (1 - holder) * size.y)
    if owner ~= nil and owner.rigidbody ~= nil then
        local velocity = owner.rigidbody.velocity
        local speed = math.sqrt(velocity.x * velocity.x + velocity.z * velocity.z)
        if speed > 1 and cooldown <= 0 then bobTime = bobTime + dt * 7 end
        local amount = bobAmount * size.y * math.min(speed / 60, 1)
        offset = offset + Vector2(math.cos(bobTime) * amount, math.abs(math.sin(bobTime)) * amount)
    end
    entity.uiTransform.position = offset
end
