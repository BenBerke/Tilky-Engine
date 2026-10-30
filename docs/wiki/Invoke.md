# Invoke

The Invoke functions call a function **later**: after some seconds, after some frames, or over and
over. Every script has them; there is no table in front of them.

```lua
function OnTriggerEnter(other)
    Invoke(OpenDoor, 2)   -- OpenDoor() in 2 seconds
end
```

| Function | Returns | |
|---|---|---|
| [`Invoke(fn, delay, ...)`](#invoke) | handle | Calls `fn(...)` after `delay` seconds of game time. |
| [`InvokeReal(fn, delay, ...)`](#invokereal) | handle | The same, in real seconds that `timeScale` doesn't change. |
| [`InvokeFrame(fn, frames, ...)`](#invokeframe) | handle | Calls `fn(...)` after a number of frames. |
| [`InvokeRepeating(fn, delay, interval, ...)`](#invokerepeating) | handle | Calls `fn(...)` after `delay`, then every `interval` seconds. |
| [`CancelInvoke([handle])`](#cancelinvoke-and-isinvoking) | | Cancels one invoke, or all of this script's. |
| [`IsInvoking([handle])`](#cancelinvoke-and-isinvoking) | boolean | Whether one invoke, or any of this script's, is still waiting. |

---

## Invoke

```lua
Invoke(fn, delay, ...)   -- handle
```

| Parameter | Type | |
|---|---|---|
| `fn` | function | What to call: one of the script's own functions or an inline `function() ... end`. For a `public function`, see [below](#which-script-an-invoke-belongs-to). |
| `delay` | number | Seconds of game time to wait. Negative counts as `0`. |
| `...` | anything, optional | Passed on to `fn` when it is called. Any number of them. |

`fn` is called once, `delay` seconds from now, with the extra arguments. The seconds are **game
time**: they follow [`GameTime.timeScale`](GameTime.md#timescale), so slow motion stretches the wait
and `timeScale = 0` holds it until time runs again.

- A `delay` of `0` calls `fn` on the **next frame**, never straight away. Nothing scheduled runs on
  the frame it was scheduled in.
- Invokes are checked once per frame, after every script's `Update` and `FixedUpdate`. So `fn`
  runs on the first frame at or after the time is up.
- Pass the function itself, without `()`: `Invoke(OpenDoor, 2)`. `Invoke(OpenDoor(), 2)` would call
  `OpenDoor` now and pass its result, which raises an error unless it returned a function.
- The arguments are the values at the time of the `Invoke` call. A `Vector3` or a number passed
  in won't change if your variable changes later.
- An error inside `fn` is shown in the console, with the script's name and the line, like an error
  in `Update`.

```lua
-- Scripts/Mine.lua (trigger): blow up 1.5 seconds after being stepped on.
public number damage = 50

function OnTriggerEnter(other)
    Invoke(Explode, 1.5, other)
end

function Explode(victim)
    if victim.isValid then
        local health = victim:GetScript("Health")
        if health.isValid then health:TakeDamage(damage) end
    end
    entity:Destroy()
end
```

```lua
-- An inline function: show a hint 5 seconds into the level.
function Start()
    Invoke(function()
        Game.MessageBox.SetMessage("Press E to open doors")
    end, 5)
end
```

---

## InvokeReal

```lua
InvokeReal(fn, delay, ...)   -- handle
```

The same as [`Invoke`](#invoke), but `delay` is **real** seconds: `timeScale` doesn't slow or stop
it. Use it for things that happen while the game is paused or in slow motion, such as menus and
ending a slow-motion effect.

```lua
-- Slow motion for 3 real seconds, however slow the game is going.
GameTime.timeScale = 0.25
InvokeReal(function() GameTime.timeScale = 1 end, 3)
```

---

## InvokeFrame

```lua
InvokeFrame(fn, frames, ...)   -- handle
```

| Parameter | Type | |
|---|---|---|
| `frames` | integer | How many frames to wait. `0` and `1` both mean the next frame. Fractions are rounded down. |

Calls `fn(...)` after a number of **frames** instead of seconds. Useful for "once everything else
has had its turn", such as reading a position after physics has moved it.

```lua
function Start()
    -- Every script's Start has run by the next frame, so the others are set up.
    InvokeFrame(LinkToOthers, 1)
end
```

Frames are counted while the game updates, whatever `timeScale` is.

---

## InvokeRepeating

```lua
InvokeRepeating(fn, delay, interval, ...)   -- handle
```

| Parameter | Type | |
|---|---|---|
| `delay` | number | Seconds of game time before the first call. `0` = next frame. |
| `interval` | number | Seconds of game time between calls after that. |
| `...` | anything, optional | Passed to `fn` on every call. |

Calls `fn(...)` after `delay` seconds, then every `interval` seconds, until it is cancelled with
[`CancelInvoke`](#cancelinvoke-and-isinvoking) or the script goes away. Like `Invoke`, it follows
`timeScale`.

It is called **at most once per frame**. An `interval` shorter than a frame (or `0`) calls it every
frame, without trying to catch up.

```lua
-- Scripts/Blinker.lua: a light that blinks while the alarm is on.
public Sector room = nil
local blink

public function StartAlarm()
    if IsInvoking(blink) then return end
    blink = InvokeRepeating(Toggle, 0, 0.5)
end

public function StopAlarm()
    CancelInvoke(blink)
    room.light = Vector3(255, 255, 255)
end

function Toggle()
    if room.light.y > 0 then
        room.light = Vector3(255, 0, 0)
    else
        room.light = Vector3(255, 255, 255)
    end
end
```

---

## CancelInvoke and IsInvoking

```lua
CancelInvoke(handle)   -- cancels that one
CancelInvoke()         -- cancels everything this script has scheduled

IsInvoking(handle)     -- boolean
IsInvoking()           -- boolean: has this script scheduled anything?
```

Every Invoke function returns a **handle**, a number that stands for that one invoke. Keep it in a
variable to cancel or check it later.

- `CancelInvoke(handle)` stops it from being called. A handle that has already been called (one-shot)
  or cancelled is ignored.
- `IsInvoking(handle)` is `true` until a one-shot invoke has been called, and until a repeating one
  is cancelled.
- A handle only works in the script that made it. Another script's handle is never found.
- A handle that isn't a number raises an error. In particular `CancelInvoke(nil)` is an error,
  not "cancel everything", so a variable that was never set doesn't silently cancel all of them.
  Call `CancelInvoke()` with no arguments for that.

```lua
-- Scripts/Bomb.lua: explode in 10 seconds unless defused.
local timer
local armedAt

function Start()
    armedAt = GameTime.time
    timer = Invoke(Explode, 10)
end

public function Defuse()
    if IsInvoking(timer) then
        CancelInvoke(timer)
        Debug.Print("Defused with", 10 - (GameTime.time - armedAt), "seconds left")
    end
end
```

---

## Which script an invoke belongs to

An invoke belongs to the script that **called** the Invoke function, even when `fn` calls into
another script.

A [`public function`](Script.md) can't be passed to an Invoke function directly, neither your
own (the script won't compile: public functions can't be used as values) nor another script's
(`guard.Alert` would be called without the hidden reference it needs). Wrap the call in an inline
function instead:

```lua
-- On a switch: alert the guard in 1 second.
public Behaviour guard = nil

function OnTriggerEnter(other)
    Invoke(function() guard:Alert() end, 1)
end
```

This invoke belongs to the switch's script, so it follows the switch's lifetime.

- **Disabled** (the script's own Enabled box, or `entity.enabled = false`): its invokes wait. They
  don't count down while it is off and carry on from where they were when it is back on.
- **Destroyed** (`entity:Destroy()`, the script removed): its invokes are dropped. Ones scheduled
  in `OnDestroy` never run.
- **Level change** ([`Game.LoadLevel`](Game.md#loadlevel)) and pressing **Stop**: every invoke is
  dropped.
- **Top level**: the Invoke functions can't be called from a script's top-level code (outside any
  function), since the script isn't set up yet. Call them from `Start` or later.

Sector scripts have the Invoke functions too, and the same rules apply.
