# Particle Emitter

**Inspector name:** Particle Emitter · **Lua:** `entity.particleEmitter` · **Public field type:** `ParticleEmitter`

A Particle Emitter throws out small images: blood, sparks, smoke, shell casings, splashes. Particles
aren't entities, so you can have hundreds without slowing the game down. They can't run scripts or be
found with `Game.FindEntity`, and nothing can collide with them.

A particle can also leave a **stain** where it lands: a flat picture stuck to a wall, floor or ceiling.
Stains ride along on doors and lifts.

An entity can have several Particle Emitters, such as one for droplets and one for mist.

## Quick start

To make a burst whenever something happens, without spawning anything:

1. Make a prefab and add a Particle Emitter to it. It needs no sprite.
2. Add an image to **Images**, and set **Burst Min** and **Burst Max**.
3. Fire it, with either of these:
   - A dispatch: **Emit Particles**, with your prefab in its **Prefab** field.
   - A script: `Game.EmitParticles("Prefabs/Effects/My Burst.prefab", position)`

The prefab is only a list of settings. Nothing is ever spawned from it, so a hundred shots cost almost nothing.

For something that keeps going, such as a smoking chimney, put the emitter on an entity in your level,
set **Rate** above 0 and tick **Play On Start**.

## Settings

### Emission

| Setting | What it does |
|---|---|
| **Play On Start** | Fires one burst when the entity starts, then keeps going if **Rate** is above 0 |
| **Rate** | Particles per second while playing. 0 means bursts only. |
| **Burst Min**, **Burst Max** | How many particles one burst throws, picked at random in between |

### Motion

| Setting | What it does |
|---|---|
| **Offset** | Where particles start, relative to the entity and turned with it |
| **Direction** | Which way they go, turned with the entity |
| **Spread** | Degrees either side of **Direction**. 180 goes every way. |
| **Speed Min**, **Speed Max** | Start speed, in units per second |
| **Gravity** | Units per second gained downward each second. Below 0, they rise. |
| **Drag** | The share of its speed a particle loses each second |
| **Lifetime Min**, **Lifetime Max** | How long each one lasts, in seconds |

### Look

| Setting | What it does |
|---|---|
| **Images** | Each particle picks one at random |
| **Animate** | Plays **Images** in order over each particle's life, instead of picking one |
| **Size Min**, **Size Max** | Start size, in world units |
| **Size At End** | Its size at the end of its life, as a multiple of where it started |
| **Color At Start**, **Color At End** | Multiplied with the image. Set the end's alpha to 0 to fade out. |

### Surfaces

| Setting | What it does |
|---|---|
| **On Hitting A Surface** | **Pass Through**, **End There** or **Bounce** |
| **Bounciness** | The share of its speed it keeps off a surface. It rests once slow enough on a floor. |
| **Stain Images** | Left where a particle meets a surface. One is picked at random. |
| **Stain Chance** | From 0 to 1, how likely each hit is to leave a stain |
| **Stain Size Min**, **Stain Size Max** | How big a stain is, in world units |
| **Stain Lifetime** | Seconds until it fades. 0 keeps it until **Max Stains** removes it. |
| **Stain Color** | Multiplied with the stain image |
| **No Stain Tag** | No stains on the floor or ceiling of sectors with this tag, such as liquids |

Pass Through is the cheapest. The other two check every particle against the level each frame.

## How many there can be

**World Settings** has **Max Particles** (2048) and **Max Stains** (256). Over either one, the oldest
are removed to make room.

## Scripting

| Property | Type | Description |
|---|---|---|
| `isValid` | boolean, read-only | |
| `isActive` | boolean | Off, it fires nothing. Live particles carry on. |
| `isPlaying` | boolean, read-only | True while the steady stream (**Rate**) is on |
| `rate` | number | |
| `burstMin`, `burstMax` | integer | |
| `offset`, `direction` | `Vector3` | |
| `spread`, `speedMin`, `speedMax`, `gravity`, `drag` | number | |
| `lifetimeMin`, `lifetimeMax`, `sizeMin`, `sizeMax`, `sizeEnd`, `bounciness` | number | |
| `colorStart`, `colorEnd` | `Vector4` | |

| Method | Description |
|---|---|
| `Play()` | Turns the steady stream on |
| `Stop()` | Turns the steady stream off. Live particles carry on. |
| `Emit([count], [position], [direction])` | Fires a burst. Left out, these use the emitter's own numbers and the entity's place. |

To fire a prefab's emitters or leave a mark without an emitter of your own, see
[`Game.EmitParticles`](Game.md#emitparticles) and [`Game.StainSurface`](Game.md#stainsurface).

```lua
public Entity burstPrefab = nil

-- Call this from another script, or from a Call Function dispatch.
function Burst()
    local up = Vector3(0, 1, 0)
    Game.EmitParticles(burstPrefab, entity.transform.position, up, 2)   -- twice the usual number
end
```

The dispatch **Enable / Disable Component** and **Set Field** both work on a Particle Emitter. A
disabled entity's emitters fire nothing.
