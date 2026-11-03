-- Scripts/Player/QuickActions.lua
--
-- ONE JOB: the two things you can do whatever gun you're holding: kick and throw a
-- grenade. They're drawn on the HUD's own Action View picture, over the weapon.
--
-- Why this is a script: no dispatch can aim where the player is looking, or vary the
-- damage from one kick to the next.
--
-- Change the keys and numbers below in the inspector, on the Player prefab. To make
-- something kickable without hurting it, give it the Kickable tag and a Rigidbody:
-- that's how the Explosive Barrel works.

public Key kickKey = Key.F
public Key grenadeKey = Key.G

-- Kick.
public int kickDamageMin = 6
public int kickDamageMax = 14
public number berserkMultiplier = 5     -- damage multiplier while Berserk isn't 0
public number kickRange = 24
public number kickForce = 170           -- how hard it shoves, in units per second
public number kickLift = 30             -- and how much of that goes upward
public number kickTime = 0.5            -- seconds before you can kick again
public number kickHitDelay = 0.14       -- seconds from the key to the boot landing
public FlipbookAsset kickFlipbook = nil
public number kickFlipbookSeconds = 0.36
public string kickSwingSound = "Sounds/FX/kick swing.wav"
public string kickHitSound = "Sounds/FX/kick hit.wav"
public string kickFleshSound = "Sounds/FX/blood hit.wav"

-- Grenade.
public Entity grenadePrefab = nil
public string ammoVariable = "Grenades"
public number throwSpeed = 170
public number throwLift = 40            -- extra speed upward, for the arc
public number throwTime = 0.7           -- seconds before you can throw again
public number throwReleaseDelay = 0.16  -- seconds from the key to it leaving your hand
public FlipbookAsset throwFlipbook = nil
public number throwFlipbookSeconds = 0.42
public string throwSound = "Sounds/FX/grenade throw.wav"
public string pinSound = "Sounds/FX/grenade pin.wav"
public string noneSound = "Sounds/Player/no way.wav"

local kickWait = 0
local kickLands = -1
local throwWait = 0
local throwLeaves = -1

local function ViewDirection(yawOffset, pitchOffset)
    local yaw = mathT.DegToRad(entity.camera.yaw + yawOffset)
    local pitch = mathT.DegToRad(entity.camera.pitch + pitchOffset)
    return Vector3(math.sin(yaw) * math.cos(pitch), math.sin(pitch), math.cos(yaw) * math.cos(pitch))
end

local function ShowAction(flipbook, seconds)
    local view = Global.ActionView
    if view == nil or not view.isValid or flipbook == nil then return end
    local script = view:GetScript("ActionView")
    if script.isValid then script:Play(flipbook, seconds) end
end

-- Straight ahead first, then a little to each side and lower, because a kick is wider
-- than a bullet and what you're kicking is usually below your eye.
local KICK_FAN = { { 0, 0 }, { -14, 0 }, { 14, 0 }, { 0, -16 }, { -14, -16 }, { 14, -16 } }

local function FindKickTarget(eye)
    local wallHit = nil
    for index, angles in ipairs(KICK_FAN) do
        local direction = ViewDirection(angles[1], angles[2])
        -- Only the straight one counts as a shot, for shootable switches.
        local rayType = index == 1 and RayType.Bullet or RayType.General
        local hit = Game.Raycast(eye, direction, kickRange, entity.id, true, rayType)
        if hit ~= nil then
            if hit.entity ~= nil then return hit, direction end
            wallHit = wallHit or { hit = hit, direction = direction }
        end
    end
    if wallHit ~= nil then return wallHit.hit, wallHit.direction end
    return nil, nil
end

local function LandKick()
    local eye = entity.camera:GetEyePosition()
    local hit, direction = FindKickTarget(eye)

    Game.NoiseAlert(entity)
    if hit == nil then return end

    if Global.KickView ~= nil then Global.KickView(0.7) end
    if Global.ShakeView ~= nil then Global.ShakeView(0.5) end

    local target = hit.entity
    if target == nil then
        Audio.Play2D(kickHitSound, 0.7)
        if Global.Fx ~= nil then Global.Fx.BulletHit(eye, direction, hit) end
        return
    end

    local kickable = target:HasTag("Kickable")
    local body = target.rigidbody
    if body ~= nil then
        -- A kickable thing stands still until something kicks it.
        if kickable then body.isStatic = false end
        if not body.isStatic then
            local flat = Vector3(direction.x, 0, direction.z)
            local shove = mathT.Vector3Normalize(flat) * kickForce + Vector3(0, kickLift, 0)
            body.velocity = body.velocity + shove
        end
    end

    if kickable then
        Audio.Play2D(kickHitSound)
        return
    end

    local damage = mathT.Random(kickDamageMin, kickDamageMax)
    if Game.GetVariable("Berserk") ~= 0 then damage = damage * berserkMultiplier end
    Global.ApplyDamage(target, damage, entity)

    if target:HasTag("Bleeds") then
        Audio.Play2D(kickFleshSound)
        if Global.Fx ~= nil then Global.Fx.Blood(hit.position - direction, direction, 0.6) end
    else
        Audio.Play2D(kickHitSound)
    end
end

local function ThrowGrenade()
    if grenadePrefab == nil then return end

    local direction = ViewDirection(0, 0)
    local grenade = Game.CreateEntity(grenadePrefab)
    local start = entity.camera:GetEyePosition() + direction * 5
    grenade.transform.position = start - Vector3(0, grenade.transform.scale.z * 0.5, 0)

    Global.LaunchOrders = Global.LaunchOrders or {}
    Global.LaunchOrders[grenade.id] = {
        owner = entity,
        velocity = direction * throwSpeed + Vector3(0, throwLift, 0) + entity.rigidbody.velocity * 0.5,
    }
    Game.SetVariable(ammoVariable, Game.GetVariable(ammoVariable) - 1)
    Audio.Play2D(throwSound)
end

function Update()
    local dt = GameTime.deltaTime
    kickWait = kickWait - dt
    throwWait = throwWait - dt

    -- What's already under way finishes, unless you've died.
    local alive = Game.GetVariable("Health") > 0
    if kickLands >= 0 then
        kickLands = kickLands - dt
        if kickLands < 0 and alive then LandKick() end
    end
    if throwLeaves >= 0 then
        throwLeaves = throwLeaves - dt
        if throwLeaves < 0 and alive then ThrowGrenade() end
    end

    if GameTime.timeScale == 0 or not alive then return end

    if Input.GetKeyDown(kickKey) and kickWait <= 0 then
        kickWait = kickTime
        kickLands = kickHitDelay
        Audio.Play2D(kickSwingSound)
        ShowAction(kickFlipbook, kickFlipbookSeconds)
    end

    if Input.GetKeyDown(grenadeKey) and throwWait <= 0 then
        if Game.GetVariable(ammoVariable) > 0 then
            throwWait = throwTime
            throwLeaves = throwReleaseDelay
            Audio.Play2D(pinSound)
            ShowAction(throwFlipbook, throwFlipbookSeconds)
        else
            throwWait = 0.4
            Audio.Play2D(noneSound)
        end
    end
end
