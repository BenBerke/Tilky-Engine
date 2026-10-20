-- Scripts/World/ExitSwitch.lua
--
-- Put this on an entity standing in front of an exit switch. Press E next
-- to it: the switch texture flips, and a moment later the next level loads.
-- Leave Next Level empty on the last level to show the win screen instead.
--
-- Global.inventory (health, ammo, weapons) carries over to the next level.

public Wall switchWall = nil
public string pressedTexture = "Textures/Walls/sw2exit.png"
public string nextLevel = ""
public number useRange = 24
public number delay = 1
public string sound = "Sounds/switch_exit"

local player
local pressed = false
local timer = 0

function Start()
    player = Game.FindEntity("Player")
end

function Update()
    if player == nil or not player.isValid then return end

    if not pressed then
        local stats = player:GetScript("Player")
        if Input.GetKeyDown(Key.E) and not stats.dead
            and entity.transform:DistanceTo2D(player) <= useRange then
            pressed = true
            timer = delay
            if switchWall ~= nil then switchWall.topTexture = pressedTexture end
            if entity.audioSource ~= nil then
                entity.audioSource.soundFileName = sound
                entity.audioSource:Play()
            end
        end
        return
    end

    timer = timer - GameTime.deltaTime
    if timer > 0 then return end
    timer = math.huge -- only once

    if nextLevel ~= "" then
        Game.LoadLevel(nextLevel)
    else
        player:GetScript("Player"):Win()
    end
end
