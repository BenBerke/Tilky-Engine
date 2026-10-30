# Audio

`Audio` plays a sound **once**, without an [Audio Source](AudioSource.md) component. It is for the
short sounds that just happen: a gunshot, an explosion, a footstep, a UI click, a pickup jingle.

```lua
Audio.PlayAt("Sounds/explosion", entity.transform.position)   -- in the world
Audio.Play2D("Sounds/UI/click")                                -- the same everywhere
```

| Function | |
|---|---|
| [`PlayAt(sound, position, [gain], [pitch])`](#playat) | Plays a sound once at a point in the world. |
| [`Play2D(sound, [gain], [pitch])`](#play2d) | Plays a sound once with no position: same volume and balance wherever the listener is. |

**Audio or an Audio Source?** Use `Audio` for fire-and-forget sounds. Use an
[Audio Source](AudioSource.md) for anything you need to control after it starts (stop, pause,
loop, change pitch while it plays) or that should **follow** a moving entity: `PlayAt` stays where
it started.

---

## PlayAt

```lua
Audio.PlayAt(sound, position, [gain], [pitch])
```

| Parameter | Type | |
|---|---|---|
| `sound` | string | A `.wav` path inside `Assets`, extension optional: `"Sounds/explosion"` or `"Sounds/explosion.wav"`. The same as an Audio Source's [`soundFileName`](AudioSource.md#sound-files). |
| `position` | `Vector3` | Where in the world the sound plays. |
| `gain` | number, optional | Volume, `1` by default. `0` is silent; above `1` is louder. Below `0` counts as `0`. |
| `pitch` | number, optional | `1` by default. `2` is an octave up and twice as fast, `0.5` an octave down. Below `0.01` counts as `0.01`. |

The sound plays once, from the start, at `position`, the way Unity's
`AudioSource.PlayClipAtPoint` does. It gets quieter with distance from the listener (the active
camera) and comes from the right direction, using the level's audio settings (**World Settings >
Audio**) and the same falloff as a new Audio Source: reference distance `1`, roll-off factor `1`,
and no max distance.

- Any number can play at once; a new one never cuts off another.
- The sound stays at `position` even if whatever made it moves away.
- It plays to the end. There is no way to stop one early; use an Audio Source for that.
- It stops when the level changes ([`Game.LoadLevel`](Game.md#loadlevel)) or you stop playing.
- Like every sound, it isn't slowed or paused by [`GameTime.timeScale`](GameTime.md#timescale).
- **Use mono files.** A stereo file plays without fading or direction, like `Play2D`.
- A sound that doesn't exist, or arguments of the wrong type, raise an error naming the problem.

```lua
-- Scripts/Barrel.lua: a bang where the barrel was, slightly different each time.
public function Explode()
    Audio.PlayAt("Sounds/explosion", entity.transform.position, 1, mathT.RandomF(0.9, 1.1))
    entity:Destroy()
end
```

---

## Play2D

```lua
Audio.Play2D(sound, [gain], [pitch])
```

The same as [`PlayAt`](#playat) without a position: the sound plays at the listener, at the same
volume wherever the camera is and whichever way it faces. Stereo files keep their left/right
balance.

```lua
-- Scripts/Coin.lua (entity with a trigger Collider)
function OnTriggerEnter(other)
    if not other.hasPlayerController then return end

    Audio.Play2D("Sounds/coin", 0.8)
    Global.coins = (Global.coins or 0) + 1
    entity:Destroy()   -- the sound keeps playing: it doesn't belong to the coin
end
```
