# GameTime

`GameTime` tells a script how much time has passed, and sets how fast the game runs. Every value
is read-only except `timeScale`.

| Property | Type |                                                                       |
|---|---|-----------------------------------------------------------------------|
| `deltaTime` | number | Seconds since the previous frame, times `timeScale`. Use it in `Update`. |
| `unscaledDeltaTime` | number | Real seconds since the previous frame. `timeScale` doesn't change it. |
| [`timeScale`](#timescale) | number | **read/write.** How fast the game runs: `1` normal, `0.5` half speed, `0` paused. |
| [`time`](#time-unscaledtime-and-framecount) | number | Seconds of game time since the game started. Follows `timeScale`. |
| `unscaledTime` | number | Real seconds the game has run since it started. |
| `frameCount` | integer | Frames the game has run since it started. |
| `fixedDeltaTime` | number | The fixed step `FixedUpdate` runs on: always `1/60` (about `0.0167`). |
| `osTime` | integer | The real-world clock: whole seconds since 1 January 1970 (UTC).       |
 |`fps`| integer | Frames-per-second smoothed to the closes integer                      |
| `timeInSeconds` | integer | Time since the engine start in seconds |
---

## deltaTime

Frames don't all take the same time. Anything that should happen **per second** rather than
**per frame** has to be multiplied by `deltaTime`, or it will run faster on faster computers.

```lua
public number speed = 30   -- units per second

function Update()
    -- Wrong: moves 30 units every frame (1800 per second at 60 fps).
    -- entity.transform:AddPosition(Vector3(speed, 0, 0))

    -- Right: moves 30 units per second at any frame rate.
    entity.transform:AddPosition(Vector3(speed * GameTime.deltaTime, 0, 0))
end
```

Timers work the same way: count `deltaTime` down (or up) yourself.

```lua
local cooldown = 0

function Update()
    cooldown = cooldown - GameTime.deltaTime
    if cooldown <= 0 and Input.GetMouseButtonDown(Input.MouseLeft) then
        cooldown = 0.25   -- at most 4 shots per second
        Fire()
    end
end
```

For "do this in 2 seconds", [`Invoke`](Invoke.md) is simpler than a timer of your own.

`deltaTime` is the frame time multiplied by [`timeScale`](#timescale), so at `timeScale = 0` it
is `0`. A long stall (a loading hitch, a breakpoint) makes the next `deltaTime` large. If a big
jump would break something, cap it: `local dt = mathT.Min(GameTime.deltaTime, 0.1)`.

---

## timeScale

```lua
GameTime.timeScale = 0.25   -- slow motion
GameTime.timeScale = 0      -- pause
GameTime.timeScale = 1      -- normal
```

`timeScale` multiplies `deltaTime`, so everything that runs on game time speeds up, slows down or
stops with it:

- physics, rigidbodies and the Player Controller's movement,
- sector moves and light fades, flipbooks, the sky's `rotationSpeed`,
- event delays, timers, doors and message durations,
- `FixedUpdate` (at `0` it isn't called at all),
- [`Invoke`](Invoke.md) and `InvokeRepeating`, and your own code that uses `deltaTime`.

What keeps going at any `timeScale`:

- **Sound.** Playing sounds go on at normal speed. Pause them yourself if you want.
- **`Update`** is still called every frame, so a pause menu script can still read input. Use
  `unscaledDeltaTime` and [`InvokeReal`](Invoke.md) for anything that should move while paused.
- **Mouse look**, since it follows the mouse rather than time.

Negative values are raised to `0`. A value that isn't a number raises an error.

`timeScale` goes back to `1` every time the game starts (pressing **Play**, or launching the
exported game) and when you press **Stop**. It is **kept** across
[`Game.LoadLevel`](Game.md#loadlevel), so set it back yourself if a level change shouldn't carry a
pause or slow motion over.

```lua
-- Scripts/PauseMenu.lua: Escape pauses and unpauses.
local paused = false

function Update()
    if Input.GetKeyDown(Key.Escape) then
        paused = not paused
        GameTime.timeScale = paused and 0 or 1
    end
end
```

```lua
-- Scripts/BulletTime.lua: slow motion for 3 real seconds.
public function SlowMotion()
    GameTime.timeScale = 0.3
    InvokeReal(function() GameTime.timeScale = 1 end, 3)
end
```

---

## time, unscaledTime and frameCount

- `time` adds up `deltaTime`: seconds of **game** time since the game started. It stands still
  while `timeScale` is `0`.
- `unscaledTime` adds up `unscaledDeltaTime`: **real** seconds the game has run.
- `frameCount` counts frames the game has run.

All three start at `0` when the game starts and keep counting across level changes. In the
editor's Play mode they don't count while the game is paused with the mouse released (**Tab**),
since the game doesn't update then.

```lua
-- Bob up and down, in step with game time.
local baseY

function Start()
    baseY = entity.transform.position.y
end

function Update()
    local p = entity.transform.position
    entity.transform.position = Vector3(p.x, baseY + math.sin(GameTime.time * 3) * 2, p.z)
end
```

---

## fixedDeltaTime

Inside `FixedUpdate`, use `fixedDeltaTime` instead of `deltaTime`: each call stands for exactly
one 1/60 s step. See [Callback Functions](CallbackFunctions.md#fixedupdate).

```lua
local ticks = 0

function FixedUpdate()
    ticks = ticks + 1
    elapsed = ticks * GameTime.fixedDeltaTime   -- exact, no drift
end
```

---

## osTime

`osTime` is the clock on the player's computer, in whole seconds. It keeps counting whatever the
game does, so it is not useful for gameplay timing. Use it for things like:

- **Seeding random numbers** so each run is different: `mathT.RandomSeed(GameTime.osTime)`.
- Measuring how long a whole play session lasted.

```lua
local startedAt

function Start()
    startedAt = GameTime.osTime
end

function OnTriggerEnter(other)
    if other.hasPlayerController then
        Debug.Print("Finished in", GameTime.osTime - startedAt, "seconds")
    end
end
```

## fps

`fps` is the current frames-per-second smoothed to the nearest integer. Useful for debugging

```lua
function Update()
    Debug.Print("Current FPS:" .., GameTime.fps)
end
```

## timeInSeconds

`timeInSeconds` is ng

```lua
function Update()
    Debug.Print("Current FPS:" ... GameTime.fps)
end
