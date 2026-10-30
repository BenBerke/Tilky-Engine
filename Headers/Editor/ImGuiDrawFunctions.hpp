//
// Created by berke on 6/2/2026.
//

#ifndef TILKY_ENGINE_IMGUIDRAWFUNCTIONS_HPP
#define TILKY_ENGINE_IMGUIDRAWFUNCTIONS_HPP

#include <functional>
#include <string>
#include <vector>

#include "Headers/Objects/Level.hpp"
#include "Headers/Objects/Entity.hpp"
#include "Headers/Objects/Sector.hpp"
#include "Headers/Objects/Wall.hpp"
#include "Headers/Objects/Components.hpp"

// ImGuiDrawFunctions.hpp
// Shared inspector/menu drawing functions for Map Editor and Runtime Editor.

namespace ImGuiDrawFunctions {

    struct EntityInspectorState {
        // For inspectors that aren't the level's (the prefab editor): the
        // entity window's title (with its own "###id") and no Delete.
        std::string windowTitle;
        bool allowDelete = true;
        // A Snap to Floor button, for inspectors of a level's entities. It
        // only sets snapToFloorRequested; the caller does the snapping and
        // clears it.
        bool allowSnapToFloor = true;
        bool snapToFloorRequested = false;

        // Snap to Wall (Headers/Editor/WallSnap.hpp), shown with Snap to Floor.
        // The caller fills in wallSnapStatus and wallSnapGap every frame
        // before drawing; the controls only set the requests below.
        enum class WallSnapStatus { Off, Picking, Snapped };
        bool allowSnapToWall = true;
        WallSnapStatus wallSnapStatus = WallSnapStatus::Off;
        float wallSnapGap = 0.0f;
        bool wallSnapButtonRequested = false; // Snap to Wall, Cancel or Disable Snap, by status
        bool switchSidesRequested = false;
        bool wallSnapGapChanged = false;       // wallSnapGap holds the new value
        // Drawn at the top of the entity window, e.g. the prefab editor's Save row.
        std::function<void()> drawExtras;
    };

    // ── Spacing utility ───────────────────────────────────────────────────────
    void PutSpace(int n);

    // ── Inspector windows ─────────────────────────────────────────────────────
    // Returns true when the caller should delete the object.
    // `open`      – pointer to the bool controlling window visibility (may be null)
    // `draggable` – use DragFloat widgets instead of InputFloat
    // The header shows the object's own stable ID (sector.id / wall.id) -
    // the same number the hierarchy shows - never its vector index.

    // A non-null copyRequested adds a Copy button to the header; it is set to
    // true on the frame the button is pressed.
    // Make Door, picked on one of the sector's floors with its settings.
    // The caller makes the door (MapEditorInternal::MakeDoor).
    struct MakeDoorRequest {
        int floorIndex = -1; // -1: nothing picked
        float lip = 4.0f;
        float wait = 3.0f;
    };

    // A non-null windowId puts the window under that ImGui ID ("Title###id"),
    // so inspectors of different kinds can share one window - the Map
    // Editor's single inspector. Null keeps each kind in its own window.
    bool DrawSectorEditor   (Sector &sector, bool *open, bool draggable, bool *copyRequested = nullptr,
                             MakeDoorRequest *makeDoorRequest = nullptr, const char *windowId = nullptr);
    // A non-null openSectorRequest adds buttons for the wall's sectors under
    // the header; it is set to the pressed one's ID on that frame.
    bool DrawWallEditor     (Wall   &wall,   bool *open, bool draggable, const char *windowId = nullptr,
                             ID *openSectorRequest = nullptr);
    bool DrawLinedefEditor  (Linedef &linedef, bool *open, bool draggable, const char *windowId = nullptr);
    // The level's own Events and Dispatches sections (World Settings).
    void DrawLevelEventSections(Level &level, bool draggable);
    // The entity window: every component is a foldout section in it.
    bool DrawEntityEditor(Entity &entity, EntityInspectorState &state, bool *open, bool draggable);

    // The fields of one component (`instanceID` is a ScriptInstanceID for
    // scripts), without a header - what a foldout shows when open. Call
    // between BeginPrefabFields/EndPrefabFields for that component.
    void DrawComponentFields(Entity &entity, int componentType, std::uint64_t instanceID, bool draggable);

    // One component's foldout header, Unity style: the arrow, an Enabled
    // checkbox when `enabled` is set, the label, and a menu button (also on
    // right-click) with Move Up / Move Down, the prefab Revert / Apply items
    // and Remove. Open or closed is remembered per component type in the
    // user settings. The caller acts on what the result asks for.
    struct ComponentHeader {
        std::string label;
        int componentType = -1;        // CMP_*
        std::uint64_t instanceID = 0;
        bool *enabled = nullptr;
        const char *enabledTooltip = nullptr;
        bool canMoveUp = false;
        bool canMoveDown = false;
        bool canRemove = true;
        // Runs right after the header item, for drag and drop onto it.
        std::function<void()> onHeaderItem;
    };

    struct ComponentHeaderResult {
        bool open = false;
        bool enabledChanged = false;
        bool removeRequested = false;
        bool moveUpRequested = false;
        bool moveDownRequested = false;
    };

    ComponentHeaderResult DrawComponentHeader(const Entity &entity, const ComponentHeader &header);
    void SmallMetaText(const char* fmt, ...);
    void Tooltip(const char *text);
    void BeginSection(const char *label);
    void FieldWidth(float width = 200.0f);
    void EndSection();
    bool DangerButton(const char *label);
    void DrawInspectorHeader(const char *kind, const char *detail = nullptr);

    // The 8 directional texture slots, laid out as a compass for 90/45-degree
    // sprites. Shared by the Sprite inspector and the flipbook editor.
    void DrawDirectionalTextureSlots(std::array<std::string, 8>& textures, SideCount sideCount);

    // ── Prefab instance marks ─────────────────────────────────────────────────
    // Between BeginPrefabFields and EndPrefabFields, each PrefabField scope
    // wraps one field's widgets. When the entity is a prefab instance and
    // overrides that field, the field is tinted with a bar in the margin;
    // right-clicking it offers Revert / Apply to Prefab. Does nothing for
    // other entities. componentType -1 = the entity's own fields (enabled,
    // tags); field names are level.bson's keys ("color", "publicValues/speed").
    void BeginPrefabFields(Entity &entity, int componentType, std::uint64_t instanceID);
    void EndPrefabFields(); // also runs the Revert/Apply picked, now that the fields are drawn

    class PrefabField {
    public:
        explicit PrefabField(std::string fieldName);
        ~PrefabField();
        PrefabField(const PrefabField&) = delete;
        PrefabField& operator=(const PrefabField&) = delete;
    private:
        std::string field;
        bool active = false;
        bool overridden = false;
        float startY = 0.0f;
    };

    // The "Prefab: <file>" bar with Open / Apply All / Revert All / Unpack
    // and the removed components, at the top of an instance's entity window.
    void DrawPrefabInstanceBar(Entity &entity);

    // Component headers on an instance: "+ " before added components'
    // labels and a margin bar on headers with overrides (DrawPrefabCardMarks
    // goes right after the header item). Their Revert / Apply items are in
    // the header's menu.
    std::string PrefabCardLabel(const Entity &entity, int componentType, std::uint64_t instanceID, const std::string &label);
    void DrawPrefabCardMarks(const Entity &entity, int componentType, std::uint64_t instanceID);

    // Runs a prefab action picked in the entity window (bar or cards). Call
    // once the window's component lists are drawn.
    void FlushPrefabActions();

    // ── Runtime-only HUD overlay ──────────────────────────────────────────────
    // Compact keybind hint shown only in Runtime Editor.
    // Call once per frame after all other ImGui windows.
    void DrawRuntimeHUD();

    // ── Toast notification ────────────────────────────────────────────────────
    // One short message at the bottom centre of the window for `duration`
    // seconds; a new one replaces it. Call DrawNotification once per frame,
    // after the other windows.
    void ShowNotification(const char *msg, bool isError = false, float duration = 3.0f);
    void DrawNotification(float dt);
    // Same, but centred horizontally with its top edge at `topY` (screen
    // pixels) instead of at the bottom of the window.
    void DrawNotificationBelow(float dt, float topY);

    // An undo history: `undoLabels` oldest first, `redoLabels` in the order
    // they would be redone, `initialLabel` for the state before them all.
    // Newest at the top, the current state highlighted and the redo steps
    // greyed. Returns how many undo steps the clicked row has done
    // (0 for the initial state), or -1 when nothing was clicked.
    int DrawHistoryList(const std::vector<std::string>& undoLabels,
                        const std::vector<std::string>& redoLabels,
                        const std::string& initialLabel);

    // ── Mouse/keyboard focus control ──────────────────────────────────────────
    // focus=true  → allow mouse input into ImGui
    // focus=false → disable mouse from ImGui (game has it)
    void SetImGuiFocus(bool focus);

} // namespace ImGuiDrawFunctions

#endif //TILKY_ENGINE_IMGUIDRAWFUNCTIONS_HPP