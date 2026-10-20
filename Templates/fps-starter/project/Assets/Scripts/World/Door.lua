-- Scripts/World/Door.lua  (sector script)
--
-- A door is a thin sector between two rooms whose ceiling sits on its
-- floor while closed. Opening moves the ceiling up; closing moves it back
-- down. Attach this script to that sector.
--
-- Walk up to the door and press E. A locked door needs the matching key
-- card first. The door closes by itself after a while, but never on
-- something standing in the doorway.

public number openHeight = 32       -- above the door's floor
public number speed = 64            -- units per second
public number waitTime = 4          -- seconds before closing again
public bool stayOpen = false
public enum(None, Blue, Red) lock = None
public number useRange = 28
public string openSound = "Sounds/door_open"
public string closeSound = "Sounds/door_close"
public string lockedSound = "Sounds/no_way"

local LOCK_COLORS = { [1] = "blue", [2] = "red" }

local player
local speaker              -- an Audio Source at the door, made in Start
local state = "closed"     -- closed, opening, open, closing
local timer = 0

local function PlaySound(path)
    if speaker == nil or path == "" then return end
    speaker.soundFileName = path
    speaker:Play()
end

-- Is anything solid standing in the doorway? Corpses, pickups and the
-- door's own sound entity don't count: they have no active solid collider.
local function Blocked()
    for _, e in ipairs(sector:GetEntities()) do
        local collider = e.collider
        if collider ~= nil and collider.isActive and not collider.isTrigger then return true end
    end
    return false
end

local function Open()
    state = "opening"
    sector:MoveCeilingTo(1, sector.floorHeight + openHeight, speed)
    PlaySound(openSound)
end

local function Close()
    state = "closing"
    sector:MoveCeilingToFloor(1, speed)
    PlaySound(closeSound)
end

local function TryUse()
    if state ~= "closed" and state ~= "closing" then return end

    local color = LOCK_COLORS[lock]
    if color ~= nil then
        local stats = player:GetScript("Player")
        if not stats:HasKey(color) then
            stats:ShowMessage("You need the " .. color .. " key card to open this door.")
            PlaySound(lockedSound)
            return
        end
    end

    Open()
end

function Start()
    player = Game.FindEntity("Player")

    -- Sector scripts have no entity of their own, so make a small one in the
    -- middle of the door to play its sounds from.
    local center = sector:GetCenter()
    local soundEntity = Game.CreateEntity()
    soundEntity.name = "Door Sound"
    soundEntity.transform.position = Vector3(center.x, sector.floorHeight + 16, center.y)
    speaker = soundEntity:AddComponent(Component.AudioSource)
    speaker.referenceDistance = 40
end

function Update()
    if player == nil or not player.isValid then return end

    if Input.GetKeyDown(Key.E) and sector:DistanceToSector(player) <= useRange then
        TryUse()
    end

    if state == "opening" then
        if not sector:IsCeilingMoving(1) then
            state = "open"
            timer = waitTime
        end
    elseif state == "open" and not stayOpen then
        timer = timer - GameTime.deltaTime
        if timer <= 0 then
            -- Someone in the doorway? Wait a little longer.
            if Blocked() then timer = 1 else Close() end
        end
    elseif state == "closing" then
        if Blocked() then
            Open() -- bounce back up instead of closing on someone
        elseif not sector:IsCeilingMoving(1) then
            state = "closed"
        end
    end
end
