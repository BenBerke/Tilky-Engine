# Lua Script Examples

Copy-paste-ready Lua scripts for Tilky. Every example is a complete script: drop it into your
project's `Assets/Scripts/` folder (sub-folders are fine), attach it in the inspector, and fill in
its public fields.

| File | What's inside |
|------|---------------|
| [01_hello_world.md](01_hello_world.md) | Lifecycle functions, logging, timers |
| [02_public_fields.md](02_public_fields.md) | Every `---@field` type and how it shows up in the inspector |
| [03_player.md](03_player.md) | Crouch, double jump, speed zones, jump pads |
| [04_entity_movement.md](04_entity_movement.md) | Patrol, orbit, bob & spin, follow, keyboard mover |
| [05_doors.md](05_doors.md) | Automatic, use-key, locked, and switch-controlled doors |
| [06_lifts_and_platforms.md](06_lifts_and_platforms.md) | Lifts, call-button elevators, crushers, rising lava |
| [07_lighting.md](07_lighting.md) | Flicker, pulse, alarm strobe, day/night, light switch |
| [08_walls_and_textures.md](08_walls_and_textures.md) | Scrolling textures, texture switches, color cycling |
| [09_interaction.md](09_interaction.md) | Look-and-press interaction, levers, hitscan weapon |
| [10_health_and_damage.md](10_health_and_damage.md) | Health script, damage zones, calling other scripts |
| [11_pickups.md](11_pickups.md) | Health packs, coins, keys, a coin counter |
| [12_enemy_ai.md](12_enemy_ai.md) | Chase/attack state machine with line of sight, wandering |
| [13_utilities.md](13_utilities.md) | Timers, easing, event bus, respawner (no coroutines needed) |
| [14_ui.md](14_ui.md) | HUD text, popup messages, pulsing UI |
| [15_audio.md](15_audio.md) | Footsteps, toggleable radio, sounds from sector scripts |
| [16_camera_effects.md](16_camera_effects.md) | FOV kick, zoom, head bob, screen shake |
| [17_level_flow.md](17_level_flow.md) | Teleporters, checkpoints, kill counters, level exit |
| [18_debugging.md](18_debugging.md) | Print helpers, raycast inspector, safe calls |

---

## How scripts work

A script is attached either to an **Entity** (entity script) or to a **sector** (sector script).
Each attachment gets its own private environment, so two copies of the same script never share
variables.

| Function | When it runs |
|----------|--------------|
| `OnEnable()` | When the script becomes active (before `Start` the first time) |
| `Start()` | Once, the first time the script is active. All scripts already exist by now |
| `Update()` | Every frame. Use `GameTime.deltaTime` (seconds) for anything time-based |
| `FixedUpdate()` | On a fixed 60 Hz step. `GameTime.fixedDeltaTime` is the step length |
| `OnDisable()` | When the script or its Entity is disabled |
| `OnDestroy()` | When the script is torn down (level stop, `Destroy()`) |

Every function is optional. A runtime error inside any of them is printed in red to the in-game
console and logged, and the script keeps running.

### Globals available to every script

| Name | What it is |
|------|------------|
| `entity` | The Entity this script is on. On a sector script it is an invalid placeholder (`entity.isValid == false`) |
| `sector` | Sector scripts only: the sector the script is on |
| `GameTime` | `deltaTime`, `fixedDeltaTime`, `osTime` (wall-clock seconds since 1970, UTC) |
| `Input` | Keyboard and mouse: `GetKey`, `GetKeyDown`, `GetKeyUp`, `GetMouseButton*`, `GetMousePosition` |
| `Game` | `Raycast(...)`, `LoadLevel(name)`, `FindEntity(name)`, `FindEntities(name)`, `FindEntitiesWithTag(tag)`, `GetEntity(id)`, `GetEntities()` |
| `Debug` | `Print` (in-game console), `LogInfo`, `LogWarning`, `LogError`, `LogCritical` |
| `mathT` | Everything in Lua's `math` (`Abs`, `Floor`, `Sin`, ...), constants (`Pi`, `Tau`, `Infinity`, ...), `Clamp`, `Lerp`, `SmoothDamp`, `MoveTowards`, angle helpers, random helpers and `Vector2*`/`Vector3*`/`Vector4*` functions. The script editor's autocomplete lists them all |
| `Vector2`, `Vector3`, `Vector4` | Constructors: `Vector3(x, y, z)`. Support `+ - * /` (with a vector or a number), unary `-`, `==` and `tostring` |
| `Scripts` | One table shared by **every** script in the level. Reset when the level starts |

Only the Lua `base`, `math`, `table` and `string` libraries are loaded. There is **no** `os`, `io`,
`require`, or `coroutine`. Do timing with a variable and `GameTime.deltaTime` (see
[13_utilities.md](13_utilities.md)).

### Public fields

A public field is a plain top-level variable with a `---@field` comment directly above its default
value. The inspector reads the comment, so the script is never run just to build the UI:

```lua
---@field speed number @ Move Speed
speed = 30
```

Full syntax and all types are in [02_public_fields.md](02_public_fields.md).

---

## Conventions used by the engine

- **Axes:** `x` and `z` are the ground plane, `y` is up. Sector vertices are `Vector2`, where the
  vertex's `y` is the world `z`.
- **Feet, not eyes:** `transform.position` is at an entity's feet. The player's camera sits
  `playerController.eyeHeight` above it, so anything aimed "from the player's eyes" uses
  `position.y + eyeHeight`.
- **Facing:** a camera with yaw `a` (degrees) looks along `x = sin(a)`, `z = cos(a)`.
- **Player and camera:** the player scripts assume the `Camera` component is on the same
  Entity as the `PlayerController` (the usual setup), and log an error if it isn't. If your
  camera lives on a separate Entity, add a public field such as
  `---@field playerCamera Camera` to the script and use that instead of `entity.camera`.
- **Lists are 1-based:** `sector:GetWall(1)` is the first wall.
- **Colors:** wall, floor, ceiling, and sprite colors are `Vector4` in `0..1`. `sector.light` is a
  `Vector3` in `0..255`.
- **Speeds:** the default player walks at 46 units/s with an eye height of 12, so door heights of
  ~40 and lift speeds of ~30-60 feel right.

## Things that will bite you

1. **Vectors are copies.** `transform.position.y = 5` does nothing. Read, change, write back:
   ```lua
   local p = transform.position
   transform.position = Vector3(p.x, 5, p.z)
   ```
2. **Vector math makes new vectors.** `a + b`, `v * 2`, `v / 2` and `-v` work and return a new
   vector, so `transform.position = transform.position + offset` is the way to move something.
   `*` and `/` between two vectors work per component. Many older examples here still do the math
   component by component, which works too.
3. **Set public fields' values in `Start`, not at the top of the file.** The inspector's values are
   applied *after* the file's top level runs, so top-level code only ever sees the inline default.
4. **Calling into another script uses a colon and an explicit `self`.**
   ```lua
   -- In Health.lua
   function TakeDamage(self, amount) ... end

   -- Anywhere else
   target:GetScript("Health"):TakeDamage(10)
   ```
5. **Share state by mutating `Scripts`, never by replacing it.** `Scripts.keys = Scripts.keys or {}`
   is fine. `Scripts = {}` only changes your own copy of the variable.
6. **Sector scripts can't be reached with `GetScript`.** Only entity scripts can. Use the shared
   `Scripts` table (or a public field on an entity) to talk to a sector script.
7. **Setters can throw.** For example a sector's ceiling must stay above its floor. Wrap risky
   writes in `pcall` (see [05_doors.md](05_doors.md)) so one bad value doesn't spam the console
   every frame.
8. **There is no `Instantiate`.** To get at other objects, use an `Entity` public field,
   `Game.FindEntity("Name")`, `Game.FindEntitiesWithTag("Tag")`, `Game.Raycast`,
   `sector:GetEntity(i)`, or the `Scripts` table. The `Find` calls go through every Entity in the
   level, so call them in `Start` and keep the result instead of calling them every frame.
9. **Tags are read-only from Lua.** Assign them in the editor, then test with `HasTag("Name")`.
10. **`Entity:Destroy()` is deferred** to the end of the frame, so the object is still valid for
    the rest of the current frame.
11. **`Game.LoadLevel` from a script is experimental.** The engine's own code carries a TODO about
    checking it in a running game. See [17_level_flow.md](17_level_flow.md).

## How these examples were checked

Every script in this folder was syntax-checked and executed for several hundred simulated frames
against a strict mock of the bindings (any member that doesn't exist in the real API raises an
error) on Lua 5.5. That catches typos and misuse of the API, but the scripts have **not** been
played inside the engine itself, so expect to tune numbers (speeds, distances, heights) to your
level's scale.

Script blocks start with a header comment naming the suggested path and what it attaches to:

```text
-- Scripts/Doors/AutoDoor.lua (sector script)
```
