# Input

`Input` reads the keyboard and mouse. The cursor itself (locked, free, shown) is set with the
[`Cursor`](#cursor) table.

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
| Special | `Key.Space`, `Key.Escape`, `Key.Enter`, `Key.Tab`, `Key.Backspace`, `Key.CapsLock` |
| Function keys | `Key.F1` to `Key.F12` |
| Arrows | `Key.Left`, `Key.Right`, `Key.Up`, `Key.Down` |
| Navigation | `Key.Insert`, `Key.Delete`, `Key.Home`, `Key.End`, `Key.PageUp`, `Key.PageDown` |
| Modifiers | `Key.LShift`, `Key.RShift`, `Key.LCtrl`, `Key.RCtrl`, `Key.LAlt`, `Key.RAlt` |
| Punctuation | `Key.Minus` (`-`), `Key.Equals` (`=`), `Key.LeftBracket` (`[`), `Key.RightBracket` (`]`), `Key.Backslash` (`\`), `Key.Semicolon` (`;`), `Key.Quote` (`'`), `Key.BackQuote` (`` ` ``), `Key.Comma` (`,`), `Key.Period` (`.`), `Key.Slash` (`/`) |
| Numpad | `Key.Keypad0` to `Key.Keypad9`, `Key.KeypadPlus`, `Key.KeypadMinus`, `Key.KeypadMultiply`, `Key.KeypadDivide`, `Key.KeypadPeriod`, `Key.KeypadEnter`, `Key.NumLock` |
| Other | `Key.PrintScreen`, `Key.ScrollLock`, `Key.Pause` |

The values are plain numbers, so you can keep one in a variable or compare two with `==`.

A misspelled key (`Key.Spcae`) is `nil`, and passing `nil` or a number that isn't a key raises an
error naming the problem.

Keys are physical positions on a US layout. On other layouts `Key.Z` is the key where Z is on a US
keyboard, which keeps WASD-style controls in the same place. The same goes for punctuation:
`Key.Semicolon` is the key right of L, whatever your keyboard prints on it.

In the editor's Play mode, **Tab** also frees the mouse and pauses the game for the editor, so it's
a poor choice for a gameplay key there. The exported game doesn't do this.

### The GetAnyKey functions

They return `nil` when no key qualifies. Keys without a value in the `Key` table, like the Windows
key, are ignored. If several keys qualify, they return the one that comes first: letters, then the number
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
| `GetMouseDelta()` | `Vector2` | How far the mouse moved since the last frame, in pixels. +x is right, +y is down. |
| `GetMouseScroll()` | number | How far the scroll wheel turned this frame: positive away from you (up), negative towards you, `0` when still. One notch is `1` on most mice; smooth-scrolling mice and touchpads give fractions. |
| `MouseLeft`, `MouseMiddle`, `MouseRight` | integer | Button numbers to pass to the functions above. |
| `MouseBack`, `MouseForward` | integer | The two side buttons many mice have (buttons 4 and 5). |

```lua
if Input.GetMouseButtonDown(Input.MouseLeft) then Fire() end
if Input.GetMouseButton(Input.MouseRight) then Aim() end
```

`GetMouseDelta` works whether or not the cursor is locked: while it's locked the cursor doesn't
move, but the delta still says how far the mouse did. Mouse look itself is done by the
[Player Controller](PlayerController.md); use the delta for your own, such as turning a
turret or an object being inspected.

```lua
-- Scroll through weapon slots 1..4.
local slot = 1

function Update()
    local scroll = Input.GetMouseScroll()
    if scroll > 0 then slot = slot % 4 + 1 end
    if scroll < 0 then slot = (slot - 2) % 4 + 1 end
end
```

---

## Cursor

`Cursor` sets what the mouse cursor does, like Unity's `Cursor`. Both are read/write properties.

| Property | Type | |
|---|---|---|
| `lockState` | `CursorLockMode` | `Locked`, `None` or `Confined`. See below. |
| `visible` | boolean | Whether the cursor is drawn while it isn't `Locked`. |

| `CursorLockMode` | |
|---|---|
| `Locked` | Hidden and held in place: the mouse only turns the view. How levels start by default. |
| `None` | Free: it moves around and can leave the window. |
| `Confined` | Free, but it can't leave the window. |

- A `Locked` cursor is always hidden, whatever `visible` says.
- The [Player Controller](PlayerController.md)'s **mouse look only runs while the cursor is
  `Locked`**, so moving the pointer over a menu doesn't spin the view. Its movement keys still work;
  set `playerController.isActive = false` too if the menu should stop the player.
- **When a level starts** (pressing Play, launching the exported game, or
  [`Game.LoadLevel`](Game.md#loadlevel)), the cursor is set from that level's **World Settings >
  Cursor**: **Lock Cursor on Level Start** (`Locked`, or `None` when unticked) and **Hide Cursor on
  Level Start** (`visible` false, or true when unticked). Both are ticked by default. This happens
  before the level's scripts start, so a script's `Start` can still change it. A main-menu level
  would untick both.
- A wrong value (`Cursor.lockState = 7`, `Cursor.visible = "yes"`) raises an error.

**In the editor's Play mode**, Tab still frees the mouse and pauses the game for the editor, and
clicking the game view takes it back. A script freeing the cursor is different: the game keeps
running, exactly as in the exported game, so you can test your menus. When you click back in after
Tab, the cursor goes back to what the script last set.

```lua
-- Scripts/PauseMenu.lua (on the player): Escape opens and closes a menu.
local open = false

function Update()
    if not Input.GetKeyDown(Key.Escape) then return end

    open = not open
    Cursor.lockState = open and CursorLockMode.None or CursorLockMode.Locked
    Cursor.visible = open
    entity.playerController.isActive = not open
    GameTime.timeScale = open and 0 or 1
end
```

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
public number maxCharge = 1.5

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
