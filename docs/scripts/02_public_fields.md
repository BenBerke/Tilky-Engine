# 02 - Public Fields

Public fields are top-level variables that show up in the inspector. You declare one with
`public`, a type, a name and a default value:

```text
public <type> <name> = <default>
```

- The inspector label comes from the name: `moveSpeed` shows as "Move Speed", `jump_key` as
  "Jump Key".
- `public` only works at the top level of the script, not inside a function or block, and not
  together with `local`.
- The default has to be a plain value (`5`, `1.5`, `true`, `"text"`, `Vector3(0, 1, 0)`, `nil`),
  since the editor reads it without running the script. `= <default>` can be left out; the field
  then starts at zero, empty or `nil`.
- A mistake in a declaration (an unknown type, a default of the wrong type, ...) is a script error
  shown in the inspector, like a syntax error.

## Supported types

| Type | Lua value in the script | Default value |
|------------|-------------------------|-----------------|
| `number` / `float` | number | `1.5` |
| `int` / `integer` | number | `3` |
| `bool` / `boolean` | boolean | `true` |
| `string` | string | `"hello"` |
| `Vector2` / `Vector3` / `Vector4` | vector | `Vector3(0, 1, 0)` |
| `enum(A, B, C)` | integer, the option's position starting at `0` | one of the options, e.g. `B` |
| `Key` | a [`Key`](../wiki/Input.md#keys) value | `Key.E` |
| `Entity` | `Entity` (or `nil` if unassigned) | `nil` |
| `Behaviour` | another script on some Entity (or `nil`) | `nil` |
| `Wall` | `Wall` (or `nil` if unassigned) | `nil` |
| `Sector` | `Sector` (or `nil` if unassigned) | `nil` |
| `Transform`, `Sprite`, `Model`, `Flipbook`, `AudioSource`, `PlayerController`, `Camera`, `Collider`, `Rigidbody` | that component (or `nil`) | `nil` |
| `Asset` / `Texture` | the asset's path as a string | `nil` |
| `FlipbookAsset` | a `.fpk` flipbook's path as a string, for [`flipbook:Play`](../wiki/Flipbook.md#scripting) | `nil` |

Array types (`number[]`) are not supported yet.

## Example: every type in one script

**Attach to:** any Entity.

```lua
-- Scripts/Examples/AllFieldTypes.lua (entity script)
public number speed = 3.5
public int lives = 3
public bool canFly = false
public string title = "Boss"
public Vector3 offset = Vector3(0, 10, 0)
public Vector4 tint = Vector4(1, 0.5, 0.5, 1)
public enum(Idle, Patrol, Chase) mode = Patrol
public Entity target = nil
public Transform targetTransform = nil
public Behaviour partner = nil
public Texture icon = nil

-- Enum values arrive as numbers, in the order they were listed above.
local MODE_IDLE, MODE_PATROL, MODE_CHASE = 0, 1, 2

function Start()
    Debug.Print(title, "speed", speed, "lives", lives, "canFly", canFly)
    Debug.Print("offset", offset.x, offset.y, offset.z)
    Debug.Print("tint", tint.x, tint.y, tint.z, tint.w)

    if mode == MODE_PATROL then
        Debug.Print("starting in Patrol mode")
    end

    if target ~= nil then
        Debug.Print("target is", target.name)
    end

    if targetTransform ~= nil then
        local p = targetTransform.position
        Debug.Print("target transform at", p.x, p.y, p.z)
    end

    if partner ~= nil and partner.isValid then
        Debug.Print("partner lives on", partner.entity.name)
    end

    if icon ~= nil then
        Debug.Print("icon path:", icon)
    end
end
```

**Notes**

- **Read fields in `Start` or later.** The inspector's values are applied after the file's top
  level runs, so `local s = speed` at the top of the file would capture the inline default (`3.5`)
  and not the value the designer set.
- References that are unassigned (or point at something that no longer exists) arrive as `nil`.
  Always check before using them.
- `Entity`, `Wall` and `Sector` fields can be filled by dragging a row from the Hierarchy onto the
  field. An entity can also be dragged straight off the level view; it snaps back to where it was
  when you drop it on the field. The dropdown still works too.
- Component fields (`AudioSource`, `Sprite`, `Collider`, ...) work the same way: drop an entity on
  the field and it picks that entity's **first** component of that type. An entity without one
  isn't accepted. To pick a second or third one, use the dropdown.
- A `Behaviour` field lets one script read and write another script's variables. See
  [Using another script's variables](#using-another-scripts-variables) below.
- Sector scripts can't declare fields named `sector` or `entity`. Those names are reserved
  for the built-in globals.
- Enum defaults are written as a number (`mode = 1`), and the option's position in the annotation
  decides its number, so keep that order stable once levels use it.

## Using another script's variables

A `Behaviour` field holds a reference to **one specific script** on some Entity. Through it you can
read and write that script's top-level variables (its public fields included) and call its
functions, as if they were your own.

**Attach to:** `Generator.lua` on one Entity, `Lamp.lua` on another (or the same) Entity.

```lua
-- Scripts/Examples/Generator.lua (entity script)
public number power = 100
public bool running = true

-- Called from other scripts with a colon, so it takes `self` first.
function Drain(self, amount)
    power = math.max(0, power - amount)
    if power == 0 then running = false end
end
```

```lua
-- Scripts/Examples/Lamp.lua (entity script)
public Behaviour generator = nil
public number drainPerSecond = 5

function Update()
    if generator == nil or not generator.isValid then return end

    -- Read the generator's variables.
    if not generator.running then return end
    Debug.Print("generator power:", generator.power)

    -- Call its function...
    generator:Drain(drainPerSecond * GameTime.deltaTime)

    -- ...or write a variable directly.
    if generator.power < 10 then
        generator.running = false
    end
end
```

Select the Lamp's Entity, open the **Generator** field's dropdown and pick the entry
`<Generator Entity> / Generator.lua`. The dropdown lists every script on every Entity in the
level, as `Entity / Script`.

**Notes**

- A write such as `generator.power = 50` changes the generator's own `power`. The generator sees the
  new value the next time it reads `power`.
- Only **top-level, non-`local`** variables and functions are visible. A `local` in the other
  script can't be reached.
- `isValid`, `entity` and `enabled` are built into every Behaviour reference, so a variable with one
  of those names can't be reached through it. `generator.entity` is the Entity the script is on, and
  `generator.enabled = false` switches that one script off.
- The reference points at one exact script instance. If an Entity has two copies of the same script,
  the dropdown shows both and the field keeps the one you picked.
- Sector scripts can't be referenced by a `Behaviour` field or `GetScript`. Only entity scripts can.

### Through an Entity field

A `Behaviour` field has to be pointed at one exact script. When you have an **Entity** instead (from
an `Entity` field, a raycast, a trigger and so on), ask it for the script by file name with
`GetScript`. The result is the same kind of reference, so everything above still applies.

**Attach to:** `Lamp.lua` on any Entity, with `Generator.lua` attached to the Entity you pick.

```lua
-- Scripts/Examples/Lamp.lua (entity script)
public Entity generatorEntity = nil
public number drainPerSecond = 5

local generator = nil

function Start()
    if generatorEntity == nil then return end

    -- Looks for a script named Generator on that Entity.
    generator = generatorEntity:GetScript("Generator")
    if not generator.isValid then
        Debug.Print(generatorEntity.name .. " has no Generator script")
        generator = nil
    end
end

function Update()
    if generator == nil or not generator.isValid then return end
    if not generator.running then return end

    generator:Drain(drainPerSecond * GameTime.deltaTime)
    Debug.Print(generatorEntity.name, "power:", generator.power)
end
```

Drag the generator's row from the Hierarchy onto the **Generator Entity** field, or pick it from the
dropdown.

**Notes**

- `GetScript("Generator")` matches the script's **file name** and ignores its folder.
- If the Entity has no such script, `GetScript` returns a reference whose `isValid` is `false`, not
  `nil`. Check `isValid` before using it.
- If the Entity has several scripts with that name, `GetScript` returns the first one. Use a
  `Behaviour` field instead when you need a specific one.
- `entity:GetScripts()` returns every script on the Entity. It's useful when you don't know the
  script's name, e.g. to call `Interact` on whatever script defines it. See
  [09 - Interaction](09_interaction.md).
- Looking the script up once in `Start` and keeping it in a `local` is cheaper than calling
  `GetScript` every frame. The `isValid` check in `Update` covers the Entity being destroyed later.
- More examples: [10 - Health and Damage](10_health_and_damage.md).
