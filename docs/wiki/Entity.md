# Entity

An entity is one thing placed in a level: the player, a monster, a lamp, a pickup, a HUD label.
On its own an entity is just a **name**, some **tags** and an **ID**. Everything it can actually do
comes from its **components**: a [Transform](Transform.md) gives it a place in the world, a
[Sprite](Sprite.md) makes it visible, a [Collider](Collider.md) makes it solid, a
[Script](Script.md) gives it custom behaviour, and so on.

## World entities and UI entities

There are two kinds of entity:

| Kind | Starts with | Used for |
|---|---|---|
| World entity | a [Transform](Transform.md) | Anything that exists in the 3D level. |
| UI entity | a [UI Transform](UITransform.md) | HUD elements drawn on top of the screen: [Image](UISprite.md) and [Text](UIText.md). |

UI entities are listed separately in the Hierarchy. They have no Transform, so they are never
"inside" a sector, never collide and `entity:GetSector()` returns `nil` for them.

## In the editor

| Field | What it does |
|---|---|
| **Name** | Display name. `Game.FindEntity("Name")` looks entities up by it. Names don't have to be unique. |
| **Tags** | Labels from the project's tag list (Project Settings). Scripts can test them with `HasTag`, and `Game.FindEntitiesWithTag` finds every entity with one. Scripts can read tags but not change them. |
| **Components** | The components on this entity. **Add Component** adds one, clicking one opens its settings, and **Remove** deletes it. |

An entity can hold **one** of each component, except scripts: it can have any number of
**Custom Script** components, even several copies of the same script.

---

## Scripting

In an entity script, the global `entity` is the entity the script is attached to. You can get
other entities from:

| Where from | How |
|---|---|
| A public field | `public Entity target` |
| A callback | `other` in `OnCollisionEnter(other)`, `entity` in a sector's `OnEntityEnter(entity)` |
| A new one | `Game.CreateEntity()`, see [Creating entities](#creating-entities) |
| By name | `Game.FindEntity("Player")` (first match or `nil`), `Game.FindEntities("Crate")` (list) |
| By tag | `Game.FindEntitiesWithTag("enemy")` (list) |
| By ID | `Game.GetEntity(id)`, for example with a raycast hit's `entityID` |
| All of them | `Game.GetEntities()` |
| In a sector | `sector:GetEntities()`, `sector:GetEntitiesWithTag(tag)` |
| From a script reference | `behaviour.entity` |
| A raycast | `Game.Raycast(...).entity` |

An `Entity` value is a **safe handle**, not the entity itself. If the entity is destroyed, the
handle stays usable: `isValid` becomes `false`, component properties return `nil`, and writes do
nothing. Check `isValid` on any entity you keep around between frames.

### Properties

| Property | Type | | Description |
|---|---|---|---|
| `id` | integer | read-only | Stable ID, unique within the level. It never changes while the level is loaded. |
| `isValid` | boolean | read-only | `false` once the entity has been destroyed. |
| `name` | string | read/write | The display name. |
| `enabled` | boolean | read/write | Whether the entity's scripts run. See [enabled](#enabled). |
| `tagCount` | integer | read-only | Number of tags. |
| `hasScript` | boolean | read-only | `true` if any script is attached. |

### Components

Each component has a `has...` flag and a property that returns the component, or `nil` if the
entity doesn't have one. An entity can have **several** components of the same type (every type
except Transform and UI Transform). The property returns the **first** one, and
`GetComponents` returns them all. See [Several components of one type](#several-components-of-one-type).

| Flag | Component property | Page |
|---|---|---|
| `hasTransform` | `transform` | [Transform](Transform.md) |
| `hasSprite` | `sprite` | [Sprite](Sprite.md) |
| `hasModel` | `model` | [Model](Model.md) |
| `hasFlipbook` | `flipbook` | [Flipbook](Flipbook.md) |
| `hasAudioSource` | `audioSource` | [Audio Source](AudioSource.md) |
| `hasPlayerController` | `playerController` | [Player Controller](PlayerController.md) |
| `hasCamera` | `camera` | [Camera](Camera.md) |
| `hasCollider` | `collider` | [Collider](Collider.md) |
| `hasRigidbody` | `rigidbody` | [Rigidbody](Rigidbody.md) |
| `hasUITransform` | `uiTransform` | [UI Transform](UITransform.md) |
| `hasUISprite` | `uiSprite` | [UI Sprite](UISprite.md) |
| `hasUIText` | `uiText` | [UI Text](UIText.md) |

Scripts can add and remove them with `AddComponent` and `RemoveComponent`. See
[Adding and removing components](#adding-and-removing-components).

### Methods

| Method | Returns | Description |
|---|---|---|
| `Destroy()` | | Queues the entity for removal at the end of this frame. See [Destroying](#destroying-entities). |
| `GetScript(name)` | Behaviour | An attached script by name. `"Health"` and `"Player/Health"` both match `Scripts/Player/Health.lua`. With several matches, returns the first. If there's no match, returns an invalid reference (`isValid == false`), never `nil`. |
| `GetScriptById(instanceId)` | Behaviour | An attached script by its unique instance ID. |
| `GetScripts()` | Behaviour[] | Every script on the entity. |
| `HasScriptNamed(name)` | boolean | `true` if a script matching `name` is attached. Same matching as `GetScript`. |
| `HasTag(tag)` | boolean | `true` if the entity has `tag`. An unknown tag name returns `false`. |
| `GetTag(index)` | string | The `index`-th tag, 1-based. Raises an error if out of range. |
| `GetSector()` | Sector or `nil` | The sector the entity stands in, or `nil` outside the map or without a Transform. See [Sector occupancy](Sector.md#occupancy). |
| `AddComponent(component)` | the component | Adds a new component, e.g. `Component.Sprite`, after any it already has, and returns it. See [Adding and removing components](#adding-and-removing-components). |
| `GetComponents(component)` | table | Every component of that type, in order, e.g. `entity:GetComponents(Component.AudioSource)`. An empty table if there are none. |
| `RemoveComponent(component)` | boolean | With a type (`Component.Collider`), removes **every** component of that type. With a component (`entity.collider`), removes just that one. `false` if there was nothing to remove. |

Script references (`Behaviour`) are covered on the [Script](Script.md#behaviour-references) page.

### Callbacks

Entity scripts can define these, on top of the usual `Start`, `Update` and friends. Details and
timing are in [Callback Functions](CallbackFunctions.md).

| Callback | Called when |
|---|---|
| `OnCollisionEnter(other)`, `OnCollision(other)`, `OnCollisionExit(other)` | This entity's solid collider starts touching, keeps touching, or stops touching another. |
| `OnTriggerEnter(other)`, `OnTrigger(other)`, `OnTriggerExit(other)` | A trigger collider starts, keeps, or stops overlapping (this entity is the trigger, or the thing inside it). |
| `OnSectorChange(sector)` | This entity moved into a different sector. `sector` is `nil` if it left the map. |

---

## enabled

`entity.enabled` is the entity's on/off switch **for scripts**:

- When it is `false`, none of the entity's scripts get `Update` or `FixedUpdate`, and they don't
  receive collision, trigger or sector-change callbacks. Each script gets `OnDisable` once.
- Turning it back on calls `OnEnable` (and `Start`, the first time) on every script whose own
  Enabled box is ticked.
- It does **not** hide the entity, stop its physics or stop its sound. A disabled entity is still
  drawn, still collides and is still counted inside its sector. To make something disappear, change
  the components themselves: shrink its [Transform](Transform.md) scale to 0 so its sprite or model
  isn't drawn, and turn off its [collider](Collider.md). See the example below.
- It is saved with the level.

## Destroying entities

`entity:Destroy()` removes an entity and all of its components. It is safe to call from any
callback, including on the script's own entity.

The removal is **deferred** to the end of the frame. Until then the entity keeps working: it is
drawn, it collides, its handle is valid. At the end of the frame every script on it gets
`OnDestroy`, then it is gone. After that:

- `isValid` is `false` on every handle to it;
- its sector gets `OnEntityExit`;
- anything that was touching it gets `OnCollisionExit` / `OnTriggerExit` next frame, with an
  invalid `other`.

## Creating entities

`Game.CreateEntity()` adds a new, empty world entity to the level and returns it;
`Game.CreateEntity(true)` adds a UI entity. It starts with only a Transform (or UI Transform). Give
it more with [`AddComponent`](#adding-and-removing-components). It can't be given a script. See
[Game.CreateEntity](Game.md#createentity).

## Adding and removing components

```lua
local sprite = entity:AddComponent(Component.Sprite)   -- returns the new Sprite
entity:RemoveComponent(sprite)                         -- removes that one Sprite
entity:RemoveComponent(Component.Collider)             -- removes every Collider; true if there was one
```

Components are picked from the global `Component` table. The script editor and VS Code suggest
its values as you type `Component.`:

| World entities | UI entities |
|---|---|
| `Component.Transform`, `Component.Sprite`, `Component.Model`, `Component.Flipbook`, `Component.AudioSource`, `Component.PlayerController`, `Component.Camera`, `Component.Collider`, `Component.Rigidbody` | `Component.UITransform`, `Component.UISprite`, `Component.UIText` |

- The values are plain numbers. A misspelled one (`Component.Sprit`) is `nil`, and passing `nil` or
  a number that isn't in the table raises an error.
- `AddComponent` always adds a **new** component after the ones the entity already has, and
  returns it. On an entity with no Sprite yet, that is the same value as `entity.sprite`.
  Transform and UI Transform are the exception: an entity has only one, so for those the existing
  one is returned.
- A new component starts with its default settings, listed on its own page. Set what you need on
  the returned value. Only that component is added: unlike in the editor, adding
  `Component.PlayerController` doesn't also add a Rigidbody, Collider and Camera.
- `RemoveComponent(Component.X)` removes **every** component of that type.
  `RemoveComponent(component)` removes only the component you pass, which must belong to this
  entity (another entity's raises an error). Either way it returns `true` if something was
  removed and `false` if not. Removed components are gone straight away: `entity.collider` moves
  on to the next Collider (or `nil`) from the next line on, and component values you kept report
  `isValid == false`.
- World components can't be added to a UI entity (one with a UI Transform), and UI components
  can't be added to a world entity (one with a Transform). Trying raises an error.
- **Scripts** can't be added or removed this way, so there is no `Component.Script`.
- On an entity that has been destroyed, `AddComponent` returns `nil` and `RemoveComponent`
  returns `false`.

Changes made while the game runs are not saved into the level. Stopping the game in the editor
puts every entity back the way it was.

Some components behave differently when added while the game runs:

- **Player Controller**: a new one starts with `isActive` off. Set it to `true` to switch control
  to it (see [Which controller is used](PlayerController.md#which-controller-is-used)). Removing the
  running one stops player movement until another is ticked.
- **Camera**: a new camera starts with `isActive` off, so it doesn't cut the view. Set it to `true`
  to switch to it (see [Only one camera renders](Camera.md#only-one-camera-renders)). Removing the
  active camera leaves nothing to draw the level with until another is ticked.
- **Audio Source**: `playOnStart` has nothing to wait for, so a new source never starts by itself.
  Call `Play()`, or set `looping` with a `soundFileName`.

## Several components of one type

Every component except Transform and UI Transform can be added more than once: two Audio Sources
so footsteps don't cut off the voice, a Sprite plus a shadow Sprite, several Colliders making one
shape.

- **Order.** An entity's components of one type have an order. `entity.audioSource` and the other
  component properties return the **first** one; `entity:GetComponents(Component.AudioSource)`
  returns all of them in order. In the inspector each one has its own card. Drag a card onto
  another of the same type to reorder them. The order is saved with the level.
- **Offsets.** Sprite, Model, Collider and Audio Source have an `offset`: a position relative to the
  [Transform](Transform.md), turned with the entity's rotation. Use it to place several of them
  at different spots on one entity.
- **References.** A `public AudioSource door` field (and the other component fields) points at one
  exact component, so it keeps pointing at the right one when others are added, removed or
  reordered. See [public fields](Script.md#public-fields).
- **What duplicates do:**

| Component | With several on one entity |
|---|---|
| Sprite, Model | Each is drawn, at its own offset. They all use the Transform's scale. |
| Audio Source | Each plays its own sound at its own offset, so they don't cut each other off. |
| Collider | Together they are one compound shape. Each sits at its offset and pushes the whole entity. Collision and trigger callbacks still fire once per pair of entities. See [Collider](Collider.md#several-on-one-entity). |
| Rigidbody | Only the **first** one simulates. The others are kept but do nothing. |
| Camera, Player Controller | Still only one active in the whole level, counting each component: ticking one unticks every other, including the ones on the same entity. |
| UI Sprite, UI Text | Each is drawn in the UI Transform's rectangle. UI components have no offset. |

```lua
-- Scripts/Robot.lua: a voice that footsteps can't interrupt.
local feet, voice

function Start()
    local sources = entity:GetComponents(Component.AudioSource)
    feet, voice = sources[1], sources[2]
    voice.offset = Vector3(0, 12, 0) -- at head height
end

function Speak()
    voice.soundFileName = "Sounds/beep"
    voice:Play()
end
```

```lua
-- Scripts/Thrower.lua (on the player): F throws a ball with a sprite and a trigger collider.
public number speed = 300

local balls = {}

function Update()
    if Input.GetKeyDown(Key.F) then
        local ball = Game.CreateEntity()
        ball.name = "Ball"
        ball.transform.position = entity.transform.position

        ball:AddComponent(Component.Sprite).northTextureFileName = "Textures/ball.png"

        local collider = ball:AddComponent(Component.Collider)
        collider.isTrigger = true
        collider.scale = Vector3(8, 8, 8)

        table.insert(balls, ball)
    end

    for _, ball in ipairs(balls) do
        if ball.isValid then
            ball.transform.position = ball.transform.position + entity.camera.forward * speed * GameTime.deltaTime
        end
    end
end
```

## Examples

### Follow another entity

```lua
-- Scripts/Follow.lua
public Entity target = nil
public number speed = 30
public number stopDistance = 16

function Update()
    if target == nil or not target.isValid then return end

    local here = entity.transform.position
    local there = target.transform.position
    local toTarget = Vector3(there.x - here.x, 0, there.z - here.z)

    if toTarget.length <= stopDistance then return end

    local step = toTarget.normalized * math.min(speed * GameTime.deltaTime, toTarget.length - stopDistance)
    entity.transform.position = here + step
end
```

### Count everything with a tag

```lua
-- Scripts/EnemyCounter.lua
local enemies = {}

function Start()
    enemies = Game.FindEntitiesWithTag("enemy")
end

function Update()
    local alive = 0
    for _, e in ipairs(enemies) do
        if e.isValid then alive = alive + 1 end
    end

    if alive == 0 then
        Debug.Print("All enemies defeated!")
        entity.enabled = false -- stop checking
    end
end
```

### Hide and show an entity

`enabled` doesn't hide anything, so change the parts you can see and touch. A sprite's colour
alpha doesn't hide it either (only transparent pixels in the texture are cut out), but a sprite or
model with a scale of 0 isn't drawn:

```lua
-- Scripts/Hideable.lua (entity with a Sprite or Model, and optionally a Collider)
local shownScale

function Start()
    shownScale = entity.transform.scale
end

function SetShown(self, shown)
    if shown then
        entity.transform.scale = shownScale
    else
        entity.transform.scale = Vector3(0, 0, 0)
    end

    if entity.collider ~= nil then entity.collider.isActive = shown end
end
```

Other scripts call it with `thing:GetScript("Hideable"):SetShown(false)`.

### Pick up whatever touches you

```lua
-- Scripts/Coin.lua (entity with a trigger Collider)
function OnTriggerEnter(other)
    if not other.hasPlayerController then return end

    Global.coins = (Global.coins or 0) + 1
    Debug.Print("Coins:", Global.coins)
    entity:Destroy()
end
```
