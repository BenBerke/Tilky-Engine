# Audio Source

**Inspector name:** Audio Source · **Lua:** `entity.audioSource` · **Public field type:** `AudioSource`

An Audio Source plays sounds from the entity's position in the world. Sounds are 3D: they get
quieter with distance and come from the direction of the entity. Use one for anything that makes a
noise: a humming generator, a door, a monster, the player's footsteps.

Each Audio Source plays **one sound at a time**. Starting a new one stops the current one. For
several overlapping sounds, put Audio Sources on several entities.

## How it works

### Sound files

- Sounds must be **`.wav`** files, 8- or 16-bit, mono or stereo.
- They must be inside the project's **`Assets/Sounds`** folder (sub-folders are fine). Every sound
  there is loaded ahead of time, so playing one never waits for the disk.
- `soundFileName` is the path **inside `Assets/Sounds`**. The extension is optional:
  `Assets/Sounds/Doors/open.wav` can be written `"Doors/open"` (the form the inspector stores) or
  `"Doors/open.wav"`.
- **Use mono files for 3D sounds.** Stereo files usually play without distance fading or direction.

### Where the sound is and who hears it

- The sound plays at the entity's [Transform](Transform.md) position. The source follows the
  entity every frame.
- The **listener**, the "ears", is the player's eye position, facing where the camera looks. The
  [Player Controller](PlayerController.md) moves it every frame. Without an active Player
  Controller the listener stays where it was.

### Playing

There are three ways a sound starts:

| How | When |
|---|---|
| **Play On Start** | Once, when the level starts. |
| **Looping** | A looping source with a sound set **plays all the time**. Whenever it isn't playing, the engine starts it again, even without Play On Start. |
| `audio:play()` | Right now, from the beginning. If something was already playing on this source, it is cut off. |

**Looping** makes the sound repeat. A looping source can't be stopped directly. To silence it, set
`looping = false` (it finishes the current pass and stops) or set `gain = 0` (instantly silent,
still running).

There is no `stop` or `pause` function.

### Volume and pitch

- **Gain** is the volume multiplier. `1` is the file's own volume, `0` is silent and above `1`
  is louder.
- **Pitch** speeds the sound up or slows it down. `2` is an octave higher and twice as fast, and
  `0.5` is an octave lower and half speed. Slight random pitch changes (0.9 to 1.1) stop repeated
  sounds from sounding mechanical.

### Distance

How the sound fades with distance:

- **Reference Distance**: closer than this, the sound is at full volume. Further away it starts to
  fade.
- **Rolloff Factor**: how quickly it fades past the reference distance. `1` is natural, bigger
  fades faster, and `0` never fades.
- **Max Distance**: past this, it stops getting any quieter.

With the default model, volume ≈ `referenceDistance / (referenceDistance + rolloff × (distance − referenceDistance))`.
With the defaults (reference 1, rolloff 1), a sound 50 units away is at about 2% volume. That's
very quiet at this engine's scale, so raise **Reference Distance** to something like `32`–`128` for
sounds that should carry across a room.

### Sound cone

**Inner Angle**, **Outer Angle** and **Outer Gain** describe a directional speaker. Full volume
inside the inner cone, `outerGain` × volume outside the outer cone, and a blend in between. The
engine doesn't give sources a facing direction yet, so every source plays equally in all
directions and **these settings currently have no effect**.

### Level-wide settings

The level's audio settings, not the component's, control master volume, the Doppler effect,
speed of sound and the distance model for all sources.

## In the editor

| Field | Lua | Default | Notes |
|---|---|---|---|
| **Sound** | `soundFileName` | empty | See [Sound files](#sound-files). |
| **Pitch** | `pitch` | `1` | |
| **Gain** | `gain` | `1` | |
| **Looping** | `looping` | off | |
| **Play On Start** | `playOnStart` | off | Only matters when the level starts. |
| **Reference Distance** | `referenceDistance` | `1` | |
| **Max Distance** | `maxDistance` | `10000` | |
| **Rolloff Factor** | `rollOffFactor` | `1` | |
| **Inner Angle** / **Outer Angle** | `innerConeAngle` / `outerConeAngle` | `360` / `360` | No effect yet. |
| **Outer Gain** | `outerGain` | `0` | No effect yet. |

## Scripting

| Property | Type | | Description |
|---|---|---|---|
| `isValid` | boolean | read-only | `false` if the entity or its Audio Source is gone. |
| `name` | string | read-only | Internal source name, like `entity_12_audio`. |
| `soundFileName` | string | read/write | The sound `play()` plays and looping repeats. Changing it doesn't interrupt what's playing. |
| `pitch` | number | read/write | Applies immediately, even mid-sound. |
| `gain` | number | read/write | Applies immediately. |
| `looping` | boolean | read/write | |
| `playOnStart` | boolean | read/write | |
| `referenceDistance` | number | read/write | |
| `maxDistance` | number | read/write | |
| `rollOffFactor` | number | read/write | |
| `innerConeAngle` | number | read/write | No effect yet. |
| `outerConeAngle` | number | read/write | No effect yet. |
| `outerGain` | number | read/write | No effect yet. |

| Method | Description |
|---|---|
| `play()` | Plays `soundFileName` from the start, cutting off anything already playing on this source. Does nothing if `soundFileName` is empty. |
| `clearSoundFileName()` | Empties `soundFileName`. What's already playing carries on. |
| `setSourcePosition(position)` | Moves the sound. It's pointless on an entity with a Transform, because the source is put back on the entity every frame. |

## Examples

### Play a sound when something happens

```lua
-- Scripts/Audio/Doorbell.lua (entity with an Audio Source and a trigger Collider)
-- Path inside Assets/Sounds. (Asset fields only accept textures, so use a string.)
---@field sound string @ Sound
sound = "Bells/ding.wav"

function OnTriggerEnter(other)
    if not other.hasPlayerController then return end

    local audio = entity.audioSource
    audio.soundFileName = sound
    audio:play()
end
```

### Random pitch for repeated sounds

```lua
-- Scripts/Audio/Footsteps.lua (on the player, with an Audio Source)
---@field stepSound string @ Step Sound
stepSound = "Footsteps/step.wav"

---@field stepDistance number @ Units Per Step
stepDistance = 24

local travelled = 0
local last

function Start()
    last = entity.transform.position
    entity.audioSource.soundFileName = stepSound
end

function Update()
    local p = entity.transform.position
    local moved = Vector2(p.x - last.x, p.z - last.z).length
    last = p

    if not entity.rigidbody.isGrounded then return end

    travelled = travelled + moved
    if travelled >= stepDistance then
        travelled = 0
        local audio = entity.audioSource
        audio.pitch = mathT.RandomF(0.9, 1.1)
        audio:play()
    end
end
```

### A radio you can switch on and off

```lua
-- Scripts/Audio/Radio.lua (entity with a looping Audio Source)
-- Press E near it to toggle.
---@field range number
range = 48

local on = true
local player
local volume

function Start()
    player = Game.FindEntity("Player")
    volume = entity.audioSource.gain
end

function Update()
    if player == nil or not Input.GetKeyDown("E") then return end

    local d = mathT.Vector3Distance(player.transform.position, entity.transform.position)
    if d > range then return end

    on = not on
    entity.audioSource.gain = on and volume or 0 -- looping sources can't be stopped, only muted
end
```

### Fade out

```lua
-- Scripts/Audio/FadeOut.lua
-- entity:GetScript("FadeOut"):Fade(2) fades this source to silence over 2 seconds.
local speed = 0

function Fade(self, seconds)
    speed = entity.audioSource.gain / math.max(seconds, 0.001)
end

function Update()
    if speed <= 0 then return end

    local audio = entity.audioSource
    audio.gain = math.max(0, audio.gain - speed * GameTime.deltaTime)
    if audio.gain == 0 then speed = 0 end
end
```

More sound scripts are in [`docs/scripts/15_audio.md`](../scripts/15_audio.md).
