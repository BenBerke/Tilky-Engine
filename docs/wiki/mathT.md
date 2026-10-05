# mathT

`mathT` is the engine's maths table. It has everything in Lua's own `math` library under
capitalised names, plus the helpers games need all the time: clamping, interpolation, angles,
random numbers and vector maths.

```lua
local t = mathT.Clamp01(timer / duration)
local height = mathT.Lerp(bottom, top, mathT.SmoothStep(0, 1, t))
```

Lua's standard `math` library is still there too (`math.floor`, `math.random`, ...). Use
whichever you like. Note that `mathT.Random*` and `math.random` are separate generators:
`mathT.RandomSeed` doesn't affect `math.random`.

**Radians or degrees?** `Sin`, `Cos`, `Tan`, `Asin`, `Acos`, `Atan` and `Atan2` use **radians**,
like Lua's `math`. Everything with `Angle` in its name, `Vector2Rotate` /
`Vector2FromAngle`, and the `Quaternion*` functions use **degrees**. Convert with `DegToRad` and `RadToDeg`.

---

## Constants

| Name | Value                                             |
|---|---------------------------------------------------|
| `Pi` | 3.14159265359f                                    |
| `Tau` | `2 * Pi`, one full turn in radians.               |
| `HalfPi` | `Pi / 2`, a quarter turn in radians.              |
| `E` | Euler's number,  2.718281828459045f               |
| `Sqrt2` | Square root of 2.                                 |
| `Infinity` / `NegativeInfinity` | Positive and negative infinity.                   |
| `Epsilon` | `1e-6`, the default tolerance of `Approximately`. |
| `MaxInteger` / `MinInteger` | Largest and smallest Lua integer.                 |

---

## Basic

| Function | Returns |                                                                                         |
|---|---|-----------------------------------------------------------------------------------------|
| `Abs(x)` | number | Absoulete value                                                                         |
| `Sign(x)` | integer | `-1`, `0` or `1`.                                                                       |
| `Floor(x)` / `Ceil(x)` | integer |                                                                                         |
| `Round(x)` | integer | Rounds half away from zero: `Round(2.5)` is `3`, `Round(-2.5)` is `-3`.                 |
| `Round(x, digits)` | number | Rounds to that many decimals: `Round(3.14159, 2)` is `3.14`.                            |
| `Trunc(x)` | integer | Drops the fraction (rounds toward zero).                                                |
| `Frac(x)` | number | The fraction: `x - Trunc(x)`. `Frac(-1.25)` is `-0.25`.                                 |
| `Min(a, b, ...)` / `Max(a, b, ...)` | number | Any number of arguments.                                                                |
| `Mod(x, d)` | number | Floored modulo, like Lua's `%`: the result has the sign of `d`. `Mod(-1, 360)` is `359`. |
| `Fmod(x, d)` | number | C-style remainder: the result has the sign of `x`. `Fmod(-1, 360)` is `-1`.             |
| `Sqrt(x)` | number |                                                                                         |
| `Pow(base, exponent)` | number |                                                                                         |
| `Exp(x)` | number | `E` raised to `x`.                                                                      |
| `Log(x[, base])` | number | Natural log, or the log in `base`.                                                      |
| `Log10(x)` / `Log2(x)` | number |                                                                                         |
| `Hypot(x, y)` | number | `Sqrt(x*x + y*y)`, without overflow.                                                    |

---

## Trigonometry

| Function | |
|---|---|
| `Sin(r)`, `Cos(r)`, `Tan(r)` | Radians in. |
| `Asin(x)`, `Acos(x)` | Radians out. |
| `Atan(y[, x])` | Radians out. With two arguments it is the same as `Atan2(y, x)`. |
| `Atan2(y, x)` | Radians out, full `-Pi..Pi` range. |
| `Sinh(x)`, `Cosh(x)`, `Tanh(x)` | Hyperbolic functions. |
| `DegToRad(degrees)` / `RadToDeg(radians)` | Conversions. |

---

## Interpolation and ranges

| Function | |
|---|---|
| `Clamp(x, min, max)` | Keeps `x` between `min` and `max`. `min` must not be larger than `max`. |
| `Clamp01(x)` | Keeps `x` between `0` and `1`. |
| `Lerp(a, b, t)` | `a` at `t = 0`, `b` at `t = 1`. **Not clamped**: `t = 2` goes past `b`. |
| `LerpClamped(a, b, t)` | `Lerp` with `t` clamped to `0..1`. |
| `InverseLerp(a, b, x)` | Where `x` sits between `a` and `b`: `0` at `a`, `1` at `b`. |
| `Remap(x, inMin, inMax, outMin, outMax)` | Maps `x` from one range to another (not clamped). Returns `outMin` if `inMin == inMax`. |
| `SmoothStep(a, b, t)` | Like `LerpClamped`, but eases in and out. |
| `MoveTowards(current, target, maxDelta)` | Steps toward `target` by at most `maxDelta`, never overshooting. |
| `SmoothDamp(current, target, velocity, smoothTime[, deltaTime])` | Smooth, spring-like follow. Returns **two** values: the new value and the new velocity. See below. |
| `Repeat(x, length)` | Wraps `x` into `0..length`. Returns `0` if `length <= 0`. |
| `PingPong(x, length)` | Bounces `x` back and forth between `0` and `length`. |
| `Wrap(x, min, max)` | Wraps `x` into `min..max`. |
| `Snap(x, step)` | Rounds `x` to the nearest multiple of `step`. |

### MoveTowards or Lerp?

`MoveTowards` moves at a **steady speed** and stops exactly on the target. Multiply the speed by
`GameTime.deltaTime`:

```lua
height = mathT.MoveTowards(height, targetHeight, 30 * GameTime.deltaTime)
```

`Lerp` with a fixed `t` each frame slows down as it gets closer and never quite arrives. For a
smooth follow, `SmoothDamp` is usually better.

### SmoothDamp

Lua functions can't change their arguments, so `SmoothDamp` hands the velocity back as a second
return value. Keep it in a variable and pass it back in next frame. `deltaTime` defaults to
`GameTime.deltaTime`.

```lua
public number targetY = 64

local velocity = 0

function Update()
    local p = entity.transform.position
    local y
    y, velocity = mathT.SmoothDamp(p.y, targetY, velocity, 0.3)
    entity.transform.position = Vector3(p.x, y, p.z)
end
```

---

## Angles (degrees)

| Function | |
|---|---|
| `DeltaAngle(from, to)` | The shortest signed turn from `from` to `to`, in `-180..180`. `DeltaAngle(350, 10)` is `20`. |
| `LerpAngle(a, b, t)` | `Lerp` for angles, taking the short way round. `t` is clamped. |
| `MoveTowardsAngle(current, target, maxDelta)` | `MoveTowards` for angles, taking the short way round. |
| `NormalizeAngle(degrees)` | Wraps an angle into `-180..180`. |

```lua
-- Turn toward the player at 90 degrees per second.
local player
local facing = 0

function Start()
    player = Game.FindEntity("Player")
end

function Update()
    if player == nil then return end

    local p, me = player.transform.position, entity.transform.position
    -- Atan2(x, z) is the angle the same way round as rotation Y and camera yaw.
    local wanted = mathT.RadToDeg(mathT.Atan2(p.x - me.x, p.z - me.z))
    facing = mathT.MoveTowardsAngle(facing, wanted, 90 * GameTime.deltaTime)
    entity.transform.rotation = mathT.QuaternionFromEuler(0, facing, 0)
end
```

`transform.rotation` is a quaternion, not an angle, so `QuaternionFromEuler` turns the angle into
one (see [Quaternions](#quaternions)). `Vector2ToAngle` measures from +X instead, so it doesn't
line up with rotation Y. To face a target straight away, with no turning speed, use
`transform:lookAt` (see [Transform](Transform.md#scripting)).

---

## Checks

| Function | |
|---|---|
| `Approximately(a, b[, epsilon])` | `true` if `a` and `b` differ by at most `epsilon` (default `Epsilon`). Use it instead of `==` for numbers that came out of maths. |
| `IsNaN(x)` | |
| `IsInfinite(x)` | |
| `IsFinite(x)` | `false` for NaN and infinities. |

---

## Random numbers

| Function | Returns |
|---|---|
| `Random(max)` | Integer from `0` to `max - 1`. Returns `0` if `max <= 0`. |
| `Random(min, max)` | Integer from `min` to `max`, **both included**. |
| `RandomF()` | Number from `0` to `1`. |
| `RandomF(max)` | Number from `0` to `max`. |
| `RandomF(min, max)` | Number from `min` to `max`. |
| `RandomBool()` | `true` or `false`, 50/50. |
| `RandomOnUnitCircle()` | `Vector2` direction of length 1. |
| `RandomInsideUnitCircle()` | `Vector2` point inside a circle of radius 1, evenly spread. |
| `RandomOnUnitSphere()` | `Vector3` direction of length 1. |
| `RandomInsideUnitSphere()` | `Vector3` point inside a sphere of radius 1, evenly spread. |
| `RandomFast()` | Number from `0` to `1` from a fixed table of 512 values. Cheaper, but repeats and ignores `RandomSeed`. |
| `RandomSeed(seed)` | Restarts every generator above except `RandomFast`. The same seed always gives the same sequence. |

The generator starts from the same seed every time the engine starts, so a level plays out the
same way each run unless you reseed it. For different results each run:

```lua
function Start()
    mathT.RandomSeed(GameTime.osTime)
end
```

The seed is shared by **every** script, so reseeding in one script changes what the others get.

```lua
-- Pick a random entry from a list.
local sounds = {"Hit1.wav", "Hit2.wav", "Hit3.wav"}
local pick = sounds[mathT.Random(1, #sounds)]
```

---

## Vector2

These all take and return [`Vector2`](Vector.md) values and never change their arguments. For
simple sums you can also use operators: `a + b`, `a * 2`, `v.length`, `v.normalized`.

| Function | |
|---|---|
| `Vector2Add(a, b)`, `Vector2Sub(a, b)` | |
| `Vector2Mul(a, b)`, `Vector2Div(a, b)` | Per component. |
| `Vector2Scale(v, s)`, `Vector2Negate(v)` | |
| `Vector2Length(v)`, `Vector2LengthSquared(v)` | |
| `Vector2Normalize(v)` | Length-1 copy. A zero vector stays zero. |
| `Vector2Distance(a, b)`, `Vector2DistanceSquared(a, b)` | |
| `Vector2Dot(a, b)` | |
| `Vector2Cross(a, b)` | A number: positive when `b` is counter-clockwise from `a`. |
| `Vector2Lerp(a, b, t)` | Not clamped. |
| `Vector2MoveTowards(current, target, maxDistance)` | Never overshoots. |
| `Vector2Angle(from, to)` | Unsigned angle in degrees, `0..180`. |
| `Vector2SignedAngle(from, to)` | Signed angle in degrees, `-180..180`, positive counter-clockwise. |
| `Vector2Rotate(v, degrees)` | Rotates counter-clockwise. |
| `Vector2FromAngle(degrees)` | Unit vector at that angle: `0` is `+x`, `90` is `+y`. |
| `Vector2ToAngle(v)` | The opposite of `Vector2FromAngle`. |
| `Vector2Perpendicular(v)` | `v` turned 90 degrees counter-clockwise. |
| `Vector2Reflect(v, normal)` | Bounces `v` off a surface. `normal` should have length 1. |
| `Vector2Project(v, onto)` | The part of `v` that points along `onto`. |
| `Vector2ClampLength(v, maxLength)` | Shortens `v` if it is longer than `maxLength`. |
| `Vector2Min(a, b)`, `Vector2Max(a, b)` | Per component. |

Remember that a map position's `.y` is the world `z`. To turn a 3D position into a map position,
use `Vector2(p.x, p.z)`.

---

## Vector3

| Function | |
|---|---|
| `Vector3Add(a, b)`, `Vector3Sub(a, b)` | |
| `Vector3Mul(a, b)`, `Vector3Div(a, b)` | Per component. |
| `Vector3Scale(v, s)`, `Vector3Negate(v)` | |
| `Vector3Length(v)`, `Vector3LengthSquared(v)` | |
| `Vector3Normalize(v)` | Length-1 copy. A zero vector stays zero. |
| `Vector3Distance(a, b)`, `Vector3DistanceSquared(a, b)` | |
| `Vector3Dot(a, b)`, `Vector3Cross(a, b)` | |
| `Vector3Lerp(a, b, t)` | Not clamped. |
| `Vector3MoveTowards(current, target, maxDistance)` | Never overshoots. |
| `Vector3Angle(from, to)` | Unsigned angle in degrees, `0..180`. |
| `Vector3Reflect(v, normal)` | `normal` should have length 1. |
| `Vector3Project(v, onto)` | |
| `Vector3ProjectOnPlane(v, planeNormal)` | `v` with the part along `planeNormal` removed. |
| `Vector3ClampLength(v, maxLength)` | |
| `Vector3Min(a, b)`, `Vector3Max(a, b)` | Per component. |

```lua
-- Fly toward a target at a steady speed.
local pos = entity.transform.position
entity.transform.position = mathT.Vector3MoveTowards(pos, target, speed * GameTime.deltaTime)
```

---

## Vector4 (colours)

| Function | |
|---|---|
| `Vector4Add(a, b)`, `Vector4Sub(a, b)` | |
| `Vector4Mul(a, b)` | Per component, e.g. tinting a colour. |
| `Vector4Scale(v, s)` | |
| `Vector4Lerp(a, b, t)` | Fading between two colours. Not clamped. |

```lua
-- Fade a sprite from white to red over one second.
local t = mathT.Clamp01(timer)
entity.sprite.color = mathT.Vector4Lerp(Vector4(1, 1, 1, 1), Vector4(1, 0, 0, 1), t)
```

---

## Quaternions

Rotations, such as [`transform.rotation`](Transform.md#rotation), are quaternions stored in a
`Vector4` `(x, y, z, w)`. Angles are in degrees and use the same X/Y/Z as the inspector: Y turns
left and right like camera `yaw`. An entity faces its local +Z, `Vector3(0, 0, 1)`.

| Function | |
|---|---|
| `QuaternionFromEuler(x, y, z)` | Rotation from X/Y/Z angles in degrees. Returns a `Vector4`. |
| `QuaternionToEuler(q)` | The X/Y/Z angles in degrees, as a `Vector3`. Y comes back in `-90..90` (see below). |
| `QuaternionAngleAxis(axis, degrees)` | Rotation of `degrees` around the `Vector3` `axis`. A zero axis gives no rotation. |
| `QuaternionMultiply(a, b)` | Both rotations combined: `b` first, then `a`. |
| `QuaternionRotate(q, v)` | Rotates the `Vector3` `v` by `q`. |

`QuaternionToEuler` gives back *a* set of angles for the rotation, not always the ones you put in.
Y is kept within `-90..90`, so a turn of Y `135` reads back as `180, 45, 180`, which is the same
rotation. To read how far an entity is turned left or right, use its facing direction instead:

```lua
local f = mathT.QuaternionRotate(entity.transform.rotation, Vector3(0, 0, 1))
local yaw = mathT.RadToDeg(mathT.Atan2(f.x, f.z)) -- same as camera yaw
```

To make an entity face a point or a direction, use
[`transform:lookAt`](Transform.md#scripting) / `transform:lookDirection` instead of building the
quaternion yourself.

```lua
-- Spin around the vertical axis.
local turn = mathT.QuaternionAngleAxis(Vector3(0, 1, 0), 90 * GameTime.deltaTime)
entity.transform.rotation = mathT.QuaternionMultiply(turn, entity.transform.rotation)

-- Which way is the entity facing?
local facing = mathT.QuaternionRotate(entity.transform.rotation, Vector3(0, 0, 1))
```
