# 10. Finish Your Game

You can build rooms, doors and lifts, and fill them with enemies and weapons. Here's how to turn that
into a game someone else can play.

## Put your level in the game

The game plays levels in a list that you control.

1. Open **Project Settings** and find **Levels**. Every level in your project is listed, and a new one
   joins the bottom by itself.
2. Drag the list into the order you want.

The top level opens first, so keep **Main Menu** there. **New Game** and every exit switch go to the next
level down, so put your first level straight under the menu.

The examples can stay below your last level. Its **End Game Switch** ends the game, so the player never
reaches them, and they'll be handy to look at later. When you want them gone, click **Del** next to a level.
That deletes the file, and it can't be undone.

The main menu's **Select Level** buttons are made for the example levels. Edit or delete them in the
**Main Menu** layout.

**A good checklist for each level:**

- One **Player**, from `Prefabs/Player`
- An **Exit Switch** or **Exit Trigger**, or the **End Game Switch** in your last level
- Enough ammo for the enemies you placed
- A chance to get a key before each door it locks

## Health and weapons between levels

Weapons, ammo, health and armor carry over to the next level automatically, just as in Doom. Keys don't,
and neither do powerups. A player who died and pressed **Try Again** restarts the level with a pistol.

## Darkness in the distance

Far-off walls and monsters fade into the dark, which does a lot to make a level feel like Doom. To change it for a
level, open **World Settings > Rendering**:

| Setting | What it does |
|---|---|
| **Distance Light Start** | How far away things start to get darker |
| **Distance Light End** | How far away they reach their darkest |
| **Distance Light Darkest** | How dark that is: 0 is black, and 1 turns the fade off |

This kit's levels already have these set. A level you make with **Create Level** starts with the engine's gentler
fade, so copy the numbers from a kit level to match: Start 32, End 380 and Darkest 0.2 indoors.

## The menus and HUD

The HUD and menus are **UI layouts**, in the **UI Layouts** folder. A layout is a screen that's saved once
and shown in many levels, so changing the HUD once changes it everywhere.

| Layout | What it is |
|---|---|
| **HUD** | The status bar at the bottom of the screen |
| **Pause Menu** | What Esc opens |
| **Death Screen** | YOU DIED, Try Again and Main Menu |
| **Intermission** | The screen between levels, with kills, items, secrets and time |
| **Victory** | What the End Game Switch shows |
| **Main Menu** | The title screen |

**To change one,** double-click it in the Asset Browser. It opens in the UI Editor. Click the text or picture
you want to change and edit it in the inspector.

Most of what you see on these screens is a **picture**, as it was in Doom:

- **Menu words** like NEW GAME are in `UI/Words`. To add a menu row, duplicate a button and pick another
  picture. You can draw your own in any paint program. For plain text, use **UI Text** with the font
  `Fonts/Classic HUD`, which has the same letters.
- **Status bar numbers** are in `UI/Digits`. Each digit is its own small picture, and the `PictureNumber`
  script on the "... Number" entity picks which ones to show.
- **The face** is in `UI/Faces`, and the `Face` script picks one.
- **The skull beside a menu** is the `MenuCursor` script. It moves by itself to the selected button, and lets
  players use the Up, Down and Enter keys.

A level that you copied from **Blank Base Level** already shows these, except the Main Menu. If you create a level from
scratch with **Create Level**, add them yourself in **World Settings > UI Layouts**: HUD, Pause Menu, Death
Screen, Intermission and Victory.

**To change the title screen,** edit the **Main Menu** layout. Its background is `UI/Pictures/TITLEPIC`:
replace that picture with your own.

## Using your own art and sound

Drop files into the project's `Assets` folder, using the Asset Browser or your computer's file manager:

- **Walls and floors:** add pictures to `Textures/Walls` and `Textures/Flats`. They then appear with the rest.
- **Sounds:** add `.wav`, `.mp3`, `.ogg` or `.flac` files to `Sounds`. Dispatches can then play them.
- **Music:** the kit comes with a track for the menu and each example level, in the `Music` folder. To change
  a level's music, add your own `.mp3` or `.ogg` file to the project, then pick it in **World Settings > Music**.
  A copy of **Blank Base Level** starts with the Level 1 track. Long tracks make your project slower to open,
  because the whole file is loaded at the start.
- **Enemy sprites:** a duplicated enemy needs new flipbooks in `Animations`. Open one to see how it's put
  together. Each is a list of frames with the picture for each angle.

## Testing

Press **P** often. A quick test after each room saves you from finding ten problems at the end.

Have someone else play it too. You know where everything is, and they don't. Watching them get lost shows
you exactly where the level needs a light, a sign or a hint.

## Export your game

1. Open **Project Settings**, and go to the **Build** section.
2. Click **Export Game...**
3. Pick an empty folder.

The folder now holds your game, ready to zip up and share. Whoever receives it runs the program inside, and
doesn't need the engine.

Keep the `Freedoom` folder in your project if you're using its art, so its licence goes with your game. The
kit's music is public domain (CC0), so it has no such rule. `Music/CREDITS.txt` lists who made each track.

## Where to go from here

- Open the example levels and read their comment pins. Copy whatever you like.
- Try the other drawing tools: **Polygon**, **Circle** and **Curve** make interesting rooms.
- [Sector](../Sector.md), [Monster AI](../MonsterAI.md) and [Pathfinder](../Pathfinder.md) explain every
  setting in detail.
- When you want something the kit can't do yet, [Getting Started with Scripting](../GettingStarted.md) is
  your next step.

That's everything you need to make your game. Go and make something horrible.

---

Next, when you want to change how the game plays: [How the kit works](11-How-The-Kit-Works.md)
