# Vector2, Vector3, Vector4

Vectors are small groups of numbers. The engine uses them for positions, directions, sizes and
colours.

| Type | Fields | Used for |
|---|---|---|
| `Vector2` | `x`, `y` | Map positions (`.y` is the world `z`), screen positions, UI sizes. |
| `Vector3` | `x`, `y`, `z` | World positions, velocities, scales, directions. Also a sector's `light` (`0..255`). |
| `Vector4` | `x`, `y`, `z`, `w` | Colours (red, green, blue, alpha, `0..1`) and the Transform's rotation quaternion. |

---

## Making one

```lua
local a = Vector2(3, 4)
local b = Vector3(0, 10, 0)
local red = Vector4(1, 0, 0, 1)
local zero = Vector3()          -- all zeros
```

Pass every component or none: `Vector3(1, 2)` is an error.

---

## Fields and properties

| Name | Vector2 | Vector3 | Vector4 | |
|---|:---:|:---:|:---:|---|
| `x`, `y` | ✓ | ✓ | ✓ | read/write |
| `z` | | ✓ | ✓ | read/write |
| `w` | | | ✓ | read/write |
| `length` | ✓ | ✓ | | read-only. Length of the vector. |
| `lengthSquared` | ✓ | ✓ | | read-only. Cheaper than `length`; fine for comparing distances. |
| `normalized` | ✓ | ✓ | | read-only. A copy with length 1. A zero vector stays zero. |

```lua
local toPlayer = player.transform.position - entity.transform.position
if toPlayer.length < 64 then
    local dir = toPlayer.normalized
end
```

---

## Operators

| Expression | Result |
|---|---|
| `a + b`, `a - b` | Added or subtracted per component. |
| `a * b`, `a / b` | Multiplied or divided per component. |
| `v * 2`, `2 * v` | Every component times a number. |
| `v / 2` | Every component divided by a number. (`2 / v` is not supported.) |
| `-v` | Every component negated. |
| `a == b` | `true` if every component is exactly equal. |
| `tostring(v)` | Text like `Vector3(1, 2, 3)`. `Debug.Print(v)` does this for you. |

Both sides must be the same type: `Vector3 + Vector2` is an error.

`==` compares exactly, so two positions that came out of maths are rarely equal. Compare the
distance instead: `(a - b).length < 0.01`.

---

## Changing one component

Setting `x`, `y`, `z` or `w` straight on a component's vector property changes the component:

```lua
entity.transform.position.y = 50        -- moves the entity
entity.rigidbody.velocity.y = 0         -- stops falling
entity.sprite.color.w = 0.5             -- half transparent
```

The same goes for colours, velocities and every other writable vector property. Read-only ones
such as `camera.forward` raise an error instead.

## Vectors in variables are copies

A vector you put in a variable or a table is your own **copy**. Changing it changes nothing else:

```lua
local start = entity.transform.position
start.y = 50                            -- only changes `start`
entity.transform.position = start       -- now the entity moves
```

The other way round is safe too: after `entity.transform.position = p`, changing `p` doesn't
move the entity again.

This works because the engine rewrites `<something>.<property>.x = value` when it loads your
script, so only a direct `a.b.position.y = ...` assignment writes through. Going through a
variable (`local p = a.b.position; p.y = ...`) always changes the copy.

---

## More vector maths

Dot and cross products, distances, angles, rotation, reflection, `MoveTowards`, `Lerp` and more
are in [`mathT`](mathT.md#vector2):

```lua
local d = mathT.Vector3Distance(a, b)
local facing = mathT.Vector2ToAngle(Vector2(dir.x, dir.z))
local pos = mathT.Vector3MoveTowards(pos, target, speed * GameTime.deltaTime)
```
