# 11. How the Kit Works

Until now you've used the kit by dragging things into levels. The next few pages open it up, so you can
change how the game itself plays.

Nothing in the kit is hidden or built into the engine. Every rule of the game is something you can open
and edit. This page gives you the map, and the pages after it go through each part.

## The one big idea

The kit's parts don't talk to each other much. Nearly everything goes through **variables**, which are
named numbers such as `Health` and `Bullets` that every level can read.

```
   A medikit                          The HUD
   "add 25 to Health"                 "show the value of Health"
          |                                  ^
          v                                  |
          +-------->   Health = 75   --------+
                            ^
                            |
   A monster's bite: "take away some Health"
```

The medikit doesn't know the HUD exists. It changes a number, and the HUD shows whatever the number says.
This is why you can replace one part without breaking the others.

## Where everything lives

Everything is in your project's `Assets` folder.

| Folder | What's in it |
|---|---|
| `Levels` | The menu, the Prefab Gallery, Blank Base Level and the four example levels |
| `Prefabs/Player` | The player. Most of the game's rules are in here. |
| `Prefabs/Enemies` | One prefab for each enemy |
| `Prefabs/Projectiles` | Fireballs, rockets, plasma and grenades |
| `Prefabs/Effects` | Explosions, plus the particle effects: blood, gibs, bullet puffs, casings, debris and splashes |
| `Prefabs/Weapons/In Hand` | The weapons you hold, which do the shooting |
| `Prefabs/Weapons/Pickups` | The weapons lying on the floor, which give you the one in your hands |
| `Prefabs/Pickups` | Ammo, health, armor, keys and powerups |
| `Prefabs/Level Logic` | Switches, triggers, secrets and teleporters |
| `UI Layouts` | The HUD and all the menu screens |
| `Scripts` | The kit's Lua scripts, for the few jobs dispatches can't do |
| `Animations` | Flipbooks: the animation frames for enemies and weapons |
| `Sprites`, `Textures`, `Sounds`, `Music`, `UI`, `Fonts` | Art and audio |
| `Freedoom` | The licence for the art, which must stay with your game if you use that art |

## The variables

You'll find all of these in **Project Settings > Global Event Variables**. "Global" means they keep their
value from one level to the next.

| Variables | What they mean |
|---|---|
| `Health`, `Armor` | Just what they say |
| `ArmorType` | 0 is none, 1 is green, and 2 is blue. Blue armor absorbs more of each hit. |
| `Bullets`, `Shells`, `Rockets`, `Cells`, `Grenades` | Ammo you have |
| `MaxBullets`, `MaxShells`, `MaxRockets`, `MaxCells`, `MaxGrenades` | The most ammo you can carry |
| `HasFist`, `HasPistol`, `HasShotgun`... | 1 if you own that weapon, 0 if not |
| `HasBackpack` | 1 if you found a backpack, which doubles the ammo you can carry |
| `GoreLevel` | The Options menu's Gore setting: 0 is off, 1 is normal, and 2 is extra |
| `HasRedKey`, `HasBlueKey`, `HasYellowKey` | 1 if you hold that key. The HUD uses these for its icons. |
| `Berserk`, `Invulnerable`, `RadSuit` | 1 while you have that powerup |
| `Kills`, `Items`, `Secrets` | What you've done on this level |
| `TotalKills`, `TotalItems`, `TotalSecrets` | How many this level has. Each one counts itself in when the level starts. |
| `Time` | Seconds spent on this level |
| `CurrentAmmo` | The ammo of the weapon in your hands, for the HUD |
| `IncomingDamage` | A hit waiting to be applied. See "How damage works" below. |
| `MenuState` | 0 is no menu, 1 is the pause menu, and 2 is another screen that Esc must not interrupt |
| `Tally...` | Used by the Intermission screen while it counts up |

## The tags

Tags are labels on entities. You'll find them in **Project Settings > Tags**.

| Tag | Who has it | What it's for |
|---|---|---|
| `Player` | The player | Pickups and triggers only react to this tag |
| `Enemy` | Every enemy | An enemy's attacks use it to tell when they've hit another monster rather than the player |
| `Bleeds` | Enemies with blood | A bullet makes blood instead of a puff, and big enough hits burst it into pieces |
| `Kickable` | The Explosive Barrel | A kick shoves it instead of hurting it. It needs a Rigidbody. |
| `Liquid`, with `Water`, `Nukage` or `Lava` | Sectors | Makes a sector splash, and keeps blood and burn marks off it |
| `Pickup` | Every pickup | Marks it as something you can collect |
| `RedKey`, `BlueKey`, `YellowKey` | The player, after finding a key | Locked doors check for these |
| `HUD`, `Pause Menu`, `Options Menu`, `Death Screen`, `Intermission`, `Victory` | Everything on that screen | Lets one call reach a whole screen at once |

**Event Tags** are different, and they're listed separately in **Project Settings > Event Tags**. They connect
a thing that happens to a thing that reacts, as you saw on [page 9](09-Events-And-Dispatches.md). The
kit uses `Switch 1` and `Trigger 1` for its general-purpose switch and trigger.

## How a pickup works

Pickups are a good first example, because they use every idea in small amounts. This is the Medikit:

```
   Event: Collide                    Only the Player can set it off.
     |                               The Max Triggers setting of 1 means once.
     +--> calls its own "Pick Up"
     +--> calls "Pickup Flash" on everything tagged HUD

   Dispatches called "Pick Up":
     Change Variable   Health + 25
       then, if Health > 100:  calls "Cap Health 100" on whoever touched it
     Play Sound        the pickup sound
     Show Message      "Picked up a medikit."
     Destroy           after a tiny delay
```

Its **Collide** event also has a **Condition**, `Health < 100`, so you can't waste a medikit at full health.

Several dispatches can share one name, and calling that name runs all of them. Almost all of the kit is built
this way.

## How damage works

Damage has one rule: **everything goes through the `PlayerHealth` script on the player.** That way armor,
invulnerability, the pain sound and the HUD's red flash all work no matter what hurt you.

An event can't hand a number to a dispatch, so damage from dispatches takes two steps:

```
   1. Change Variable:  IncomingDamage = 15       "here's how much"
   2. Call on the Player:  "Take Damage"          "apply it now"
          |
          v
      PlayerHealth script
          takes off armor first, according to ArmorType
          takes off Health
          plays the pain sound
          sends the Lua Signal "PlayerHurt"  --> the HUD flashes red, the face reacts
```

Monsters' bites and toxic floors both work this way. Scripts can skip the variable, as explained at the
end of [page 12](12-Inside-The-Player.md).

Damage to **monsters** works in a different way. It goes straight to the enemy's **Monster AI** component, which
has a **Health** field and handles its own pain and death.

## How the game flows from level to level

```
   Level starts
     |
     +-- Player's On Start  --> "New Level": clears Kills, Time, keys and powerups
     +-- if Health <= 0     --> "Pistol Start": full health and a pistol
     |
   You play
     |
     +-- You die      --> "Die", and the Death Screen shows
     |                       Try Again keeps Health at 0, so the restart gives a Pistol Start
     |
     +-- Exit Switch  --> the Intermission shows, then Load Level goes to the next one
```

The **New Game** and **Select Level** buttons set `Health` to 0 before they load a level, and so does **Restart
Level** on the pause menu. That's how they get you a fresh pistol and 50 bullets: to the player, it looks just like
coming back from the dead.

Pressing **P** in the editor skips those buttons. The game then uses the starting values in **Project Settings > Global
Event Variables**, where `Health` is 100, so Pistol Start doesn't run.

## Before you change anything

Three habits will save you trouble:

1. **Duplicate first.** Right-click a prefab and choose **Duplicate**, then change the copy. If you break it,
   the original is still there.
2. **Test the smallest change you can.** Change one number, press **P**, and see what it did.
3. **Change a prefab, not a copy.** If you want every Zombie to change, double-click the prefab file and press
   **Ctrl + S** when done. See [page 5](05-Prefabs.md).

---

Next: [Inside the player](12-Inside-The-Player.md)
