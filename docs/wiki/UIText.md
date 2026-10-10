# UI Text

**Inspector name:** Text · **Lua:** `entity.uiText` · **Public field type:** not available

A Text component draws text on the HUD: scores, ammo counts, messages, timers.

## How it works

- The text starts at the **top-left** corner of the entity's [UI Transform](UITransform.md)
  rectangle, inset from the left and top edges (8 pixels at the
  [UI Reference Height](#size-and-the-ui-reference-height)).
- It is drawn in its **Color** (white by default), in its **Font** (the engine's default font,
  Noto Sans, when none is set) at its **Font Size**.
- **Color** is RGBA, each channel `0..1`, like a [Sprite](Sprite.md)'s. Unlike sprites and UI
  images, text really is blended: an alpha of `0.5` makes it half see-through, and `0` hides it.
  Sector light never affects it.
- The UI Transform's size is only used to find that corner: text isn't scaled to fit, isn't wrapped
  and isn't clipped. Long text runs past the rectangle.
- A new line (`\n`, or Enter in the inspector) starts a new line of text, one line height below.
- The UI Transform's `rotation` doesn't turn text.
- Text is drawn after every [Image](UISprite.md), so it always appears on top.

Because only the top-left corner matters, the easiest way to place text is with the **pivot** at
`0, 0` and the anchor where you want the text to start. Centring text isn't automatic: the width of
the text isn't known to scripts.

### Fonts

Any `.ttf` or `.otf` file under `Assets` can be a Text's font. The Asset Browser shows them as font
assets: drag one onto the **Font** field (or double-click it, then click the field).
**Clear** goes back to the default font.

- The **Font** is stored as the file's path inside `Assets`, with its extension, e.g.
  `"Fonts/title.ttf"`. Renaming or moving the file in the Asset Browser updates the Texts in the
  open level that use it.
- A font that can't be loaded (wrong path, broken file) is reported in the log once, and the text
  is drawn in the default font instead.
- Exported games include every file in `Assets`, so the font ships with the game.

### Characters

Text is **UTF-8**, so any language works: `"Ölüm"`, `"Привет"`, `"→ ★"`. Each character is
prepared the first time it is drawn, so there's no list of supported characters to set up.

If the Text's font doesn't have a character, it is taken from the default font (Noto Sans covers
Latin, Greek and Cyrillic). If neither has it, the font's "missing character" box is drawn and the
log says which character was missing, once.

### Size and the UI Reference Height

**Font Size** is the text's size in pixels **at the project's UI Reference Height**, set in
**Project Settings > UI** (default `1080`). The text is drawn at:

```
drawn size = Font Size × window height / UI Reference Height
```

So with the defaults, a Font Size of `48` is 48 pixels tall in a 1080-pixel-tall window and 24
pixels in a 540-pixel one: text keeps its size relative to the screen. The 8-pixel inset scales
the same way. UI Transform positions and sizes are still plain pixels and do **not** scale, so
very different window sizes can move text relative to its rectangle's other edges.

A Font Size of `0` hides the text. The largest drawn size is 512 pixels.

The UI Editor's canvas previews each Text in its own Font and Font Size, at the editor window's
height.

## In the editor

In the **UI Editor**: **Add Text Component**.

| Field | Lua | Default | Notes |
|---|---|---|---|
| **Text** | `text` | empty | What to show. Enter starts a new line. |
| **Font** | `font` | empty (default font) | A `.ttf` / `.otf` from the Asset Browser. See [Fonts](#fonts). |
| **Font Size** | `fontSize` | `48` | Pixels at the UI Reference Height. See [Size](#size-and-the-ui-reference-height). |
| **Color** | `color` | `1, 1, 1, 1` | RGBA, `0..1`. Alpha below `1` makes the text see-through. |

## Scripting

| Property | Type | | Description |
|---|---|---|---|
| `isValid` | boolean | read-only | `false` if the entity or its Text is gone. |
| `text` | string | read/write | Shown from the next frame. UTF-8; `\n` starts a new line. |
| `font` | string | read/write | Font path relative to `Assets`, with its extension, e.g. `"Fonts/title.ttf"`. `""` = the default font. |
| `fontSize` | number | read/write | Pixels at the project's UI Reference Height. `0` or less hides the text. |
| `color` | Vector4 | read/write | RGBA, each `0..1`. `color.w` is the alpha. |

```lua
-- A title that's bigger and uses its own font.
local title = entity.uiText
title.font = "Fonts/title.ttf"
title.fontSize = 96
title.color = Vector4(1, 0.8, 0.2, 1) -- gold
title.text = "Level 1\nThe Hangar"
```

### Fade text out

```lua
-- Scripts/UI/FadeOut.lua (UI entity with a Text): fades the text out over `duration` seconds.
public number duration = 2

local elapsed = 0

function Update()
    elapsed = elapsed + GameTime.deltaTime
    local c = entity.uiText.color
    entity.uiText.color = Vector4(c.x, c.y, c.z, math.max(0, 1 - elapsed / duration))
end
```

A colour read from `color` is a copy, so change it and assign it back, as above.

Changing `font` or `fontSize` often is fine: each font is loaded once, and characters at a new size
are prepared the first time they're drawn.

Build strings with `..` and `tostring`, or `string.format` for number formatting:

```lua
entity.uiText.text = string.format("Time %02d:%02d", minutes, seconds)
```

Only write `text` when the value actually changes. Setting it every frame works, but it's wasted
work.

## Examples

### Timer

```lua
-- Scripts/UI/Timer.lua (UI entity with a Text)
local elapsed = 0
local shown = -1

function Update()
    elapsed = elapsed + GameTime.deltaTime

    local whole = math.floor(elapsed)
    if whole == shown then return end
    shown = whole

    entity.uiText.text = string.format("%02d:%02d", whole // 60, whole % 60)
end
```

### Message that disappears

```lua
-- Scripts/UI/Message.lua (UI entity with a Text)
-- Anything can show a message with: Global.ShowMessage("Door unlocked", 3)
local timeLeft = 0

function Start()
    entity.uiText.text = ""

    Global.ShowMessage = function(message, seconds)
        entity.uiText.text = message
        timeLeft = seconds or 2
    end
end

function Update()
    if timeLeft <= 0 then return end

    timeLeft = timeLeft - GameTime.deltaTime
    if timeLeft <= 0 then entity.uiText.text = "" end
end
```

### Show what the player is looking at

```lua
-- Scripts/UI/LookAtLabel.lua (UI entity with a Text)
public Entity player = nil
public number reach = 200

local shown = nil

function Update()
    if player == nil then return end

    local hit = player.camera:Raycast(reach, false)

    local label = ""
    if hit ~= nil and hit.entity ~= nil then label = hit.entity.name end

    if label ~= shown then
        shown = label
        entity.uiText.text = label
    end
end
```

More HUD scripts are in [`docs/scripts/14_ui.md`](../scripts/14_ui.md).
