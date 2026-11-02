# 12. Inside the Player

The player prefab has 65 dispatches, which can look scary. Don't worry: most of them are one-line jobs like "set
Bullets to 50". They come in a few groups, and this page explains each group.

To follow along, double-click **Prefabs/Player/Player** in the Asset Browser and look at its **Events** and
**Dispatches** in the inspector.

## Quick changes

| I want to... | Change this |
|---|---|
| Walk faster | **Player Controller > Speed** (100) |
| Run faster | **Player Controller > Running Speed** (132) |
| Stop faster or slower, like ice | **Player Controller > Deceleration** (280). Lower is more slippery. |
| Start moving faster | **Player Controller > Acceleration** (520) |
| Steer more in the air | **Player Controller > Air Control** (0.25) |
| Jump higher | **Player Controller > Jump Strength** (40) |
| Remove jumping | Set **Jump Strength** to 0 |
| Change how far you can press a switch | **Player Controller > Use Reach** (20) |
| Change the Use key | **Player Controller > Use Key** |
| Be taller or shorter | **Player Controller > Eye Height** (10.25) |
| Turn off the walking bob | **ViewFeel** script **> Bob Amount** set to 0 |
| Make armor stronger | **PlayerHealth** script **> Green Armor Absorbs** (0.333) and **Blue Armor Absorbs** (0.5) |
| Start with more ammo or health | The **Pistol Start** dispatches, below |

The run key only works while you're also holding forward. That's how the engine works, and there's no
setting for it.

## The events: what starts the dispatches

The player has only a few events. All 65 dispatches are started by these, or called from other things.

| Event | Calls | When |
|---|---|---|
| **On Start** | New Level | Every time a level starts |
| **On Start**, if `Health <= 0` | Pistol Start | New game, or after dying |
| **Timer**, every second | Tick | All the time |
| **Condition Met**, `Bullets > MaxBullets` | Cap Bullets | When you have too many. There's one for each ammo type. |
| **Condition Met**, `Health <= 0` | Die, and "Show" on the Death Screen | When you die |

A **Condition Met** event runs once when its condition becomes true. It can run again after the
condition has gone back to false.

## The dispatches, group by group

### New Level (14 dispatches)

Each one is a **Change Variable** that sets a variable to 0. These variables must start clean on each level:

- **The score:** `Kills`, `TotalKills`, `Items`, `TotalItems`, `Secrets`, `TotalSecrets`, `Time`
- **Menus:** `MenuState`
- **Keys:** `HasRedKey`, `HasBlueKey`, `HasYellowKey`
- **Powerups:** `Berserk`, `Invulnerable`, `RadSuit`

**To let keys carry over to the next level,** deleting the three key rows isn't quite enough. The icons would stay
lit, but the player in each level is a new entity, so the key **tag** that doors check for would be gone. Also add
an **On Start** event with the Condition `HasRedKey` equals 1, calling **Get Red Key**, and the same for the other two.

### Pistol Start (21 dispatches)

These set up a new life. This is where you decide what the player starts with:

| Variable | Set to |
|---|---|
| `Health` | 100 |
| `Armor`, `ArmorType` | 0 |
| `Bullets` | 50 |
| `Shells`, `Rockets`, `Cells` | 0 |
| `MaxBullets` | 200 |
| `MaxShells`, `MaxRockets` | 50 |
| `MaxCells` | 300 |
| `HasBackpack` | 0 |
| `HasFist`, `HasPistol` | 1 |
| All the other `Has...` weapons | 0 |

**Try it:** find the Pistol Start row for `HasShotgun`, change its value to 1, and find the one for
`Shells` and change it to 20. Now every new game starts with a loaded shotgun.

**To see it, start from the Main Menu level and click New Game.** Pressing **P** in another level skips Pistol Start and
uses the values in **Project Settings > Global Event Variables** instead. Change `HasShotgun` and `Shells` there too, and
pressing **P** gives you the shotgun as well.

### Tick (1 dispatch)

Adds 1 to `Time`. The Intermission screen shows this as the time you took.

### Ammo caps (8 dispatches)

A pickup just adds ammo, without checking if you have room. These put it back down if you go over.

There are two for each ammo type, because of the backpack:

```
   Cap Bullets                     Bullets = 200
     then, if HasBackpack is 1:    go on to "Cap Bullets Backpack"

   Cap Bullets Backpack            Bullets = 400
```

The second one is a **Next Dispatch** with **Go On If** set to a variable check. You'll find more about those
on [page 16](16-More-Engine-Features.md).

**To change how much you can carry,** say 300 bullets, you need to change it in four places:

1. Pistol Start: `MaxBullets`
2. **Cap Bullets**: the value
3. **Cap Bullets Backpack**: the value, for the doubled amount
4. The **Backpack** prefab in `Prefabs/Pickups/Ammo`, which sets the new `Max` values

### Health and armor caps (4 dispatches)

**Cap Health 100**, **Cap Health 200**, **Cap Armor 200** and **Green Armor Type**. The player never calls
these by itself. Pickups call them on the **Instigator**, meaning whoever touched the pickup.

Why not one fixed cap? Because Doom has two. A medikit can't take you past 100, but a health bonus can
take you to 200. So each pickup asks for the cap it wants.

### Keys (6 dispatches)

There are two dispatches for each of **Get Red Key**, **Get Blue Key** and **Get Yellow Key**:

- **Add / Remove Tag** gives the player the `RedKey` tag. Locked doors look for this.
- **Change Variable** sets `HasRedKey` to 1. The HUD's key icon looks for this.

Skull keys call the same dispatches, so a door doesn't know which of the two you found.

**To add a green key:**

1. Add a tag `GreenKey` in **Project Settings > Tags** and a variable `HasGreenKey` in **Global Event Variables**.
2. On the player, duplicate the two **Get Red Key** dispatches, naming them **Get Green Key**, and pick the new tag and variable.
3. Add a row to **New Level** that sets `HasGreenKey` to 0.
4. Duplicate `Prefabs/Pickups/Keys/Red Key`, give it a new sprite, and have its Collide event call **Get Green Key**.
5. On a door, set **Required Tag** to `GreenKey`.
6. For the HUD, see [page 15](15-Change-The-UI.md).

### Powerups (4 dispatches)

| Dispatch | What it does |
|---|---|
| **Get Invulnerability** | Sets `Invulnerable` to 1, and calls End Invulnerability after 30 seconds |
| **End Invulnerability** | Sets it back to 0 |
| **Get Radiation Suit** | Sets `RadSuit` to 1, and calls End Radiation Suit after 60 seconds |
| **End Radiation Suit** | Sets it back to 0 |

To make a powerup last longer, change the wait in its **Next Dispatch** row.

`PlayerHealth` reads `Invulnerable`, and toxic floors read `RadSuit`. Those variables are the whole
effect. The powerup needs nothing more.

### Take Damage (1 dispatch)

A **Call Function** that runs `TakeIncomingDamage` on the `PlayerHealth` script. [Page 11](11-How-The-Kit-Works.md)
explains how it's used.

### Die (6 dispatches)

| Dispatch | Why |
|---|---|
| **Play Sound** `Player/death` | The scream |
| **Change Variable** `MenuState` = 2 | So Esc can't open the pause menu over the death screen |
| **Set Field** Speed = 0 | Stop walking |
| **Set Field** Running Speed = 0 | Stop running |
| **Set Field** Jump Strength = 0 | Stop jumping |
| **Set Field** Eye Height = 2.5, over time | The camera sinks to the floor |

**Try it:** set the Eye Height row's value to 6 and your dead body stays higher up. Raise its speed and you
drop faster.

## Recipes

### A floor that hurts

This is how the example levels make toxic pools, and you can build it with no script:

1. Click the sector. In its **Events**, add **Entity Stays In Sector**.
2. Set **Filter Tags** to `Player` and **Interval** to 1, for once per second.
3. Add a **Condition**: `RadSuit` equals 0. Now the suit protects you.
4. Add a dispatch named **Hurt**: **Change Variable**, `IncomingDamage`, Set, 10.
5. On it, click **+ Add Next Dispatch**, and choose the **Instigator** and its **Take Damage**.
6. Have the event call **Hurt**.

Leave out the condition and the floor hurts even with a suit.

### A secret that gives you bullets

1. Duplicate `Prefabs/Level Logic/Secret Spot`.
2. Double-click the copy and add a dispatch named **Found**: **Change Variable**, `Bullets`, Add, 20.
3. Save it and place it in a level.

The event already calls **Found**, so your new row runs with the others. You don't need to check for too many bullets,
because the player's ammo cap takes care of that.

Doing the same with `Health` takes more care, because nothing caps it by itself. Use a **Next Dispatch** that calls
**Cap Health 100** on the Instigator, with **Go On If** set to `Health > 100`, as the Medikit does.

### Hurting the player from a script

There are two ways, and they do the same thing:

```lua
-- With a script: pass the number directly.
player:GetScript("PlayerHealth"):TakeDamage(20)

-- Or: damage anything, player or monster, and credit the attacker for infighting.
Global.ApplyDamage(target, 20, entity)
```

`Global.ApplyDamage` comes from the `DamageRules` script. See [page 16](16-More-Engine-Features.md) for where to
learn scripting.

## Why are they all on the player?

Because the player is in every level, so anything on it is available everywhere. A pickup in any level can
call **Get Red Key** on whoever touched it.

If you'd rather have fewer rows, a script could set all 21 Pistol Start values in a few lines. Dispatches
were used here so you can change them without reading code.

---

Next: [Change the enemies](13-Change-Enemies.md)
