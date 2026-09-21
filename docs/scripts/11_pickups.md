# 11 - Pickups

Pickups are plain entity scripts that compare distances every frame. For a nicer look, add
[BobAndSpin](04_entity_movement.md#bob-and-spin) to the same GameObject.

Collected items are recorded in the shared `Scripts` table so any other script (a door, the HUD,
an exit) can react:

| Key | Meaning |
|-----|---------|
| `Scripts.coins` | Number of coins collected |
| `Scripts.keys[name]` | `true` once the key called `name` has been collected |

---

## Pickup (health, coin, or key)

**Attach to:** the item GameObject.

```lua
-- Scripts/Pickups/Pickup.lua (entity script)
---@field player GameObject @ Player
player = nil

---@field kind enum(Health,Coin,Key) @ Kind
kind = 0

---@field amount number @ Amount (health restored / coins given)
amount = 25

---@field keyName string @ Key Name (for Key pickups)
keyName = "red"

---@field pickupRadius number @ Pickup Radius
pickupRadius = 14

local KIND_HEALTH, KIND_COIN, KIND_KEY = 0, 1, 2

local transform

-- Returns true if the pickup was used up. It stays in the world if it wasn't.
local function Apply()
    if kind == KIND_HEALTH then
        local health = player:GetScript("Health")
        if not health.isValid or health.currentHealth >= health.maxHealth then
            return false   -- already at full health, leave it for later
        end

        health:Heal(amount)
        Debug.Print("+" .. amount .. " health")
        return true

    elseif kind == KIND_COIN then
        Scripts.coins = (Scripts.coins or 0) + amount
        return true

    elseif kind == KIND_KEY then
        Scripts.keys = Scripts.keys or {}
        Scripts.keys[keyName] = true
        Debug.Print("Picked up the " .. keyName .. " key")
        return true
    end

    return false
end

function Start()
    transform = gameObject.transform

    if player == nil then
        Debug.LogWarning("Pickup " .. gameObject.name .. " has no player assigned")
    end
end

function Update()
    if player == nil or not player.isValid then return end

    local p = player.transform.position
    local me = transform.position
    local dx, dz = p.x - me.x, p.z - me.z

    -- Flat distance, so a height difference doesn't count against the radius.
    if math.sqrt(dx * dx + dz * dz) > pickupRadius then return end

    if Apply() then
        local sound = gameObject.audioSource
        if sound ~= nil then sound:play() end

        gameObject:Destroy()
    end
end
```

**Notes**

- Health pickups are only consumed if the player actually needs the health.
- `Scripts.coins = (Scripts.coins or 0) + amount` works even before anything has created the
  counter, since a missing value is `nil` and `nil or 0` is `0`.
- Destroying the GameObject also removes its `AudioSource`, so the sound may be cut off. If that
  happens, play the pickup sound from an `AudioSource` on the player instead.
- Enum options are numbered from `0` in the order they appear in the annotation, which is why the
  `KIND_...` constants match `enum(Health,Coin,Key)`.

---

## Coin counter (HUD)

**Attach to:** a UI GameObject with a `UIText` component.

```lua
-- Scripts/Pickups/CoinCounter.lua (entity script)
local label
local shown = -1

function Start()
    label = gameObject.uiText

    if label == nil then
        Debug.LogError("CoinCounter: " .. gameObject.name .. " has no UIText")
        return
    end

    Scripts.coins = Scripts.coins or 0
end

function Update()
    if label == nil then return end

    local coins = Scripts.coins or 0

    -- Only touch the text when the number changes.
    if coins ~= shown then
        shown = coins
        label.text = "Coins: " .. math.floor(coins)
    end
end
```

**Notes**

- Writing UI text every frame works, but only assigning it when the value changes is cheaper and
  is a good habit.
- `math.floor` makes `25.0` print as `25`.
