# Scripts (shared table)

`Scripts` is an empty Lua table that **every script in the level shares**. Anything one script
puts in it, every other script can read. Use it for level-wide state (a score, collected keys,
whether an alarm is going off) and for simple events between scripts.

```lua
-- In a coin script:
Scripts.score = (Scripts.score or 0) + 1

-- In a HUD script:
entity.uiText.text = "Score: " .. (Scripts.score or 0)
```

---

## Why it exists

Every script attachment runs in its own space. A global variable in one script, such as
`health = 100`, belongs to that one attachment: other scripts don't see it, and two enemies with
the same script each have their own `health`. That keeps scripts from stepping on each other, but
it means you need somewhere shared for things the whole level cares about. That is `Scripts`.

To reach **one particular** script's variables instead, use `entity:GetScript("Name")`. See
[Script](Script.md#behaviour-references).

---

## Rules

- **It starts empty every time the level starts.** Nothing carries over from the last play
  session, and nothing is saved.
- **Write into it, don't replace it.** `Scripts.score = 0` is right. `Scripts = {}` only points
  your own script's `Scripts` variable at a new table: every other script keeps the old one.
- **Missing keys are `nil`.** Use `Scripts.score or 0` until something has set it.
- **Order matters in `Start`.** Entity scripts run `Start` before sector scripts, in level order.
  If script A sets `Scripts.x` in its `Start`, script B can only rely on it in its own `Start` if
  B starts later. Reading it in `Update` is always safe.
- **Pick distinct names.** All scripts share one table, so two unrelated scripts that both use
  `Scripts.timer` will overwrite each other. Prefixing helps: `Scripts.doorTimer`, or a sub-table
  per system, `Scripts.inventory = Scripts.inventory or {}`.

---

## Examples

### Collected keys

```lua
-- Scripts/Keys/KeyPickup.lua (trigger)
---@field keyName string
keyName = "red"

function OnTriggerEnter(other)
    if not other.hasPlayerController then return end

    Scripts.keys = Scripts.keys or {}
    Scripts.keys[keyName] = true
    Debug.Print("Picked up the", keyName, "key")
    entity:Destroy()
end
```

```lua
-- Scripts/Keys/LockedDoor.lua (sector script on the door sector)
---@field keyName string
keyName = "red"

function OnEntityEnter(e)
    if not e.hasPlayerController then return end

    if Scripts.keys ~= nil and Scripts.keys[keyName] then
        sector:MoveCeilingTo(1, sector.floorHeight + 40, 60)
    else
        Debug.Print("You need the", keyName, "key")
    end
end
```

### Shared helper functions

Functions can go in `Scripts` too, so several scripts can use the same code.

```lua
-- Scripts/Shared/Helpers.lua (attach to any one entity in the level)
Scripts.helpers = {}

function Scripts.helpers.FlatDistance(a, b)
    return Vector2(a.x - b.x, a.z - b.z).length
end
```

```lua
-- In any other script, from Start onwards:
local d = Scripts.helpers.FlatDistance(player.transform.position, entity.transform.position)
```

This file has its code at the top level rather than in `Start`, so the helpers already exist when
every other script's `Start` runs: all scripts are loaded before any `Start`.

For events between scripts and a fuller set of helpers, see [Script](Script.md#events-between-scripts)
and [`docs/scripts/13_utilities.md`](../scripts/13_utilities.md).
