# Input

`Input` reads the keyboard and mouse.

```lua
function Update()
    if Input.GetKeyDown(Key.E) then Debug.Print("E was pressed") end
end
```

Keys come from the [`Key`](#keys) table: `Key.E`, `Key.Space`, `Key.LShift`. Input is checked once per frame, so read it in `Update`, not in
`FixedUpdate` (which may run zero or several times in a frame and miss or repeat a press).

---

## Keyboard

| Function | Returns | |
|---|---|---|
| `GetKeyDown(key)` | boolean | `true` only on the frame the key was pressed. |
| `GetKey(key)` | boolean | `true` every frame while the key is held. |
| `GetKeyUp(key)` | boolean | `true` only on the frame the key was released. |
| `GetDoubleKey(key, keyTwo)` | boolean | `true` while both keys are held. |
| `GetDoubleKeyDown(key, keyTwo)` | boolean | `true` on the frame both keys become held: either one was just pressed while the other is down. Good for shortcuts like `Key.LCtrl, Key.S`. |
| `GetKeyName(key)` | string | The key's name in the `Key` table: `"E"` for `Key.E`, `"Space"` for `Key.Space`. Good for on-screen prompts like `[E] Open`. |
| `GetAnyKey()` | `Key` or `nil` | A held key. |
| `GetAnyKeyDown()` | `Key` or `nil` | A key pressed this frame. |
| `GetAnyKeyUp()` | `Key` or `nil` | A key released this frame. |

### Keys

Every key is a value in the global `Key` table. The script editor and VS Code suggest them as you
type `Key.`.

| Group | Values |
|---|---|
| Letters | `Key.A` to `Key.Z` |
| Number row | `Key.Alpha0` to `Key.Alpha9` (the row above the letters, not the numpad) |
| Special | `Key.Space`, `Key.Escape`, `Key.Enter`, `Key.Tab`, `Key.Backspace` |
| Arrows | `Key.Left`, `Key.Right`, `Key.Up`, `Key.Down` |
| Modifiers | `Key.LShift`, `Key.RShift`, `Key.LCtrl`, `Key.RCtrl`, `Key.LAlt`, `Key.RAlt` |

The values are plain numbers, so you can keep one in a variable or compare two with `==`.

A misspelled key (`Key.Spcae`) is `nil`, and passing `nil` or a number that isn't a key raises an
error naming the problem.

Keys are physical positions on a US layout. On other layouts `Key.Z` is the key where Z is on a US
keyboard, which keeps WASD-style controls in the same place.

### The GetAnyKey functions

They return `nil` when no key qualifies. Keys without a value in the `Key` table, like `F1`, are
ignored. If several keys qualify, they return the one that comes first: letters, then the number
row, then the rest. They are best for "press any key to continue" and for simple key rebinding.

```lua
-- Wait for a key, then remember it as the "use" key.
local useKey = Key.E
local listening = false

function Update()
    if listening then
        local key = Input.GetAnyKeyDown()
        if key ~= nil then
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
    if not Input.GetKeyDown(Key.F) then return end

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
    if Input.GetKey(Key.G) then
        charge = mathT.Min(charge + GameTime.deltaTime, maxCharge)
    end

    if Input.GetKeyUp(Key.G) then
        local power = charge / maxCharge   -- 0..1
        Debug.Print("Throw with power", mathT.Round(power, 2))
        charge = 0
    end
end
```
