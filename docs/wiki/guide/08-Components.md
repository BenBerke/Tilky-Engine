# 8. Components: What Things Are Made Of

You can make a whole game without this page. Read it when you want to change how something works, or
make something new.

## An entity is a list of abilities

An **entity** is one thing in your level. By itself it's almost nothing: a name. What it can do comes
from its **components**, and each component is one ability.

Think of building a character by picking cards:

| Component | The ability it gives |
|---|---|
| **Transform** | "I have a place in the world" |
| **Sprite** | "I have a picture" |
| **Collider** | "I take up space, and things can touch me" |
| **Rigidbody** | "I fall and get pushed around" |
| **Monster AI** | "I hunt the player and attack" |
| **Pathfinder** | "I walk around obstacles" |
| **Flipbook** | "My picture animates" |
| **Player Controller** | "The keyboard and mouse move me" |
| **Camera** | "The game is seen through my eyes" |
| **Custom Script** | "I run this piece of code" |

## See it for yourself

1. In the Asset Browser, double-click **Prefabs/Enemies/Zombie**.
2. The inspector shows its components as a list of cards. Click a card's title to fold or unfold it.
3. Hover over any field to see what it does.

The zombie is a Transform, a Sprite, a Collider, a Rigidbody, a Flipbook, a Monster AI and a
Pathfinder. A medikit is much smaller: a Transform, a Sprite and a Collider, plus an event that you'll
meet on the next page. A lamp is smaller still.

So the difference between a lamp and a zombie isn't some special "enemy" type. The zombie just has more
cards.

## Changing things

Try these on a duplicate of a prefab, so you always have the original. Right-click the prefab in the
Asset Browser and choose **Duplicate**, then double-click your copy.

| To get | Change |
|---|---|
| A tougher enemy | **Monster AI > Health** |
| A faster enemy | **Pathfinder > Speed** |
| A bigger enemy | **Transform > Scale**. The collider grows with it. |
| An enemy that hurts more | In its **Dispatches**, the number the **Bite** or **Shoot** dispatch sets `IncomingDamage` to. For an enemy that throws fireballs, the **Damage** numbers in its projectile prefab, in `Prefabs/Projectiles`. |
| A solid decoration | Add a **Collider**, and set **Is Trigger** off |
| A decoration you can walk through | Remove its **Collider** |

To add a card, click **Add Component** at the bottom of the inspector. To remove one, click the **⋮**
on its title.

## What's a trigger?

A **Collider** can be one of two things, and the **Is Trigger** setting decides which:

- **Off, solid:** you bump into it. Walls, enemies, columns.
- **On, a trigger:** you walk through it, but the game notices. Pickups, walk-over triggers, secret
  spots.

## Making a new weapon

Weapons show how far you can get by duplicating. Each weapon is two prefabs: the one you see in your
hands, and the one you pick up.

1. **Duplicate a prefab in `Prefabs/Weapons/In Hand`,** such as Shotgun. In its **Weapon** script, change
   the numbers: damage, number of pellets, spread, how fast it fires, what ammo it uses, and its sprites.
2. **Add it to the player.** Double-click **Prefabs/Player/Player**, find the **WeaponHolder** script and
   drag your weapon into a free **Weapon** field. Weapons further down the list count as stronger.
3. **Make it pick-up-able.** Duplicate a prefab in `Prefabs/Weapons/Pickups`, and make it set your weapon's
   **Owned Variable** to 1. Add a new variable for that in **Project Settings > Global Event Variables**, such as `HasMyGun`.

## What about scripts?

A **Custom Script** is a component that runs code in the Lua language. The kit uses them only where
ordinary settings can't do the job, such as firing a shot with random spread. You'll find them in
`Assets/Scripts`, with comments explaining each one.

You never have to touch them. If you'd like to learn, start with
[Getting Started with Scripting](../GettingStarted.md).

---

Next: [Events and dispatches: making things happen](09-Events-And-Dispatches.md)
