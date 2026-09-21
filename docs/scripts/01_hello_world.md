# 01 - Hello World and the Lifecycle

## Lifecycle logger

**Attach to:** any GameObject.

Prints when each lifecycle function runs, plus a heartbeat message every few seconds. Attach it,
press play, and then try disabling the GameObject or the script to watch `OnDisable` / `OnEnable`
fire.

```lua
-- Scripts/Examples/HelloWorld.lua (entity script)
---@field greeting string @ Greeting
greeting = "Hello from Tilky!"

---@field printEvery number @ Heartbeat Interval (s)
printEvery = 2.0

local timer = 0.0

function OnEnable()
    Debug.Print("[" .. gameObject.name .. "] OnEnable")
end

function Start()
    Debug.Print(greeting, "- I am", gameObject.name, "(id " .. gameObject.id .. ")")
end

function Update()
    timer = timer + GameTime.deltaTime

    if timer >= printEvery then
        timer = timer - printEvery
        Debug.Print(gameObject.name .. " is still alive")
    end
end

function FixedUpdate()
    -- Runs at a fixed 60 Hz. Nothing to do here, it is only listed for completeness.
end

function OnDisable()
    Debug.Print("[" .. gameObject.name .. "] OnDisable")
end

function OnDestroy()
    Debug.Print("[" .. gameObject.name .. "] OnDestroy")
end
```

**Notes**

- `Debug.Print(...)` joins any number of arguments with spaces (like Lua's `print`) and shows them
  in the in-game console. `Debug.LogInfo/LogWarning/LogError` go to the engine log instead.
- Anything time-based should be multiplied by `GameTime.deltaTime`, otherwise it runs faster on
  faster machines.

---

## A script on a sector

**Attach to:** a sector (sector inspector, Scripts section).

Sector scripts have the same lifecycle, but `sector` replaces `gameObject`.

```lua
-- Scripts/Examples/SectorInfo.lua (sector script)
function Start()
    local name = sector.name
    if name == "" then name = "(unnamed)" end

    Debug.Print("Sector", sector.id, name)
    Debug.Print("  floors:", sector.floorCount, " walls:", sector.wallCount,
                " vertices:", sector.vertexCount, " neighbours:", sector.neighborCount)
    Debug.Print("  floor height:", sector.floorHeight)

    for i = 1, sector.neighborCount do
        local neighbour = sector:GetNeighbor(i)
        if neighbour.isValid then
            Debug.Print("  next to sector", neighbour.id)
        end
    end
end
```

**Notes**

- `sector.floorHeight` is a shortcut for `sector:GetFloor(1).floorHeight`. Sectors can have several
  floor/ceiling intervals, so use `GetFloor(n)` for the others.
- `GetNeighbor(i)` can return an invalid reference, so check `isValid` first.
