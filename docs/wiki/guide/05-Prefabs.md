# 5. Prefabs: Drag and Drop Your Game

## What a prefab is

A **prefab** is a ready-made thing saved as a file. The kit has one for every enemy, weapon, pickup
and decoration. To put one in your level, you just drag it onto the map.

```
  Asset Browser                         The map

  Prefabs/Enemies/Zombie.prefab  ---->  a zombie, ready to fight
```

There's no setup. A zombie you drop in already sees, chases, shoots, bleeds and dies. A medikit
already heals.

## Try it

1. In the Asset Browser, open **Prefabs > Enemies**.
2. Drag **Zombie** onto your room.
3. Open **Prefabs > Weapons > Pickups** and drag in **Shotgun**.
4. Open **Prefabs > Pickups > Ammo** and drag in **Shells**.
5. Press **P**, pick up the shotgun and fight.

To move something, press **E** for Entity Mode and drag it. To delete it, click it and press
**Delete**. To turn an enemy so it faces another way, right-click it and change its rotation.

## What's in the box

Open the **Prefab Gallery** level and press **P** to walk around all of these.

### Enemies: `Prefabs/Enemies`

From weakest to strongest, roughly:

| Enemy | Attack |
|---|---|
| **Zombie** | Rifle, from a distance |
| **Shotgun Zombie** | Shotgun, hurts up close |
| **Minigun Zombie** | Fast bullets |
| **Serpentipede** | Claws and fireballs |
| **Flesh Worm** | Bites, fast and tough |
| **Stealth Worm** | Like the Flesh Worm, but hard to see |
| **Deadflare** | Flies, and charges at you |
| **Trilobite** | Flies, and spits fireballs |
| **Hangman** | Fire that follows you |
| **Combat Slug** | Three explosive shots at once |
| **Technospider** | Plasma |
| **Matribite** | Flies, and spits out Deadflares |
| **Pain Bringer** | Tough, with claws and fireballs |
| **Pain Lord** | The Pain Bringer's bigger cousin |
| **Assault Tripod** | Boss. Fires rockets. |
| **Large Technospider** | Boss. Fires a stream of bullets. |

Enemies wake when they see or hear you, and they can open doors.

### Weapons: `Prefabs/Weapons/Pickups`

**Pistol**, **Chainsaw**, **Shotgun**, **Super Shotgun**, **Minigun**, **Rocket Launcher**,
**Plasma Rifle** and **SKAG 1337**. The player always has fists and a pistol. Picking up any other
weapon switches to it, and the number keys change between them.

### Pickups: `Prefabs/Pickups`

| Folder | Prefabs |
|---|---|
| **Ammo** | Clip, Ammo Box, Shells, Shell Box, Rocket, Rocket Box, Cell, Cell Pack, Backpack |
| **Health** | Stimpack, Medikit, Health Bonus, Soul Sphere, Mega Sphere |
| **Armor** | Green Armor, Blue Armor, Armor Bonus |
| **Keys** | Red, Blue and Yellow Key, Red, Blue and Yellow Skull |
| **Powerups** | Berserk, Invulnerability, Radiation Suit |

### Level pieces: `Prefabs/Level Logic`

These are what make a level into a game:

| Prefab | What it does |
|---|---|
| **Exit Switch** | Press it to finish the level. **Every level needs a way out.** |
| **Exit Trigger** | Walk through it to finish the level |
| **End Game Switch** | Finishes the whole game and shows the Victory screen. Use it in your last level. |
| **Switch** | Press it to fire the event tag "Switch 1", see [page 9](09-Events-And-Dispatches.md) |
| **Walk Over Trigger** | Walk through it to fire the event tag "Trigger 1" |
| **Secret Spot** | Counts as a found secret when you step on it |
| **Teleport Trigger** and **Teleport Destination** | Step on the trigger to go to the destination. See below. |

### Also there

**Hazards** has the Explosive Barrel. **Decorations** has lamps, torches, pillars, trees, gore and
more. Some are solid and some you can walk through.

## Setting up a teleporter

Some prefabs have a field for you to fill in. The teleporter is the first one you'll meet:

1. Drag **Teleport Trigger** to where the player should step.
2. Drag **Teleport Destination** to where they should arrive.
3. Right-click the trigger. In the inspector, find its **Teleporter** script.
4. Drag the destination from the map or the **Hierarchy** panel into the **Destination** field.

## Changing one, or changing all of them

The prefab file and the copies in your levels stay linked. This is the most useful thing to
understand about prefabs.

**To change just this one:** right-click it on the map and edit it. The inspector tints whatever you
changed, so you can see how it differs from the prefab. Everything else keeps following the prefab.

**To change all of them, everywhere:** double-click the prefab file in the Asset Browser, edit it
and save with **Ctrl + S**. Every copy in every level updates, except for values you changed by hand
on a copy.

Try it: double-click **Prefabs/Enemies/Zombie**, find **Monster AI** and raise **Health**. Now every
zombie in the game is tougher.

**To make a new kind of thing:** right-click a prefab in the Asset Browser, choose **Duplicate**, and
change the copy. A duplicated Zombie with more health and a different sprite is a new enemy. That's
all it takes.

---

Next: [Add a door](06-Doors.md)
