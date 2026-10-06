# 15 - Audio

Sound comes from an `AudioSource` component (`entity.audioSource`, `nil` if there isn't one).

| Member | Notes |
|--------|-------|
| `audio.soundFileName` | The sound to play. `audio:ClearSoundFileName()` empties it |
| `audio:Play()` | Plays `soundFileName` on this source, from the start |
| `audio:Stop()`, `audio:Pause()` | Stop (rewinds) or pause. Either one also keeps a looping sound from restarting by itself |
| `audio:Resume()` | Continues a paused sound |
| `audio:IsPlaying()` | `true` while the sound is playing |
| `audio.gain` | Volume |
| `audio.pitch` | Playback speed and pitch, `1` is normal |
| `audio.looping` | Loop the sound |
| `audio.playOnStart` | Whether the level starts it automatically |
| `audio.referenceDistance`, `maxDistance`, `rollOffFactor` | How volume falls off with distance |
| `audio.innerConeAngle`, `outerConeAngle`, `outerGain` | Directional sound, pointing the way `transform.rotation` faces (see [Audio Source](../wiki/AudioSource.md#sound-cone)) |
| `audio:SetSourcePosition(Vector3)` | Move the sound's 3D position until the next frame, when it goes back to the entity |

A sound is named by its path inside `Assets`, for example `"Sounds/Footsteps/step.wav"` for
`Assets/Sounds/Footsteps/step.wav`. The `.wav` is optional and the file can be in any folder under
`Assets`. `Asset` public fields only pick textures, so take sound paths as `string` fields.

The sound always plays from the entity that owns the `AudioSource`, and follows it as it moves.

---

## Footsteps

**Attach to:** the player, which needs an `AudioSource` and a `Rigidbody`.

Plays a step sound every `stride` units of ground travelled, alternating between two sounds, with
a little random pitch so it doesn't sound mechanical. Because it's based on distance, the steps
naturally speed up when sprinting.

```lua
-- Scripts/Audio/Footsteps.lua (entity script)
public string stepSoundA = "Sounds/Footsteps/stepA.wav"
public string stepSoundB = "Sounds/Footsteps/stepB.wav"
public number stride = 22
public number pitchVariation = 0.1

local audio, rb
local travelled = 0.0
local useA = true

function Start()
    audio = entity.audioSource
    rb = entity.rigidbody

    if audio == nil or rb == nil then
        Debug.LogError("Footsteps: " .. entity.name .. " needs an AudioSource and a Rigidbody")
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

    -- Alternate sounds; fall back to whichever one is set.
    local sound = useA and stepSoundA or stepSoundB
    if sound == "" then sound = useA and stepSoundB or stepSoundA end
    useA = not useA

    if sound ~= "" then audio.soundFileName = sound end
    audio.pitch = 1 + mathT.RandomF(-pitchVariation, pitchVariation)
    audio:Play()
end
```

---

## Toggleable radio

**Attach to:** a prop with an `AudioSource`. In the inspector, set a music file, tick *Looping*,
and tick *Play On Start*.

The music is paused from the start of the level until the player walks up and presses a key.
Pausing also stops the engine from restarting the looping sound by itself.

```lua
-- Scripts/Audio/Radio.lua (entity script)
public Entity player = nil
public Key useKey = Key.E
public number useDistance = 30

local audio
local isOn = false

function Start()
    audio = entity.audioSource

    if audio == nil then
        Debug.LogError("Radio: " .. entity.name .. " has no AudioSource")
        return
    end

    audio:Pause()
end

function Update()
    if audio == nil or player == nil or not player.isValid then return end
    if not Input.GetKeyDown(useKey) then return end

    if entity.transform:DistanceTo2D(player) <= useDistance then
        isOn = not isOn
        if isOn then audio:Resume() else audio:Pause() end
        Debug.Print(isOn and "Radio on" or "Radio off")
    end
end
```

---

## Playing sounds from a sector script

Sectors have no `AudioSource`, but a sector script can borrow one: give it a public
`Entity` field that points at any object with an `AudioSource`, and play that. These are the
lines to add to a door or lift script:

```lua
public Entity soundSource = nil

local function PlaySound()
    if soundSource == nil then return end

    local audio = soundSource.audioSource
    if audio ~= nil then audio:Play() end
end
```

Call `PlaySound()` at the moment the state changes. In `UseDoor.lua` (see
[05_doors.md](05_doors.md)) that is right after `isOpen = true` or `isOpen = false`.

The sound plays where the `soundSource` entity is, so put that entity at the door. If several
doors share one source, move the entity to the door before playing; the sound follows it:

```lua
soundSource.transform.position = doorPosition -- a Vector3 you work out from the door's walls
audio:Play()
```
