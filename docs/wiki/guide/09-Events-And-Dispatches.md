# 9. Events and Dispatches: Making Things Happen

This is how you make your level do things without writing code.

**An event is something that happens. A dispatch is something to do about it.**

```
   EVENT                                DISPATCH
   The player touched me        --->    Add 10 to Health
   The player pressed Use on me --->    Open this door
   The boss died                --->    Open the exit gate
```

Entities, sectors and walls can all have events and dispatches. They're in the inspector.

## See a real one

Double-click **Prefabs/Pickups/Health/Medikit** and look at its events and dispatches:

- **Event: Collide**, so it reacts when something touches it.
  - It has a **filter** so that only things tagged `Player` count.
  - It has a **condition**: `Health` is less than 100. That's why you don't waste a medikit at full
    health.
  - It has **Max Triggers** of 1, so it only happens once.
  - It calls a dispatch named **Pick Up**.
- **Dispatches named Pick Up:** add 25 to `Health`, play a sound, show a message, and destroy the
  medikit.

That's the whole medikit, and there's no code in it. Now you could build one yourself.

## Connecting things with an event tag

An event on one thing often needs to set off a dispatch on something else, like a switch that opens a
door across the room. An **event tag** is the wire between them.

Think of it as a radio channel: the switch sends on channel "Switch 1", and anything listening to
"Switch 1" reacts. The two don't need to know about each other.

## Build it: a switch that opens a door

1. **Place the switch.** Drag **Prefabs/Level Logic/Switch** next to a wall. In its inspector, click
   **Snap to Wall**, then right-click the wall. It lies flat on the wall now, and it already sends on the
   event tag `Switch 1` when you press it.
2. **Draw a door.** Draw a thin sector between two rooms, as on [page 6](06-Doors.md), but don't click
   Make Door. Instead:
3. **Shut it.** Right-click the sector and set its **Ceiling Height** equal to its **Floor Height**.
4. **Add the dispatch.** In the sector's **Dispatches**, add a **Door** dispatch. Set **Door Type** to
   **Open and Stay**.
5. **Tune it in.** Set the dispatch's event tag to `Switch 1`.

Press **P** and press the switch. The door opens.

For a second, separate switch, make a new event tag in **Project Settings > Event Tags**. Each pair of
switch and door gets its own channel.

The walk-over trigger works the same way, on the tag `Trigger 1`. **Level 3's** trap uses this: taking
the key walks you over a trigger, and three doors listening on the tag `Trap` open at once.

## Calling a dispatch directly

There's a second way to connect things. An event can call a dispatch **by name** on a chosen target,
such as **Self** (this thing), **Instigator** (whoever set it off) or everything with a certain tag. The
medikit uses that to call its own **Pick Up**.

Use whichever reads better. Event tags are good for one thing setting off another across the level.
Names are good inside a single prefab.

## Variables: the game's memory

A **variable** is a named number the game remembers. All the kit's values are global variables, so they
last from level to level:

| Variable | What it holds |
|---|---|
| `Health`, `Armor` | The player's health and armor |
| `Bullets`, `Shells`, `Rockets`, `Cells` | Ammo |
| `HasRedKey`, `HasBlueKey`, `HasYellowKey` | 1 when the player has that key |
| `Kills`, `Items`, `Secrets` | What the screen between levels counts |

Any event can check one in its condition, and a **Change Variable** dispatch can change one.

UI Text can display a variable too: that's how the HUD works.

## Some useful events

| Event | When it happens |
|---|---|
| **On Start** | When the level starts or the thing appears |
| **Collide** | Something touches this entity |
| **Receive Ray** | The player pressed Use on this, or shot it |
| **Entity Enter** | Something walks into this sector |
| **Died** | This enemy died |
| **Timer** | Time has passed |
| **Condition Met** | A variable reached a value |

## Some useful dispatches

| Dispatch | What it does |
|---|---|
| **Change Variable** | Set or add to a variable |
| **Door**, **Lift**, **Raise Stairs** | Move sectors |
| **Play Sound**, **Show Message** | Tell the player something |
| **Spawn Prefab** | Create something: a monster, an explosion |
| **Destroy**, **Enable**, **Disable** | Remove, hide or show something |
| **Teleport** | Move something somewhere else |
| **Load Level** | Go to a level |

Hover over any field in the inspector to see what it does.

## Build it: a monster that appears

1. Place an enemy in your level, then right-click it and untick **Enabled** at the top of the inspector.
   It won't exist in the game until something enables it.
2. Give it an **Enable** dispatch on the event tag `Trigger 1`.
3. Drag a **Walk Over Trigger** into a doorway.

Press **P**. Walk through the doorway, and the monster appears.

---

Next: [Finish your game](10-Finish-Your-Game.md)
