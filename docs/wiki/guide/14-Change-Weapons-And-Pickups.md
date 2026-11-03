# 14. Change the Weapons and Pickups

## How a weapon works

Each weapon is two prefabs, which never touch each other. They're connected by a variable.

```
   Prefabs/Weapons/Pickups/Shotgun          Prefabs/Weapons/In Hand/Shotgun
   (lying on the floor)                     (always there, inside the Player)

   Collide: HasShotgun = 1  ---------->     Weapon script: Owned Variable = HasShotgun
            Shells + 8                      "Now that this is 1, I can be used."
```

The `WeaponHolder` script on the player holds a list of all the in-hand weapons. It handles switching, and it picks up
the new weapon when an **Owned Variable** becomes non-zero.

In-hand prefabs hold no picture. The weapon that's up plays its flipbooks on the HUD's **Weapon View**, so every weapon
appears in the same place. To move them all, move Weapon View in the UI Editor ([page 15](15-Change-The-UI.md)).

## Changing a weapon

Double-click a prefab in `Prefabs/Weapons/In Hand` and look at its **Weapon** script.

| Field | What it does |
|---|---|
| **Attack Type** | `Bullets`, `Melee` or `Projectile` |
| **Slot** | Which number key selects it. Press the key again to cycle through weapons in that slot. |
| **Owned Variable** | The variable that says you have it |
| **Ammo Variable** | Which ammo it uses. Blank means none. |
| **Ammo Per Shot** | How much each shot uses |
| **Use As Fallback** | If it's on, the game can switch to this weapon when you run out of ammo. Off for dangerous ones like the Rocket Launcher. |
| **Shot Delay** | Seconds before the shot leaves, to match the animation |
| **Fire Time** | Seconds between shots. Lower is faster. |
| **Pellets** | Bullets per shot. The Shotgun has 7. |
| **Spread** | How far they scatter, in degrees |
| **Damage Min**, **Damage Max** | Damage of each pellet, picked at random in between |
| **Range** | How far it reaches |
| **Berserk Multiplier** | Extra damage with the Berserk powerup. The Fist has 10. |
| **Puff Prefab**, **Blood Prefab** | What appears where the bullet hits |
| **Projectile Prefab** | What it fires, for the `Projectile` type |
| **Ready Flipbook**, **Fire Flipbook** | Its animations |
| **Trigger Sound**, **Fire Sound**, **Hit Sound** | Its sounds |
| **Raise Time** | How long it takes to swap to this weapon |
| **Bob Amount** | How much it sways as you walk |

Here are the starting numbers:

| Weapon | Slot | Ammo | Per shot | Pellets | Spread | Damage |
|---|---:|---|---:|---:|---:|---|
| Fist | 1 | None | | | | 2 to 20, range 16 |
| Chainsaw | 1 | None | | | | 2 to 20, range 16 |
| Pistol | 2 | Bullets | 1 | 1 | 2.8 | 5 to 15 |
| Shotgun | 3 | Shells | 1 | 7 | 5.6 | 5 to 15 each |
| Super Shotgun | 3 | Shells | 2 | 20 | 11.2 | 5 to 15 each |
| Minigun | 4 | Bullets | 1 | 1 | 5.6 | 5 to 15 |
| Rocket Launcher | 5 | Rockets | 1 | | | Rocket |
| Plasma Rifle | 6 | Cells | 1 | | | Plasma |
| SKAG 1337 | 7 | Cells | 40 | | | SKAG Ball |

**Try it:** open **In Hand/Pistol** and set **Pellets** to 3 and **Fire Time** to half what it was. Press **P**.

For projectile weapons, the damage is on the projectile. The table is on [page 13](13-Change-Enemies.md).

## The weapon list

On the player, the **WeaponHolder** script has twelve **Weapon** fields, numbered 1 to 12. Nine are used,
in this order, which leaves 10, 11 and 12 free:

Fist, Chainsaw, Pistol, Shotgun, Super Shotgun, Minigun, Rocket Launcher, Plasma Rifle, SKAG 1337.

The order matters. When you run out of ammo, the game switches to the last weapon in the list that has ammo and
**Use As Fallback** on. So stronger weapons go lower down.

Its **Alternate Fire Key** is a second fire button, Left Ctrl by default.

## Recipe: a new weapon

Let's make an auto-shotgun.

1. **Add a variable.** In **Project Settings > Global Event Variables**, add `HasAutoShotgun`.
2. **Reset it.** On the player, add a **Pistol Start** dispatch: **Change Variable**, `HasAutoShotgun`, Set, 0. If
   you skip this, the player keeps the weapon after dying.
3. **Make the weapon.** Duplicate `In Hand/Shotgun` and name it **Auto Shotgun**. Set its **Owned Variable** to
   `HasAutoShotgun`, its **Fire Time** lower, and its **Slot** to 8.
4. **Give it to the player.** In **WeaponHolder**, drag it into **Weapon** field 10.
5. **Make the pickup.** Duplicate `Pickups/Shotgun` and name it **Auto Shotgun**. In its dispatches, change
   `HasShotgun` to `HasAutoShotgun`. Give it a new sprite if you have one.
6. Save both with **Ctrl + S**, drag the pickup into a level and press **P**. Pressing **8** selects it.

The HUD only has **Arm** boxes for slots 2 to 7, so a new slot won't show there. See [page 15](15-Change-The-UI.md).

## Recipe: a new kind of ammo

1. Add two variables: `Darts`, starting at 0, and `MaxDarts`, starting at 100. The starting value is what pressing
   **P** in the editor uses.
2. On the player, add **Pistol Start** rows: `Darts` 0 and `MaxDarts` 100.
3. Add a dispatch **Cap Darts** that sets `Darts` to 100. Add a **Condition Met** event, `Darts > MaxDarts`, that calls it.
4. Set your weapon's **Ammo Variable** to `Darts`.
5. Duplicate `Pickups/Ammo/Clip` and make it add to `Darts`. Also change its **Collide** event's **Condition** to
   `Darts < MaxDarts`, so it's only picked up when there's room.

The ammo number in the HUD will work without anything else, because `WeaponHolder` copies the ammo of the
current weapon to `CurrentAmmo`.

## Recipe: a new projectile

1. Duplicate `Prefabs/Projectiles/Fireball`.
2. In its **Projectile** script, set **Speed**, **Damage Min** and **Damage Max**. **Homing** is how many degrees per
   second it turns toward its target, and 0 flies straight. **Lifetime** is how long it lasts.
3. Set **Impact Prefab** to what appears when it hits.
4. Pick it in an enemy's **FireProjectile** script or a weapon's **Projectile Prefab**.

## Changing pickups

Each pickup has the same structure: a **Collide** event that only the player can set off, once, and some **Pick Up** dispatches.

| Pickup | What it does |
|---|---|
| Stimpack | Health +10, cap 100 |
| Medikit | Health +25, cap 100 |
| Health Bonus | Health +1, cap 200 |
| Soul Sphere | Health +100, cap 200 |
| Mega Sphere | Health 200, armor 200, blue armor |
| Green Armor | Armor 100, type 1 |
| Blue Armor | Armor 200, type 2 |
| Armor Bonus | Armor +1 |
| Clip | Bullets +10 |
| Ammo Box | Bullets +50 |
| Shells, Shell Box | Shells +4, +20 |
| Rocket, Rocket Box | Rockets +1, +5 |
| Cell, Cell Pack | Cells +20, +100 |
| Backpack | Sets `HasBackpack`, doubles every `Max`, adds 10 bullets, 4 shells, 1 rocket and 20 cells |
| Berserk | Sets `Berserk` to 1 and Health to 100 |

**Make a Medikit heal more:** double-click it, find the **Change Variable** dispatch named **Pick Up**, and change 25.

**Have it only pick up when needed:** the Medikit's **Collide** event has a **Condition** of `Health < 100`.
Without one, the player would use it up at full health. If you add a new healing pickup, keep that condition.

**Pickups that count as items** have an **On Start** event calling **Count Me**, and a `Items` +1 row in **Pick Up**.
They're what the Intermission's item count uses. Remove all three and the pickup doesn't count.

**Changing the message:** the text comes from the **Show Message** dispatch. How it looks is in
**World Settings > Message Box**, for each level.

## Recipe: a pickup that does something unusual

Because a pickup's dispatches can do anything, you've already got everything you need. For example, a "panic
button" that wakes every monster and gives you rockets:

1. Duplicate **Medikit** and change its **Sprite**.
2. Change its first **Pick Up** to add 10 to `Rockets`.
3. Add a **Noise Alert** dispatch named **Pick Up**, which wakes up monsters as gunfire does.
4. Change the sound and message, then try it out.

---

Next: [Change the UI](15-Change-The-UI.md)
