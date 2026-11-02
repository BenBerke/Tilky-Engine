# 13. Change the Enemies

Every enemy in the kit is built the same way. Once you can read one, you can read them all. Double-click
**Prefabs/Enemies/Zombie** to follow along.

## What an enemy is made of

| Component | Its job |
|---|---|
| **Sprite** | The picture, with 8 sides so it looks right from every angle |
| **Flipbook** | Plays the animation frames |
| **Collider** and **Rigidbody** | Make it solid and let it stand on floors |
| **Pathfinder** | Walks around, the way Doom's monsters do |
| **Monster AI** | The brain: senses, attacks, health and pain |
| **Corpse** script | Leaves the body behind after it dies |

Flying enemies have **Air Entity** on, in **Pathfinder**, and **Gravity Scale** 0, in **Rigidbody**.

## Quick changes

| I want it to... | Change this |
|---|---|
| Take more hits | **Monster AI > Health** |
| Move faster | **Pathfinder > Speed** |
| Flinch less often | **Monster AI > Pain Chance %** |
| Shoot more often from far away | Lower **Monster AI > Distance Scale**, or raise **Distance Offset** |
| See further | **Monster AI > Sight Range** |
| See all around it, not just ahead | **Monster AI > Field Of View** |
| Ignore noise unless you're in sight | **Monster AI > Ambush** |
| Be awake from the start | **Monster AI > Start Awake** |
| Never fight other monsters | Turn off **Monster AI > Infights** |
| Not count on the Intermission screen | Delete its **Count Me** dispatch and the `Kills` row of its **Die** |
| Drop something when it dies | Add a **Spawn Prefab** dispatch named **Die** |
| Be see-through | **Sprite**'s color alpha. The Stealth Worm uses 0.3. |

Here's every enemy's starting point:

| Enemy | Health | Speed | Pain chance | Attack |
|---|---|---|---:|---|
| Zombie | 20 | 17.5 | 78 | Bullet, 9. Drops a Clip. |
| Shotgun Zombie | 30 | 17.5 | 66 | Bullet, 18. Drops a Shotgun. |
| Minigun Zombie | 70 | 17.5 | 66 | Bullet, 8. Drops a Minigun. |
| Serpentipede | 60 | 23 | 78 | Melee 12, Fireball |
| Flesh Worm | 150 | 44 | 70 | Melee 22 |
| Stealth Worm | 150 | 44 | 70 | Melee 22 |
| Deadflare | 100 | 30 | 100 | Flies, melee 15 |
| Trilobite | 400 | 23 | 50 | Flies, melee 30, Green Ball |
| Hangman | 300 | 44 | 39 | Melee 30, homing Tracer |
| Combat Slug | 600 | 17.5 | 31 | 3 Slug Balls in a fan |
| Technospider | 500 | 35 | 50 | Spider Plasma |
| Matribite | 400 | 23 | 50 | Flies, spits Deadflares |
| Pain Bringer | 500 | 23 | 20 | Melee 40, Red Ball |
| Pain Lord | 1000 | 23 | 20 | Melee 40, Red Ball |
| Assault Tripod | 4000 | 47 | 8 | Rockets |
| Large Technospider | 3000 | 35 | 16 | Bullet, 15 |

## The events and what they call

The Monster AI component makes the decisions, and then reports them as events. The events only add the sound,
the damage and the score.

| Event | When it fires | Dispatch it calls |
|---|---|---|
| **On Start** | The level begins | **Count Me**: adds 1 to `TotalKills` |
| **Target Spotted** | It has just noticed you | **Sight Sound** |
| **Active** | Now and then while it hunts, by **Active Chance %** | **Active Sound** |
| **Pain** | It's hurt, and passed its Pain Chance | **Flinch**: pain picture and sound |
| **Melee Ready** | It's decided to bite or claw | **Bite**, see below |
| **Missile Ready** | It's decided to shoot | **Shoot** or **Shot Sound**, see below |
| **Died** | Health ran out | **Die**: adds 1 to `Kills`, plays a sound, calls `BecomeCorpse` |

## Attacks

Why are there two rows for one attack? Because monsters can fight each other, and the player takes damage
in a different way from a monster. So an attack event has two versions, using **Filter Tags**.

### A bite

```
   Melee Ready, Filter Tags = Player
     --> "Bite":      IncomingDamage = 22, with a short Delay
                      then Next Dispatch: Instigator's "Take Damage"
                      plus the bite sound

   Melee Ready, Filter Tags = Enemy
     --> "Bite Sound"
     --> Instigator's "Hit By Monster"      (Monster AI: Hurt, 12)
```

**To make it bite harder,** change the number in **Bite**, and the one in **Hit By Monster** if you want it
to hit other monsters harder too.

### A gunshot

```
   Missile Ready, any target   --> "Shot Sound"
   Missile Ready, Player, with Chance   --> "Shoot": IncomingDamage, then Take Damage
   Missile Ready, Enemy        --> Instigator's "Hit By Monster"
```

**Chance** is how the zombie misses. The sound plays every time, but the damage only happens some of the time.

### A projectile

Fireballs and rockets aren't dispatches at all. The enemy has a `FireProjectile` script, which reacts to
Missile Ready by itself:

| Field | What it does |
|---|---|
| **Projectile Prefab** | What it fires, from `Prefabs/Projectiles` |
| **Shots** | How many at once |
| **Fan Degrees** | How far apart they spread |
| **Delay** | The wait, to match the throwing animation |
| **Muzzle Height** | How high up the shot starts |
| **Fire Sound** | The sound |

The damage is on the projectile, in its **Projectile** script:

| Projectile | Speed | Damage |
|---|---:|---:|
| Fireball | 87.5 | 3 to 24 |
| Green Ball | 87.5 | 5 to 40 |
| Tracer | 87.5 | 10 to 80, homing |
| Red Ball | 131 | 8 to 64 |
| Slug Ball | 175 | 8 to 64 |
| Rocket | 175 | 20 to 160 |
| Spider Plasma | 218 | 5 to 40 |
| Plasma | 218 | 5 to 40 |
| SKAG Ball | 218 | 100 to 800 |

Its **Impact Prefab** is what appears when it hits. For explosions, that prefab has an **Explosion** script with
a **Radius** and **Damage**. A rocket's reach is 32 and its damage 128.

## Monster AI's settings

Every setting has a tooltip when you hover over it. Here's what the sections are for:

- **Senses:** how it notices you. **Target Tags** is who it hates. **Hearing** lets a **Noise Alert** wake it.
- **Attacks:** when it attacks. It's more likely up close. **Missile Min Range** and **Missile Max Range** set
  the distances it shoots from. **Distance Offset** and **Distance Scale** set how much distance matters.
  **Attack Pause** is how long it stands still for each attack, so match it to the animation.
- **Health And Flipbooks:** **Walking**, **Attacking** and **Dying** say which animation plays.
- **Pain And Infighting:** **Pain Pause** is how long it flinches. **Infights** decides if it turns on a monster
  that hit it.
- **Other:** **Active Chance %** decides how often it mutters.

[Monster AI](../MonsterAI.md) and [Pathfinder](../Pathfinder.md) explain every one in full.

## Recipe: a new enemy

Let's make a **Fire Zombie**, tougher than a Zombie, that throws fireballs instead of shooting.

1. Right-click **Prefabs/Enemies/Serpentipede** and choose **Duplicate**. It already has a projectile attack.
   Name the copy **Fire Zombie**.
2. Double-click it, and in **Monster AI**, set **Walking**, **Attacking** and **Dying** to the Zombie's flipbooks.
3. Set **Health** to 40 and **Pathfinder > Speed** to 17.5.
4. Turn off **Has Melee**, so that it only throws.
5. Change the sounds in **Sight Sound**, **Flinch**, and **Die** to whatever fits.
6. Save with **Ctrl + S**, drag it into a level and press **P**.

To use your own art, make new flipbooks in `Animations`. Open an existing one to see how the 8 sides are laid out.

## Recipe: an enemy that spawns helpers

The Matribite shows how:

1. Add a **Missile Ready** event, with **Chance** 50, calling a dispatch named **Spit**.
2. Make **Spit** a **Spawn Prefab** that creates a Deadflare.
3. Give the dispatch a **Cooldown** of 3 seconds, so it can't fill the room.

## Recipe: a boss door

When an enemy dies, open a door. This is how Level 4 works:

1. On the door's sector, add a **Door** dispatch that listens on the event tag `Boss Dead`, with **Door Type** set to
   stay open. See [page 9](09-Events-And-Dispatches.md).
2. Right-click the boss on the map. On its **Died** event, set **Tag** to `Boss Dead`.

Change this on the one boss in the level, not on the prefab. Otherwise every enemy of that kind would open the door.

---

Next: [Change the weapons and pickups](14-Change-Weapons-And-Pickups.md)
