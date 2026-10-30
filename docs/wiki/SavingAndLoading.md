# Saving and Loading

The `Saving` table keeps values **after the game closes**: unlocked levels, the best time, coins,
options, the last checkpoint. Values live in named **save files**, and each value has a name.

```lua
Saving.SaveNumber("progress", "coins", 120)
local coins = Saving.LoadNumber("progress", "coins")   -- 120, even after a restart
```

There's nothing else to call: whatever you save is written to disk at the end of the frame.

For values that only need to last until the game closes, use [`Global`](Global.md) instead.

---

## Saving

```lua
Saving.SaveNumber(fileName, name, value)
Saving.SaveText(fileName, name, value)
Saving.SaveBool(fileName, name, value)
Saving.SaveVector3(fileName, name, value)
```

| Parameter | Type | |
|---|---|---|
| `fileName` | string | Which save file, e.g. `"progress"` or `"options"`. Made the first time you save to it. See [File names](#file-names). |
| `name` | string | The value's name inside the file, e.g. `"coins"`. Any non-empty text. |
| `value` | number, string, boolean or `Vector3` | What to save. It must be the kind the function is for: `SaveText` takes a string, and so on. |

Saving again under the same name replaces the old value, even with a different kind.

| Function | Saves | |
|---|---|---|
| `SaveNumber` | any number | Whole numbers stay whole: save `5` and you load `5`, not `5.0`. NaN and infinity can't be saved. |
| `SaveText` | a string | |
| `SaveBool` | `true` or `false` | |
| `SaveVector3` | a `Vector3` | Positions, rotations as angles, colours. |

---

## Loading

```lua
Saving.LoadNumber(fileName, name, [default])    -- number
Saving.LoadText(fileName, name, [default])      -- string
Saving.LoadBool(fileName, name, [default])      -- boolean
Saving.LoadVector3(fileName, name, [default])   -- Vector3
```

Returns the value saved as `name` in `fileName`. When nothing is saved there yet (the first time
the game runs, or the file doesn't exist), you get `default`, or without one:

| Function | Without a default |
|---|---|
| `LoadNumber` | `0` |
| `LoadText` | `""` |
| `LoadBool` | `false` |
| `LoadVector3` | `Vector3(0, 0, 0)` |

```lua
local volume = Saving.LoadNumber("options", "volume", 0.8)   -- 0.8 until the player changes it
```

These raise an error:

- **Loading with the wrong kind.** `LoadNumber` on a value saved with `SaveText` tells you which
  `Load` function it needs.
- **A default of the wrong kind**, like `Saving.LoadNumber("options", "volume", "loud")`.

A value you saved this frame can be loaded straight away, before it's on disk.

---

## Other functions

| Function | Returns | |
|---|---|---|
| `HasKey(fileName, name)` | boolean | Whether `name` is saved in the file. |
| `DeleteKey(fileName, name)` | boolean | Removes `name` from the file. `false` if it wasn't there. |
| `FileExists(fileName)` | boolean | Whether the save file exists. |
| `DeleteFile(fileName)` | boolean | Deletes the whole save file. `false` if there was none. |
| `GetFiles()` | list of strings | Every save file's name, sorted. Good for a "pick a slot" menu. |

All of them see this frame's changes: a file you just saved to exists, one you just deleted doesn't.

---

## File names

Each save file is one file on disk, so its name has to be a valid, plain file name:

- Not empty, at most 100 characters.
- **No folders**: no `/` or `\`. Every save file sits in the same folder.
- Can't start with `.`, or end with `.` or a space.
- None of `: * ? " < > |`.
- Not a name Windows reserves (`CON`, `NUL`, `COM1`...).

Anything else raises an error at the call. Don't add an extension: the engine adds its own.

Using a few files for different things keeps them tidy: `"options"` for settings, `"slot1"`,
`"slot2"`... for save slots.

---

## When it's written

Saving never waits for the disk. Everything saved during a frame is collected, and **at the end of
the frame** each changed file is written once, whether the save came from `Update`, a collision,
an [`Invoke`](Invoke.md) or an event. So saving the same value ten times in a frame costs one write.

Each file is written to a temporary file first and then put in place, so a crash or power cut
during a write leaves the old save or the new one, never a broken file.

Still, writing a file takes time. Save when something happens (a checkpoint, a level finished,
an option changed), **not every frame**. Saving the player's position in every `Update` writes
the file 60 times a second.

---

## Where the files are

| | Folder | Format |
|---|---|---|
| Playing in the editor | `Saves` in your project folder, next to `Assets` | JSON: plain text you can open and read while testing |
| The exported game | The player's own app-data folder: `C:\Users\<player>\AppData\Roaming\<project name>\Saves` | BSON: smaller and quicker to read, and not as easy for players to edit |

- The project's `Saves` folder isn't shown in the Asset Browser, and **exports never include it**,
  so your test saves don't ship with the game.
- The exported game saves per Windows user, so players sharing a PC keep their own progress. It
  doesn't write next to the game, which may be in a read-only folder.
- The folder is named after the project, so **renaming the project** gives an exported game a new,
  empty save folder.
- **Project Settings > Saves > Clear Saves** deletes everything in the project's `Saves` folder, so
  the next Play starts fresh. It doesn't touch players' saves from an exported game.

If a save file can't be read (it was damaged, or edited into broken JSON while testing), it is
renamed to `<name>.json.unreadable` and that save starts empty, with an error in the log. It isn't
overwritten, so you can look at what went wrong.

---

## Examples

### Unlocked levels

```lua
-- Scripts/LevelExit.lua (trigger at the end of a level)
public string nextLevel = "Level2"

function OnTriggerEnter(other)
    if not other.hasPlayerController then return end

    Saving.SaveBool("progress", "unlocked_" .. nextLevel, true)
    Game.LoadLevel(nextLevel)
end
```

```lua
-- In the level select menu
if Saving.LoadBool("progress", "unlocked_Level2") then
    -- show the Level 2 button
end
```

### Best time

```lua
-- Scripts/Finish.lua
function OnTriggerEnter(other)
    if not other.hasPlayerController then return end

    local time = GameTime.time
    local best = Saving.LoadNumber("records", Game.levelName, math.huge)

    if time < best then
        Saving.SaveNumber("records", Game.levelName, time)
        Game.MessageBox.SetMessage("New best time: " .. mathT.Round(time, 2))
    end
end
```

### Checkpoints

```lua
-- Scripts/Checkpoint.lua (trigger)
function OnTriggerEnter(other)
    if not other.hasPlayerController then return end

    Saving.SaveText("slot1", "level", Game.levelName)
    Saving.SaveVector3("slot1", "position", entity.transform.position)
end
```

```lua
-- Scripts/Player.lua: start at the checkpoint, if this level has one.
function Start()
    if Saving.LoadText("slot1", "level") == Game.levelName then
        entity.transform.position = Saving.LoadVector3("slot1", "position")
    end
end
```

### A "New Game" button

```lua
function NewGame()
    Saving.DeleteFile("slot1")
    Game.LoadLevel("Level1")
end
```
