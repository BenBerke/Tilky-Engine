# Game

`Game` is the table for the **level as a whole**: creating and finding entities, finding the
sector at a point, casting rays, changing the sky, and moving to another level.

```lua
local player

function Start()
    player = Game.FindEntity("Player")
end
```

| Function | Returns | |
|---|---|---|
| [`CreateEntity(isUIEntity, [position], [rotation])`](#createentity) | `Entity` | Adds a new entity to the level, or a copy of one (prefabs too), optionally placed. |
| [`FindEntity(name)`](#finding-entities) | `Entity` or `nil` | The first entity with exactly this name. |
| [`FindEntities(name)`](#finding-entities) | list of `Entity` | Every entity with exactly this name. |
| [`FindEntitiesWithTag(tag)`](#finding-entities) | list of `Entity` | Every entity with this tag. |
| [`FindNearestWithTag(tag, from, [maxDistance])`](#findnearestwithtag) | `Entity` or `nil` | The closest enabled entity with this tag. |
| [`OverlapSphere(center, radius, [tag])`](#overlapsphere) | list of `Entity` | Every entity whose collider touches a sphere, nearest first. |
| [`GetEntity(id)`](#finding-entities) | `Entity` or `nil` | The entity with this ID. |
| [`GetEntities()`](#finding-entities) | list of `Entity` | Every entity in the level. |
| [`GetSectorAt(position)`](#getsectorat) | `Sector` or `nil` | The sector at a point on the map. |
| [`Raycast(origin, direction, length, ignoredEntityID, requireCollider, [rayType])`](#raycast) | table or `nil` | What a line hits first. |
| [`LineOfSight(from, to)`](#lineofsight) | boolean | Whether anything solid is between two points. |
| [`Signal(name, [instigator])`](#signal) | | Fires every Lua Signal event with this name. |
| [`LoadLevel(levelName)`](#loadlevel) | | Switches to another level at the end of this frame. |

| Property | Type | | |
|---|---|---|---|
| [`levelName`](#levelname) | string | read-only | The current level's name. |
| [`MessageBox`](#messagebox) | table | read-only | The level's message box: show text in it, change how it looks. |
| [`Sky`](#sky) | table | read-only | The level's sky: its image, rotation and colours. |

---

## CreateEntity

```lua
Game.CreateEntity()          -- world entity
Game.CreateEntity(true)      -- UI entity
Game.CreateEntity(enemy)     -- a copy of an entity, or a new instance of a prefab

Game.CreateEntity(enemy, position)             -- ...placed at a point
Game.CreateEntity(enemy, position, rotation)   -- ...and turned
Game.CreateEntity(false, position)             -- an empty world entity, placed
```

| Parameter | Type | |
|---|---|---|
| `isUIEntity` | boolean, optional | `true` makes a [UI entity](Entity.md#world-entities-and-ui-entities). Leave it out (or pass `false`) for a world entity. |
| `entity` | Entity | Instead of a boolean: the new entity is a copy of this one. See [Copies and prefabs](#copies-and-prefabs). |
| `position` | `Vector3`, optional | Where to put the new entity's [Transform](Transform.md). |
| `rotation` | `Vector4`, optional | Its rotation, a quaternion like `transform.rotation`. Write `mathT.QuaternionFromEuler(0, 90, 0)` for angles in degrees. |

`position` and `rotation` are each optional: pass `nil` for one to keep the default (the origin and
no rotation for a new entity, the original's for a copy). They are set before the entity's scripts
start, so `Start` already sees them. A UI entity has no Transform, so giving one a position or
rotation raises an error; set its `uiTransform.position` instead. A value of the wrong type also
raises an error.

Adds a new entity to the level right away and returns it. It is the same as placing a new entity in
the editor:

- It is named `"Entity"`, has no tags and is enabled.
- A world entity gets a [Transform](Transform.md) at the origin. A UI entity gets a
  [UI Transform](UITransform.md) instead. It has no other components.
- It gets a new `id`, so `Game.FindEntity`, `Game.GetEntities` and the other lookups find it from
  now on.

Give it more components with [`AddComponent`](Entity.md#adding-and-removing-components), for
example `Component.Sprite` to draw it or `Component.Collider` to make it solid, or a script by name:
see [Adding scripts](Entity.md#adding-scripts).

### Copies and prefabs

Passing an entity makes a copy of it, every component and script included, and returns the copy.
Its scripts start on the next frame (their `Start` runs then).

The usual way is a prefab: drag a `.prefab` from the Asset Browser onto an `Entity` field, then
spawn it:

```lua
public Entity enemy -- a .prefab dropped here in the inspector

function Update()
    if Input.GetKeyDown(Key.E) then
        Game.CreateEntity(enemy, entity.transform.position, entity.transform.rotation)
    end
end
```

An `Entity` field holding a prefab gives a copy of the prefab's entity that is **not** in the level:
you can read it (`enemy.name`, `enemy.sprite`...) and change it, which changes what this play
session spawns from it, but never the `.prefab` file. The new entity starts where the prefab's own
Transform is.

Passing an entity from the level copies it as it is right now, named with `Copy` added.

Entities created while the game runs are **not** saved into the level. Stopping the game in the
editor removes them.

```lua
-- Drop a visible marker where the player is standing.
function Update()
    if not Input.GetKeyDown(Key.M) then return end

    local marker = Game.CreateEntity()
    marker.name = "Waypoint"
    marker.transform.position = entity.transform.position
    marker:AddComponent(Component.Sprite).northTextureFileName = "Textures/flag.png"
end
```

---

## Finding entities

```lua
Game.FindEntity(name)            -- Entity or nil
Game.FindEntities(name)          -- { Entity, ... }
Game.FindEntitiesWithTag(tag)    -- { Entity, ... }
Game.GetEntity(id)               -- Entity or nil
Game.GetEntities()               -- { Entity, ... }
```

- Names and tags must match **exactly**, including upper and lower case.
- The lists are ordinary 1-based Lua tables. They are empty, never `nil`, when nothing matches.
- The result is a snapshot. Entities created or destroyed later don't appear in or disappear from
  a list you already have, so check `e.isValid` before using an entity you kept.
- Every call loops over the whole level. Call them once in `Start` and keep the result instead
  of calling them every frame.
- `GetEntity` takes the number from `entity.id` or from a raycast's `entityID`.

```lua
-- Count the enemies left and open the exit when none remain.
public Sector exit = nil

function Update()
    for _, e in ipairs(Game.FindEntitiesWithTag("enemy")) do
        if e.isValid then return end
    end

    exit:MoveCeilingTo(1, exit.floorHeight + 40, 60)
end
```

(For a real level, find the enemies once in `Start` instead of every frame.)

---

## FindNearestWithTag

```lua
Game.FindNearestWithTag(tag, from, [maxDistance])   -- Entity or nil
```

| Parameter | Type | |
|---|---|---|
| `tag` | string | The tag to look for, exactly as in Project Settings. |
| `from` | `Vector3` or `Entity` | Where to measure from. An entity measures from its position and is never the answer itself, so an enemy looking for the nearest other enemy doesn't find itself. |
| `maxDistance` | number, optional | Only entities at most this far away. No limit when left out. |

Returns the entity with `tag` whose [Transform](Transform.md) position is closest to `from`, or
`nil` if there is none (within `maxDistance`).

- Distance is straight-line, in 3D, between the positions (the feet). Colliders and walls don't
  matter: the nearest one may be behind a wall. Check [`LineOfSight`](#lineofsight) if that matters.
- [Disabled](Entity.md#enabled) entities and entities without a Transform (UI entities) are
  skipped, like Unity's tag lookups skip inactive objects.
- A tag nobody has, or that doesn't exist, gives `nil`.
- It loops over the whole level, like `FindEntitiesWithTag`. Every frame is fine for a handful of
  scripts; for many, call it a few times a second instead (see [`InvokeRepeating`](Invoke.md#invokerepeating)).

```lua
-- Scripts/Turret.lua: aim at the closest enemy in range.
public number range = 400

function Update()
    local target = Game.FindNearestWithTag("Enemy", entity, range)
    if target ~= nil then
        entity.transform:LookAt(target.transform.position, true)
    end
end
```

---

## OverlapSphere

```lua
Game.OverlapSphere(center, radius, [tag])   -- { Entity, ... }
```

| Parameter | Type | |
|---|---|---|
| `center` | `Vector3` | The sphere's centre. |
| `radius` | number | Its radius. Below 0 counts as 0, which finds whatever the point is inside. |
| `tag` | string, optional | Only entities with this tag. |

Every entity with a [Collider](Collider.md) touching or inside the sphere, like Unity's
`Physics.OverlapSphere`. The list is ordinary and 1-based, empty (never `nil`) when nothing is
there.

- It tests the colliders' real shapes (capsule, sphere, box), the same shapes physics uses. An
  entity without an active collider is never found.
- **Trigger colliders count too.** Check `e.collider.isTrigger` to leave them out.
- Each entity is listed **once**, even with several colliders, **nearest first** (by its position).
- [Disabled](Entity.md#enabled) entities and colliders with `isActive` off are left out.
- **Walls don't block it**: an explosion found this way reaches through walls. Use
  [`LineOfSight`](#lineofsight) to keep only what it can see.

```lua
-- Scripts/Barrel.lua: blow up, hurting everything nearby that the blast can see.
public number blastRadius = 96
public number damage = 60

public function Explode()
    local center = entity.transform.position + Vector3(0, 8, 0)

    for _, victim in ipairs(Game.OverlapSphere(center, blastRadius)) do
        local health = victim:GetScript("Health")
        local target = victim.transform.position + Vector3(0, 8, 0)
        if victim.id ~= entity.id and health.isValid and Game.LineOfSight(center, target) then
            health:TakeDamage(damage)
        end
    end

    entity:Destroy()
end
```

---

## GetSectorAt

```lua
Game.GetSectorAt(position)   -- Sector or nil
```

| Parameter | Type | |
|---|---|---|
| `position` | `Vector2` or `Vector3` | A `Vector2` is a map position (`.y` is world `z`). For a `Vector3`, only `x` and `z` are used. |

Returns the [sector](Sector.md) that contains the point, or `nil` if the point is outside the map.
Height is ignored, and if sectors are nested the innermost one wins. It is the same rule that
decides which sector an entity is in, so `Game.GetSectorAt(e.transform.position)` gives the same
answer as `e:GetSector()`.

```lua
-- Only drop a pickup where there is floor.
local spot = entity.transform.position + Vector3(mathT.RandomF(-32, 32), 0, mathT.RandomF(-32, 32))
local where = Game.GetSectorAt(spot)
if where ~= nil then pickup.transform.position = Vector3(spot.x, where.floorHeight, spot.z) end
```

---

## Raycast

```lua
Game.Raycast(origin, direction, length, ignoredEntityID, requireCollider, [rayType])   -- table or nil
```

| Parameter | Type | |
|---|---|---|
| `origin` | `Vector3` | Where the ray starts. |
| `direction` | `Vector3` | Which way it goes. It doesn't need length 1. A zero vector never hits anything. |
| `length` | number | How far it reaches. |
| `ignoredEntityID` | integer | An entity the ray passes through, usually the caster's own `entity.id`. Pass `0` to ignore nothing. |
| `requireCollider` | boolean | See below. |
| `rayType` | `RayType` | Optional, `RayType.General` by default. See [Ray types](#ray-types). |

The first five arguments are required.

The ray stops at the **first** thing it hits: a wall, an entity, a floor or a ceiling. If it hits
nothing within `length`, the result is `nil`. Otherwise it is a table:

| Field | Type | |
|---|---|---|
| `type` | string | `"Entity"`, `"Wall"`, `"SectorFloor"` or `"SectorCeiling"`. |
| `typeID` | integer | The same as a number: `1` Entity, `2` Wall, `3` SectorFloor, `4` SectorCeiling. |
| `position` | `Vector3` | The point that was hit. |
| `distance` | number | From `origin` to `position`. |
| `entity` | `Entity` or `nil` | Set when an entity was hit. |
| `wall` | [`Wall`](Wall.md) or `nil` | Set when a wall was hit. |
| `sector` | [`Sector`](Sector.md) or `nil` | Set when a floor or ceiling was hit. |
| `entityID`, `wallID`, `sectorID` | integer | The IDs of the above. When nothing of that kind was hit, they hold a large invalid number, so test `hit.entity ~= nil` rather than the ID. |

### What the ray can hit

- **Walls**: every solid part of a wall, including the steps above and below an opening.
  The open part of a doorway lets the ray through.
- **Floors and ceilings** of every interval of every sector, slopes included: a sloped
  surface is hit where it actually is, as drawn.
- **Entities**, depending on `requireCollider`:
  - `true`: only entities with an active, non-trigger [Collider](Collider.md). Sphere, capsule
    and box shapes are all used.
  - `false`: every entity with a [Transform](Transform.md), using a box the size of its
    `scale`, standing on its position. Colliders are ignored. This also hits things like
    invisible markers, so `true` is usually what you want.

For a ray from a camera through the crosshair, use
[`camera:Raycast([length], [requireCollider])`](Camera.md#scripting). It starts at the eye
(`transform.position` is at the **feet**), follows the camera's current `yaw` and `pitch`, ignores
the camera's own entity, and returns the same table. `camera:ScreenToRay(x, y)` gives the
`origin, direction` for any other point on screen.

```lua
-- Scripts/Shoot.lua (on the player): damage whatever is under the crosshair.
public number damage = 20

function Update()
    if not Input.GetMouseButtonDown(Input.MouseLeft) then return end

    local hit = entity.camera:Raycast(2000, true)
    if hit == nil then return end

    if hit.entity ~= nil then
        local health = hit.entity:GetScript("Health")
        if health.isValid then health:TakeDamage(damage) end
    elseif hit.wall ~= nil then
        Debug.Print("Hit a wall at", hit.position)
    end
end
```

### Ray types

A ray other than `RayType.General` fires the **Receive Ray** events of what it hits (set up in the
editor's Events section of a wall, linedef, sector or entity), with the ignored entity as the
instigator. It also fires them on every enabled linedef it passes on the way, since linedefs
don't stop rays.

| `RayType` | |
|---|---|
| `General` | The default. Fires nothing. |
| `Interact` | What the Player Controller's Use key casts. |
| `LineOfSight` | |
| `Bullet` | |
| `Custom` | |

```lua
-- Shoot a switch: its Receive Ray (Bullet) event opens the door.
entity.camera:Raycast(2000, true, RayType.Bullet)
```

---

## Signal

```lua
Game.Signal(name, [instigator])
```

Fires every **Lua Signal** event named `name`, on any wall, linedef, sector, entity or the level
itself. `instigator` (an `Entity`, optional) is what the events' tag filters check and what their
dispatches receive. The level decides what a signal does, so a script can announce something
without knowing what reacts to it.

```lua
-- Scripts/Boss.lua
function TakeDamage(amount)
    health = health - amount
    if health <= 0 then
        Game.Signal("boss_dead", entity)
    end
end
```

---

## MessageBox

`Game.MessageBox` is the level's message box: the text **Show Message** dispatches put on screen.
Scripts can show text in it and change how it looks. Its functions are called with a `.`, and its
style is a set of properties.

| Function | Returns | |
|---|---|---|
| `SetMessage(text, [color], [duration])` | | Shows `text`, like a Show Message dispatch. |
| `Clear()` | | Removes every message on screen now. |
| `Reset()` | | Puts the style back to how the level was saved. |
| `GetMessages()` | list of strings | The texts on screen, top to bottom (newest first when stacking). |

### SetMessage

```lua
Game.MessageBox.SetMessage(text, [color], [duration])
```

| Parameter | Type | |
|---|---|---|
| `text` | string | What to show. `\n` starts a new line. |
| `color` | `Vector3` | Optional. The text colour, 0..255 per channel. White by default. |
| `duration` | number | Optional. Seconds on screen, 3 by default. Must be above 0. |

With `stack` off, a new message (from a script or a dispatch) replaces the one shown; with it on,
they stack, newest at the top. A message shown from a `Start` callback stays on screen.

```lua
-- Scripts/Pickup.lua
function OnTriggerEnter(other)
    Game.MessageBox.SetMessage("Picked up the red key", Vector3(255, 60, 60))
end

Game.MessageBox.SetMessage("Checkpoint reached")               -- white, 3 seconds
Game.MessageBox.SetMessage("Hold on...", Vector3(255, 255, 255), 10)
```

### Style

The same settings as the editor's **World Settings > Message Box**, read/write. A change applies to
every message on screen at once, and lasts until the level loads again (or `Reset()`). Sizes are
pixels at the project's UI Reference Height, so they scale with the window like UI Text.

| Property | Type | Default | |
|---|---|---|---|
| `font` | string | `""` | Font file relative to Assets (`.ttf`/`.otf`). `""` is the engine's font. |
| `fontSize` | number | `36` | At least 1. |
| `anchor` | `MessageAnchor` | `TopCenter` | Where on screen the messages sit. |
| `offset` | `Vector2` | `(0, 100)` | Moves the messages from the anchor. +x right, +y down. |
| `align` | `TextAlign` | `Center` | How lines of different widths line up: `Left`, `Center`, `Right`. |
| `wrapWidth` | number | `0` | Lines longer than this wrap between words. 0 never wraps. |
| `background` | boolean | `false` | Draws a box behind each message. |
| `backgroundColor` | `Vector4` | `(0, 0, 0, 153)` | The box colour, 0..255 per channel, alpha included. Tints the texture. |
| `backgroundTexture` | string | `""` | Image stretched over the box, relative to Assets. `""` is a solid box. |
| `padding` | `Vector2` | `(16, 8)` | Space between the text and the box's edges (x left/right, y top/bottom). Only with `background`. |
| `shadow` | boolean | `false` | Draws a copy of the text behind it. |
| `shadowColor` | `Vector4` | `(0, 0, 0, 191)` | 0..255 per channel, alpha included. |
| `shadowOffset` | `Vector2` | `(2, 2)` | How far the shadow is moved. +x right, +y down. |
| `outline` | boolean | `false` | Draws a border around the letters. Can be on with `shadow`. |
| `outlineColor` | `Vector4` | `(0, 0, 0, 255)` | 0..255 per channel, alpha included. |
| `outlineThickness` | number | `2` | At least 0. |
| `fadeIn` | number | `0` | Seconds to fade in, inside each message's duration. |
| `fadeOut` | number | `0` | Seconds to fade out, inside each message's duration. |
| `stack` | boolean | `false` | Off: a new message replaces the shown one. On: they stack, newest at the top. |
| `maxMessages` | integer | `5` | With `stack`, the oldest goes once there are more. At least 1. |
| `spacing` | number | `8` | Space between stacked messages. |

Numbers below a property's minimum are raised to it, and colour channels are kept in 0..255. A value
of the wrong type, an `anchor`/`align` that isn't one of the enum's values, or a name that isn't a
property raises an error.

`MessageAnchor` values: `TopLeft`, `TopCenter`, `TopRight`, `MiddleLeft`, `Center`, `MiddleRight`,
`BottomLeft`, `BottomCenter`, `BottomRight`. `TextAlign` values: `Left`, `Center`, `Right`.

```lua
-- Scripts/Cutscene.lua: subtitles at the bottom, then back to the level's own style.
function Start()
    local box = Game.MessageBox
    box.anchor = MessageAnchor.BottomCenter
    box.offset = Vector2(0, -60)
    box.wrapWidth = 900
    box.background = true
    box.backgroundColor = Vector4(0, 0, 0, 180)
    box.fadeIn = 0.2
    box.fadeOut = 0.4
    box.SetMessage("Where did everyone go?", Vector3(255, 255, 255), 4)
end

function EndCutscene()
    Game.MessageBox.Clear()
    Game.MessageBox.Reset()
end
```

---

## LineOfSight

```lua
Game.LineOfSight(from, to)   -- boolean
```

| Parameter | Type | |
|---|---|---|
| `from` | `Vector3` | One end of the line. |
| `to` | `Vector3` | The other end. |

`true` when nothing solid is between `from` and `to`. Walls (their solid parts, the same as for
[`Raycast`](#raycast)), floors and ceilings block the line. **Entities never do**, so there is no
entity to ignore. If `from` and `to` are the same point, the result is `true`.

Use it for "can this enemy see the player?" checks. It is much cheaper than calling `Raycast`
again and again to step past every entity standing in between.

```lua
-- Scripts/Guard.lua: notice the player when there is a clear line to their eyes.
public Entity player
public number eyeHeight = 12

function Update()
    if player == nil then return end

    local eyes = entity.transform.position + Vector3(0, eyeHeight, 0)
    local target = player.transform.position + Vector3(0, eyeHeight, 0)

    if Game.LineOfSight(eyes, target) then
        Debug.Print("I see you")
    end
end
```

---

## LoadLevel

```lua
Game.LoadLevel(levelName)
```

| Parameter | Type | |
|---|---|---|
| `levelName` | string | A level's file name, with or without `.bson`: `"Level2"` or `"Level2.bson"`. The file can be in any folder under `Assets`. |

Switches the game to another level. The level is loaded from its **saved file**, so in the editor,
unsaved changes to that level aren't included. Save it before pressing Play if you want them.

**When it happens.** The call only queues the switch, the same way
[`entity:Destroy()`](Entity.md#destroying-entities) is queued. The rest of the frame carries on in
the old level: code after the call still runs, and `Game.levelName` is still the old name. Once the
frame is over:

1. Every script in the old level gets `OnDestroy`.
2. The new level replaces the old one. Its entities, sectors, sounds and sky are all its
   own, exactly as saved.
3. The new level starts just like the first one does: its camera and Player Controller are picked,
   and every script gets `OnEnable` and `Start`.

If `LoadLevel` is called several times in one frame, the last call wins. Calls made from
`OnDestroy` during a switch are ignored. Calls from the new level's `Start` are fine: they switch
again after the new level's first frame.

**What carries over.** Only the [`Global`](Global.md) table. Everything else belongs to the level.
Each level has its own player entity, placed in the editor. Entities, sectors and walls you kept in
variables or in `Global` don't carry over; see [Global](Global.md#rules).

**Errors.** A name that isn't a string, a level that doesn't exist, or a name that more than one
level file has, raises an error at the call, and nothing is queued. If the file exists but can't be read, the error is logged and the game stays
on the current level.

**In the editor.** Pressing **Stop** always puts you back on the level you pressed **Play** on,
whatever level the game ended on.

Loading happens in one go, so the game may freeze for a moment while a big level loads.

```lua
-- Scripts/Flow/Exit.lua (trigger at the end of the level)
public string nextLevel = "Level2"

function OnTriggerEnter(other)
    if other.hasPlayerController then Game.LoadLevel(nextLevel) end
end
```

```lua
-- Restart the current level when the player falls out of the map.
function Update()
    if entity:GetSector() == nil then Game.LoadLevel(Game.levelName) end
end
```

---

## levelName

```lua
Game.levelName   -- "Level2"
```

The current level's name: its file name in the `Levels` folder, without `.bson`. It changes when
the new level starts, not when `LoadLevel` is called. Assigning to it raises an error.

---

## Sky

```lua
function Start()
    Game.Sky.mode = SkyMode.Equirectangular
    Game.Sky.texture = "Skies/sunset.png"
    Game.Sky.rotationSpeed = 2
end
```

Every level has one sky, drawn behind everything: wherever no wall, floor, ceiling or sprite
covers the screen, you see the sky. To open a ceiling to it, make the ceiling see-through (alpha 0
in its color). The sky is set in the editor under **World Settings > Rendering > Sky** and saved
with the level. `Game.Sky` changes it from a script: its settings are read/write properties, and
its functions are called with a `.`. Changes last until the level is loaded again (or
`Game.Sky.Reset()`), and are undone when you stop playing in the editor.

| Property | Type | |
|---|---|---|
| `mode` | `SkyMode` | How the image wraps around the camera (below). |
| `texture` | string | The image for Equirectangular and Cylinder skies. |
| `rotation` | number | See the settings below. |
| `rotationSpeed` | number | |
| `horizonOffset` | number | |
| `tint` | `Vector3` | |
| `fallbackColor` | `Vector3` | |

| Function | Returns | |
|---|---|---|
| `SetCubemapFace(face, path)` | | One image of a Cubemap sky. |
| `GetCubemapFace(face)` | string | |
| `Reset()` | | Puts every setting back to how the level was saved. How far `rotationSpeed` has turned the sky is kept, so it doesn't jump. |

| Mode | Images | |
|---|---|---|
| `SkyMode.Cubemap` | six, set with `Game.Sky.SetCubemapFace` | A cube around the camera. |
| `SkyMode.Equirectangular` | one 2:1 panorama, in `Game.Sky.texture` | A sphere around the camera. The middle of the image is Front. |
| `SkyMode.Cylinder` | one strip, in `Game.Sky.texture` | DOOM's sky. The image repeats every 90 degrees (a 256 pixel wide image), and its rows are tied to the screen: row 0 at the top, row 100 in the middle, 200 rows top to bottom. Looking up and down doesn't move it, just like in DOOM. |

**Directions.** Front is +Z (where a camera with yaw 0 looks), Right is -X, Left is +X, Up is +Y.
`SkyFace` has `Right`, `Left`, `Up`, `Down`, `Front` and `Back`. Every face is drawn as if you were
standing inside the cube looking at it, upright. Up has Front along its bottom edge, Down has
Front along its top edge.

```lua
-- A cubemap from Assets/Skies/Space.
local faces = {
    [SkyFace.Right] = "right", [SkyFace.Left] = "left", [SkyFace.Up] = "up",
    [SkyFace.Down] = "down", [SkyFace.Front] = "front", [SkyFace.Back] = "back",
}

function Start()
    Game.Sky.mode = SkyMode.Cubemap
    for face, name in pairs(faces) do
        Game.Sky.SetCubemapFace(face, "Skies/Space/" .. name .. ".png")
    end
end
```

| Setting | Unit | |
|---|---|---|
| `rotation` | degrees | Turns the sky the same way camera yaw turns. |
| `rotationSpeed` | degrees per second | Added to the rotation every frame while the game runs. The editor view never spins. |
| `horizonOffset` | degrees | Positive raises the horizon. On Cubemap and Equirectangular skies it moves every point of the sky up by that angle; on a Cylinder sky it is the angle at the middle of the screen. |
| `tint` | `Vector3`, 0..255 | Multiplies the image. White (255, 255, 255) leaves it unchanged. |
| `fallbackColor` | `Vector3`, 0..255 | Fills the screen when the sky has no usable image: no texture, a Cubemap with a face missing, or an image that fails to load. It also shows through transparent pixels. |

```lua
-- Dusk: slowly turn the sky red.
local t = 0

function Update()
    t = math.min(t + GameTime.deltaTime / 10, 1)
    Game.Sky.tint = Vector3(255, 255 - 135 * t, 255 - 135 * t)
end
```

Setting `mode` to a value that isn't in `SkyMode` raises an error, and the cube face functions do
the same for a value that isn't in `SkyFace`. So do a value of the wrong type and a name that isn't
one of the properties. Paths are relative to `Assets`; an empty string clears the image.
