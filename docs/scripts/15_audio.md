# 15 - Audio

Sound comes from an `AudioSource` component (`gameObject.audioSource`, `nil` if there isn't one).

| Member | Notes |
|--------|-------|
| `audio.soundFileName` | The sound to play. `audio:clearSoundFileName()` empties it |
| `audio:play()` | Plays `soundFileName` on this source |
| `audio.gain` | Volume |
| `audio.pitch` | Playback speed and pitch, `1` is normal |
| `audio.looping` | Loop the sound |
| `audio.playOnStart` | Whether the level starts it automatically |
| `audio.referenceDistance`, `maxDistance`, `rollOffFactor` | How volume falls off with distance |
| `audio:setSourcePosition(Vector3)` | Move the sound's 3D position |

Audio has **no stop or pause function**. To silence a looping sound, set `gain` to `0`.

Sound files arrive in a script as `Asset` (or `Texture`) public fields. Their value is the asset
path, which can be assigned straight to `audio.soundFileName`.

---

## Footsteps

**Attach to:** the player, which needs an `AudioSource` and a `Rigidbody`.

Plays a step sound every `stride` units of ground travelled, alternating between two sounds, with
a little random pitch so it doesn't sound mechanical. Because it's based on distance, the steps
naturally speed up when sprinting.

```lua
-- Scripts/Audio/Footsteps.lua (entity script)
---@field stepSoundA Asset @ Step Sound A
stepSoundA = nil

---@field stepSoundB Asset @ Step Sound B
stepSoundB = nil

---@field stride number @ Distance Per Step
stride = 22

---@field pitchVariation number @ Random Pitch Variation
pitchVariation = 0.1

local audio, rb
local travelled = 0.0
local useA = true

function Start()
    audio = gameObject.audioSource
    rb = gameObject.rigidbody

    if audio == nil or rb == nil then
        Debug.LogError("Footsteps: " .. gameObject.name .. " needs an AudioSource and a Rigidbody")
    end
end

function Update()
    if audio == nil or rb == nil then return end

    local v = rb.velocity
    local speed = math.sqrt(v.x * v.x + v.z * v.z)

    if not rb.isGrounded or speed < 1 then return end

    travelled = travelled + speed * GameTime.deltaTime
    if travelled < stride then return end
    travelled = travelled - stride

    -- Alternate sounds; fall back to whichever one is assigned.
    local sound = useA and stepSoundA or stepSoundB
    if sound == nil then sound = stepSoundA or stepSoundB end
    useA = not useA

    if sound ~= nil then audio.soundFileName = sound end
    audio.pitch = 1 + mathT.RandomF(-pitchVariation, pitchVariation)
    audio:play()
end
```

---

## Toggleable radio

**Attach to:** a prop with an `AudioSource`. In the inspector, set a music file, tick *Looping*,
and tick *Play On Start*.

The music plays from the start of the level, but it's silent (gain `0`) until the player walks
up and presses a key.

```lua
-- Scripts/Audio/Radio.lua (entity script)
---@field player GameObject @ Player
player = nil

---@field useKey string @ Toggle Key
useKey = "E"

---@field useDistance number @ Use Distance
useDistance = 30

---@field volume number @ Volume When On
volume = 1.0

local audio
local isOn = false

function Start()
    audio = gameObject.audioSource

    if audio == nil then
        Debug.LogError("Radio: " .. gameObject.name .. " has no AudioSource")
        return
    end

    audio.gain = 0.0   -- there is no Stop(), so "off" means volume 0
end

function Update()
    if audio == nil or player == nil or not player.isValid then return end
    if not Input.GetKeyDown(useKey) then return end

    local p = player.transform.position
    local me = gameObject.transform.position
    local dx, dz = p.x - me.x, p.z - me.z

    if math.sqrt(dx * dx + dz * dz) <= useDistance then
        isOn = not isOn
        audio.gain = isOn and volume or 0.0
        Debug.Print(isOn and "Radio on" or "Radio off")
    end
end
```

---

## Playing sounds from a sector script

Sectors have no `AudioSource`, but a sector script can borrow one: give it a public
`GameObject` field that points at any object with an `AudioSource`, and play that. These are the
lines to add to a door or lift script:

```lua
---@field soundSource GameObject @ Sound Source (an object with an AudioSource)
soundSource = nil

local function PlaySound()
    if soundSource == nil then return end

    local audio = soundSource.audioSource
    if audio ~= nil then audio:play() end
end
```

Call `PlaySound()` at the moment the state changes. In `UseDoor.lua` (see
[05_doors.md](05_doors.md)) that is right after `isOpen = true` or `isOpen = false`.

To place the sound at the door, move the source first:

```lua
audio:setSourcePosition(soundSource.transform.position)
```
