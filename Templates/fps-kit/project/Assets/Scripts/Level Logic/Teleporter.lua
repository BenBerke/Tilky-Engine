-- Scripts/Level Logic/Teleporter.lua
--
-- ONE JOB: sends the player to the Teleport Destination you drag onto this field.
--
-- Why this is a script: a prefab's dispatches can't point at an entity in your
-- level, but a field on the placed prefab can. (Sectors have a Make Teleporter
-- button too. It needs no prefab or script, but you draw a sector for each pad.)

public Entity destination = nil
public string sound = "Sounds/World/teleport.wav"
public Entity fogPrefab = nil     -- the flash at both ends

local function Fog(position)
    if fogPrefab ~= nil then Game.CreateEntity(fogPrefab, position) end
end

function OnTriggerEnter(other)
    if destination == nil or not other:HasTag("Player") then return end

    Fog(other.transform.position)

    other.transform.position = destination.transform.position
    other.rigidbody.velocity = Vector3(0, 0, 0)

    -- Face the way the destination's arrow points.
    local forward = destination.transform.forward
    other.camera.yaw = mathT.RadToDeg(math.atan(forward.x, forward.z))

    Fog(destination.transform.position)
    Audio.Play2D(sound)
end
