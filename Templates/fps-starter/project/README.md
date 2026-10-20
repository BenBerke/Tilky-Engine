# FPS Starter

A small, complete first-person shooter to learn from and build on: two levels, two weapons, two
enemy types, key cards, doors, lifts and exit switches. Every script in `Assets/Scripts` is
commented, so reading them is the quickest way to see how it all fits together.

## Controls

| Key | Action |
|---|---|
| W A S D, mouse | Move and look |
| Left Shift + W | Run |
| Left mouse / Left Ctrl | Fire |
| 1 / 2 | Pistol / Shotgun |
| E | Open doors, call lifts, press switches |

## What's where

| Folder | Contents |
|---|---|
| `Assets/Levels` | `Level 1 - Outpost` (the start level) and `Level 2 - Depot`. |
| `Assets/Prefabs` | The Player, enemies, pickups, props and effects. Change a prefab and every copy in both levels follows. |
| `Assets/Scripts/Game` | `GameRules`: shared helpers in the `Global` table (damage, noise, sprite frames). |
| `Assets/Scripts/Player` | `Player` (health, armor, keys, HUD, death and win) and `Weapons` (guns, ammo, firing). |
| `Assets/Scripts/Enemies` | `Enemy` (one AI script for every enemy, set up by its public fields) and `Projectile` (the imp's fireball). |
| `Assets/Scripts/Pickups` | `Pickup`: health, armor, ammo, weapons and keys. |
| `Assets/Scripts/World` | Sector scripts for `Door`, `Lift`, `ToxicFloor` and `LightEffect`; `ExitSwitch` and `Animate` for entities. |
| `Assets/Scripts/Effects` | `Effect` (bullet puffs, blood) and `Barrel` (explodes when shot). |
| `Assets/Scripts/UI` | `ScaleWithScreen`: keeps HUD images the same size on any window. |
| `Assets/Sprites`, `Textures`, `Sounds` | Art and sound, from Freedoom. |

## Ideas for your first changes

- **A tougher enemy**: open `Prefabs/Enemies/Imp.prefab`, raise its Health and Speed, and every imp
  in both levels changes.
- **A new enemy**: copy a prefab, point its Sprite Folder at new frames and change its sounds and
  attack fields. `Enemy.lua` handles the rest.
- **A new weapon**: add an entry to the `WEAPONS` table at the top of `Weapons.lua` and give the
  player a pickup for it.
- **A third level**: make a new level with a Player prefab and the HUD entities (copy them from an
  existing level), then set the last level's Exit Switch **Next Level** to its name.

## Credits

The textures, sprites and sounds come from [Freedoom](https://freedoom.github.io), made by the
Freedoom contributors and released under a BSD licence. You can use them in your own games,
including commercial ones, as long as you keep their licence notice with them:
`Assets/Freedoom/COPYING.txt` (licence) and `Assets/Freedoom/CREDITS.txt` (contributors).
