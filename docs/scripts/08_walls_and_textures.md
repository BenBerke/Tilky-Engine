# 08 - Walls and Textures

A sector script can reach every wall of its sector:

```lua
for i = 1, sector.wallCount do
    local wall = sector:GetWall(i)   -- a WallRef
    if wall.isValid then             -- always check, some slots can be empty
        Debug.Print("wall", wall.id, "is", wall.length, "units long")
    end
end
```

Useful `WallRef` members:

| Member | Type | Notes |
|--------|------|-------|
| `wall.color` | `Vector4` | Tint, `0..1` per channel (r, g, b, a) |
| `wall.textureOffset` | `Vector2` | Scrolls the texture. Change it over time for moving surfaces |
| `wall.textureFileName` | `string` | Swap the texture. `wall:clearTextureFileName()` removes it |
| `wall.start`, `wall["end"]`, `wall.length` | read-only | Endpoints (`Vector2`) and length. `end` is a Lua keyword, so use `wall["end"]` |
| `wall.frontSector`, `wall.backSector` | integer IDs | `backSector` identifies the neighbour on a portal wall |
| `wall:HasTag("Name")` | boolean | Tags are assigned in the editor |

Tags are the easiest way to say *which* walls a script should touch. Create a tag in the project
settings, put it on the walls, and filter with `wall:HasTag(...)`.

---

## Scrolling texture

**Attach to:** the sector whose walls should scroll: a waterfall, a conveyor belt, lava.

```lua
-- Scripts/Walls/ScrollingWall.lua (sector script)
---@field scrollX number @ Scroll X (per second)
scrollX = 0.0

---@field scrollY number @ Scroll Y (per second)
scrollY = 0.25

---@field onlyTag string @ Only Walls With Tag (empty = all)
onlyTag = ""

local walls = {}

function Start()
    for i = 1, sector.wallCount do
        local wall = sector:GetWall(i)

        if wall.isValid and (onlyTag == "" or wall:HasTag(onlyTag)) then
            walls[#walls + 1] = wall
        end
    end
end

function Update()
    local dt = GameTime.deltaTime

    for _, wall in ipairs(walls) do
        local offset = wall.textureOffset
        wall.textureOffset = Vector2(offset.x + scrollX * dt, offset.y + scrollY * dt)
    end
end
```

**Notes**

- Walls are looked up **once** in `Start` and kept in a table, so `Update` doesn't have to search
  the sector every frame.
- Set `onlyTag` to something like `Scroll` to animate only some walls of the sector.

---

## Texture switch (press to change a wall's texture)

**Attach to:** the sector containing a wall tagged `Switch`.

Looks at the wall and presses a key to flip it between an "off" and an "on" texture. It uses
`Game.Raycast` from the camera to see which wall is being looked at.

```lua
-- Scripts/Walls/TextureSwitch.lua (sector script)
---@field player GameObject @ Player
player = nil

---@field switchTag string @ Wall Tag
switchTag = "Switch"

---@field offTexture Texture @ Off Texture
offTexture = nil

---@field onTexture Texture @ On Texture
onTexture = nil

---@field useKey string @ Use Key
useKey = "E"

---@field useDistance number @ Use Distance
useDistance = 40

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
    local pc = player.playerController
    if camera == nil or pc == nil then return end

    -- Cast a ray from the player's eyes (feet position + eye height) along the camera's forward direction.
    local p = player.transform.position
    local eyes = Vector3(p.x, p.y + pc.eyeHeight, p.z)
    local hit = Game.Raycast(eyes, camera.forward, useDistance, player.id, false)
    if hit == nil or hit.type ~= "Wall" then return end

    for _, s in ipairs(switches) do
        if s.wall.id == hit.wallID then
            s.on = not s.on

            local texture = s.on and onTexture or offTexture
            if texture ~= nil then s.wall.textureFileName = texture end

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
  `wall.textureFileName`.
- To make the switch *do* something, set a channel from
  [05_doors.md](05_doors.md) right where `s.on` changes:
  `Scripts.channels = Scripts.channels or {}` then `Scripts.channels["door1"] = s.on`.

---

## Color cycling

**Attach to:** a sector with walls that should shimmer, like a disco room.

Cycles the tint of walls through the rainbow with three phase-shifted sine waves.

```lua
-- Scripts/Walls/ColorCycle.lua (sector script)
---@field cyclesPerSecond number @ Cycles Per Second
cyclesPerSecond = 0.25

---@field onlyTag string @ Only Walls With Tag (empty = all)
onlyTag = ""

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
