# GameTime

`GameTime` tells a script how much time has passed. All of its values are read-only.

| Property | Type |                                                                       |
|---|---|-----------------------------------------------------------------------|
| `deltaTime` | number | Seconds since the previous frame. Use it in `Update`.                 |
| `fixedDeltaTime` | number | The fixed step `FixedUpdate` runs on: always `1/60` (about `0.0167`). |
| `osTime` | integer | The real-world clock: whole seconds since 1 January 1970 (UTC).       |
 |`fps`| integer | Frames-per-second smoothed to the closes integer                      |
| `timeInSeconds` | integer | Time since the engine start in seconds |
---

## deltaTime

Frames don't all take the same time. Anything that should happen **per second** rather than
**per frame** has to be multiplied by `deltaTime`, or it will run faster on faster computers.

```lua
---@field speed number
speed = 30   -- units per second

function Update()
    -- Wrong: moves 30 units every frame (1800 per second at 60 fps).
    -- entity.transform:addPosition(Vector3(speed, 0, 0))

    -- Right: moves 30 units per second at any frame rate.
    entity.transform:addPosition(Vector3(speed * GameTime.deltaTime, 0, 0))
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

`deltaTime` is the real frame time. There is no time scale or pause yet, and a long stall (a
loading hitch, a breakpoint) makes the next `deltaTime` large. If a big jump would break
something, cap it: `local dt = mathT.Min(GameTime.deltaTime, 0.1)`.

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
    Debug.Print("Current FPS:" ... GameTime.fps)
end
```

## timeInSeconds

`timeInSeconds` is ng

```lua
function Update()
    Debug.Print("Current FPS:" ... GameTime.fps)
end
