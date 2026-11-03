-- Scripts/Weapons/Weapon.lua
--
-- ONE JOB: one weapon, firing and moving. Every prefab in
-- Prefabs/Weapons/In Hand runs this same script with different numbers, so a new
-- weapon is a duplicated prefab with new values in the inspector, not new code.
--
-- Why this is a script: no dispatch can fire a ray with random spread.
--
-- WeaponHolder.lua (on the player) decides when this weapon is up and when to
-- fire. The picture is the HUD's one Weapon View image (WeaponView.lua), which
-- shows whichever weapon is up. This script does the rest:
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

-- Projectile.
public Entity projectilePrefab = nil

-- How it looks while moving.
public number raiseTime = 0.2       -- seconds to come up or go down
public number bobAmount = 0.035     -- sway while walking, as a share of the weapon's height

-- How it feels to fire. All 0 for a quiet weapon.
public number recoil = 0            -- degrees the view jumps up
public number shake = 0             -- how hard the view shakes
public number spriteKick = 0        -- how far the picture jumps down, as a share of its height
public number flashLight = 0        -- how much brighter the room gets for a moment
public Entity casingPrefab = nil    -- thrown out to the right with each shot

local owner = nil
local holder = 0        -- 0 = down, 1 = up
local wanted = 0        -- which way it's heading
local cooldown = 0
local shotTimer = -1    -- seconds until the queued shot, below 0 when there's none
local bobTime = 0
local showing = false   -- is this the weapon on the Weapon View?
local kickBack = 0      -- how far the picture is jumped down, 0 to 1 of spriteKick

-- The HUD's Weapon View script. Nil if this level has no HUD: the weapon still
-- works, it just can't be seen.
local function FindView()
    local viewEntity = Global.WeaponView
    if viewEntity == nil or not viewEntity.isValid then return nil end
    local view = viewEntity:GetScript("WeaponView")
    return view.isValid and view or nil
end

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

    local view = FindView()
    if view ~= nil and fireFlipbook ~= "" then view:PlayFlipbook(fireFlipbook) end
end

-- ── The shot ────────────────────────────────────────────────────────────────

-- A direction from the view, turned by some degrees.
local function ViewDirection(yawOffset, pitchOffset)
    local yaw = mathT.DegToRad(owner.camera.yaw + yawOffset)
    local pitch = mathT.DegToRad(owner.camera.pitch + pitchOffset)
    return Vector3(math.sin(yaw) * math.cos(pitch), math.sin(pitch), math.cos(yaw) * math.cos(pitch))
end

local function FireRay()
    local direction = ViewDirection(mathT.RandomF(-spread, spread), mathT.RandomF(-spread, spread) * 0.3)

    -- RayType.Bullet also fires any Receive Ray (Bullet) events, so a wall can be
    -- a shootable switch. Only things with a solid collider stop a shot.
    local eye = owner.camera:GetEyePosition()
    local hit = Game.Raycast(eye, direction, range, owner.id, true, RayType.Bullet)
    if hit == nil then return false end

    local bleeds = hit.entity ~= nil and hit.entity:HasTag("Bleeds")
    if hit.entity ~= nil and hit.entity.monsterAI ~= nil then
        local damage = mathT.Random(damageMin, damageMax)
        if Game.GetVariable("Berserk") ~= 0 then damage = damage * berserkMultiplier end
        Global.ApplyDamage(hit.entity, damage, owner)
    end

    -- Blood carries on the way the shot was going, so it stains what's behind.
    if Global.Fx ~= nil then
        if bleeds and Global.Fx.GoreOn() then
            Global.Fx.Blood(hit.position - direction, direction)
        elseif not bleeds then
            Global.Fx.BulletHit(eye, direction, hit)
        end
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

    -- How it feels.
    kickBack = 1
    if recoil > 0 and Global.KickView ~= nil then Global.KickView(recoil) end
    if shake > 0 and Global.ShakeView ~= nil then Global.ShakeView(shake) end
    local eye = owner.camera:GetEyePosition()
    if flashLight > 0 and Global.Fx ~= nil then Global.Fx.Flash(eye, flashLight) end
    if casingPrefab ~= nil then
        local yaw = mathT.DegToRad(owner.camera.yaw)
        local forward = Vector3(math.sin(yaw), 0, math.cos(yaw))
        local right = Vector3(forward.z, 0, -forward.x)
        Game.EmitParticles(casingPrefab, eye + forward * 5 + right * 2.5 - Vector3(0, 2.5, 0), right + Vector3(0, 0.6, 0))
    end

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

    -- Up and down.
    local step = raiseTime > 0 and dt / raiseTime or 1
    holder = wanted > holder and math.min(wanted, holder + step) or math.max(wanted, holder - step)

    cooldown = cooldown - dt
    kickBack = math.max(0, kickBack - dt * 7)
    if shotTimer >= 0 then
        shotTimer = shotTimer - dt
        if shotTimer < 0 and owner ~= nil then Shoot() end
    end

    -- Only the weapon that's up touches the picture. WeaponHolder lowers one
    -- completely before it raises the next, so they never overlap.
    local isUp = holder > 0
    if not isUp and not showing then return end
    showing = isUp

    local view = FindView()
    if view == nil then return end

    -- A menu or a lowered weapon means nothing is drawn.
    if not isUp or GameTime.timeScale <= 0 then
        view:Hide()
        return
    end

    -- Back to the resting frame once the firing frames are done. This is also
    -- what puts this weapon's picture up as it starts to rise.
    if cooldown <= 0 and readyFlipbook ~= "" and not view:IsShowingFlipbook(readyFlipbook) then
        view:PlayFlipbook(readyFlipbook)
    end

    -- Sink while lowered, sway while walking. UI positions are in pixels, so
    -- everything is measured against the size the picture currently has.
    local size = view:GetSize()
    local offset = Vector2(0, (1 - holder + kickBack * spriteKick) * size.y)
    if owner ~= nil and owner.rigidbody ~= nil then
        local velocity = owner.rigidbody.velocity
        local speed = math.sqrt(velocity.x * velocity.x + velocity.z * velocity.z)
        if speed > 1 and cooldown <= 0 then bobTime = bobTime + dt * 11 end
        local amount = bobAmount * size.y * math.min(speed / 80, 1)
        offset = offset + Vector2(math.cos(bobTime) * amount, math.abs(math.sin(bobTime)) * amount)
    end
    view:ShowAt(offset)
end
