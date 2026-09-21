# 02 - Public Fields

Public fields are top-level variables that show up in the inspector. You declare one with a
`---@field` comment **immediately above** its default value:

```text
---@field <name> <type> @ <Display Name>
<name> = <default>
```

- The `@ Display Name` part is optional. Without it the variable name is shown.
- Blank lines and ordinary `--` comments may sit between the annotation and the assignment, but
  another `---@field` may not.
- The default has to be a simple literal (`5`, `1.5`, `true`, `"text"`, `Vector3(0, 1, 0)`), since
  the inspector reads it as text without running the script.

## Supported types

| Annotation | Lua value in the script | Default literal |
|------------|-------------------------|-----------------|
| `number` / `float` | number | `1.5` |
| `int` / `integer` | number | `3` |
| `bool` / `boolean` | boolean | `true` |
| `string` | string | `"hello"` |
| `Vector2` / `Vector3` / `Vector4` | vector | `Vector3(0, 1, 0)` |
| `enum(A,B,C)` | integer, the option's position starting at `0` | the option's number, e.g. `1` |
| `GameObject` | `GameObject` (or `nil` if unassigned) | `nil` |
| `Behaviour` | another script on some GameObject (or `nil`) | `nil` |
| `Transform`, `Sprite`, `AudioSource`, `PlayerController`, `Camera`, `Collider`, `Rigidbody` | that component (or `nil`) | `nil` |
| `Asset` / `Texture` | the asset's path as a string | `nil` |

Array types (`number[]`) are not supported yet.

## Example: every type in one script

**Attach to:** any GameObject.

```lua
-- Scripts/Examples/AllFieldTypes.lua (entity script)
---@field speed number @ Move Speed
speed = 3.5

---@field lives int @ Lives
lives = 3

---@field canFly bool @ Can Fly
canFly = false

---@field title string @ Title
title = "Boss"

---@field offset Vector3 @ Offset
offset = Vector3(0, 10, 0)

---@field tint Vector4 @ Tint
tint = Vector4(1, 0.5, 0.5, 1)

---@field mode enum(Idle,Patrol,Chase) @ Mode
mode = 1

---@field target GameObject @ Target
target = nil

---@field targetTransform Transform @ Target Transform
targetTransform = nil

---@field partner Behaviour @ Partner Script
partner = nil

---@field icon Texture @ Icon
icon = nil

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
        Debug.Print("partner lives on", partner.gameObject.name)
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
- To make a field a *script-to-script* API, just read it through a `Behaviour`:
  `partner.speed = 10` writes into that script's own `speed`.
- Sector scripts can't declare fields named `sector` or `gameObject`. Those names are reserved
  for the built-in globals.
- Enum defaults are written as a number (`mode = 1`), and the option's position in the annotation
  decides its number, so keep that order stable once levels use it.
