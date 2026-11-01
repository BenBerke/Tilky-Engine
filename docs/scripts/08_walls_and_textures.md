# 08 - Walls and Textures

A sector script can reach every wall of its sector:

```lua
for i = 1, sector.wallCount do
    local wall = sector:GetWall(i)   -- a Wall
    if wall.isValid then             -- always check, some slots can be empty
        Debug.Print("wall", wall.id, "is", wall.length, "units long")
    end
end
```

Useful `Wall` members:

| Member | Type | Notes |
|--------|------|-------|
| `wall.color` | `Vector4` | Tint, `0..1` per channel (r, g, b, a) |
| `wall.topTextureOffset`, `wall.bottomTextureOffset`, `wall.middleTextureOffset` | `Vector2` | Shifts a texture |
| `wall.topScrollSpeed`, `wall.bottomScrollSpeed`, `wall.middleScrollSpeed` | `Vector2` | Offset Per Second: the texture scrolls by itself, no `Update` needed |
| `wall.topTexture`, `wall.bottomTexture`, `wall.middleTexture` | `string` | Swap a texture (replaces every frame with this one). Reading gives the frame showing now. `wall:ClearTopTexture()` / `wall:ClearBottomTexture()` / `wall:ClearMiddleTexture()` remove it |
| `wall:SetTopFrames(frames)`, `wall:AddTopFrame(texture, seconds)`, `wall:GetTopFrames()` | | Animated textures; also `Bottom` and `Middle` versions |
| `wall.topAnimation`, `wall.bottomAnimation`, `wall.middleAnimation` | `TextureAnimation` | `Loop`, `PingPong` or `Once` |
| `wall.topAnchor`, `wall.bottomAnchor`, `wall.middleAnchor` | `WallAnchor` | Where a texture is pinned (`Auto`, `TopEdge`, `BottomEdge`, `World`) |
| `wall.isPortal` | boolean | `true` if a sector is on both sides. Bottom and Middle only show on portals |
| `wall.middleHasCollision`, `wall.middleIgnoredByRaycast` | boolean | Whether the middle (fence, bars) blocks movement, and whether raycasts pass through it |
| `wall.start`, `wall["end"]`, `wall.length` | read-only | Endpoints (`Vector2`) and length. `end` is a Lua keyword, so use `wall["end"]` |
| `wall.frontSector`, `wall.backSector` | integer IDs | `backSector` identifies the neighbour on a portal wall |
| `wall:HasTag("Name")` | boolean | Tags are assigned in the editor |

Tags are the easiest way to say *which* walls a script should touch. Create a tag in the project
settings, put it on the walls, and filter with `wall:HasTag(...)`.

---

## Scrolling texture

A texture that always scrolls (a waterfall, a conveyor belt, lava) needs no script at all: set
**Offset Per Second** under the texture's frames in the wall, floor or ceiling inspector. A script
only helps when the scroll should change during the game.

**Attach to:** the sector whose walls should scroll.

```lua
-- Scripts/Walls/ScrollingWall.lua (sector script)
public number scrollX = 0.0
public number scrollY = 0.25
public string onlyTag = ""

function Start()
    for i = 1, sector.wallCount do
        local wall = sector:GetWall(i)

        if wall.isValid and (onlyTag == "" or wall:HasTag(onlyTag)) then
            wall.topScrollSpeed = Vector2(scrollX, scrollY)
        end
    end
end
```

**Notes**

- The engine moves the texture every frame from then on, so there is no `Update`. Set the speed
  back to `Vector2(0, 0)` to stop it.
- Set `onlyTag` to something like `Scroll` to animate only some walls of the sector.

---

## Animated texture

Flickering screens, lava, blinking lights: give a wall, floor or ceiling more than one **frame** in
its inspector (**Add Frame**), each with a duration in seconds, and pick an **Animation**: **Loop**
(`1 2 3 1 2 3`), **Ping-Pong** (`1 2 3 2 1`) or **Once** (`1 2 3`, then stay on 3). A script can
build the same thing:

**Attach to:** the sector containing walls tagged `Screen`.

```lua
-- Scripts/Walls/Screens.lua (sector script)
public Texture frame1 = nil
public Texture frame2 = nil
public Texture frame3 = nil
public number seconds = 0.2

function Start()
    for i = 1, sector.wallCount do
        local wall = sector:GetWall(i)

        if wall.isValid and wall:HasTag("Screen") then
            wall:SetTopFrames({
                {texture = frame1, duration = seconds},
                {texture = frame2, duration = seconds},
                {texture = frame3, duration = seconds},
            })
            wall.topAnimation = TextureAnimation.Loop
        end
    end
end
```

**Notes**

- Animations run on game time: `GameTime.timeScale = 0` pauses them, and every surface starts on
  frame 1 when the level starts, so walls with the same frames blink together.
- `wall:RestartTopAnimation()` jumps back to frame 1, which is how to replay a **Once** animation.
- Floors and ceilings have the same members with `Floor` / `Ceiling` in the name:
  `sector:GetFloor(1):SetFloorFrames(...)`, `floorAnimation`, `ceilingScrollSpeed`.

---

## Texture switch (press to change a wall's texture)

**Attach to:** the sector containing a wall tagged `Switch`.

Looks at the wall and presses a key to flip it between an "off" and an "on" texture. It uses
`camera:Raycast` from the camera to see which wall is being looked at.

```lua
-- Scripts/Walls/TextureSwitch.lua (sector script)
public Entity player = nil
public string switchTag = "Switch"
public Texture offTexture = nil
public Texture onTexture = nil
public Key useKey = Key.E
public number useDistance = 40

local switches = {}   -- wall -> on/off

function Start()
    if player == nil then
        Debug.LogWarning("TextureSwitch in sector " .. sector.id .. " has no player assigned")
        return
    end

    for i = 1, sector.wallCount do
        local wall = sector:GetWall(i)

        if wall.isValid and wall:HasTag(switchTag) then
            switches[#switches + 1] = { wall = wall, on = false }
        end
    end
end

function Update()
    if player == nil or not player.isValid or #switches == 0 then return end
    if not Input.GetKeyDown(useKey) then return end

    local camera = player.camera
    if camera == nil then return end

    -- Cast a ray from the player's eyes through the middle of the screen.
    local hit = camera:Raycast(useDistance, false)
    if hit == nil or hit.type ~= "Wall" then return end

    for _, s in ipairs(switches) do
        if s.wall.id == hit.wallID then
            s.on = not s.on

            local texture = s.on and onTexture or offTexture
            if texture ~= nil then s.wall.topTexture = texture end

            Debug.Print("Switch is now " .. (s.on and "ON" or "OFF"))
            return
        end
    end
end
```

**Notes**

- `hit.wallID` is the ID of the wall the ray struck, which is compared to each switch wall's `id`.
- The fifth argument to `Raycast` (`false`) means entities don't need a collider to be hit.
  Walls are always hit.
- Texture fields (`Texture`) hold the asset's path, which can be assigned directly to
  `wall.topTexture` or `wall.bottomTexture`.
- To make the switch *do* something, set a channel from
  [05_doors.md](05_doors.md) right where `s.on` changes:
  `Global.channels = Global.channels or {}` then `Global.channels["door1"] = s.on`.

---

## Color cycling

**Attach to:** a sector with walls that should shimmer, like a disco room.

Cycles the tint of walls through the rainbow with three phase-shifted sine waves.

```lua
-- Scripts/Walls/ColorCycle.lua (sector script)
public number cyclesPerSecond = 0.25
public string onlyTag = ""

local walls = {}
local clock = 0.0

function Start()
    for i = 1, sector.wallCount do
        local wall = sector:GetWall(i)

        if wall.isValid and (onlyTag == "" or wall:HasTag(onlyTag)) then
            walls[#walls + 1] = wall
        end
    end
end

function Update()
    clock = clock + GameTime.deltaTime * cyclesPerSecond * 2 * math.pi

    -- Three sine waves 120 degrees apart, remapped from -1..1 to 0..1.
    local r = 0.5 + 0.5 * math.sin(clock)
    local g = 0.5 + 0.5 * math.sin(clock + 2.094)
    local b = 0.5 + 0.5 * math.sin(clock + 4.188)

    for _, wall in ipairs(walls) do
        local current = wall.color
        wall.color = Vector4(r, g, b, current.w)   -- keep each wall's own alpha
    end
end
```
