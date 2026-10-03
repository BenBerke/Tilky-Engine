# UI Sprite

**Inspector name:** Image · **Lua:** `entity.uiSprite` · **Public field type:** not available

An Image draws a picture on the HUD, on top of the game: crosshairs, health bar backgrounds,
icons, a logo. It fills the rectangle of the entity's [UI Transform](UITransform.md).

## How it works

- The texture is stretched to fill the UI Transform's rectangle exactly (`resolvedPosition`,
  `resolvedSize`). For an undistorted picture, give the UI Transform the same aspect ratio as the
  image.
- It turns with the UI Transform's `rotation`.
- It isn't affected by any sector's light. The texture's own transparency is kept.
- Images are drawn before [Text](UIText.md), so text always appears on top of images.
- Like every image in `Assets`, it is packed into the texture atlas when the level loads.

## In the editor

In the **UI Editor**, the component shows up as **Sprite** (**Add Sprite Component**).

| Field | Lua | Notes |
|---|---|---|
| **Texture** | `textureIndex` | Image path relative to `Assets`, with the extension. The editor warns if the file can't be found. |

## Scripting

| Property | Type | | Description |
|---|---|---|---|
| `isValid` | boolean | read-only | `false` if the entity or its Image is gone. |
| `textureIndex` | string | read/write | Despite the name, the image's **path**, e.g. `"Textures/UI/crosshair.png"`. |

Switching images from a script works like any other texture change: any image in `Assets` can be
used (see the note on the [Sprite](Sprite.md) page).

```lua
-- Scripts/UI/AmmoIcon.lua (UI entity with an Image)
---@field fullIcon string
fullIcon = "Textures/UI/ammo_full.png"

---@field emptyIcon string
emptyIcon = "Textures/UI/ammo_empty.png"

function Update()
    local wanted = (Global.ammo or 0) > 0 and fullIcon or emptyIcon
    if entity.uiSprite.textureIndex ~= wanted then entity.uiSprite.textureIndex = wanted end
end
```

### Showing and hiding an image

Images have no visibility switch, and `entity.enabled` only affects scripts. Shrink the element
instead:

```lua
-- Scripts/UI/Toggle.lua (UI entity with an Image)
-- Other scripts call icon:GetScript("Toggle"):SetShown(false)
local shownSize

function Start()
    shownSize = entity.uiTransform.scale
end

function SetShown(self, shown)
    entity.uiTransform.scale = shown and shownSize or Vector2(0, 0)
end
```

This works for images anchored to a point. For stretched images, move them off-screen instead,
for example with `position = Vector2(0, -10000)`.

## Examples

### Damage flash overlay

```lua
-- Scripts/UI/DamageFlash.lua
-- A red, semi-transparent full-screen Image (anchors 0,0 to 1,1).
-- Call flash:GetScript("DamageFlash"):Flash() when the player is hurt.
---@field duration number
duration = 0.2

local timeLeft = 0

function Start()
    entity.uiTransform.position = Vector2(0, -100000) -- hidden
end

function Flash(self)
    timeLeft = duration
    entity.uiTransform.position = Vector2(0, 0)
end

function Update()
    if timeLeft <= 0 then return end

    timeLeft = timeLeft - GameTime.deltaTime
    if timeLeft <= 0 then entity.uiTransform.position = Vector2(0, -100000) end
end
```

### Key icons

```lua
-- Scripts/UI/KeyIcon.lua (one Image per key, e.g. a red key icon)
-- Shows the icon once Global.keys[keyName] is true (set by a pickup script).
---@field keyName string @ Key Name
keyName = "red"

---@field size Vector2 @ Icon Size
size = Vector2(48, 48)

function Update()
    local has = Global.keys ~= nil and Global.keys[keyName] == true
    entity.uiTransform.scale = has and size or Vector2(0, 0)
end
```
