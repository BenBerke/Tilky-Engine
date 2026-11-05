# 16. More Engine Features

This page is a tour of what the engine can do beyond what the kit uses every day. Skim it now, and come back
when you need something.

## Every event

An event belongs to an entity, a sector, a wall, a linedef or the level.

**On an entity:**

| Event | Fires when |
|---|---|
| **On Start** | The level starts |
| **Collide** | Something touches it. On a trigger, this means walking into it. |
| **Change Sector** | It moves into another sector |
| **Grounded In Sector** | It lands on a sector's floor |
| **Enabled**, **Disabled**, **Destroyed** | That's just happened to it |
| **Receive Ray** | A ray hits it. The player's Use key and bullets are both rays. |
| **Condition Met** | Its **Condition** has just become true |
| **Timer** | Every so often. Set **Interval** and **First Delay**. |
| **Lua Signal** | A script sent a signal with this **Signal Name** |
| **Key Press**, **Key Release** | The player used a key |
| **Button Click**, **Button Release**, **Button Hover Enter**, **Button Hover Exit** | Used with the UI Button component |
| **Pathfinder Arrived**, **Blocked**, **Stuck**, **Started Moving**, **Stopped Moving** | Used with the Pathfinder component |
| **Target Spotted**, **Target Lost**, **Melee Ready**, **Missile Ready**, **Pain**, **Died**, **Active** | Used with the Monster AI component |

**On a sector:** **Entity Enter**, **Entity Exit**, **Entity Stays In Sector**, **Entity Grounded** and **Move Finished**.

**On a wall:** **Entity Collide** and **Entity Crossover**.

## Every dispatch

| Group | Dispatches |
|---|---|
| **Sector and wall** | Set Color, Set Texture, Set Collision, Set Light, Light Effect, Enable / Disable Linedef |
| **Moving floors** | Move floor to ceiling, Move ceiling to floor, Move to Height, Door, Lift, Raise Stairs, Stop Movement |
| **Entities** | Add / Remove Tag, Destroy, Disable, Enable, Change Velocity, Move To, Teleport, Spawn Prefab, Emit Particles, Enable / Disable Component |
| **Fields** | Set Field, Set Sprite, Set UI Text |
| **Enemies** | Pathfinder, Monster AI, Noise Alert |
| **Sound** | Play Sound, Music |
| **Game** | Load Level, Pause Game, Resume Game, Quit Game, Show Message |
| **Logic** | Change Variable, Relay, Reset Events, Call Function |

Hover over a setting in the editor to see its tooltip.

## Settings every event has

| Setting | What it does |
|---|---|
| **Filter Tags** | Only react to entities with these tags |
| **Max Triggers** | How many times it can ever fire. 1 means once, and 0 means forever. |
| **Chance** | Percent chance of firing each time |
| **Condition** | Only fire if a variable matches. You can use variables like `Health <= 0`. |

## Settings every dispatch has

| Setting | What it does |
|---|---|
| **Name** | What events call it by. Several can share a name. |
| **Delay** | Seconds to wait before running |
| **Cooldown** | Seconds before it can run again |
| **Cooldown Triggers** | How many calls it takes to start the cooldown. 1 starts it on every call. |

## Chains: + Add Next Dispatch

Rather than making everything happen at once, a dispatch can call another when it's done. Click
**+ Add Next Dispatch**, and you get:

- **Who:** this entity, the instigator, or a tagged group
- **Which** dispatch, by name
- **Wait:** the seconds to wait first. 0 means the next frame.
- **Go On If:** **Always**, **Variable** to check one, or **Chance** to go on at random

You can stack several. This lets you write small programs with no code. For example, a spawner that sends a wave
every 10 seconds, three times, then opens a door:

```
   Spawn Wave:  Spawn Prefab  (a Zombie)
                Change Variable WavesLeft, Subtract 1
     Next:      Self "Spawn Wave", Wait 10, Go On If WavesLeft > 0
     Next:      Self "Open Door",            Go On If WavesLeft = 0
```

**Using Relay.** A **Relay** does nothing but run other dispatches: every dispatch on an event tag, or the named dispatches of one target. It's useful as a junction.

**Using Reset Events.** This lets a **Max Triggers** event fire again, such as a one-shot trap you want to rearm.

## Set Field: change any setting while playing

**Set Field** changes a component's setting while the game runs, and the kit uses it a lot. Pick the entity, the
component, the field and the value.

- **Over Time** with a **Speed** makes it change smoothly instead of jumping.
- **Revert** puts it back after a while, for flashes and temporary effects.

That's how the player's Eye Height sinks when dying. It works for Fields like a Camera's **FOV**, for a zoom, or
Rigidbody **Gravity Scale**, for low gravity.

## Lighting

**Set Light** changes a sector's brightness. **Light Effect** makes it keep changing, and is how the kit's
corridors flicker.

**World Settings > Rendering** has the sky and the fade into the distance, which you met on [page 10](10-Finish-Your-Game.md).

The **Lamp** decorations don't give off light by themselves. Sectors do. To fake a lamp's glow, draw a small brighter sector around it.

## Moving more than doors and lifts

- **Raise Stairs** makes a staircase rise, one step at a time, out of neighbouring sectors.
- **Move to Height** sends a floor or ceiling to any height, and fires **Move Finished** on arrival.
- **Teleport** moves an entity to **Coordinates**, an **Entity** or a **Sector**. **Facing** sets which way it
  looks, and **Stop Velocity** stops it moving. On a sector, **Teleport Entities Inside** moves everything standing in it.

## Snap to Wall

Flat things, such as switches and signs, can stick to walls. Select one and click **Snap to Wall** in its inspector, then
right-click the wall. It stays flat against the wall, and you can drag it along.

This only helps in the editor. If a wall moves during play, what's snapped to it stays where it was.

## Linedefs

A linedef is a line on the map that isn't a wall. You can walk through it, but it can fire events when something crosses.
That makes it a clean way to make a trigger with no invisible box.

In Geometry Mode (**G**), turn on **Linedef Mode**, and draw one. **Blocks Pathfinders** stops monsters walking through it,
which is great for keeping them in their area.

## Sound

**World Settings > Listener Settings** decides how sound fades with distance, for each level. The kit's levels use
a reference distance of 40 and a maximum of 350, which matches Doom's feel. A new level uses the engine's defaults, so copy these numbers.

An **Audio Source** component plays a sound from where its entity stands. Turn on **Looping** for machines or
torches, and it keeps going with no dispatches needed.

**World Settings > Music** holds a list of tracks, played **Once**, on **Loop**, **Ping-pong** or **Shuffle**. Levels with the
same list carry on playing with no interruption. See [Audio](../Audio.md) and [AudioSource](../AudioSource.md).

## Physics

**World Settings > Physics** has **Gravity** and **Max Physics Sub-steps**. Sub-steps stop very fast things passing
through walls, at some cost in speed. Each entity's **Rigidbody** has **Mass**, **Friction** and **Gravity Scale**.

A collider's **Step Height** is how high a step it can climb, and its **Max Slope** how steep a floor it can walk on.
[Collider](../Collider.md) and [Rigidbody](../Rigidbody.md) go into detail.

## Editing in 3D

Press **V** to open the **Runtime Editor**. It lets you fly through the level in 3D and adjust things while seeing the
result, which is often easier than switching between the map and the game. See [page 2](02-Tour-Of-The-Editor.md).

## Writing your own scripts

Eventually you'll want something dispatches can't do. Scripts use the **Lua** language, and aren't as hard as you'd
expect. Start with [Getting Started with Scripting](../GettingStarted.md).

The kit's scripts are a good place to learn, since each one has one job and explains itself at the top:

| To learn about | Read |
|---|---|
| A small, whole script | `Scripts/Level Logic/Teleporter.lua` |
| Rays and damage | `Scripts/Weapons/Weapon.lua` |
| Moving something yourself | `Scripts/Combat/Projectile.lua` |
| Hurting everything in an area | `Scripts/Combat/Explosion.lua` |
| Bouncing something yourself | `Scripts/Combat/Grenade.lua` |
| Particles and marks | `Scripts/Combat/Fx.lua` |
| Reacting to a Monster AI decision | `Scripts/Enemies/FireProjectile.lua` |

A script's `public` values appear in the inspector, which is how the kit's scripts get their settings.
A script can also talk to dispatches: it can read and set variables, and **Lua Signal** events catch what it sends with
`Game.Signal`.

Things you can only do from a script include saving the game. See [Saving and Loading](../SavingAndLoading.md).

The reference pages are [Entity](../Entity.md), [Game](../Game.md), [Input](../Input.md),
[Callback Functions](../CallbackFunctions.md) and [Invoke](../Invoke.md).

---

Next: [When something goes wrong](17-Troubleshooting.md)
