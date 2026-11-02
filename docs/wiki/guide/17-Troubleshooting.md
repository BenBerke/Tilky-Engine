# 17. When Something Goes Wrong

Find what you're seeing, and try the fixes in order.

## The level

**I press P and nothing moves, or I see a "no camera" screen.**
The level has no player, or the player is disabled. Drag in **Prefabs/Player/Player**.

**The HUD is missing.**
The level has no layouts. Add them in **World Settings > UI Layouts**: HUD, Pause Menu, Death Screen,
Intermission and Victory. Or start your level as a copy of **Blank Base Level**.

**A wall looks wrong, or the player walks through it.**
Check the sector's shape. Two sectors that should join need to share their wall exactly. See
[page 3](03-Sectors.md).

**The player can't get up a step.**
Lower the step, or raise the player's Collider **Step Size**.

**The level is too bright, or too dark far away.**
See "Darkness in the distance" on [page 10](10-Finish-Your-Game.md).

**The exit doesn't go where I expect.**
Exits go to the next level in **Project Settings > Levels**. Drag the list into the order you want.

## Prefabs

**I changed a prefab and one copy didn't change.**
That copy has its own value for that field, shown by a tint in the inspector. Its own value wins.

**My change vanished.**
Prefabs save with **Ctrl + S**. **P** only saves the level.

**The copy on the map looks like a prefab, but changing the prefab does nothing.**
Check that you edited the right one. Duplicated prefabs are separate files.

## Enemies

**An enemy doesn't move.**
Check these in order. Has it seen or heard you? **Ambush** stops noise waking it unless you're in sight. Is **Drive Pathfinder**
on, in **Monster AI**? Does it fit through the opening? Is it stuck in a wall or above the floor?

**An enemy doesn't attack.**
Check **Has Melee** and **Has Missile**, and that it has an event for **Melee Ready** or **Missile Ready**. A
projectile enemy also needs **Projectile Prefab** set in its `FireProjectile` script.

**An attack hurts other monsters but not me, or the opposite.**
You need one event row for each case, with **Filter Tags**. See "Attacks" on [page 13](13-Change-Enemies.md).

**The kill count is wrong.**
Every enemy should have both **Count Me** and a `Kills` row in **Die**.

**A group of Deadflares pushes me around.**
This is a known weakness. They shove a player who's standing still.

## Weapons and pickups

**I picked up a weapon but can't select it.**
Check that its **Owned Variable** in the in-hand prefab matches what the pickup sets, and that it's in a **WeaponHolder** field.

**I changed Pistol Start, but nothing's different when I press P.**
Pressing **P** in a level doesn't run Pistol Start, unless `Health` is 0. Start from the Main Menu level with **New Game**,
or also change the variable's value in **Project Settings > Global Event Variables**.

**The player keeps a weapon after dying.**
Add a **Pistol Start** row setting that `Has...` variable to 0.

**A pickup does nothing.**
Check that its **Collide** event's **Filter Tags** has `Player`, that its **Condition** isn't false, such as `Health < 100` at
full health, and that its Collider has **Is Trigger** on.

**A pickup falls through the floor.**
Triggers ignore floors, so a pickup can't have a moving Rigidbody. Remove the Rigidbody.

**The weapon draws over the HUD, or under it.**
Check **Draw Order** on its **UI Sprite**. In-hand weapons use -1. See [page 15](15-Change-The-UI.md).

## Events and dispatches

**An event doesn't fire.** Check, in order:

1. **Filter Tags:** does the entity have that tag?
2. **Condition:** is it really true?
3. **Max Triggers:** has it already used up its turns?
4. **Chance:** is it set low?
5. **Disabled entity:** disabled entities don't listen. Put the event on something enabled.

**The inspector says "(missing)" next to a call.**
Nothing there has a dispatch with that name. You probably renamed it. Pick it again. Names must match exactly.

**A dispatch runs but nothing happens.**
Add a **Show Message** dispatch with the same name. If the message appears, the dispatch was called, and the problem
is in what it does.

**A Condition Met event only ran once.**
It fires when its condition *becomes* true. It can't fire again until the condition has been false.

**The Set Field flash pops rather than fades.**
Turn on **Over Time**, as well as setting **Speed**.

**A switch does nothing when I press Use.**
Stand closer, up to **Use Reach**. Check the switch's **Receive Ray** event: **Ray Type** should be **Interact**, and
**Filter Tags** should have `Player`.

## The UI

**A new button works with the mouse but not the keyboard.**
Its **Lua Signal** event's **Signal Name** has to be `Menu Pick` followed by its exact name.

**A new button isn't in the cursor's list.**
It needs the tag in **MenuCursor**'s **Menu Tag**.

**A menu stays on screen in the next level.**
The button that loads the level has to call **Hide** on that menu.

**The game is still paused after a menu.**
That button also needs **Resume Game**.

**A number shows the wrong digits.**
`PictureNumber` finds pictures by name. Check the names: `<Digit Name> 1` is the ones.

## Scripts

**My script doesn't run.** A syntax error is shown in the inspector, under the script's **File Name**, as soon as you
save the file. See [Script](../Script.md).

**My script can't find a new entity.** A new entity's scripts can't be reached until the next frame. See
[Invoke](../Invoke.md) for doing something a little later.

## Still stuck?

Build the smallest copy of your problem that you can: a new level, one room, one enemy. It often works there,
and then the difference tells you what's wrong.
