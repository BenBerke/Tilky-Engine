# Flipbook

**Inspector name:** Flipbook · **Lua:** `entity.flipbook` · **Public field type:** `Flipbook`
(the component) and `FlipbookAsset` (a `.fpk` file)

A Flipbook animates a [Sprite](Sprite.md): a walking monster, a burning torch, a spinning coin. On
a UI entity it animates a [UI Sprite](UISprite.md) instead: a blinking icon, an animated
crosshair. The animation itself lives in a **flipbook file** (`.fpk`) that you make in the editor.
The Flipbook component plays that file on one of the entity's sprites.

A Flipbook never draws anything itself. While the game runs, it copies the current frame's
textures into the sprite, and the sprite draws them as usual.

## How it works

### Flipbook files (.fpk)

A `.fpk` file is a list of **frames** plus a few playback settings. Many entities can use the same
file: a hundred imps share one `imp_walk.fpk`.

Each frame has:

- a **name**, unique within the file, e.g. `windup`. Scripts use it with `SetFrame` and `GetFrame`.
- **8 textures**, one for each direction, in the same order as the Sprite's slots: N, NE, E, SE, S,
  SW, W, NW.
- an optional **duration** in seconds. `0` uses the file's frames per second.
- an optional **event function**: a script function called when playback reaches the frame. See
  [Frame events](#frame-events).

The file's settings:

| Setting | Default | |
|---|---|---|
| **Frames per second** | `10` | How long each frame shows, unless it has its own duration. |
| **Loop mode** | Loop | **Once** stops on the last frame. **Loop** starts over. **Ping-pong** plays forward, then backward, and repeats. |
| **Play on start** | on | Starts playing when the level starts. Otherwise a script starts it with `Play()`. |

### Directions

A flipbook has no direction setting of its own: it uses the **sprite's** Direction Level. A frame
always stores all 8 textures, but a single-direction sprite only shows slot 0 (N), and a 4-sided
sprite only shows N, E, S and W. See [Sprite: Directions](Sprite.md#directions).

So one file can drive sprites with different direction levels. The Flipbook inspector warns about
frames that are missing a texture the sprite will actually show.

### On UI entities

The same component works on UI entities. Add it in the **UI Editor** with **Add Component >
Flipbook**, or from a script with `entity:AddComponent(Component.Flipbook)`. It animates one of
the entity's UI Sprites, and everything else on this page applies unchanged: same `.fpk` files,
same settings, same frame events, same `entity.flipbook` in Lua.

A UI Sprite shows one picture, so it only uses each frame's **slot 0 (N)**. For a UI flipbook,
fill in N on every frame and leave the rest empty. While it plays, the UI Sprite's `textureIndex`
is overwritten at every frame change.

### Which sprite

An entity can have several Sprites (or UI Sprites). The inspector's **Sprite** dropdown picks the
one this Flipbook animates. **First sprite** (the default) always means whichever sprite is first, even
after you reorder them. An entity can have several Flipbooks, for example one per sprite.

### When it runs

Flipbooks only play in the game. In the editors (including the UI Editor's canvas), a sprite
shows its own textures.

Each frame, after every script's `Update`, every Flipbook advances and copies its frame into its
sprite. A `Play()` call in `Update` is visible on the same frame. If the game stalls, playback
catches up by skipping ahead, and **every frame passed still fires its event**.

The sprite's textures are overwritten while the game runs, but never saved: Play mode works on a
copy of the level. A script that sets the sprite's textures itself while a flipbook plays is
overwritten at the next frame change. Call `Stop()` or `Pause()` first.

## Frame events

Type a function name into a frame's **Event function** box, e.g. `Footstep`. When playback reaches
that frame, the engine calls `Footstep` on **every enabled script on the entity** that defines it,
passing the frame's name:

```lua
function Footstep(frameName)
    entity.audioSource:Play()
end
```

- Several frames can call the same function: frames 2 and 6 can both call `Footstep`.
- An event fires when playback **reaches** a frame: at the start of playback, at each step, and
  again each time a loop comes back to it. `SetFrame` jumps without firing.
- If none of the entity's scripts defines the function, a warning is logged once and nothing
  else happens. Check the spelling.
- The name must be a plain Lua name (letters, digits and `_`, not starting with a digit). Lua
  keywords and engine names like `Update` or `Start` are refused. The editor marks a bad name in
  red and won't save until it's fixed.
- An error in the function is reported like any callback error and doesn't stop the animation.

## In the editor

### Making a flipbook

1. In the Asset Browser, right-click a folder (or empty space) and choose
   **Create File > Flipbook (.fpk)**. The new file opens in the flipbook editor.
2. Press **Add Frame** for each picture of the animation.
3. Press **Edit** on a frame and drag a texture from the Asset Browser onto each direction you
   need.
4. Press **Play** in the Preview to check the timing. **Side** picks which direction the preview
   shows.
5. **Save** (or Ctrl+S).

Double-clicking a `.fpk` in the Asset Browser opens it. Each file gets its own window, and a `*`
in the title means unsaved changes. Closing a window with unsaved changes asks whether to save.

### The frame list

| Action | How |
|---|---|
| Reorder | Drag a frame onto another one. |
| Rename | Double-click the name, or right-click > **Rename**. Enter keeps it, Escape cancels. |
| Edit textures and event | **Edit** opens the frame in its own window. |
| Duplicate, insert, delete | Right-click the frame. |

A frame's event function is shown next to its name, in red if it isn't a valid name.

Renaming a frame doesn't update scripts that call `SetFrame` with the old name. They report an
error when they run.

### Renaming and moving files

Renaming or moving a `.fpk` in the Asset Browser updates the Flipbook components and
`FlipbookAsset` script fields in the open level. Renaming or moving a texture updates every
`.fpk` under `Assets` that uses it, including ones open in the flipbook editor.

### The Flipbook component

| Field | Lua | Default | Notes |
|---|---|---|---|
| **Flipbook File** | `flipbookFileName` | empty | Path relative to `Assets`, with `.fpk`. Drag a flipbook from the Asset Browser. **Open in Flipbook Editor** opens it. |
| **Sprite** | | First sprite | Which of the entity's sprites to animate. |
| **Speed** | `speed` | `1` | Playback rate. `2` is twice as fast, `0` freezes it. |

The **Info** section shows the file's frame count and settings, and warns if the entity has no
sprite, the file can't be loaded, or a frame has no texture for a side the sprite shows.

## Scripting

| Property | Type | | Description |
|---|---|---|---|
| `isValid` | boolean | read-only | `false` if the entity or its Flipbook is gone. |
| `isPlaying` | boolean | read-only | `true` while frames are advancing. `false` when paused, stopped, or a Once flipbook has finished. |
| `flipbookFileName` | string | read/write | The `.fpk` path, e.g. `"Animations/imp_walk.fpk"`. Setting a different file rewinds to its first frame, without changing whether it plays. |
| `speed` | number | read/write | Playback rate, `1` = the file's own timing. `0` or less freezes it. |

| Method | Description |
|---|---|
| `Play()` | Plays the current file. Resumes from the current frame. A finished Once flipbook starts over. |
| `Play(file)` | Plays `file`. Does nothing if `file` is already playing, so it's safe to call every frame. A different file starts from its first frame. |
| `Play(file, true)` | Plays `file` from its first frame, even if it's already playing. |
| `Pause()` | Stops advancing and keeps the current frame. |
| `Resume()` | Continues from the current frame. |
| `Stop()` | Stops and rewinds to the first frame, which the sprite then shows. |
| `SetFrame(name)` | Jumps to the frame called `name`, without firing its event. An unknown name is reported in the console (with the script's file and line) and changes nothing. |
| `GetFrame()` | The name of the frame being shown. `""` if there's no file. |

A `FlipbookAsset` public field holds the file's path as a string, so it can go straight into
`Play`:

```lua
public FlipbookAsset walkAnim = nil
-- walkAnim is "Animations/imp_walk.fpk", or "" if nothing is picked
```

## Examples

### Walk and idle

```lua
-- Scripts/Flipbooks/WalkIdle.lua
-- Entity with a Sprite, a Flipbook and a Rigidbody.
public FlipbookAsset idleAnim = nil
public FlipbookAsset walkAnim = nil

function Update()
    local velocity = entity.rigidbody.velocity
    local moving = velocity.x * velocity.x + velocity.z * velocity.z > 1

    -- Play does nothing when that file is already playing, so this is
    -- safe every frame.
    if moving then entity.flipbook:Play(walkAnim) else entity.flipbook:Play(idleAnim) end
end
```

### Footsteps from frame events

```lua
-- Scripts/Flipbooks/Footsteps.lua
-- In the walk flipbook, set the Event function of the frames where a foot
-- lands to "Footstep".
public number pitchVariation = 0.1

function Footstep(frameName)
    local audio = entity.audioSource
    if audio == nil then return end

    audio.pitch = mathT.RandomF(1 - pitchVariation, 1 + pitchVariation)
    audio:Play()
end
```

### Attack that deals damage on one frame

```lua
-- Scripts/Flipbooks/Attack.lua
-- attackAnim is a Once flipbook. Its "strike" frame calls DealDamage.
-- The target needs the Health script from docs/scripts/10_health_and_damage.md.
public FlipbookAsset attackAnim = nil
public FlipbookAsset idleAnim = nil
public Entity target = nil
public number damage = 10

local attacking = false

function StartAttack()
    attacking = true
    entity.flipbook:Play(attackAnim, true)
end

function DealDamage(frameName)
    if target ~= nil then target:GetScript("Health"):TakeDamage(damage) end
end

function Update()
    -- A Once flipbook stops on its last frame.
    if attacking and not entity.flipbook.isPlaying then
        attacking = false
        entity.flipbook:Play(idleAnim)
    end
end
```

### Show a frame for a state

```lua
-- Scripts/Flipbooks/Lever.lua
-- A flipbook with two frames named "off" and "on", Play on start turned off.
public bool isOn = false

function Start()
    entity.flipbook:SetFrame(isOn and "on" or "off")
end

function Toggle()
    isOn = not isOn
    entity.flipbook:SetFrame(isOn and "on" or "off")
end
```
