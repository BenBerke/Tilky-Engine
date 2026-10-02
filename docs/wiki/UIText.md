# UI Text

**Inspector name:** Text · **Lua:** `entity.uiText` · **Public field type:** not available

A Text component draws a line of text on the HUD: scores, ammo counts, messages, timers.

## How it works

- The text starts at the **top-left** corner of the entity's [UI Transform](UITransform.md)
  rectangle, 8 pixels in from the left and top edges.
- It is drawn in **white**, in the engine's UI font, at a fixed size of about **48 pixels**.
- The UI Transform's size is only used to find that corner: text isn't scaled to fit, isn't wrapped
  and isn't clipped. Long text runs past the rectangle.
- The UI Transform's `rotation` doesn't turn text.
- Text is drawn after every [Image](UISprite.md), so it always appears on top.

Because only the top-left corner matters, the easiest way to place text is with the **pivot** at
`0, 0` and the anchor where you want the text to start. Centring text isn't automatic: the width of
the text isn't known to scripts.

## In the editor

In the **UI Editor**: **Add Text Component**.

| Field | Lua | Notes |
|---|---|---|
| **Text** | `text` | What to show. |

## Scripting

| Property | Type | | Description |
|---|---|---|---|
| `isValid` | boolean | read-only | `false` if the entity or its Text is gone. |
| `text` | string | read/write | Shown from the next frame. |

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
---@field player Entity
player = nil

---@field reach number
reach = 200

local shown = nil

function Update()
    if player == nil then return end

    local p = player.transform.position
    local eye = Vector3(p.x, p.y + player.playerController.eyeHeight, p.z)
    local hit = Game.Raycast(eye, player.camera.forward, reach, player.id, false)

    local label = ""
    if hit ~= nil and hit.entity ~= nil then label = hit.entity.name end

    if label ~= shown then
        shown = label
        entity.uiText.text = label
    end
end
```

More HUD scripts are in [`docs/scripts/14_ui.md`](../scripts/14_ui.md).
