# Tilky Wiki

Reference pages for everything you can place in a level and everything a Lua script can touch.

## Start here

- [Getting Started with Scripting](GettingStarted.md) walks you from an empty project to a working
  script, step by step.
- [Callback Functions](CallbackFunctions.md) lists every function the engine calls on your
  scripts, when it calls it and in what order.

## World

| Page | What it is |
|---|---|
| [Entity](Entity.md) | Anything placed in a level: the player, an enemy, a pickup, a HUD label. It is a name, tags and a set of components. |
| [Sector](Sector.md) | A room: a 2D shape on the map with floors, ceilings and a light. Sectors can have scripts too. |
| [Wall](Wall.md) | One edge of a sector. It has a texture and a colour, and it can be a solid wall or an opening to the next sector. |

## Components

Components are what give an entity its behaviour. Add them in the entity inspector with
**Add Component**.

| Page | Inspector name | In short |
|---|---|---|
| [Transform](Transform.md) | Transform | Position, rotation and scale. Nearly every other component needs one. |
| [Sprite](Sprite.md) | Sprite | A flat image in the world: billboards and 4- or 8-direction sprites. |
| [Model](Model.md) | Model | A 3D model file (`.glb`, `.gltf`, `.obj`, ...). |
| [Audio Source](AudioSource.md) | Audio Source | Plays a sound from the entity's position. |
| [Script](Script.md) | Custom Script | Attaches a Lua script. |
| [Player Controller](PlayerController.md) | Player Controller | Built-in first-person movement and mouse look. |
| [Camera](Camera.md) | Camera | Where the game is rendered from. |
| [Collider](Collider.md) | Collider | A collision shape. It can also be a trigger volume. |
| [Rigidbody](Rigidbody.md) | Rigidbody | Velocity and gravity, so physics can move the entity. |
| [UI Transform](UITransform.md) | UI Transform | Where a UI element sits on screen. |
| [UI Sprite](UISprite.md) | Image | A picture on the HUD. |
| [UI Text](UIText.md) | Text | A text label on the HUD. |

## Built-in Lua tables

Every script can use these globals without setting anything up.

| Page | In short |
|---|---|
| [Game](Game.md) | The level as a whole: find entities, find the sector at a point, raycasts. |
| [Input](Input.md) | Keyboard and mouse. |
| [GameTime](GameTime.md) | `deltaTime`, `fixedDeltaTime` and the real-world clock. |
| [Debug](Debug.md) | Print to the in-game console or write to the engine log. |
| [Scripts](Scripts.md) | A table shared by every script, for level-wide state. |
| [mathT](mathT.md) | Maths helpers: clamping, interpolation, angles, random numbers, vector maths. |
| [Vector2, Vector3, Vector4](Vector.md) | Positions, directions and colours, with `+ - * /`. |

Besides these, a script also gets `entity` (or `sector` in a sector script) and Lua's standard
`math`, `string` and `table` libraries. `os`, `io` and coroutines are not available.

## More

- [`docs/sector.md`](../sector.md) is the full Sector scripting API. The [Sector](Sector.md) page
  here summarises it and explains what sectors are.
- [`docs/scripts/`](../scripts/README.md) has ready-to-use example scripts: doors, lifts,
  pickups, enemy AI, UI and more.

## Conventions used on every page

- **Axes.** `x` and `z` are the ground plane and `y` is up. A map position is a `Vector2` whose
  `.y` is the world `z`.
- **Units.** Distances are map units. The default player is 12 units tall at the eyes and walks at
  46 units per second, so a doorway is about 40 units tall.
- **Colours.** Wall, floor, ceiling and sprite colours are `Vector4` with channels from `0` to `1`.
  A sector's `light` is a `Vector3` with channels from `0` to `255`.
- **Lists are 1-based**, as usual in Lua.
- **Read-only** in a table means scripts can read the value but not change it.
