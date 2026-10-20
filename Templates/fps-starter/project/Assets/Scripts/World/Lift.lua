-- Scripts/World/Lift.lua  (sector script)
--
-- A platform that lowers so you can step on, waits, then rises again,
-- carrying you up. Attach it to the lift's sector; the sector's floor
-- height in the editor is the top stop.
--
-- Press E next to the lift (or while standing on it) to call it down.

public number lowHeight = 0         -- the bottom stop
public number speed = 48
public number waitTime = 3
public number useRange = 32
public string startSound = "Sounds/lift_start"
public string stopSound = "Sounds/lift_stop"

local player
local speaker
local highHeight
local state = "up"          -- up, lowering, down, raising
local timer = 0

local function PlaySound(path)
    if speaker == nil or path == "" then return end
    speaker.soundFileName = path
    speaker:Play()
end

local function MoveTo(height, nextState)
    state = nextState
    sector:MoveFloorTo(1, height, speed)
    PlaySound(startSound)
end

function Start()
    player = Game.FindEntity("Player")
    highHeight = sector.floorHeight

    local center = sector:GetCenter()
    local soundEntity = Game.CreateEntity()
    soundEntity.name = "Lift Sound"
    soundEntity.transform.position = Vector3(center.x, lowHeight + 8, center.y)
    speaker = soundEntity:AddComponent(Component.AudioSource)
    speaker.referenceDistance = 40
end

function Update()
    if player == nil or not player.isValid then return end

    if state == "up" and Input.GetKeyDown(Key.E) and sector:DistanceToSector(player) <= useRange then
        MoveTo(lowHeight, "lowering")
    end

    if state == "lowering" and not sector:IsFloorMoving(1) then
        PlaySound(stopSound)
        state = "down"
        timer = waitTime
    elseif state == "down" then
        timer = timer - GameTime.deltaTime
        if timer <= 0 then MoveTo(highHeight, "raising") end
    elseif state == "raising" and not sector:IsFloorMoving(1) then
        PlaySound(stopSound)
        state = "up"
    end
end
