# Getting Started with Scripting

This guide takes you from nothing to a working script, then builds up the ideas you'll use in
every script after that. Follow it in order the first time. Each step builds on the one before.

You don't need to know Lua already. The language basics used here are explained as they come up.

---

## 1. What a script is

A script is a `.lua` file in your project's `Assets/Scripts/` folder. On its own it does nothing.
It runs once you **attach** it to something:

- **An entity**, through the entity's **Custom Script** component. Most scripts are these.
- **A sector**, through the **Scripts** section of the sector inspector. Use these for rooms that do
  things: doors, lifts, traps, flickering lights.

The engine never calls your script at random. It calls specific **callback functions** at specific
moments: `Start` when the level begins, `Update` every frame, `OnCollisionEnter` when something
bumps into the entity, and so on. You write the functions you care about and leave out the rest.
[Callback Functions](CallbackFunctions.md) has the full list.

Each attachment gets its own copy of the script's variables. Two enemies running the same
`Enemy.lua` each have their own `health`.

---

## 2. Before you start: a level that can play

Scripts only run while the game is playing, and the game only plays if the level has a
**camera**. The quickest setup is a player:

1. Draw at least one sector so there is floor to stand on.
2. Create an entity and name it `Player`.
3. Add these components: **Transform**, **Rigidbody**, **Collider** (Sphere), **Camera** and
   **Player Controller**.
4. In the Player Controller, tick **Is Active**.
5. Move the entity so it stands inside the sector.

Press **Save & Play**. You should be able to walk around with WASD and look with the mouse. If the
level doesn't start, the log will say why. The usual reason is a missing camera.

See [Player Controller](PlayerController.md) and [Camera](Camera.md) for what each of those does.

---

## 3. Your first script

### Create the file

1. Open the **Asset Browser** and go into `Scripts`. Make a sub-folder if you like; they are fine.
2. Right-click, then choose **Create File > Script (.lua)**. Call it `Hello`.

The new file already contains a template with a couple of example fields and empty `Start`,
`Update` and `FixedUpdate` functions. Replace everything in it with this:

```lua
function Start()
    Debug.Print("Hello from " .. entity.name)
end
```

- `function Start() ... end` defines the callback the engine runs once, when the level starts.
- `entity` is a variable every entity script gets automatically. It is the entity the script is
  attached to.
- `..` joins two strings in Lua.
- `Debug.Print` writes to the in-game console.

### Attach it

1. Select the `Player` entity.
2. **Add Component > Custom Script**.
3. Drag `Hello.lua` from the Asset Browser onto the **File Name** field.

Press **Save & Play**. The console shows `Hello from Player`.

> If the script has a syntax error, the inspector shows it under the File Name field before you
> even press play. Errors while the game is running are printed in red in the console. A script
> that errors keeps running; only the function that failed is cut short.

---

## 4. Doing something every frame

`Update` runs once per frame. Let's make an entity spin. Create a new entity with a
**Transform** and a **Sprite** (so you can see it), then attach this script:

```lua
-- Scripts/Spin.lua
local angle = 0

function Update()
    angle = angle + 90 * GameTime.deltaTime

    local radians = mathT.DegToRad(angle)
    entity.transform.forward = Vector2(mathT.Sin(radians), mathT.Cos(radians))
end
```

- `local angle = 0` is a variable that belongs to this script instance. It keeps its value
  between frames because it is declared outside the function.
- `GameTime.deltaTime` is how many seconds the last frame took. Multiplying by it turns "90 per
  frame" into "90 per **second**", so the speed is the same on a fast or a slow computer. Do this
  for **everything** that changes over time.
- `mathT` is the engine's maths table: `Sin`, `Cos`, `Lerp`, `Clamp`, random numbers and more.
  Lua's own `math` table also works.
- `transform.forward` is the direction the sprite faces. Only 4- and 8-direction sprites show it.

---

## 5. Settings you can change in the inspector (public fields)

Hard-coding `90` means editing the script to change the speed. A **public field** puts it in the
inspector instead, so each entity can have its own value:

```lua
-- Scripts/Spin.lua
---@field degreesPerSecond number @ Spin Speed
degreesPerSecond = 90

local angle = 0

function Update()
    angle = angle + degreesPerSecond * GameTime.deltaTime

    local radians = mathT.DegToRad(angle)
    entity.transform.forward = Vector2(mathT.Sin(radians), mathT.Cos(radians))
end
```

The rules:

- The `---@field name type @ Label` comment goes **directly above** a top-level
  `name = default` line. The `@ Label` part is optional.
- The default must be a plain value: `90`, `"text"`, `true`, `Vector3(0, 1, 0)` or `nil`. The
  editor reads it as text without running your script.
- The value set in the inspector is applied **after** the top of the file runs. So use the field
  inside functions (like `Start` and `Update`), not at the top level.

Fields can also point at things in the level: another entity, a sector, a wall, a component or
another script.

```lua
---@field target Entity @ Follow Target
target = nil

---@field door Sector
door = nil
```

In the inspector these become slots you fill by picking an object. If a slot is left empty, or the
object gets deleted, the value is `nil`, so check before using it:

```lua
function Update()
    if target == nil or not target.isValid then return end
    -- ...
end
```

Every supported type is listed in [Script](Script.md#public-fields).

---

## 6. Reading and changing components

An entity's components are properties on `entity`:

```lua
local transform = entity.transform      -- nil if it has no Transform
local body = entity.rigidbody           -- nil if it has no Rigidbody
```

Each component page lists what you can read and write. One rule catches everyone at first:

**Vectors are copies.** This does nothing:

```lua
entity.transform.position.y = 50        -- changes a copy, which is then thrown away
```

Read the vector, make a new one, and write it back:

```lua
local p = entity.transform.position
entity.transform.position = Vector3(p.x, 50, p.z)

-- or, with vector maths:
entity.transform.position = entity.transform.position + Vector3(0, 10, 0)
```

---

## 7. Input

`Input` reads the keyboard and mouse. Keys come from the `Key` table, like `Key.E`, `Key.Space`,
`Key.LShift` and `Key.Up`. Type `Key.` and the editor lists them all.

```lua
-- Scripts/Jumper.lua (entity with a Rigidbody)
---@field jumpSpeed number
jumpSpeed = 120

function Update()
    local body = entity.rigidbody
    if body == nil then return end

    if Input.GetKeyDown(Key.J) and body.isGrounded then
        local v = body.velocity
        body.velocity = Vector3(v.x, jumpSpeed, v.z)
    end
end
```

- `GetKeyDown` is true only on the frame the key goes down, `GetKey` while it is held, and
  `GetKeyUp` on the frame it is released.
- `Input.GetMouseButtonDown(Input.MouseLeft)` works the same way for mouse buttons.

---

## 8. Reacting to the world

Instead of checking every frame whether something happened, let the engine tell you. These
callbacks run on entity scripts:

```lua
-- Scripts/Landmine.lua
-- Entity with a Transform and a Collider with Is Trigger ticked.
function OnTriggerEnter(other)
    Debug.Print(other.name .. " stepped on a mine!")
    entity:Destroy()
end
```

- `OnTriggerEnter(other)` runs when another collider starts overlapping this entity's trigger.
  `other` is the entity that came in.
- `OnCollisionEnter(other)` is the same for solid colliders that bump into each other.
- `OnSectorChange(sector)` runs when this entity walks into a different room.

Sector scripts get `OnEntityEnter(entity)` and `OnEntityExit(entity)` for anything entering or
leaving the room:

```lua
-- Scripts/Alarm.lua (sector script)
function OnEntityEnter(e)
    if e.hasPlayerController then sector:FadeLight(Vector3(255, 40, 40), 0.3) end
end

function OnEntityExit(e)
    if sector:IsEmpty() then sector:FadeLight(Vector3(255, 255, 255), 1.0) end
end
```

Inside a sector script, `sector` is the sector it is attached to.

---

## 9. Finding other things

A script often needs something that isn't its own entity. The options, best first:

1. **A public field** (`---@field door Sector`). You pick it in the inspector, so it can't break
   if something gets renamed.
2. **Callback arguments**, like `other` in `OnTriggerEnter`.
3. **Tags.** Give entities a tag in the editor, then use
   `Game.FindEntitiesWithTag("enemy")` or `entity:HasTag("enemy")`.
4. **Names.** `Game.FindEntity("Player")` returns the first entity with that exact name, or `nil`.
5. **Raycasts.** `Game.Raycast(origin, direction, length, ignoredEntityID, requireCollider)`
   returns what a line hits first.

`Game.Find...` searches every entity in the level. Call it once in `Start` and keep the result,
not every frame.

---

## 10. Talking to other scripts

`entity:GetScript("Health")` returns the `Health` script attached to that entity. You can read
and write its variables, and call its functions with a colon:

```lua
-- Scripts/Health.lua
---@field maxHealth number
maxHealth = 100

health = 0

function Start()
    health = maxHealth
end

-- Called from other scripts as target:GetScript("Health"):TakeDamage(10).
-- The colon passes the script itself as the first argument, so it needs `self`.
function TakeDamage(self, amount)
    health = health - amount
    if health <= 0 then entity:Destroy() end
end
```

```lua
-- Scripts/Spikes.lua (trigger)
---@field damage number
damage = 25

function OnTriggerEnter(other)
    local health = other:GetScript("Health")
    if health.isValid then health:TakeDamage(damage) end
end
```

- `GetScript` never returns `nil`. If nothing matches, it returns a handle whose `isValid` is
  `false`, so check that.
- Variables you want other scripts to see must be **global** in the script (no `local`), like
  `health` above.
- For state the whole game shares (a score, collected keys), use the [`Global`](Global.md) table:
  `Global.score = (Global.score or 0) + 1`. It is shared by every script, keeps its contents when
  the level changes, and starts empty each time the game starts.

---

## 11. Timers without coroutines

There is no `wait()` or coroutine library. Count time yourself:

```lua
-- Scripts/Blinker.lua (entity with a Sprite)
---@field interval number
interval = 0.5

local timer = 0
local visible = true

function Update()
    timer = timer + GameTime.deltaTime
    if timer < interval then return end
    timer = timer - interval

    visible = not visible
    local c = entity.sprite.color
    entity.sprite.color = Vector4(c.x, c.y, c.z, visible and 1 or 0)
end
```

---

## 12. What's not available

Only Lua's `base`, `math`, `table` and `string` libraries are loaded: no `os`, `io`, `require` or
`coroutine`. Scripts can create entities (`Game.CreateEntity()`) and add or remove their
components (`entity:AddComponent(Component.Sprite)`), but they can't attach scripts. Place anything that
needs a script in the editor ahead of time and "switch it on" when you need it: move it into place, make its sprite
visible, turn its collider on. Note that `entity.enabled = false` only pauses the entity's
**scripts**. The entity is still drawn and still collides. See [Entity](Entity.md#enabled).

---

## 13. Working in an external editor

When scripting starts, the engine writes `Assets/Scripts/.luals/tilky_api.lua`. It describes every
Tilky type and function. Open your `Scripts` folder in VS Code with the **Lua** extension (LuaLS)
and you get autocomplete and hover docs for `entity`, `Input`, `Game` and the rest. The built-in
script editor's autocomplete comes from the same data.

---

## 14. Debugging

- `Debug.Print(a, b, c)` prints any values to the in-game console, separated by spaces.
- `Debug.LogInfo`, `LogWarning`, `LogError` and `LogCritical` write to the engine log instead.
- `tostring(v)` works on vectors: `Debug.Print("pos", entity.transform.position)`.
- Some setters raise errors on bad values, like a sector ceiling below its floor. If the values
  come from somewhere you don't control, wrap the call:

  ```lua
  local ok, err = pcall(function() sector:MoveFloorTo(1, height, 40) end)
  if not ok then Debug.LogWarning(err) end
  ```

## Where to go next

- [Callback Functions](CallbackFunctions.md): exact timing and order of every callback.
- [Entity](Entity.md): everything on `entity`.
- The component pages, starting from the [wiki index](README.md).
- [`docs/scripts/`](../scripts/README.md): complete example scripts for doors, lifts, pickups,
  enemies, UI and more.
