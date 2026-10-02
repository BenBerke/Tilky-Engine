# Model

**Inspector name:** Model · **Lua:** `entity.model` · **Public field type:** `Model`

A Model draws a 3D mesh at the entity's position: crates, furniture, weapons on a table, a statue.
Unlike a [Sprite](Sprite.md), it's real 3D geometry that looks right from every angle.

## How it works

### File formats

Supported files: **`.glb`, `.gltf`, `.obj`, `.fbx`, `.dae`, `.3ds`, `.ply`, `.stl`**. `.glb` is the
easiest, because textures are packed inside the one file. Put models anywhere under `Assets`, for
example `Assets/Models/crate.glb`.

A model can bring its own textures, either embedded in the file or as separate image files next to
it. The inspector's **Textures & Files** list shows what the model needs, marking each one
**embedded** or **missing**. **Rescan Dependencies** checks again after you've added files.

### Loading

- A file is loaded the first time an entity needs it, then **shared**: a hundred crates using
  `crate.glb` load it once.
- If a file fails to load, the error is logged once and the entity draws nothing. The engine
  doesn't retry that file while running.
- Changing `fileName` from a script swaps the model on the **next frame**. Swapping to a file that
  isn't loaded yet loads it at that moment, which can cause a small hitch the first time.

### Position, rotation and scale

When a model is loaded, the engine **normalises** it, whatever size or origin it had in your
modelling tool:

- it is shrunk or grown so its **longest side is exactly 1 unit**, keeping its proportions;
- it is moved so its **bottom centre** sits at the origin.

Then it is drawn from its [Transform](Transform.md):

- The bottom centre sits at the Transform **position**, the feet, so models stand on the floor
  without any adjustment.
- The Transform's **rotation** turns it.
- The Transform's **scale** is the model's size in units, on each axis. New entities start at
  scale **32**, so any model shows up with its longest side 32 units long. A crate at `16, 16, 16`
  is 16 units across. A tall statue whose longest side is its height becomes that many units tall.

The units a model was authored in don't matter. Only the Transform scale decides how big it is.

### Lighting

The model's own colours and textures are shaded by the [sector](Sector.md#light)'s light: each
channel is multiplied by `light / 255`. Pixels whose texture alpha is below 0.1 are cut out.

### Collision

A Model has **no** collision of its own. To make a model solid, add a [Collider](Collider.md)
roughly the size of the model.

## In the editor

| Field | Lua | Notes |
|---|---|---|
| **Model File** | `fileName` | Path relative to `Assets`, with extension. Drag a model from the Asset Browser. |
| **Textures & Files** | | What the model depends on. |
| **Rescan Dependencies** | | Looks for missing files again. |

## Scripting

| Property | Type | | Description |
|---|---|---|---|
| `isValid` | boolean | read-only | `false` if the entity or its Model is gone. |
| `fileName` | string | read/write | Model path, e.g. `"Models/crate.glb"`. Empty draws nothing. |

| Method | Description |
|---|---|
| `clearFileName()` | Empties `fileName`, so nothing is drawn. |

Everything else about how a model looks is set through its [Transform](Transform.md).

## Examples

### Spinning pickup

```lua
-- Scripts/Models/Spin.lua
---@field degreesPerSecond number @ Spin Speed
degreesPerSecond = 120

local angle = 0

function Update()
    angle = angle + degreesPerSecond * GameTime.deltaTime
    local half = mathT.DegToRad(angle) * 0.5
    entity.transform.rotation = Vector4(0, mathT.Sin(half), 0, mathT.Cos(half))
end
```

### Breakable object

```lua
-- Scripts/Models/Breakable.lua
-- Swap to a broken model after enough hits: crate:GetScript("Breakable"):Hit()
---@field brokenModel string @ Broken Model
brokenModel = "Models/crate_broken.glb"

---@field hitsToBreak int @ Hits To Break
hitsToBreak = 3

local hits = 0

function Hit(self)
    hits = hits + 1
    if hits == hitsToBreak then
        entity.model.fileName = brokenModel
        if entity.collider ~= nil then entity.collider.isActive = false end
    end
end
```

### Pop in with a scale animation

```lua
-- Scripts/Models/PopIn.lua
---@field duration number @ Seconds
duration = 0.4

local finalScale
local t = 0

function Start()
    finalScale = entity.transform.scale
    entity.transform.scale = Vector3(0, 0, 0)
end

function Update()
    if t >= duration then return end

    t = math.min(t + GameTime.deltaTime, duration)
    local k = mathT.SmoothStep(0, 1, t / duration)
    entity.transform.scale = finalScale * k
end
```
