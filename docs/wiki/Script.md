# Script

**Inspector name:** Custom Script · **Lua:** reach it with `entity:GetScript(name)` · **Public field type:** `Behaviour` (or `Script`)

The Custom Script component attaches a Lua script to an entity. That's how you add behaviour the
built-in components don't have: health, doors, enemies, pickups, puzzles, UI logic. An entity can
have **any number** of scripts, including several copies of the same one.

Sectors take scripts too, in the **Scripts** section of the sector inspector. They work the same
way, with the differences listed in [Sector scripts](#sector-scripts).

New to scripting? Start with [Getting Started](GettingStarted.md).

## How it works

### The script file

A script is a `.lua` file anywhere under `Assets`. Create one in the Asset Browser with
**Create File > Script (.lua)**. The new file comes with a small template.

The component refers to the file by its path inside `Assets`, **without** the `.lua`:
`Assets/Scripts/Enemies/Imp.lua` is `Scripts/Enemies/Imp`. Drag the file from the Asset Browser onto
**File Name** to set it.

### One environment per attachment

Every attachment runs in its **own environment**. Each copy of a script has its own globals, so two
imps running `Imp.lua` never share a `health` variable. What they do share:

| Shared | What it is |
|---|---|
| Engine globals | `Input`, `Game`, `GameTime`, `Debug`, `mathT`, `Vector2`, `Vector3`, `Vector4`, `ColliderType`, and Lua's `math`, `string`, `table` |
| `Global` | A table **every** script can read and write. It keeps its contents when the level changes and starts empty each time the game starts. See [Global](Global.md). |

Each environment also gets its own owner globals:

| Global | Entity script | Sector script |
|---|---|---|
| `entity` | The entity the script is on. | An empty placeholder (`entity.isValid == false`). |
| `sector` | Not set. | The sector the script is on. |

### When the script runs

1. **Loading** (level start): the file runs from top to bottom once. That defines its functions
   and sets every variable to its inline default.
2. The inspector's values for the public fields are then written **over** those defaults.
3. Once every script in the level has loaded, `OnEnable` and `Start` run, then `Update` every
   frame, and so on. See [Callback Functions](CallbackFunctions.md).

So code at the top of the file only ever sees the inline defaults. Anything that depends on a
public field belongs in `Start` or later.

### Enabled

The component's **Enabled** box is the script's own switch. A script runs only while **both** it
and its entity are enabled (see [Entity: enabled](Entity.md#enabled)). Scripts can flip a
script's switch through a [Behaviour reference](#behaviour-references): `behaviour.enabled = false`.

### Errors

- A **syntax error** is shown in the inspector under File Name as soon as you save the file,
  without pressing play. At level start the script fails to load, its error is printed, and it
  doesn't run.
- An error **inside a callback** is printed in red in the in-game console and logged. Only that one
  call stops. The script stays loaded and its callbacks keep being called.
- When you **export**, every script is compiled ahead of time and the game ships only the compiled
  form, not your `.lua` source. A script with a mistake in it stops the export and is listed in its
  output.

## Public fields

A public field is a top-level variable the inspector can edit. Declare it with `public`, its
type, its name and a default:

```text
public <type> <name> = <default>
```

- The inspector label comes from the name: `moveSpeed` shows as "Move Speed".
- `public` only works at the top level of the script (not inside functions or blocks, and not with
  `local`).
- The default must be a plain value, because the editor reads it without running the script.
  `= <default>` is optional; without it the field starts at zero, empty or `nil`.
- Mistakes in a declaration (unknown type, wrong kind of default, a field declared twice) are
  shown in the inspector like a syntax error, and the script doesn't run until they're fixed.
- Names used by the engine can't be fields: every [callback](CallbackFunctions.md) name, `entity`,
  `sector`, `Global`, `GameTime`, `Input`, `Game` and `Debug`.
- If you delete or rename a field in the script, its saved value is dropped. A renamed field
  starts again from its default.

### Types

| Type | In Lua | Default value | Inspector |
|---|---|---|---|
| `number` / `float` | number | `1.5` | number box |
| `int` / `integer` | integer | `3` | whole-number box |
| `bool` / `boolean` | boolean | `true` | checkbox |
| `string` | string | `"text"` | text box |
| `Vector2` / `Vector3` / `Vector4` | vector | `Vector3(0, 1, 0)` | 2 to 4 number boxes |
| `enum(A, B, C)` | integer: `0` for A, `1` for B, ... | one of the options, e.g. `B` | dropdown |
| `Key` | a [`Key`](Input.md#keys) value | `Key.E` | dropdown of every key |
| `Entity` | [Entity](Entity.md) or `nil` | `nil` | entity picker |
| `Sector` | [Sector](Sector.md) or `nil` | `nil` | sector picker |
| `Wall` | [Wall](Wall.md) or `nil` | `nil` | wall picker |
| `Behaviour` / `Script` | [Behaviour](#behaviour-references) or `nil` | `nil` | picks one script on one entity |
| `Transform`, `Sprite`, `Model`, `AudioSource`, `PlayerController`, `Camera`, `Collider`, `Rigidbody` | that component or `nil` | `nil` | picks one component on one entity (an entity can have several) |
| `Asset` / `Texture` | the texture's path as a string (`""` if unset) | `nil` | texture picker |

List types (`number[]`) aren't supported.

Reference fields (Entity, Sector, Wall, Behaviour, components) store the target's **ID**, so they
survive renames. If the target is deleted, the field is `nil` when the level starts. A component
field stores the exact component, so it still points at the same one when the entity's other
components of that type are added, removed or reordered. Component fields saved before entities
could have several components of a type load empty: pick the component again in the inspector.

```lua
public number speed = 40
public enum(Idle, Patrol, Chase) mode = Patrol
public Key useKey = Key.E
public Entity target = nil
public Rigidbody targetBody = nil
public Sector door = nil

function Start()
    if mode == 2 then Debug.Print("starting in Chase mode") end
end
```

## Behaviour references

A **Behaviour** is a handle to one script instance on some entity. You get one from:

- `entity:GetScript("Health")`: the first attached script whose file name is `Health`. Matching
  uses only the last part of the path, so `"Health"` also finds `Player/Health`. If several
  match, you get the first.
- `entity:GetScriptById(id)` or `entity:GetScripts()` (all of them);
- a public field of type `Behaviour`.

Through the handle you can reach the target script's **globals**, variables and functions alike:

| Access | Meaning |
|---|---|
| `ref.isValid` | `false` if there's no such script, or it has been destroyed. Always check it. |
| `ref.entity` | The entity the script is on. |
| `ref.enabled` | The script's own Enabled switch (read/write). |
| `ref.someVariable` | Reads a global of the target script. |
| `ref.someVariable = v` | Writes a global of the target script. |
| `ref:SomeFunction(a, b)` | Calls a function of the target script. |

`GetScript` never returns `nil`. When nothing matches, it returns a handle with `isValid == false`,
and reading anything through it gives `nil`.

### Calling functions: the `self` rule

`ref:Fn(a, b)` is Lua shorthand for `ref.Fn(ref, a, b)`. The handle is passed as the **first**
argument. So a function meant to be called from other scripts must take a first parameter for it,
usually named `self`, even if it never uses it:

```lua
-- Scripts/Health.lua
public number maxHealth = 100

health = 0 -- global, so other scripts can read it

function Start()
    health = maxHealth
end

function TakeDamage(self, amount)
    health = math.max(0, health - amount)
    if health == 0 then entity:Destroy() end
end

function Heal(self, amount)
    health = math.min(maxHealth, health + amount)
end
```

```lua
-- Anywhere else
local hp = target:GetScript("Health")
if hp.isValid then
    hp:TakeDamage(25)
    Debug.Print("HP left:", hp.health)
end
```

Inside the target function, `entity` and every other global are the **target's**, not the caller's.

Only variables declared **without** `local` are visible through a reference. `local` variables stay
private.

## Sector scripts

Scripts attached to a sector:

- use `sector` instead of `entity`;
- also get `OnEntityEnter` / `OnEntityExit`, but **not** the collision, trigger or sector-change
  callbacks;
- have no entity-style Enabled switch other than their own **Enabled** box;
- **can't** be reached with `GetScript` or a `Behaviour` field. Talk to them through the shared
  `Global` table, or let the sector script find the entities it needs itself;
- are ordered after all entity scripts.

## In the editor

| Field | Notes |
|---|---|
| **File Name** | The script, as a path inside `Assets` without `.lua`. Drag a `.lua` file onto it. |
| **Enabled** | The script's own on/off switch. |
| *public fields* | One control per `public` field in the script, as described [above](#types). |

## Examples

### A shared score

```lua
-- Scripts/Score/Coin.lua (coin with a trigger Collider)
public int value = 1

function OnTriggerEnter(other)
    if not other.hasPlayerController then return end
    Global.score = (Global.score or 0) + value
    entity:Destroy()
end
```

```lua
-- Scripts/Score/ScoreLabel.lua (UI entity with a Text)
local shown = -1

function Update()
    local score = Global.score or 0
    if score ~= shown then
        shown = score
        entity.uiText.text = "Score: " .. score
    end
end
```

### Switch between behaviours

```lua
-- Scripts/AI/Brain.lua
-- The entity also has "Patrol" and "Chase" scripts. Only one is enabled at a time.
public Entity player = nil
public number sightRange = 200

local patrol, chase

function Start()
    patrol = entity:GetScript("Patrol")
    chase = entity:GetScript("Chase")
end

function Update()
    if player == nil or not patrol.isValid or not chase.isValid then return end

    local d = mathT.Vector3Distance(player.transform.position, entity.transform.position)
    local seen = d < sightRange

    patrol.enabled = not seen
    chase.enabled = seen
end
```

### Events between scripts

```lua
-- Scripts/Events/Button.lua (entity with a trigger Collider)
-- Anything can listen with: Global.onButton = function(name) ... end
function OnTriggerEnter(other)
    if other.hasPlayerController and Global.onButton ~= nil then
        Global.onButton(entity.name)
    end
end
```

```lua
-- Scripts/Events/Listener.lua (can be a sector script)
function Start()
    Global.onButton = function(buttonName)
        Debug.Print("Button pressed:", buttonName)
        if sector ~= nil then sector:MoveFloorTo(1, 64, 40) end
    end
end
```

Remember to mutate `Global` (`Global.x = ...`) rather than replace it (`Global = {}`), which
would only change your own script's copy of the variable.

For a fuller event bus, timers and other helpers, see
[`docs/scripts/13_utilities.md`](../scripts/13_utilities.md).
