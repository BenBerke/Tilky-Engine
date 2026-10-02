# Debug

`Debug` prints messages, either on screen or to the engine log.

```lua
Debug.Print("Health:", health)
```

| Function | Where it goes |
|---|---|
| `Print(...)` | The **in-game console**, on screen. Not written to the log. |
| `LogInfo(...)` | The engine log, as info. |
| `LogWarning(...)` | The engine log, as a warning. |
| `LogError(...)` | The engine log, as an error. |
| `LogCritical(...)` | The engine log, as critical. |

Every function takes **any number of arguments** of any type, like Lua's `print`. Each one is
turned into text with `tostring` and they are joined with spaces:

```lua
Debug.Print("hit", hit.type, "at", hit.position, "dist", hit.distance)
-- hit Wall at Vector3(120, 8, -40) dist 57.25
```

Vectors print as `Vector3(x, y, z)`. Tables print as an address (`table: 0x...`), so print the
fields you want instead.

---

## The in-game console

`Debug.Print` lines appear in the top-left corner of the game view:

- Each line stays for **8 seconds**, then fades out.
- At most **10 lines** are shown. A new line pushes out the oldest one.
- Script errors are shown here too, in red, for 15 seconds.

Because lines disappear on their own and only 10 fit, printing every frame from `Update` floods
the console and hides everything else, including errors. Print when something **changes**, or
throttle it:

```lua
local printTimer = 0

function Update()
    printTimer = printTimer - GameTime.deltaTime
    if printTimer <= 0 then
        printTimer = 0.5
        Debug.Print("speed", entity.rigidbody.velocity.length)
    end
end
```

---

## The engine log

The `Log*` functions write to the engine's log instead: the terminal the engine was started from
(if any) and the log file `Logs/engine_log.txt`, next to the projects folder. The file is cleared
each time the engine starts.

Use them for things you want to look at after a play session, such as which level events happened
in what order. Script errors are also written to the log with their full message.

Which level to use is up to you; they only change how the line is labelled and coloured. A common
convention is `LogWarning` for "something is set up wrong but the game can carry on", for example a
missing public field, and `LogError` for "this script can't do its job".

```lua
---@field door Sector
door = nil

function Start()
    if door == nil then
        Debug.LogWarning(entity.name .. ": no door assigned, the button won't do anything")
    end
end
```
