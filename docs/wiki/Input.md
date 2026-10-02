# Input

`Input` reads the keyboard and mouse.

```lua
function Update()
    if Input.GetKeyDown("E") then Debug.Print("E was pressed") end
end
```

Keys are named with strings. Input is checked once per frame, so read it in `Update`, not in
`FixedUpdate` (which may run zero or several times in a frame and miss or repeat a press).

---

## Keyboard

| Function | Returns | |
|---|---|---|
| `GetKeyDown(key)` | boolean | `true` only on the frame the key was pressed. |
| `GetKey(key)` | boolean | `true` every frame while the key is held. |
| `GetKeyUp(key)` | boolean | `true` only on the frame the key was released. |
| `GetDoubleKey(key, keyTwo)` | boolean | `true` while both keys are held. |
| `GetDoubleKeyDown(key, keyTwo)` | boolean | `true` on the frame both keys become held: either one was just pressed while the other is down. Good for shortcuts like `"LCtrl", "S"`. |
| `GetAnyKey()` | string | The name of a held key, or `"UNKNOWN"`. |
| `GetAnyKeyDown()` | string | The name of a key pressed this frame, or `"UNKNOWN"`. |
| `GetAnyKeyUp()` | string | The name of a key released this frame, or `"UNKNOWN"`. |

### Key names

| Group | Names |
|---|---|
| Letters | `"A"` to `"Z"` (capitals) |
| Digits | `"0"` to `"9"` (the row above the letters, not the numpad) |
| Special | `"Space"`, `"Escape"`, `"Enter"`, `"Tab"`, `"Backspace"` |
| Arrows | `"Left"`, `"Right"`, `"Up"`, `"Down"` |
| Modifiers | `"LShift"`, `"RShift"`, `"LCtrl"`, `"RCtrl"`, `"LAlt"`, `"RAlt"` |

Names are case-sensitive. A name that isn't in this list (`"a"`, `"F1"`, `"Shift"`) is not an
error: the functions just return `false`, so check the spelling if a key never seems to work.

Keys are physical positions on a US layout. On other layouts `"Z"` is the key where Z is on a US
keyboard, which keeps WASD-style controls in the same place.

### The GetAnyKey functions

If several keys qualify, they return the one that comes first internally (letters, then digits,
then the rest), and only if it has a name in the table above. Holding `F1` and `LShift` together
returns `"UNKNOWN"`, because `F1` comes first and has no name. They are best for "press any key to continue" and for simple key rebinding.

```lua
-- Wait for a key, then remember it as the "use" key.
local useKey = "E"
local listening = false

function Update()
    if listening then
        local key = Input.GetAnyKeyDown()
        if key ~= "UNKNOWN" then
            useKey = key
            listening = false
        end
    elseif Input.GetKeyDown(useKey) then
        Debug.Print("Use!")
    end
end
```

---

## Mouse

| Name | Returns | |
|---|---|---|
| `GetMouseButtonDown(button)` | boolean | `true` only on the frame the button was pressed. |
| `GetMouseButton(button)` | boolean | `true` while the button is held. |
| `GetMouseButtonUp(button)` | boolean | `true` only on the frame the button was released. |
| `GetMousePosition()` | `Vector2` | The cursor position in window pixels, from the top-left corner. |
| `MouseLeft`, `MouseMiddle`, `MouseRight` | integer | Button numbers to pass to the functions above. |

```lua
if Input.GetMouseButtonDown(Input.MouseLeft) then Fire() end
if Input.GetMouseButton(Input.MouseRight) then Aim() end
```

While the game runs, the mouse is captured for mouse look and the cursor is hidden, so
`GetMousePosition` isn't very useful yet. Mouse look itself is handled by the
[Player Controller](PlayerController.md), not by scripts.

---

## Examples

### Toggle a flashlight

```lua
-- Scripts/Flashlight.lua (sector script): F switches the room light on and off.
local on = true
local litColor

function Start()
    litColor = sector.light
end

function Update()
    if not Input.GetKeyDown("F") then return end

    on = not on
    sector:FadeLight(on and litColor or Vector3(20, 20, 20), 0.1)
end
```

### Charge a throw while a key is held

```lua
-- Scripts/ChargeThrow.lua (on the player): hold G to charge, release to throw.
---@field maxCharge number @ Seconds To Full Power
maxCharge = 1.5

local charge = 0

function Update()
    if Input.GetKey("G") then
        charge = mathT.Min(charge + GameTime.deltaTime, maxCharge)
    end

    if Input.GetKeyUp("G") then
        local power = charge / maxCharge   -- 0..1
        Debug.Print("Throw with power", mathT.Round(power, 2))
        charge = 0
    end
end
```
