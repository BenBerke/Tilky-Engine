# UI Transform

**Inspector name:** UI Transform · **Lua:** `entity.uiTransform` · **Public field type:** not available

The UI Transform places a UI element on the screen. It is to UI entities what the
[Transform](Transform.md) is to world entities: every UI entity has one, and the
[Image](UISprite.md) and [Text](UIText.md) components draw inside the rectangle it describes.

Its layout model is **anchors + pivot + offset**. Elements can stick to a corner or an edge of the
screen, or stretch with it, so a HUD keeps its layout at any window size.

## How it works

All values are in **screen space**:

- `(0, 0)` is the **top-left** corner of the window and `(1, 1)` the **bottom-right**. Y grows
  **downward**.
- **Anchors** and **pivot** are fractions from `0` to `1`.
- **Position** and **scale** are in **pixels**.

The rectangle is recalculated every frame from the current window size.

### Anchors

The anchors say which point, or which part, of the screen the element is attached to.

- **Min Anchor = Max Anchor** (the default, `0.5, 0.5`): the element is attached to that **point**
  of the screen. Its size is `scale` in pixels. Examples: `0, 0` is the top-left corner,
  `1, 0` the top-right, `0.5, 1` the bottom middle.
- **Min Anchor ≠ Max Anchor on an axis**: the element **stretches** along that axis to cover the
  span between the two anchors, and `scale` is ignored for that axis. For example, Min `0, 0` and
  Max `1, 0` makes a bar across the whole top of the screen, `scale.y` pixels tall.

Each axis is handled separately, so an element can stretch horizontally and have a fixed height.

### Pivot

The pivot is the point **of the element itself** that is placed on the anchor, as a fraction of
the element's size. `0.5, 0.5` centres the element on the anchor, `0, 0` puts its top-left corner
there, and `1, 1` its bottom-right. Match the pivot to the anchor for corner elements: anchor
`1, 0` with pivot `1, 0` keeps a top-right element fully on screen.

### Position

`position` is an offset **in pixels** from the anchor point. Positive `x` moves right and positive
`y` moves **down**.

### Putting it together

```
anchor point  = screen size × anchor
top-left      = anchor point + position − size × pivot
size          = scale  (or the anchor span, on stretched axes)
```

On a stretched axis the element always covers the span between its anchors, whatever its pivot:
its top-left on that axis is `screen size × Anchor Min + position`.

Those results are readable as `resolvedPosition` (top-left corner) and `resolvedSize`.

### Rotation

`rotation` turns the element around its centre, in degrees. It applies to [Images](UISprite.md).
[Text](UIText.md) ignores it.

## In the editor

UI entities are edited in the **UI Editor** (right-click the canvas and pick **Create UI Entity**). The
canvas previews the layout at a target resolution.

| Field | Lua | Default | Notes |
|---|---|---|---|
| **Anchor Min** | `anchorMin` | `0.5, 0.5` | Fractions of the screen. **Presets** sets both anchors at once: corners, edges, centre, and horizontal, vertical or full stretch. |
| **Anchor Max** | `anchorMax` | `0.5, 0.5` | |
| **Pivot X/Y** | `pivot` | `0.5, 0.5` | Fraction of the element. Drag inside the diagram, or use the presets. |
| **Position** | `position` | `0, 0` | Pixels from the anchor. |
| **Scale** | `scale` | `1, 1` | **Size in pixels**. The default is a 1 × 1 pixel dot, so set a real size. |
| **Rotation** | `rotation` | `0` | Degrees. |
| **Resolved Layout** | `resolvedPosition`, `resolvedSize` | | The computed result, read-only. |

### On the canvas

| Do this | To |
|---|---|
| Right-click | **Create UI Entity** (100 × 100, centred on the cursor); on an element also Duplicate, Frame and Delete. |
| Click | Select the topmost element under the cursor. Click the same spot again to select the one underneath. |
| Shift/Ctrl + click | Add the element to the selection, or take it out. |
| Drag on empty canvas | Box select every element the box touches. |
| Drag an element, the square or an arrow | Move the selection (an arrow moves along its axis only). |
| Drag a corner or edge handle | Resize; the opposite side stays put. Hold Shift to keep the aspect ratio. A stretched axis has no handles: its size comes from the anchors. |
| Drag the handle above the top edge | Rotate. |
| Arrow keys | Nudge the selection 1 pixel (10 with Shift). |
| Ctrl+D, Delete, Ctrl+Z / Ctrl+Y | Duplicate, delete, undo / redo. |
| F | Frame the selection (or the whole screen). |
| Escape | Cancel the drag, else deselect. |

While moving and resizing, edges and centres **snap** to the screen's edges and centre, to other
elements (**To Elements**), to the grid and to the safe area while those overlays are shown; a pink
guide shows what it snapped to. Rotation snaps to 15°. Hold Alt to ignore snapping, or turn
**Snap** off (N). Every key here can be rebound in **User Settings** (UI Editor groups).

**Arrange** aligns the selection's left/centre/right/top/middle/bottom edges: to the screen when
one element is selected, to their combined bounds when several are. **Distribute** spaces three or
more evenly.

## Scripting

| Property | Type | | Description |
|---|---|---|---|
| `isValid` | boolean | read-only | `false` if the entity or its UI Transform is gone. |
| `anchorMin` | Vector2 | read/write | |
| `anchorMax` | Vector2 | read/write | |
| `pivot` | Vector2 | read/write | |
| `position` | Vector2 | read/write | Pixels. |
| `scale` | Vector2 | read/write | Size in pixels. |
| `rotation` | number | read/write | Degrees. |
| `resolvedPosition` | Vector2 | read-only | Final top-left corner on screen, in pixels. Updated when the frame is drawn. |
| `resolvedSize` | Vector2 | read-only | Final size in pixels. |

Set a whole vector (`ui.position = Vector2(10, 20)`) or one component (`ui.position.x = 10`).

## Examples

### Health bar that shrinks

```lua
-- Scripts/UI/HealthBar.lua (UI entity with an Image, anchored top-left, pivot 0, 0)
public Entity player = nil
public number fullWidth = 300

local health

function Start()
    if player ~= nil then health = player:GetScript("Health") end
end

function Update()
    if health == nil or not health.isValid then return end

    local fraction = mathT.Clamp01(health.health / health.maxHealth)
    local ui = entity.uiTransform
    ui.scale = Vector2(fullWidth * fraction, ui.scale.y)
end
```

Because the pivot is at the left edge, the bar shrinks toward the left.

### Slide in from off-screen

```lua
-- Scripts/UI/SlideIn.lua
public Vector2 from = Vector2(0, -200)
public number duration = 0.5

local target
local t = 0

function Start()
    target = entity.uiTransform.position
    entity.uiTransform.position = target + from
end

function Update()
    if t >= duration then return end

    t = math.min(t + GameTime.deltaTime, duration)
    local k = mathT.SmoothStep(0, 1, t / duration)
    entity.uiTransform.position = mathT.Vector2Lerp(target + from, target, k)
end
```

### Crosshair that spins while aiming

```lua
-- Scripts/UI/Crosshair.lua (Image anchored and pivoted at 0.5, 0.5)
function Update()
    local ui = entity.uiTransform
    if Input.GetMouseButton(Input.MouseRight) then
        ui.rotation = ui.rotation + 180 * GameTime.deltaTime
        ui.scale = Vector2(48, 48)
    else
        ui.scale = Vector2(32, 32)
    end
end
```
