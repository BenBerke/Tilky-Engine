# 09 - Interaction

Two useful engine features for interaction:

- `Game.Raycast(origin, direction, length, ignoredEntityID, requireCollider)` shoots a ray and
  returns `nil` on a miss, or a table describing what it hit:

  | Key | Meaning |
  |-----|---------|
  | `type` | `"Entity"`, `"Wall"`, `"SectorFloor"`, or `"SectorCeiling"` |
  | `distance` | How far away the hit was |
  | `position` | World position of the hit (`Vector3`) |
  | `entity`, `entityID` | The GameObject that was hit (`nil` / an invalid ID if none) |
  | `wall`, `wallID` | The `WallRef` that was hit |
  | `sector`, `sectorID` | The `SectorRef` involved |

  With `requireCollider = true` only entities that have an active, non-trigger `Collider` can be
  hit. With `false` any entity can be hit, using a box the size of its transform's `scale`.
  Passing the shooter's own `id` as `ignoredEntityID` stops the ray from hitting them.

- Any script can be called by another script (`someScript:FunctionName()`), which lets you build
  a tiny "interface" system: anything that defines `Interact` can be used.

A GameObject's `transform.position` is at its **feet**. The player's eyes (and camera) sit
`playerController.eyeHeight` above that, so a ray meant to follow the player's gaze starts at
`position.y + eyeHeight` and travels along `camera.forward`. Both scripts below do exactly that.

---

## Interactor: look at it and press E

**Attach to:** the player (needs a `Camera` and a `PlayerController`).

Casts a ray from the player's eyes every frame. If the entity it hits has a script that defines an
`Interact` function, pressing the key calls it. An optional UI label shows a prompt such as
`[E] Pull lever`.

```lua
-- Scripts/Interaction/Interactor.lua (entity script)
---@field range number @ Reach
range = 48

---@field interactKey string @ Interact Key
interactKey = "E"

---@field promptLabel GameObject @ Prompt Label (optional, needs a UIText)
promptLabel = nil

local camera, pc, transform, label

function Start()
    camera = gameObject.camera
    pc = gameObject.playerController
    transform = gameObject.transform

    if promptLabel ~= nil then label = promptLabel.uiText end

    if camera == nil or pc == nil then
        Debug.LogError("Interactor: " .. gameObject.name .. " needs a Camera and a PlayerController")
    end
end

-- The ray starts at the player's eyes: feet position plus eye height.
local function EyePosition()
    local p = transform.position
    return Vector3(p.x, p.y + pc.eyeHeight, p.z)
end

local function SetPrompt(text)
    if label ~= nil and label.text ~= text then label.text = text end
end

-- Looks for a script on `entity` that defines Interact(). If there is one, optionally uses it,
-- and returns the prompt text to show. Returns nil if nothing there can be interacted with.
local function Examine(entity, use)
    local scripts = entity:GetScripts()

    for i = 1, #scripts do
        local script = scripts[i]

        if type(script.Interact) == "function" then
            if use then script:Interact(gameObject) end

            -- The target script can provide a `prompt` string of its own.
            return "[" .. interactKey .. "] " .. (script.prompt or "Interact")
        end
    end

    return nil
end

function Update()
    if camera == nil or pc == nil then return end

    -- requireCollider = true: only things with a Collider can be interacted with.
    local hit = Game.Raycast(EyePosition(), camera.forward, range, gameObject.id, true)

    local prompt = nil
    if hit ~= nil and hit.entity ~= nil then
        prompt = Examine(hit.entity, Input.GetKeyDown(interactKey))
    end

    SetPrompt(prompt or "")
end
```

**Notes**

- `script.Interact` and `script.prompt` are read straight out of the target script. If the target
  doesn't define them the value is `nil`, which is what makes this check safe.
- The player is passed to `Interact` so the target knows *who* used it.
- `GetScripts()` returns a list that only lives for the moment it's used, so the script is used
  inside the loop and never stored in a variable that outlives it.
- The target entity needs a `Collider` (since `requireCollider` is `true`).

---

## Lever

**Attach to:** a GameObject with a `Collider`. Works with the Interactor above.

Toggles a channel that doors, alarms, and lights can listen to
(see [05_doors.md](05_doors.md) and [07_lighting.md](07_lighting.md)).

```lua
-- Scripts/Interaction/Lever.lua (entity script)
---@field channel string @ Channel
channel = "door1"

---@field prompt string @ Prompt
prompt = "Pull lever"

local isOn = false

-- Called by the Interactor. `self` is this script, `who` is the GameObject that used it.
function Interact(self, who)
    isOn = not isOn

    Scripts.channels = Scripts.channels or {}
    Scripts.channels[channel] = isOn

    Debug.Print(who.name .. " turned '" .. channel .. "' " .. (isOn and "on" or "off"))
end
```

**Notes**

- Functions that other scripts call with a colon must take `self` as their first parameter:
  `interactable:Interact(gameObject)` runs `Interact(interactable, gameObject)`.
- `prompt` is a normal public field, so the Interactor can read it, and designers can edit it.

---

## Hitscan weapon

**Attach to:** the player (needs a `Camera` and a `PlayerController`).

Left mouse fires an instant ray from the player's eyes. It supports fire rate, ammo, and reloading, and damages
anything hit that has a `Health` script
([10_health_and_damage.md](10_health_and_damage.md)).

```lua
-- Scripts/Interaction/Hitscan.lua (entity script)
---@field damage number @ Damage
damage = 20

---@field range number @ Range
range = 400

---@field fireDelay number @ Seconds Between Shots
fireDelay = 0.25

---@field magazineSize int @ Magazine Size
magazineSize = 8

---@field reloadTime number @ Reload Time (s)
reloadTime = 1.5

---@field ammoLabel GameObject @ Ammo Label (optional, needs a UIText)
ammoLabel = nil

local camera, pc, transform, label
local ammo = 0
local cooldown = 0.0
local reloading = 0.0
local shownText = ""

local function UpdateLabel()
    if label == nil then return end

    local text = reloading > 0 and "Reloading..." or (ammo .. " / " .. magazineSize)
    if text ~= shownText then
        shownText = text
        label.text = text
    end
end

local function Fire()
    ammo = ammo - 1
    cooldown = fireDelay

    local p = transform.position
    local eyes = Vector3(p.x, p.y + pc.eyeHeight, p.z)

    local hit = Game.Raycast(eyes, camera.forward, range, gameObject.id, true)
    if hit == nil then return end

    if hit.entity ~= nil and hit.entity:HasScriptNamed("Health") then
        hit.entity:GetScript("Health"):TakeDamage(damage)
        Debug.Print("Hit " .. hit.entity.name)
    end
end

function Start()
    camera = gameObject.camera
    pc = gameObject.playerController
    transform = gameObject.transform
    ammo = magazineSize

    if ammoLabel ~= nil then label = ammoLabel.uiText end

    if camera == nil or pc == nil then
        Debug.LogError("Hitscan: " .. gameObject.name .. " needs a Camera and a PlayerController")
    end
end

function Update()
    if camera == nil or pc == nil then return end

    local dt = GameTime.deltaTime
    cooldown = math.max(0, cooldown - dt)

    if reloading > 0 then
        reloading = reloading - dt
        if reloading <= 0 then ammo = magazineSize end
    else
        local wantsReload = Input.GetKeyDown("R") and ammo < magazineSize
        if wantsReload or (ammo == 0 and Input.GetMouseButtonDown(Input.MouseLeft)) then
            reloading = reloadTime
        elseif cooldown <= 0 and ammo > 0 and Input.GetMouseButton(Input.MouseLeft) then
            Fire()
        end
    end

    UpdateLabel()
end
```

**Notes**

- `GetMouseButton` is true while held (automatic fire). Use `GetMouseButtonDown` instead for
  semi-automatic.
- The weapon skips the shooter itself by passing `gameObject.id` as the ignored entity.
- Enemies need a `Collider` to be hit, since `requireCollider` is `true`.
