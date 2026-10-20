-- Scripts/Enemies/Enemy.lua
--
-- One script for every enemy. The prefabs (Prefabs/Enemies) only differ in
-- their public fields: how tough they are, how they attack, which sprite
-- folder and sounds they use. Make a new enemy by copying a prefab and
-- changing those.
--
-- The enemy is a small state machine:
--   idle   - standing still, watching and listening for the player
--   chase  - walking toward the player, deciding now and then to attack
--   attack - stopped, facing the player, shooting on one frame
--   pain   - flinching after being hit
--   dead   - playing its death frames, then lying there
--
-- Needs: Sprite (8-sided), Rigidbody, sphere Collider, Audio Source.

public int health = 20
public number speed = 18
public enum(Hitscan, Projectile) attackType = Hitscan
public int damageMin = 3
public int damageMax = 15
public number aimSpread = 5         -- degrees a hitscan shot can miss by
public number meleeRange = 0        -- 0 = never melee
public int meleeDamageMin = 3
public int meleeDamageMax = 24
public number sightRange = 900
public number hearingRange = 350
public number painChance = 0.75     -- 0..1, chance to flinch when hit
public number eyeHeight = 10

-- Animation: frame letters from the sprite folder (see GameRules.ShowFrame).
public string spriteFolder = "Sprites/Enemies/Zombieman"
public string idleFrames = "AB"
public string walkFrames = "ABCD"
public string attackFrames = "EFE"
public int shootOnFrame = 2         -- which attack frame fires (1-based)
public string painFrame = "G"
public string deathFrames = "HIJKL"
public number walkFrameTime = 0.15
public number attackFrameTime = 0.25
public number deathFrameTime = 0.14

public string sightSound = "Sounds/zombie_sight"
public string idleSound = "Sounds/zombie_idle"
public string attackSound = "Sounds/pistol"
public string meleeSound = ""
public string painSound = "Sounds/zombie_pain"
public string deathSound = "Sounds/zombie_death"

public Entity projectilePrefab = nil  -- for attackType Projectile
public Entity puffPrefab = nil        -- where a hitscan shot hits a wall
public Entity dropPrefab = nil        -- left behind on death, e.g. ammo

state = "idle"   -- readable by other scripts

local player
local body, sprite, audio
local stateTimer = 0
local moveDirection = Vector3(0, 0, 0)
local moveTimer, stuckTimer, thinkTimer, idleSoundTimer = 0, 0, 0, 0
local lastHeardNoise = -1

-- Animation playback
local anim = { frames = "A", frameTime = 1, loop = true, directional = true }
local animIndex, animTimer, animFinished = 1, 0, false

-- ── Small helpers ──────────────────────────────────────────────────────

local function PlaySound(path)
    if audio == nil or path == "" then return end
    audio.soundFileName = path
    audio:Play()
end

local function Play(frames, frameTime, loop, directional)
    anim = { frames = frames, frameTime = frameTime, loop = loop, directional = directional }
    animIndex, animTimer, animFinished = 1, frameTime, false
    Global.ShowFrame(sprite, spriteFolder, frames:sub(1, 1), directional)
end

-- Advances the animation. Returns true on the frames where it changed.
local function UpdateAnimation(dt)
    if animFinished then return false end
    animTimer = animTimer - dt
    if animTimer > 0 then return false end

    animTimer = animTimer + anim.frameTime
    if animIndex >= #anim.frames then
        if not anim.loop then
            animFinished = true
            return false
        end
        animIndex = 1
    else
        animIndex = animIndex + 1
    end
    Global.ShowFrame(sprite, spriteFolder, anim.frames:sub(animIndex, animIndex), anim.directional)
    return true
end

local function Eye()
    return entity.transform.position + Vector3(0, eyeHeight, 0)
end

local function PlayerEye()
    return player.camera:GetEyePosition()
end

local function PlayerAlive()
    return player ~= nil and player.isValid and not player:GetScript("Player").dead
end

local function FlatDirectionTo(point)
    return entity.transform:DirectionTo2D(point)
end

local function CanSeePlayer()
    if not PlayerAlive() then return false end
    local distance = entity.transform:DistanceTo(player)
    if distance > sightRange then return false end
    return Game.LineOfSight(Eye(), PlayerEye())
end

local function Stop()
    local v = body.velocity
    body.velocity = Vector3(0, v.y, 0)
end

local function FacePlayer()
    entity.transform:LookDirection(FlatDirectionTo(player.transform.position))
end

-- ── States ─────────────────────────────────────────────────────────────

local function EnterChase()
    state = "chase"
    moveTimer, thinkTimer = 0, mathT.RandomF(0.6, 1.4)
    Play(walkFrames, walkFrameTime, true, true)
end

local function WakeUp()
    if state ~= "idle" then return end
    PlaySound(sightSound)
    EnterChase()
end

local function EnterAttack()
    state = "attack"
    Stop()
    FacePlayer()
    Play(attackFrames, attackFrameTime, false, true)
end

-- Picks a walking direction: mostly toward the player, sometimes off to
-- one side so a group of enemies spreads out instead of walking in a line.
local function ChooseDirection(blocked)
    local toPlayer = FlatDirectionTo(player.transform.position)
    local angle = blocked and mathT.RandomF(60, 120) * (mathT.RandomBool() and 1 or -1)
                           or mathT.RandomF(-40, 40)
    local d = mathT.Vector2Rotate(Vector2(toPlayer.x, toPlayer.z), angle)
    moveDirection = Vector3(d.x, 0, d.y)
    moveTimer = mathT.RandomF(0.4, 1.0)
end

local function HitscanAttack()
    PlaySound(attackSound)
    local eye = Eye()
    local toTarget = PlayerEye() - Vector3(0, 2, 0) - eye
    local flat = Vector2(toTarget.x, toTarget.z)
    local yaw = mathT.Atan2(toTarget.x, toTarget.z) + mathT.DegToRad(mathT.RandomF(-aimSpread, aimSpread))
    local pitch = mathT.Atan2(toTarget.y, flat.length)
    local direction = Vector3(mathT.Sin(yaw) * mathT.Cos(pitch), mathT.Sin(pitch), mathT.Cos(yaw) * mathT.Cos(pitch))

    local hit = Game.Raycast(eye, direction, sightRange, entity.id, true)
    if hit == nil then return end
    if hit.entity ~= nil and Global.IsDamageable(hit.entity) then
        Global.Damage(hit.entity, mathT.Random(damageMin, damageMax), entity)
    elseif puffPrefab ~= nil then
        local puff = Game.CreateEntity(puffPrefab)
        puff.transform.position = hit.position - direction * 0.5 - Vector3(0, puff.transform.scale.z * 0.5, 0)
    end
end

local function ProjectileAttack()
    if projectilePrefab == nil then return end
    PlaySound(attackSound)
    local eye = Eye()
    local direction = (PlayerEye() - Vector3(0, 3, 0) - eye).normalized
    local missile = Game.CreateEntity(projectilePrefab)
    -- Start in front of the enemy so it doesn't hit itself; the projectile
    -- flies the way its Transform faces (see Projectile.lua).
    local start = eye + Vector3(direction.x, 0, direction.z).normalized * 8
    missile.transform.position = start - Vector3(0, missile.transform.scale.z * 0.5, 0)
    missile.transform:LookDirection(direction, false)
end

local function DoAttack()
    if not PlayerAlive() then return end
    local distance = entity.transform:DistanceTo2D(player)
    if meleeRange > 0 and distance <= meleeRange then
        PlaySound(meleeSound)
        Global.Damage(player, mathT.Random(meleeDamageMin, meleeDamageMax), entity)
    elseif attackType == 0 then
        HitscanAttack()
    else
        ProjectileAttack()
    end
end

local function Die()
    state = "dead"
    PlaySound(deathSound)
    Stop()
    -- A corpse doesn't block bullets or the player.
    entity.collider.isActive = false
    body.isStatic = true
    Play(deathFrames, deathFrameTime, false, false)
    Global.kills = (Global.kills or 0) + 1

    if dropPrefab ~= nil then
        local drop = Game.CreateEntity(dropPrefab)
        drop.transform.position = entity.transform.position
    end
end

-- ── Called by other scripts ────────────────────────────────────────────

function TakeDamage(self, amount, source)
    if state == "dead" then return end
    health = health - amount
    if health <= 0 then
        Die()
        return
    end

    WakeUp()
    if mathT.RandomF() < painChance then
        state = "pain"
        stateTimer = 0.2
        Stop()
        PlaySound(painSound)
        Play(painFrame, stateTimer, false, true)
    end
end

-- ── Engine callbacks ───────────────────────────────────────────────────

function Start()
    player = Game.FindEntity("Player")
    body = entity.rigidbody
    sprite = entity.sprite
    audio = entity.audioSource
    idleSoundTimer = mathT.RandomF(3, 8)
    Play(idleFrames, 0.3, true, true)
end

function Update()
    local dt = GameTime.deltaTime
    local frameChanged = UpdateAnimation(dt)

    if state == "dead" then return end
    if player == nil or not player.isValid then return end

    if state == "idle" then
        -- Look for the player, and listen for gunfire.
        thinkTimer = thinkTimer - dt
        if thinkTimer <= 0 then
            thinkTimer = 0.25
            local heard = Global.noiseTime ~= nil and Global.noiseTime > lastHeardNoise
                and mathT.Vector3Distance(Global.noisePosition, entity.transform.position) <= hearingRange
            if heard then lastHeardNoise = Global.noiseTime end
            if heard or CanSeePlayer() then WakeUp() end
        end

    elseif state == "chase" then
        if not PlayerAlive() then
            Stop()
            state = "idle"
            Play(idleFrames, 0.3, true, true)
            return
        end

        -- Walk, and turn when blocked by a wall or another enemy.
        moveTimer = moveTimer - dt
        if moveTimer <= 0 then ChooseDirection(false) end
        local v = body.velocity
        body.velocity = Vector3(moveDirection.x * speed, v.y, moveDirection.z * speed)
        entity.transform:LookDirection(moveDirection)

        if Vector2(v.x, v.z).length < speed * 0.3 then
            stuckTimer = stuckTimer + dt
            if stuckTimer > 0.3 then
                stuckTimer = 0
                ChooseDirection(true)
            end
        else
            stuckTimer = 0
        end

        -- Every so often, decide whether to attack. Closer = more likely.
        thinkTimer = thinkTimer - dt
        if thinkTimer <= 0 then
            thinkTimer = mathT.RandomF(0.8, 1.6)
            if CanSeePlayer() then
                local distance = entity.transform:DistanceTo2D(player)
                local inMelee = meleeRange > 0 and distance <= meleeRange
                local chance = mathT.Clamp(0.75 - distance / 600, 0.2, 0.6)
                if inMelee or mathT.RandomF() < chance then EnterAttack() end
            end
        end

        idleSoundTimer = idleSoundTimer - dt
        if idleSoundTimer <= 0 then
            idleSoundTimer = mathT.RandomF(4, 10)
            PlaySound(idleSound)
        end

    elseif state == "attack" then
        if PlayerAlive() then FacePlayer() end
        if frameChanged and animIndex == shootOnFrame then DoAttack() end
        if animFinished then EnterChase() end

    elseif state == "pain" then
        stateTimer = stateTimer - dt
        if stateTimer <= 0 then EnterChase() end
    end
end
