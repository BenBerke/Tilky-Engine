# 15. Change the UI

[Page 10](10-Finish-Your-Game.md) showed what the layouts are. This page shows how they're wired up, so you
can add to them.

Double-click **UI Layouts/HUD** to follow along. Click things in the UI Editor and look at their inspectors.

## What things draw on top of

- **UI Text** always draws above every picture.
- Pictures draw in order of their **Draw Order**, from lowest to highest. It's a number on the **UI Sprite**.
- This is across all layouts. A picture on the HUD and a picture on the Pause Menu use the same order.

The weapon in your hands is the HUD's **Weapon View** picture, with **Draw Order** -1. That keeps it under the status bar
and every other picture. See [UI Sprite](../UISprite.md).

## How the HUD works

Almost nothing in the HUD runs by itself. Each piece watches a variable.

| Piece | How it works |
|---|---|
| **Weapon View** | An empty picture with **Fill When Empty** off. The weapon that's up plays its flipbooks on it. Move or resize it to change where every weapon appears. |
| **Crosshair** | Just a picture |
| **Ammo**, **Health**, **Armor** numbers | `PictureNumber` scripts watching `CurrentAmmo`, `Health` and `Armor` |
| **Arm 2** to **Arm 7** | **Condition Met** on `HasPistol` etc. calls **Own** or **Lost**, which **Set Field** the texture |
| **Face** | The `Face` script reads `Health` and `Invulnerable` |
| **Key icons** | **Condition Met** on `HasRedKey` etc. calls **Got** or **Lost**, which **Set Field** whether it's active |
| **Ammo table** | Count and Max for each ammo type |
| **Hurt Flash** | A full-screen color, see below |
| **Pickup Flash** | The same. Pickups call it directly. |
| **Radiation Suit Tint** | **Condition Met** on `RadSuit` |

### Change the flashes

The **Hurt Flash** listens for the **Lua Signal** `PlayerHurt`, which `PlayerHealth` sends when you take damage. Its
dispatch is a **Set Field** on the color:

- **Color** is how strong it is: the last number, alpha, is 0.14.
- **Over Time** with a **Speed** of 0.8 makes it fade in and out. Without Over Time, it pops on and off.
- **Revert** makes it go back afterward.

**To make it less harsh,** lower that alpha. **To remove the flash,** delete the event.

### Show a new number

The easy way is **UI Text**, which uses a font:

1. Add a new entity in the layout, with a **UI Text** component.
2. Turn on **Show Variable** and pick the variable.
3. Set the **Prefix** and **Suffix** if you like, such as `Darts: `.

The Doom way uses pictures, one per digit:

1. Duplicate **Health Number** and all its **Health Digit** pictures.
2. Rename the digits, keeping the numbers at the end: `Darts Digit 1` is the ones, `Darts Digit 2` the tens.
3. In the `PictureNumber` script, set **Variable** to `Darts` and **Digit Name** to `Darts Digit`.
4. **Digit Count** is the number of digit pictures, and **Minimum Digits** 2 shows 5 as 05.

The script finds digits by name, so those names must be exact.

### Show a new key icon

For the green key from [page 12](12-Inside-The-Player.md):

1. Duplicate the red key icon and move it.
2. Change its picture.
3. In its **Condition Met** events, change `HasRedKey` to `HasGreenKey`.

## How the menus work

Every menu is the same pattern. Three pieces make it work:

**1. A tag on everything.** Every picture on the screen has the same tag, such as `Pause Menu`. They start disabled.

**2. Show and Hide.** Every picture has a dispatch named **Show**, which is **Enable**, and one named **Hide**, which is
**Disable**. So anything can open the whole screen by calling "Show" on everything with that tag. That's how
the player's death opens the Death Screen.

**3. A control entity.** Disabled entities can't listen, so something enabled has to do the job. On the Pause Menu,
**Pause Control** has a **Key Press** event for Esc:

```
   Esc, if MenuState is 0:   "Open"   and everything tagged Pause Menu "Show"
        "Open":  Pause Game, a sound, then MenuState = 1

   Esc, if MenuState is 1:   "Close"  and everything tagged Pause Menu "Hide"
        "Close": Resume Game, a sound, then MenuState = 0
```

`MenuState` is why Esc won't open the pause menu on the death screen. It's set to 2 there, so neither event runs.

## The Options menu

The Pause Menu and the Main Menu each have an **Options** button, which hides their list and shows everything tagged
`Options Menu`. Its one row so far is **Gore**. Clicking it calls **Change Gore** on the **Gore Setting** entity, which is
always enabled so that it can keep running while the menu is hidden. The **GoreSetting** script on it steps
`GoreLevel` through Off, Normal and Extra, saves your choice, and shows the matching picture.

To add a row, add another button tagged `Options Menu` with its own script, the same way.

## Recipe: add a menu button

Let's add a **Hard Mode** button to a menu, which sets a variable of that name.

1. **Duplicate a button** such as **Resume**. Duplicating keeps its tag and its Show and Hide.
2. **Rename it**, say to **Hard Mode**, and move it.
3. **Change its picture.** Menu words are pictures in `UI/Words`. Draw your own, or use **UI Text**.
4. **Change what it does.** Edit its **Go** dispatches.
5. **Change its Lua Signal event's Signal Name** to `Menu Pick Hard Mode`. See below.

### Why each button has two events

A button has to work with both the mouse and the keyboard:

```
   Mouse:     Button Release event  -->  "Go"

   Keyboard:  the MenuCursor script moves between buttons, and on Enter sends
              the Lua Signal "Menu Pick <the button's name>"
              Lua Signal event  -->  "Go"
```

So if the keyboard doesn't work on your new button, check that its **Signal Name** is `Menu Pick` followed by
**exactly** the entity's name.

`MenuCursor`'s fields are **Menu Tag**, which finds the buttons, **Move Sound**, **Picture One** and **Picture Two**, for the
blinking skull, and **Gap**, the space between the skull and the button. A button only joins the cursor's list if it has that tag.

## Leaving a level from a menu

UI layouts stay on screen across a level change if the next level uses them too. So a button that loads a level also has
to hide its own menu. Otherwise, the pause menu is still there in the next level.

That's why the Main Menu button calls "Hide" as well as **Load Level**, and **Resume Game** too, so
the next level isn't paused.

## Recipe: a new screen

The easiest way is to duplicate the closest one.

1. In the Asset Browser, duplicate **Pause Menu** and name it **Map Screen**.
2. Add a tag `Map Screen` in **Project Settings > Tags**. In the layout, swap `Pause Menu` for it on every entity.
3. Change **Pause Control**: the key, and the tag in its calls. Do the same in each button's calls, and in **MenuCursor**'s **Menu Tag**.
4. Add the layout to your levels, in **World Settings > UI Layouts**.

## The other UI scripts

| Script | What it does |
|---|---|
| **ActionView** | Shows the boot or the throwing hand over the weapon, when **QuickActions** on the player asks |
| **ScreenBlood** | Shows and fades the blood on your view. **Most Shown** is how solid it can get. |
| **GoreSetting** | The Options menu's Gore row, above |
| **IntermissionTally** | Counts up the kills, items, secrets and time, using the `Tally...` variables |
| **RestartLevel** | Has one function, `RestartThisLevel`, for buttons to call |
| **Face** | **Health Variable**, **Invulnerable Variable** and **Picture Folder**. Put your own faces in the same folder structure. |

---

Next: [More engine features](16-More-Engine-Features.md)
