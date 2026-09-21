# 18 - Debugging

## Where output goes

| Call | Shows up in |
|------|-------------|
| `Debug.Print(...)` | The in-game console |
| `Debug.LogInfo/LogWarning/LogError/LogCritical(...)` | The engine log |
| A runtime error inside your script | **Both**: printed in red in the in-game console, and logged |

Every logging function accepts any number of arguments of any type and joins them with spaces,
like Lua's `print`: `Debug.Print("hp", hp, "alive", alive)`.

A script that fails to *load* (a syntax error) never runs. Its error, including the line number,
appears in the console and in the script's inspector panel.

## Autocomplete

When scripting starts, the engine writes an API description to `Assets/Scripts/.luals/tilky_api.lua`.
Editors using the Lua language server (LuaLS) can read it for autocomplete and hover docs. It
covers the bindings that have documentation registered, so some newer members may be missing from
it.

---

## Debug tools

**Attach to:** the player (needs a `Camera` and a `PlayerController`).

Two hotkeys:

- **P** prints your position, the sector index you're in, and the frame rate.
- **I** inspects whatever you're looking at: what kind of thing it is, how far away, and its name
  and tags.

```lua
-- Scripts/Debug/DebugTools.lua (entity script)
---@field infoKey string @ Player Info Key
infoKey = "P"

---@field inspectKey string @ Inspect Key
inspectKey = "I"

---@field range number @ Inspect Range
range = 500

local camera, pc, transform

local function TagList(thing)
    local tags = {}
    for i = 1, thing.tagCount do tags[#tags + 1] = thing:GetTag(i) end

    if #tags == 0 then return "(none)" end
    return table.concat(tags, ", ")
end

local function Round(value)
    return math.floor(value * 100 + 0.5) / 100
end

local function PrintPlayerInfo()
    local p = transform.position
    local fps = GameTime.deltaTime > 0 and math.floor(1 / GameTime.deltaTime) or 0

    Debug.Print("position", Round(p.x), Round(p.y), Round(p.z),
                "| sector index", transform.sectorIndex, "| fps", fps)
end

local function Inspect()
    -- Start the ray at the player's eyes. requireCollider = false: see every entity, not only solid ones.
    local p = transform.position
    local eyes = Vector3(p.x, p.y + pc.eyeHeight, p.z)
    local hit = Game.Raycast(eyes, camera.forward, range, gameObject.id, false)

    if hit == nil then
        Debug.Print("Looking at nothing")
        return
    end

    Debug.Print("Hit", hit.type, "at distance", Round(hit.distance))

    if hit.entity ~= nil then
        Debug.Print("  GameObject '" .. hit.entity.name .. "' id", hit.entity.id,
                    "tags:", TagList(hit.entity), "scripts:", #hit.entity:GetScripts())
    end

    if hit.wall ~= nil then
        Debug.Print("  Wall", hit.wall.id, "length", Round(hit.wall.length), "tags:", TagList(hit.wall))
    end

    if hit.sector ~= nil then
        local name = hit.sector.name
        if name == "" then name = "(unnamed)" end
        Debug.Print("  Sector", hit.sector.id, name, "tags:", TagList(hit.sector))
    end
end

function Start()
    camera = gameObject.camera
    pc = gameObject.playerController
    transform = gameObject.transform

    if camera == nil or pc == nil then
        Debug.LogError("DebugTools: " .. gameObject.name .. " needs a Camera and a PlayerController")
    end
end

function Update()
    if camera == nil or pc == nil then return end

    if Input.GetKeyDown(infoKey) then PrintPlayerInfo() end
    if Input.GetKeyDown(inspectKey) then Inspect() end
end
```

**Notes**

- Sectors, walls, and GameObjects all have `tagCount` and `GetTag(i)`, which is why one
  `TagList` helper works for all three.
- The function keys (`F1`...) aren't available to scripts, so debug hotkeys use letters.
  `Input.GetAnyKeyDown()` returns the name of whatever key was pressed this frame, which is handy
  for finding out what a key is called.

---

## Catching errors yourself

Setters and getters in the API throw a Lua error when given something invalid, such as an
out-of-range index, a sector ceiling below its floor, or a reference to something that no longer
exists. Left alone, that error prints in red **every frame** the code runs. `pcall` runs a
function and gives you the error instead of raising it:

```lua
local ok, result = pcall(function()
    return gameObject:GetTag(99)   -- there is no 99th tag
end)

if not ok then
    Debug.LogWarning("Could not read tag:", result)
end
```

A full demo you can attach to any GameObject:

```lua
-- Scripts/Debug/SafeCallDemo.lua (entity script)
function Start()
    -- 1. A call that works.
    local ok, count = pcall(function() return gameObject.tagCount end)
    Debug.Print("tagCount ok:", ok, "value:", count)

    -- 2. A call that fails. pcall returns false plus the error message.
    local ok2, err = pcall(function() return gameObject:GetTag(99) end)
    Debug.Print("GetTag(99) ok:", ok2, "error:", err)

    -- 3. Check for validity up front instead, which is cheaper and clearer.
    if gameObject.isValid then
        Debug.Print(gameObject.name .. " is valid")
    end
end
```

**Guidelines**

- Prefer checking `isValid` (GameObjects, sectors, walls, floors, behaviours) or `nil`
  (components, public-field references) over wrapping everything in `pcall`.
- Use `pcall` around *writes that can legitimately fail*, like sector heights, and stop the script
  (or set a `broken` flag) when one does. The door scripts in [05_doors.md](05_doors.md) show the
  pattern.
- `assert(condition, "message")` raises an error if the condition is false. That's a good way to
  say "this script can't work without X" in `Start`.
