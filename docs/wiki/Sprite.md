# Sprite

**Inspector name:** Sprite · **Lua:** `entity.sprite` · **Public field type:** `Sprite`

A Sprite draws a flat image in the world at the entity's position: the classic way to show
monsters, items, lamps and decorations in a sector-based game. It can always turn to face the
camera, and it can swap between 4 or 8 pictures depending on which side you look at it from, so a
monster looks different from the front, the side and the back.

## How it works

### Size and position

The image stands upright with its **bottom centre** on the entity's [Transform](Transform.md)
position, which is the feet:

- **width** = the Transform's `scale.x`
- **height** = the Transform's `scale.z`

The Transform's `scale.y` isn't used by sprites. New entities start at scale 32, a 32 × 32 sprite.

### Several on one entity

An entity can have several Sprites, all drawn. Each sits at the Transform position plus its
`offset` (turned with the entity's rotation). They all use the Transform's scale. `entity.sprite`
is the first one; `entity:GetComponents(Component.Sprite)` lists them all. See
[Several components of one type](Entity.md#several-components-of-one-type).

### Facing the camera

- **Normal sprites** (Is Static off) are **billboards**: they always turn to face the camera around
  the vertical axis, and always stay upright. You can never see them edge-on.
- **Static sprites** (Is Static on) don't turn. They're oriented by the Transform's **rotation**,
  like a flat card or poster placed in the world. Viewed side-on they're a thin line.

### Is Active

An inactive sprite isn't drawn at all, in the game or in the editor's 3D view. Everything else
about the entity carries on: its scripts run, its [Collider](Collider.md) still collides and its
[Audio Source](AudioSource.md) still plays. Use it to hide something and show it again later
without removing the component.

### Directions

**Direction Level** chooses how many pictures the sprite has:

| Direction Level | Lua `sideCount` | Pictures used |
|---|---|---|
| **Single** | `0` | Only the first (North) picture, from every angle. |
| **8-Sided** | `1` | All eight: N, NE, E, SE, S, SW, W, NW. |
| **4-Sided** | `2` | Four: N, E, S, W. |

With 4 or 8 sides, the engine compares the sprite's facing with the direction from the sprite to
the camera. The facing is the way the Transform's `rotation` points (its local +Z), flattened onto
the map, so only rotation Y matters. Rotation `0, 0, 0` faces +Z.

- **N** (North) is shown when the camera is **in front** of the sprite, looking at its face.
- **S** (South) is shown when the camera is **behind** it.
- The others follow in order around the sprite: N, NE, E, SE, S, SW, W, NW. Each picture covers a
  45° slice with 8 sides, or a 90° slice with 4.

To turn a directional sprite, set its rotation: **Rotation Y** in the inspector, or
`transform:LookAt(point)` / `transform:LookDirection(direction)` from Lua. If the side pictures
appear on the opposite sides from what you expected, swap E with W (and NE with NW, SE with SW).

### Colour and light

- **Color** tints the image. Each channel `0..1` multiplies the texture's colour.
- The **alpha** of Color does **not** make the sprite transparent. Only the texture's own
  transparency counts: pixels with alpha below 0.1 are cut out completely, everything else is fully
  opaque. There is no partial transparency.
- The [sector](Sector.md#light)'s light darkens the sprite. Each channel of the tint is reduced by
  `(255 - light) / 255`, so a sector light of `128` takes about half off.
- A picture slot with **no** texture draws as a solid black rectangle. Fill every slot the
  Direction Level uses.

## In the editor

| Field | Lua | Default | Notes |
|---|---|---|---|
| **Texture Index** (one per direction) | `northTextureFileName`, ... or `GetTextureFileName(slot)` | empty | Image paths relative to `Assets`. |
| **Direction Level** | `sideCount` | Single | See [Directions](#directions). |
| **Color** | `color` | `1, 1, 1, 1` | Tint. |
| **Is Static** | | off | Editor only. |
| **Is Active** | `isActive` | on | Off = not drawn. See [Is Active](#is-active). |
| **Offset** | `offset` | `0, 0, 0` | See [Several on one entity](#several-on-one-entity). |

## Scripting

| Property | Type | | Description |
|---|---|---|---|
| `isValid` | boolean | read-only | `false` if the entity or its Sprite is gone. |
| `isActive` | boolean | read/write | `false` hides the sprite. |
| `offset` | Vector3 | read/write | Position relative to the Transform, turned with its rotation. See [Several on one entity](#several-on-one-entity). |
| `color` | Vector4 | read/write | Tint, `0..1`. |
| `sideCount` | integer | read/write | `0` single, `1` 8-sided, `2` 4-sided. Other values are ignored. |
| `northTextureFileName` | string | read/write | Slot 0. |
| `northEastTextureFileName` | string | read/write | Slot 1. |
| `eastTextureFileName` | string | read/write | Slot 2. |
| `southEastTextureFileName` | string | read/write | Slot 3. |
| `southTextureFileName` | string | read/write | Slot 4. |
| `southWestTextureFileName` | string | read/write | Slot 5. |
| `westTextureFileName` | string | read/write | Slot 6. |
| `northWestTextureFileName` | string | read/write | Slot 7. |

| Method | Description |
|---|---|
| `GetTextureFileName(slot)` | The texture in `slot`. |
| `SetTextureFileName(slot, fileName)` | Sets the texture in `slot`. |
| `ClearTextureFileName(slot)` | Empties `slot`. |
| `ClearAllTextureFileNames()` | Empties all eight slots. |

> **Slots are numbered from 0**, `0` (N) to `7` (NW), unlike most lists in the Lua API. A slot
> outside `0..7` is ignored: getters return `""` and setters do nothing.

Texture paths are the same strings the inspector stores, relative to `Assets` with the extension,
e.g. `"Textures/Monsters/imp_front.png"`.

> **Any image in `Assets` can be switched to.** When the level loads, the engine packs every
> `.png`, `.jpg` and `.jpeg` file under `Assets` into the texture atlas, so a script can switch to
> an image the level doesn't use yet. An image added to `Assets` while the game is running is only
> picked up the next time the level loads.

## Examples

### Flip-book animation

```lua
-- Scripts/Sprites/Animate.lua (single-direction sprite)
-- Cycles through numbered frames: Textures/Fire/fire1.png, fire2.png, ...
public string folder = "Textures/Fire/fire"
public int frameCount = 4
public number fps = 8

local t = 0
local shown = -1

function Update()
    t = t + GameTime.deltaTime
    local frame = math.floor(t * fps) % frameCount + 1

    if frame ~= shown then
        shown = frame
        entity.sprite:SetTextureFileName(0, folder .. frame .. ".png")
    end
end
```

Only change the texture when the frame actually changes, not every frame.

For anything more than a simple loop (a picture per direction, different timings per frame,
sounds on certain frames, switching between walk and idle) use a [Flipbook](Flipbook.md) instead.
While a Flipbook plays on a sprite, it overwrites that sprite's textures.

### Flash red when hurt

```lua
-- Scripts/Sprites/HurtFlash.lua
-- Another script calls enemy:GetScript("HurtFlash"):Flash()
public number flashTime = 0.15

local timeLeft = 0

function Flash(self)
    timeLeft = flashTime
end

function Update()
    if timeLeft > 0 then
        timeLeft = timeLeft - GameTime.deltaTime
        entity.sprite.color = Vector4(1, 0.3, 0.3, 1)
    else
        entity.sprite.color = Vector4(1, 1, 1, 1)
    end
end
```

### Face the way you walk

```lua
-- Scripts/Sprites/FaceMovement.lua (8-sided sprite on something with a Rigidbody)
function Update()
    local v = entity.rigidbody.velocity

    if Vector2(v.x, v.z).length > 1 then entity.transform:LookDirection(v) end
end
```

### Glow pulse

```lua
-- Scripts/Sprites/Pulse.lua
public number speed = 2

local t = 0

function Update()
    t = t + GameTime.deltaTime
    local k = 0.6 + 0.4 * (mathT.Sin(t * speed * mathT.Tau) + 1) * 0.5
    entity.sprite.color = Vector4(k, k, k, 1)
end
```
