# UI Button

**Inspector name:** Button · **Lua:** `entity.uiButton` · **Public field type:** not available

A Button makes a whole UI entity clickable: menu buttons, pause screens, inventory slots. Its click
area is the rectangle of the entity's [UI Transform](UITransform.md), rotation included. It can run
[events](#events) and the scripts on the entity get [callbacks](#callbacks).

## How it works

- **The cursor has to be free.** Buttons only react while the cursor is unlocked and visible
  (`Cursor.lockState` not `Locked` and `Cursor.visible` true, see [Input](Input.md)). While the
  cursor is locked or hidden, buttons are skipped completely. Any hover or press they had is
  dropped without events.
- **The topmost button wins.** When buttons overlap, only the one drawn on top reacts. It also
  blocks the ones under it, even if it isn't interactable. Text is drawn over images, images follow
  their [Draw Order](UISprite.md), and otherwise later components are over earlier ones, the same
  order the UI Editor shows.
- **A click happens when the left mouse button goes down** on the button. The release comes when
  the button is let go, wherever the cursor is by then.
- **Buttons work while the game is paused** (`GameTime.timeScale` 0, for example after
  `Game.Pause()`). The delays and cooldowns of the dispatches a button event runs count real
  seconds, so they still run while paused.
- **Interactable** off: no events or callbacks, and the button shows its **Disabled** look.
- A disabled entity's button does nothing and doesn't block clicks.
- Only the entity's **first** Button does anything. The inspector says so on any others.

## States

Each state has a **Tint** and a **Texture**. They change the entity's **first**
[Image](UISprite.md):

| State | When |
|---|---|
| **Normal** | Nothing is happening. |
| **Hover** | The cursor is over the button. |
| **Pressed** | The button was clicked and the mouse button is still down, with the cursor still over it. |
| **Disabled** | **Interactable** is off. |

- **Tint** is multiplied with the Image's own **Color**, RGBA `0..1`. White (`1, 1, 1, 1`)
  leaves the Image as it is.
- **Texture**, when set, is drawn instead of the Image's own texture. Empty = the Image's
  texture.
- Without an Image, the button still works but nothing shows its state.
- The UI Editor shows the Normal (or Disabled) look.

Defaults: Normal `1, 1, 1, 1`, Hover `0.9, 0.9, 0.9, 1`, Pressed `0.7, 0.7, 0.7, 1`, Disabled
`0.7, 0.7, 0.7, 0.5`.

## In the editor

In the **UI Editor**, pick **Button** under **Add Component**.

| Field | Lua | Notes |
|---|---|---|
| **Interactable** | `interactable` | On by default. Off = no events or callbacks, Disabled look. |
| **Normal / Hover / Pressed / Disabled: Tint** | `normalColor`, `hoverColor`, `pressedColor`, `disabledColor` | RGBA, `0..1`. |
| **Normal / Hover / Pressed / Disabled: Texture** | `normalTexture`, `hoverTexture`, `pressedTexture`, `disabledTexture` | Image path relative to `Assets`, with the extension. Empty = the Image's own texture. |

The UI Editor's entity window also has **Events** and **Dispatches** sections, the same as the
entity inspector in the map editor.

## Events

A UI entity with a Button can have these events, on top of the usual entity ones:

| Event | Fires when |
|---|---|
| **Button Click** | The left mouse button goes down on the button. |
| **Button Release** | The left mouse button is let go after a click on it. |
| **Button Hover Enter** | The cursor comes onto the button. |
| **Button Hover Exit** | The cursor leaves it, or another button comes on top of it there. |

- The **instigator** is the player: the first enabled entity with the **Player** tag. The tag
  filter checks its tags, so "only if the player has the RedKey tag" works. The filter is set to
  **All** by default.
- Conditions, max triggers and chance work as on any event.

Dispatches that are useful for menus:

| Dispatch | Does |
|---|---|
| **Enable Entity** / **Disable Entity** | Shows or hides the entity it is on. Put it on a menu panel to open or close it. |
| **Pause Game** | Like `Game.Pause()`: game time stops and the cursor is freed. |
| **Resume Game** | `GameTime.timeScale` back to 1, and the cursor back to the level's World Settings (**Lock Cursor** / **Hide Cursor on Level Start**). |
| **Quit Game** | Like `Game.Quit()`: the exported game closes; in the editor, Play stops. |
| **Set UI Text** | Replaces the text of the entity's first [Text](UIText.md). |
| **Load Level**, **Play Sound**, **Change Variable**, **Lua Signal**, ... | As everywhere else. |

Pause, Resume and Quit can go on any object, not only on UI entities.

### Example: a pause menu's Resume button

1. Make a **Pause Panel** UI entity with an Image.
2. Inside the panel's area, make a **Resume** UI entity with an Image, a Text and a Button.
3. On **Resume**, add a **Button Click** event linked to the event tag `close_pause`.
4. On **Pause Panel** and on **Resume**, add a **Disable Entity** dispatch linked to
   `close_pause`. UI entities aren't nested, so hiding the panel doesn't hide the button.
5. In **World Settings**, add a **Resume Game** dispatch linked to `close_pause`. On the level it
   always runs; on an entity that is being disabled by the same click it could be skipped.

Closing the menu needs no script. Hiding it at the start and opening it with a key does:

```lua
-- Scripts/UI/PauseKey.lua (on any entity)
public Entity pausePanel = nil
public Entity resumeButton = nil

function Start()
    pausePanel.enabled = false
    resumeButton.enabled = false
end

function Update()
    if Input.GetKeyDown(Key.Escape) and not pausePanel.enabled then
        pausePanel.enabled = true
        resumeButton.enabled = true
        Game.Pause()
    end
end
```

## Callbacks

Scripts on the button's entity can define these. They take no arguments. See
[Callback Functions](CallbackFunctions.md#onclick--onrelease--onhoverenter--onhoverexit).

| Callback | When |
|---|---|
| `OnClick()` | The left mouse button goes down on the button. |
| `OnRelease()` | The left mouse button is let go after a click on it. |
| `OnHoverEnter()` | The cursor comes onto the button. |
| `OnHoverExit()` | The cursor leaves it. |

## Scripting

| Property | Type | | Description |
|---|---|---|---|
| `isValid` | boolean | read-only | `false` if the entity or its Button is gone. |
| `interactable` | boolean | read/write | `false` = no events or callbacks, Disabled look. |
| `isHovered` | boolean | read-only | The cursor is over it, and it is the topmost button there. |
| `isPressed` | boolean | read-only | Clicked, and the left mouse button isn't let go yet. |
| `normalColor`, `hoverColor`, `pressedColor`, `disabledColor` | Vector4 | read/write | RGBA, each `0..1`. A colour read from them is a copy, so change it and assign it back. |
| `normalTexture`, `hoverTexture`, `pressedTexture`, `disabledTexture` | string | read/write | Image paths. `""` = the Image's own texture. |

A change shows on the Image straight away.

```lua
-- Scripts/UI/BuyButton.lua (UI entity with an Image, a Text and a Button)
public int price = 50

function Update()
    entity.uiButton.interactable = (Global.coins or 0) >= price
end

function OnClick()
    Global.coins = Global.coins - price
    Audio.Play2D("Sounds/UI/buy.wav")
end
```

```lua
-- Scripts/UI/HoverLabel.lua (UI entity with a Button and a Text)
function OnHoverEnter() entity.uiText.text = "> Start <" end
function OnHoverExit() entity.uiText.text = "Start" end

function OnClick()
    Game.LoadLevel("E1M1")
end
```
