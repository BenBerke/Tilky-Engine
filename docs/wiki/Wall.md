# Wall

A wall is one straight edge of a [sector](Sector.md)'s outline, from a **start** point to an **end**
point on the map. Walls aren't drawn separately: they appear automatically when you draw sectors,
one for every edge.

What a wall looks like in the game depends on the sectors on its two sides:

| Sides | Result |
|---|---|
| Sector on one side only | A solid wall from that sector's floor to its ceiling. |
| Sectors on both sides | An **opening** (a portal) between the two rooms. Solid pieces are drawn only where the two rooms don't overlap in height: a step up, a lintel over a doorway, the face of a raised platform. |

So you never make a "doorway wall". You make two sectors with different floor or ceiling heights
next to each other, and the wall between them fills in the difference.

## Front and back

Every wall has a direction, from `start` to `end`. Looking down on the map:

- the **front sector** is on the **left** side of that direction;
- the **back sector** is on the **right** side;
- `normal` points toward the front (left) side.

A one-sided wall has only one of them set; the other is invalid (`4294967295`).

## Top, bottom and middle textures

A wall has three textures, **Top**, **Bottom** and **Middle**, each with its own frames, animation,
offset per second, offset, scale, flip and anchor (see [Animated textures](#animated-textures)).

- A **solid** wall (a sector on one side only) shows only Top. The inspector calls it **Texture**.
- A **portal** shows Bottom on the step under the neighbouring room's floor, and Top on everything
  else: the wall above the neighbour's ceiling, and any slab between two of its floors.
- **Middle** fills a portal's openings, where both rooms are open space: fences, bars, grates,
  windows. It always fills the whole opening (each one, when stacked floors leave several), follows
  sloped floors and ceilings, and is one surface seen from both sides. Transparent pixels show
  through. With no Middle texture there is nothing in the opening at all. Solid walls have no
  openings, so their Middle fields are greyed out.

A Middle has two switches of its own:

| Switch | Default | Effect |
|---|---|---|
| **Has Collision** | on | Blocks entity colliders like a solid wall. |
| **Ignored by Raycast** | off | Raycasts pass through it: script raycasts, `LineOfSight`, and clicking it in the runtime editor. |

### Anchor

The anchor is the height a texture is pinned to. When a floor or ceiling moves, a texture pinned
to the moving edge moves with it; otherwise the edge slides over a texture that stays put.

| Anchor | Lua | Pinned to |
|---|---|---|
| **Auto** | `WallAnchor.Auto` | The top edge, except the wall above a neighbour's ceiling, which uses its bottom edge. Doors and lifts carry their textures with them. |
| **Top Edge** | `WallAnchor.TopEdge` | The piece's top edge. |
| **Bottom Edge** | `WallAnchor.BottomEdge` | The piece's bottom edge. |
| **World** | `WallAnchor.World` | World height 0. The texture never moves; use it for door tracks or to line up neighbouring walls. |

The offset is added on top of the anchor. Textures read upright from both sides of a wall.

## Animated textures

Each texture of a wall (and each [floor and ceiling](Sector.md#animated-textures)) is a list of
**frames** instead of a single image. One frame is a still texture; add more and the surface flips
through them on its own.

| Field | Lua | Notes |
|---|---|---|
| **Frame 1**, **Frame 2**, ... | `GetTopFrames()`, `SetTopFrames(frames)`, `AddTopFrame(texture, duration)` | A texture plus how many **seconds** it shows. **^** moves a frame one place earlier, **Remove** deletes it. A surface with no frames is untextured. |
| **Add Frame** | | Adds a frame at the end, starting as a copy of the last one. |
| **Animation** | `topAnimation` | How the frames play. Greyed out with fewer than two frames. |
| **Offset Per Second** | `topScrollSpeed` | Map units (X, Y) the texture moves every second, on top of its UV Offset. Works with one frame too: conveyor belts, flowing water. |

| Animation | Lua | Plays |
|---|---|---|
| **Loop** | `TextureAnimation.Loop` | `1 2 3 1 2 3 ...` |
| **Ping-Pong** | `TextureAnimation.PingPong` | `1 2 3 2 1 2 3 ...` The end frames don't show twice. |
| **Once** | `TextureAnimation.Once` | `1 2 3`, then stays on the last frame. |

- Animations and scrolls play in **Play** and in the **3D editor view**. The 2D map view always
  shows the first frame, still.
- They run on scaled game time, so `GameTime.timeScale = 0` holds them. Every surface starts on
  frame 1 when Play starts or a level loads, so surfaces with the same frames stay in step.
- A surface starts again from frame 1 when a script replaces its frames (`SetTopFrames`, setting
  `topTexture`), when a **Set Texture** dispatch hits it, or on `RestartTopAnimation()`. The scroll
  carries on.
- Setting a single texture (from a script, a dispatch, a texture drop, or the texture tools) replaces
  every frame with that one texture. The animation mode and Offset Per Second stay.
- Copy and paste (Ctrl+C / Ctrl+V) carries all frames, the animation and the offset per second.
  "Same texture" tools (select all with this texture, flood fill, stairs) compare the first frame.

## Collision

Physics tests entity colliders against the solid parts of each wall. An opening is passable as
long as the entity fits through the gap, unless it has a Middle texture with **Has Collision** on. If the solid part in the way is a low step at the
collider's bottom, and it is no taller than its collider's **Step Size**, the entity steps up onto it
instead of being blocked. See [Collider](Collider.md#step-size).

## In the editor

| Field | Lua | Notes |
|---|---|---|
| **Name** | | Editor only. Scripts can't read it. |
| **Front Sector** / **Back Sector** | `frontSector` / `backSector` | Set by the map topology. Read-only. |
| **Texture** (solid) / **Top**, **Bottom** (portal) | `topTexture`, `bottomTexture` | The frames drawn on the solid parts. No frames means colour only. See [Animated textures](#animated-textures). |
| **Middle** (portal) | `middleTexture` | The frames in the openings. No frames means nothing is there. |
| **Animation** / **Offset Per Second** | `topAnimation`, `topScrollSpeed`, ... | See [Animated textures](#animated-textures). |
| **Wall Color** | `color` | Tint, `Vector4`, `0..1` per channel. |
| **UV Offset** | `topTextureOffset`, `bottomTextureOffset`, `middleTextureOffset` | Shifts the texture, in map units: `32` moves it by one repeat at scale `1`. |
| **UV Scale** | `topTextureScale`, `bottomTextureScale`, `middleTextureScale` | How many times the texture repeats. `2.0` means twice as many repeats. |
| **Flip U** / **Flip V** | | Mirror the texture horizontally or vertically. Editor only. |
| **Anchor** | `topAnchor`, `bottomAnchor`, `middleAnchor` | See [Anchor](#anchor). Auto is the top edge for Middle. |
| **Has Collision** / **Ignored by Raycast** | `middleHasCollision`, `middleIgnoredByRaycast` | Middle only. See [above](#top-bottom-and-middle-textures). |
| **Tags** | `HasTag`, `GetTag`, `tagCount` | Read-only from scripts. |

The sector's light also darkens the wall (see [Sector](Sector.md#light)).

---

## Scripting

### Getting a wall

| Where from | How |
|---|---|
| A public field | `public Wall switchWall` |
| A sector | `sector:GetWall(i)`, `i` from `1` to `sector.wallCount` |
| A raycast hit | `hit.wall`, when `hit.type == "Wall"` |

A `Wall` value is a safe handle. If the wall stops existing (the map was edited), `isValid` becomes
`false` and any other use raises an error. `sector:GetWall(i)` can also return an invalid wall, so
check `isValid`.

### Properties

| Property | Type | | Description |
|---|---|---|---|
| `id` | integer | read-only | Stable wall ID. |
| `isValid` | boolean | read-only | `false` if the wall no longer exists. |
| `start` | Vector2 | read-only | Start point `(x, z)`. |
| `end` | Vector2 | read-only | End point `(x, z)`. `end` is a Lua keyword, so write `wall["end"]`. |
| `dir` | Vector2 | read-only | Unit vector from start to end. |
| `normal` | Vector2 | read-only | Unit vector perpendicular to the wall, pointing to the front side. |
| `length` | number | read-only | Length in map units. |
| `frontSector` | integer | read-only | **ID** of the front sector. |
| `backSector` | integer | read-only | **ID** of the back sector. |
| `isPortal` | boolean | read-only | `true` if there is a sector on both sides, so `bottomTexture` can show. |
| `color` | Vector4 | read/write | Tint, `0..1`. |
| `topTexture` | string | read/write | Top texture path relative to `Assets`, e.g. `"Textures/brick.png"`. Reading gives the frame showing right now; writing replaces every frame with this one texture. |
| `bottomTexture` | string | read/write | Bottom texture path, same rules. Only shows on a portal. |
| `topAnimation` | TextureAnimation | read/write | How the top frames play: `TextureAnimation.Loop`, `PingPong` or `Once`. |
| `bottomAnimation` | TextureAnimation | read/write | How the bottom frames play. |
| `middleAnimation` | TextureAnimation | read/write | How the middle frames play. |
| `topScrollSpeed` | Vector2 | read/write | The top texture's **Offset Per Second**, in map units. |
| `bottomScrollSpeed` | Vector2 | read/write | The bottom texture's Offset Per Second. |
| `middleScrollSpeed` | Vector2 | read/write | The middle texture's Offset Per Second. |
| `topTextureOffset` | Vector2 | read/write | Top texture shift in map units. Change it over time to scroll. |
| `bottomTextureOffset` | Vector2 | read/write | Bottom texture shift in map units. |
| `topTextureScale` | Vector2 | read/write | Top texture repeat multiplier. At `1` the texture covers its own size (pixels / the project's Texels Per Unit). |
| `bottomTextureScale` | Vector2 | read/write | Bottom texture repeat multiplier. At `1` the texture covers its own size (pixels / the project's Texels Per Unit). |
| `topAnchor` | WallAnchor | read/write | Where the top texture is pinned, e.g. `WallAnchor.World`. |
| `bottomAnchor` | WallAnchor | read/write | Where the bottom texture is pinned. |
| `middleTexture` | string | read/write | Middle texture path, filling the openings, same rules as `topTexture`. `""` means no middle. |
| `middleTextureOffset` | Vector2 | read/write | Middle texture shift in map units. |
| `middleTextureScale` | Vector2 | read/write | Middle texture repeat multiplier. At `1` the texture covers its own size (pixels / the project's Texels Per Unit). |
| `middleAnchor` | WallAnchor | read/write | Where the middle texture is pinned. |
| `middleHasCollision` | boolean | read/write | The middle blocks entity colliders. |
| `middleIgnoredByRaycast` | boolean | read/write | Raycasts and `LineOfSight` pass through the middle. |
| `tagCount` | integer | read-only | |

### Methods

| Method | Returns | Description |
|---|---|---|
| `ClearTopTexture()` | | Removes every top frame. |
| `ClearBottomTexture()` | | Removes every bottom frame. |
| `ClearMiddleTexture()` | | Removes every middle frame, and with them the middle. |
| `GetTopFrames()` | table | The top frames: `{ {texture = "...", duration = 0.5}, ... }`. Also `GetBottomFrames()`, `GetMiddleFrames()`. |
| `SetTopFrames(frames)` | | Replaces the top frames and starts from frame 1. Each entry is `{texture = "...", duration = seconds}` (duration optional, default `0.5`) or just a texture path. A bad list raises an error and changes nothing. Also `SetBottomFrames`, `SetMiddleFrames`. |
| `AddTopFrame(texture, duration?)` | | Adds a frame at the end. `duration` in seconds, default `0.5`. Also `AddBottomFrame`, `AddMiddleFrame`. |
| `ClearTopFrames()` | | Same as `ClearTopTexture()`. Also `ClearBottomFrames`, `ClearMiddleFrames`. |
| `RestartTopAnimation()` | | Back to frame 1; the scroll carries on. Also `RestartBottomAnimation`, `RestartMiddleAnimation`. |
| `HasTag(tag)` | boolean | `true` if the wall has `tag`. |
| `GetTag(index)` | string | The `index`-th tag, 1-based. |

> **Any image in `Assets` can be switched to.** When the level loads, the engine packs every
> `.png`, `.jpg` and `.jpeg` file under `Assets` into the texture atlas, so a script can switch to
> an image the level doesn't use yet. An image added to `Assets` while the game is running is only
> picked up the next time the level loads.

### Getting the sector on each side

`frontSector` and `backSector` are **IDs**, not `Sector` values. To get the sector itself, look up a
point just off the wall's middle:

```lua
local function SideSector(wall, side)  -- side = 1 for front, -1 for back
    local s, e = wall.start, wall["end"]
    local mid = Vector2((s.x + e.x) * 0.5, (s.y + e.y) * 0.5)
    return Game.GetSectorAt(mid + wall.normal * (0.5 * side))
end
```

In a sector script, the neighbour behind a wall is also in `sector:GetNeighbor(i)`.

### Walls have no callbacks

Walls can't hold scripts. To react to a wall, use a sector script on one of its sectors, or a
raycast from an entity script.

---

## Examples

### Scrolling texture

A texture that always scrolls needs no script: set its **Offset Per Second** in the inspector. A
script can switch the scroll on and off:

```lua
-- Scripts/Walls/Conveyor.lua (sector script)
-- Starts every "conveyor" wall of this sector scrolling when the room is entered.
public Vector2 speed = Vector2(8, 0)

function OnEntityEnter(other)
    if not other:HasTag("Player") then return end

    for i = 1, sector.wallCount do
        local wall = sector:GetWall(i)
        if wall.isValid and wall:HasTag("conveyor") then wall.topScrollSpeed = speed end
    end
end
```

### Animated texture from a script

```lua
-- Scripts/Walls/Alarm Panel.lua (sector script)
-- Makes the first wall blink between two textures, then stay on the last after three passes.
public Texture offTexture = nil
public Texture onTexture = nil

function Start()
    local wall = sector:GetWall(1)
    wall:SetTopFrames({
        {texture = offTexture, duration = 0.3},
        {texture = onTexture, duration = 0.3},
        {texture = offTexture, duration = 0.3},
        {texture = onTexture, duration = 0.3},
        {texture = offTexture, duration = 0.3},
        {texture = onTexture, duration = 0.3},
    })
    wall.topAnimation = TextureAnimation.Once
end
```

### Switch that changes texture when used

```lua
-- Scripts/Walls/WallSwitch.lua (on the player)
-- Look at a wall tagged "switch" and press E to flip it.
public number reach = 48
public Texture onTexture = nil
public Sector door = nil

function Update()
    if not Input.GetKeyDown(Key.E) then return end

    local camera = entity.camera
    if camera == nil then return end

    local hit = camera:Raycast(reach, false)

    if hit == nil or hit.wall == nil or not hit.wall:HasTag("switch") then return end

    -- An unassigned Texture field is an empty string, not nil.
    if onTexture ~= nil and onTexture ~= "" then hit.wall.topTexture = onTexture end
    if door ~= nil then door:MoveCeilingTo(1, door.floorHeight + 40, 60) end
end
```

### Flash every wall in a room

```lua
-- Scripts/Walls/Alarm.lua (sector script)
public Vector4 flashColor = Vector4(1, 0.2, 0.2, 1)

local walls = {}
local original = {}
local t = 0

function Start()
    for i = 1, sector.wallCount do
        local wall = sector:GetWall(i)
        if wall.isValid then
            walls[#walls + 1] = wall
            original[#original + 1] = wall.color
        end
    end
end

function Update()
    t = t + GameTime.deltaTime
    local k = (mathT.Sin(t * 6) + 1) * 0.5

    for i, wall in ipairs(walls) do
        wall.color = mathT.Vector4Lerp(original[i], flashColor, k)
    end
end
```
