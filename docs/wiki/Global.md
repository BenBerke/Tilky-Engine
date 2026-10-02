# Global

`Global` is a Lua table that **every script shares**. Anything one script puts in it, every other
script can read. Use it for game-wide state (a score, collected keys, whether an alarm is going
off) and for simple events between scripts.

```lua
-- In a coin script:
Global.score = (Global.score or 0) + 1

-- In a HUD script:
entity.uiText.text = "Score: " .. (Global.score or 0)
```

It **keeps its contents when the level changes**, so it is also the place for anything that should
follow the player from one level to the next.

---

## Why it exists

Every script attachment runs in its own space. A global variable in one script, such as
`health = 100`, belongs to that one attachment: other scripts don't see it, and two enemies with
the same script each have their own `health`. That keeps scripts from stepping on each other, but
it means you need somewhere shared for things the whole game cares about. That is `Global`.

To reach **one particular** script's variables instead, use `entity:GetScript("Name")`. See
[Script](Script.md#behaviour-references).

---

## How long it lasts

| When | What happens to `Global` |
|---|---|
| The game starts (**Play** in the editor, or launching the exported game) | It starts empty. |
| The level changes ([`Game.LoadLevel`](Game.md#loadlevel)) | It keeps everything in it. |
| The game stops (**Stop** in the editor, or quitting) | It is thrown away. Nothing is saved to disk. |

Because it survives level changes, things you put in it for **one level** stay there in the next
one too. Keep per-level state somewhere it can be told apart, or clear it when a level begins:

```lua
-- Scripts/LevelStart.lua (on any one entity in each level): reset per-level state,
-- keep what the player carries.
function Start()
    Global.kills = 0               -- this level's counter starts again
    Global.channels = {}           -- doors and switches of the old level don't apply here
    Global.coins = Global.coins or 0   -- carried over from earlier levels
end
```

---

## Rules

- **Write into it, don't replace it.** `Global.score = 0` is right. `Global = {}` only points your
  own script's `Global` variable at a new table: every other script keeps the old one.
- **Missing keys are `nil`.** Use `Global.score or 0` until something has set it.
- **Order matters in `Start`.** Entity scripts run `Start` before sector scripts, in level order.
  If script A sets `Global.x` in its `Start`, script B can only rely on it in its own `Start` if
  B starts later. Reading it in `Update` is always safe.
- **Pick distinct names.** All scripts share one table, so two unrelated scripts that both use
  `Global.timer` will overwrite each other. Prefixing helps: `Global.doorTimer`, or a sub-table
  per system, `Global.inventory = Global.inventory or {}`.
- **Don't keep entities, sectors or walls across levels.** They belong to the level they came
  from. After a level change, one you stored may point at nothing, or at an unrelated thing in the
  new level that happens to have the same ID. Store plain values (numbers, strings, tables of them)
  for anything that should carry over.

---

## Examples

### Collected keys

```lua
-- Scripts/Keys/KeyPickup.lua (trigger)
---@field keyName string
keyName = "red"

function OnTriggerEnter(other)
    if not other.hasPlayerController then return end

    Global.keys = Global.keys or {}
    Global.keys[keyName] = true
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

    if Global.keys ~= nil and Global.keys[keyName] then
        sector:MoveCeilingTo(1, sector.floorHeight + 40, 60)
    else
        Debug.Print("You need the", keyName, "key")
    end
end
```

### Shared helper functions

Functions can go in `Global` too, so several scripts can use the same code.

```lua
-- Scripts/Shared/Helpers.lua (attach to any one entity in the level)
Global.helpers = {}

function Global.helpers.FlatDistance(a, b)
    return Vector2(a.x - b.x, a.z - b.z).length
end
```

```lua
-- In any other script, from Start onwards:
local d = Global.helpers.FlatDistance(player.transform.position, entity.transform.position)
```

This file has its code at the top level rather than in `Start`, so the helpers already exist when
every other script's `Start` runs: all scripts are loaded before any `Start`.

For events between scripts and a fuller set of helpers, see [Script](Script.md#events-between-scripts)
and [`docs/scripts/13_utilities.md`](../scripts/13_utilities.md).
