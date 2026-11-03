-- Scripts/Player/ViewFeel.lua
--
-- ONE JOB: how the view feels on the move: it bobs while you walk, dips when you land
-- from a fall, and the player grunts on a hard landing. Jumping plays its sound here too.
--
-- Why this is a script: no dispatch can move the eye height by a wave every frame.
--
-- It changes the Player Controller's Eye Height, and for a moment the camera's aim. Set
-- Bob Amount to 0 to turn the bobbing off, or remove this script for a perfectly steady view.
--
-- It also shakes and kicks the view for everything else:
--     Global.ShakeView(strength)     -- explosions, big guns. 1 is a good thump.
--     Global.KickView(degrees)       -- the view jumps up, then settles back. Recoil.

public number bobAmount = 1.2         -- how far the view moves, in world units
public number bobSpeed = 11           -- Doom's pace of about two steps a second
public number fullBobAtSpeed = 60     -- moving this fast bobs the full amount
public number hardLandingSpeed = 60   -- falling this fast (units per second) grunts
public number landingDip = 0.04       -- world units of dip per unit per second of fall
public number maxDip = 4
public string landSound = "Sounds/Player/oof.wav"
public string jumpSound = "Sounds/Player/jump.wav"
public number shakeDegrees = 1.4      -- how far a strength 1 shake moves the view
public number shakeFade = 2.6         -- how much strength it loses per second
public number kickRecovery = 7        -- how fast a recoil kick settles back

local DIP_RECOVERY = 14 -- world units per second, back up to normal

local baseEyeHeight = nil
local bobTime = 0
local bob = 0
local dip = 0
local wasGrounded = true
local lastFallSpeed = 0
local shakeStrength = 0
local kickUp = 0
local appliedYaw = 0    -- what's on the camera right now, to take off before mouse look is read
local appliedPitch = 0

function Global.ShakeView(strength)
    shakeStrength = math.min(2, math.max(shakeStrength, strength))
end

function Global.KickView(degrees)
    kickUp = math.min(8, kickUp + degrees)
end

local function UpdateViewOffsets(dt)
    local camera = entity.camera
    camera.yaw = camera.yaw - appliedYaw
    camera.pitch = camera.pitch - appliedPitch

    shakeStrength = math.max(0, shakeStrength - shakeFade * dt)
    kickUp = kickUp * math.max(0, 1 - kickRecovery * dt)

    local amount = shakeStrength * shakeDegrees
    appliedYaw = mathT.RandomF(-amount, amount)
    appliedPitch = mathT.RandomF(-amount, amount) * 0.7 + kickUp

    camera.yaw = camera.yaw + appliedYaw
    camera.pitch = camera.pitch + appliedPitch
end

function Update()
    local controller = entity.playerController
    if baseEyeHeight == nil then baseEyeHeight = controller.eyeHeight end

    if GameTime.deltaTime > 0 then UpdateViewOffsets(GameTime.deltaTime) end

    -- The Die dispatch lowers the eye by itself, so step aside.
    if Game.GetVariable("Health") <= 0 then return end

    local body = entity.rigidbody
    local velocity = body.velocity
    local dt = GameTime.deltaTime

    -- Bob only with feet on the ground, and ease in and out with speed.
    local speed = math.sqrt(velocity.x * velocity.x + velocity.z * velocity.z)
    local wanted = body.isGrounded and math.min(speed / fullBobAtSpeed, 1) * bobAmount or 0
    bob = bob + (wanted - bob) * math.min(dt * 10, 1)
    bobTime = bobTime + dt * bobSpeed

    if body.isGrounded and not wasGrounded then
        dip = math.min(lastFallSpeed * landingDip, maxDip)
        if lastFallSpeed >= hardLandingSpeed then Audio.Play2D(landSound) end
    elseif wasGrounded and not body.isGrounded and velocity.y > 1 then
        Audio.Play2D(jumpSound)
    end
    wasGrounded = body.isGrounded
    lastFallSpeed = math.max(0, -velocity.y)

    dip = math.max(0, dip - DIP_RECOVERY * dt)

    controller.eyeHeight = baseEyeHeight + math.sin(bobTime) * bob - dip
end
