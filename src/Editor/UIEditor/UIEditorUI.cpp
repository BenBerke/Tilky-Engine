//
// Created by berke on 5/16/2026.
//
#include "imgui.h"
// ASSUMPTION: this is the standard Dear ImGui "misc/cpp/imgui_stdlib.h" add-on,
// which supplies `ImGui::InputText`/`InputTextMultiline` overloads that bind
// directly to a std::string. The task description asserts the project
// "already" has such an integration - adjust the path/name below if yours
// lives somewhere else.
#include "misc/cpp/imgui_stdlib.h"

#include "src/Editor/EditorInternal.hpp"
#include "Headers/Editor/ImGuiDrawFunctions.hpp"
#include "Headers/Engine/InputManager.hpp"
#include "Headers/Engine/Local/Local.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Objects/Entity.hpp"
#include "Headers/Project/ProjectManager.hpp"
// ASSUMPTION: wherever ComponentUIText / ComponentUISprite / ComponentUITransform
// actually live in your project (e.g. Components.hpp). Adjust this path -
// it wasn't part of the files provided as reference.
#include "Headers/Objects/Components.hpp"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

/// This script is responsible for drawing the ImGUI of the UI Editor.
namespace {
    using namespace Localisation;
    using namespace MapEditorInternal;

    void Spacing(const int count = 1) {
        for (int i = 0; i < count; ++i) ImGui::Spacing();
    }

    // ---------------------------------------------------------------------
    // Style helpers - for panels OUTSIDE the entity/component editor only
    // (Hierarchy, Canvas View, Actions, Asset Browser). See the file
    // comment above for why the entity/component editor uses
    // ImGuiDrawFunctions:: instead of these.
    // ---------------------------------------------------------------------

    void SectionHeader(const char* label) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.75f, 1.00f, 1.00f));
        ImGui::TextUnformatted(">>");
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::TextUnformatted(label);
    }

    void HoverTooltip(const char* text) {
        if (text == nullptr || text[0] == '\0') return;

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22.0f);
            ImGui::TextUnformatted(text);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }

    bool FullWidthButton(const char* label) {
        return ImGui::Button(label, ImVec2(ImGui::GetContentRegionAvail().x, 0.0f));
    }

    void PushAccentStyle() {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.36f, 0.62f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.46f, 0.78f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.13f, 0.28f, 0.52f, 1.00f));
    }

    void PopAccentStyle() {
        ImGui::PopStyleColor(3);
    }

    // Local "dirty" flag, scoped to the UI Editor. MapEditorUI.cpp's own
    // hasUnsavedChanges is anonymous-namespace-local to that file (and it is
    // reference material we must not touch), so there is no shared/global
    // dirty-state symbol this file can reach.
    bool uiHasUnsavedChanges = false;

    // Centres the canvas pan on the actual current screen once, the first
    // time the UI Editor is opened - screenWidth/screenHeight aren't known
    // at static init time. After that, pan is fully user-controlled (drag
    // to pan, "Center" button to reset).
    bool uiCanvasCentered = false;

    // Hierarchy search filter (mirrors hierarchySearchBuf in MapEditorUI.cpp).
    char uiHierarchySearchBuf[64] = "";

    // ---------------------------------------------------------------------
    // UI component identity - the small, fixed set (Transform always
    // present, Text/Sprite optional) that plays the role your
    // TILKY_NORMAL_COMPONENTS X-macro / CMP_* enum plays for the general
    // ECS. Not building an X-macro/generic-dispatch system for 3 fixed
    // types - that would be inventing infrastructure you didn't ask for.
    // ---------------------------------------------------------------------

    enum class UIComponentType { Transform, Text, Sprite };

    const char* UIComponentDisplayNameKey(const UIComponentType type) {
        switch (type) {
            case UIComponentType::Transform: return "editor.ui.transform.title";
            case UIComponentType::Text: return "editor.ui.text.title";
            case UIComponentType::Sprite: return "editor.ui.sprite.title";
        }
        return "";
    }

    bool UIEntityHasComponent(Entity& entity, const UIComponentType type) {
        switch (type) {
            case UIComponentType::Transform: return entity.HasComponent<ComponentUITransform>();
            case UIComponentType::Text: return entity.HasComponent<ComponentUIText>();
            case UIComponentType::Sprite: return entity.HasComponent<ComponentUISprite>();
        }
        return false;
    }

    // Mirrors ImGuiDrawFunctions::EntityInspectorState - same two fields
    // that matter for the flow (selectedComponent/editingComponent), plus
    // the add-component combo's own transient state.
    struct UIEntityInspectorState {
        int selectedComponent = -1;
        bool editingComponent = false;
        bool addingComponent = false;
        int componentToAdd = 0;
    };

    UIEntityInspectorState uiEntityInspectorState;

    void ResetUIInspectorState() {
        uiEntityInspectorState = {};
    }

    // Detects a selection change (from ANY source - hierarchy click, canvas
    // click in UIEditorInput.cpp, entity creation, deletion) so the
    // component-editor state resets when switching entities, without every
    // call site that can change selectedUIEntityID needing to know about
    // this file's inspector state.
    ID lastKnownSelectedUIEntityID = INVALID_ID;

    // ---------------------------------------------------------------------
    // Anchor / pivot presets
    // ---------------------------------------------------------------------

    enum class UIAnchorPreset {
        TopLeft, TopCenter, TopRight,
        MiddleLeft, Center, MiddleRight,
        BottomLeft, BottomCenter, BottomRight,
        HorizontalStretch, VerticalStretch, FullStretch
    };

    Vector2 UIAnchorPresetMin(const UIAnchorPreset preset) {
        switch (preset) {
            case UIAnchorPreset::TopLeft: return {0.0f, 0.0f};
            case UIAnchorPreset::TopCenter: return {0.5f, 0.0f};
            case UIAnchorPreset::TopRight: return {1.0f, 0.0f};
            case UIAnchorPreset::MiddleLeft: return {0.0f, 0.5f};
            case UIAnchorPreset::Center: return {0.5f, 0.5f};
            case UIAnchorPreset::MiddleRight: return {1.0f, 0.5f};
            case UIAnchorPreset::BottomLeft: return {0.0f, 1.0f};
            case UIAnchorPreset::BottomCenter: return {0.5f, 1.0f};
            case UIAnchorPreset::BottomRight: return {1.0f, 1.0f};
            case UIAnchorPreset::HorizontalStretch: return {0.0f, 0.5f};
            case UIAnchorPreset::VerticalStretch: return {0.5f, 0.0f};
            case UIAnchorPreset::FullStretch: return {0.0f, 0.0f};
        }
        return {0.5f, 0.5f};
    }

    Vector2 UIAnchorPresetMax(const UIAnchorPreset preset) {
        switch (preset) {
            case UIAnchorPreset::TopLeft: return {0.0f, 0.0f};
            case UIAnchorPreset::TopCenter: return {0.5f, 0.0f};
            case UIAnchorPreset::TopRight: return {1.0f, 0.0f};
            case UIAnchorPreset::MiddleLeft: return {0.0f, 0.5f};
            case UIAnchorPreset::Center: return {0.5f, 0.5f};
            case UIAnchorPreset::MiddleRight: return {1.0f, 0.5f};
            case UIAnchorPreset::BottomLeft: return {0.0f, 1.0f};
            case UIAnchorPreset::BottomCenter: return {0.5f, 1.0f};
            case UIAnchorPreset::BottomRight: return {1.0f, 1.0f};
            case UIAnchorPreset::HorizontalStretch: return {1.0f, 0.5f};
            case UIAnchorPreset::VerticalStretch: return {0.5f, 1.0f};
            case UIAnchorPreset::FullStretch: return {1.0f, 1.0f};
        }
        return {0.5f, 0.5f};
    }

    const char* UIAnchorPresetLabelKey(const UIAnchorPreset preset) {
        switch (preset) {
            case UIAnchorPreset::TopLeft: return "editor.ui.anchor.top_left";
            case UIAnchorPreset::TopCenter: return "editor.ui.anchor.top_center";
            case UIAnchorPreset::TopRight: return "editor.ui.anchor.top_right";
            case UIAnchorPreset::MiddleLeft: return "editor.ui.anchor.middle_left";
            case UIAnchorPreset::Center: return "editor.ui.anchor.center";
            case UIAnchorPreset::MiddleRight: return "editor.ui.anchor.middle_right";
            case UIAnchorPreset::BottomLeft: return "editor.ui.anchor.bottom_left";
            case UIAnchorPreset::BottomCenter: return "editor.ui.anchor.bottom_center";
            case UIAnchorPreset::BottomRight: return "editor.ui.anchor.bottom_right";
            case UIAnchorPreset::HorizontalStretch: return "editor.ui.anchor.horizontal_stretch";
            case UIAnchorPreset::VerticalStretch: return "editor.ui.anchor.vertical_stretch";
            case UIAnchorPreset::FullStretch: return "editor.ui.anchor.full_stretch";
        }
        return "editor.ui.anchor.center";
    }

    void ApplyAnchorPreset(ComponentUITransform& transform, const UIAnchorPreset preset) {
        // Every preset above already satisfies min <= max on both axes, so
        // no extra clamping is required here.
        transform.anchorMin = UIAnchorPresetMin(preset);
        transform.anchorMax = UIAnchorPresetMax(preset);
    }

    void DrawAnchorPresetIcon(ImDrawList* drawList, const ImVec2 center, const float size,
                               const Vector2 min, const Vector2 max) {
        const ImVec2 boxMin(center.x - size * 0.5f, center.y - size * 0.5f);
        const ImVec2 boxMax(center.x + size * 0.5f, center.y + size * 0.5f);
        drawList->AddRect(boxMin, boxMax, IM_COL32(130, 130, 138, 255));

        const ImVec2 markMin(boxMin.x + min.x * size, boxMin.y + min.y * size);
        const ImVec2 markMax(boxMin.x + max.x * size, boxMin.y + max.y * size);

        if (min.x == max.x && min.y == max.y) {
            drawList->AddCircleFilled(markMin, 3.0f, IM_COL32(95, 175, 255, 255));
        } else {
            drawList->AddRectFilled(markMin, markMax, IM_COL32(95, 175, 255, 110));
            drawList->AddRect(markMin, markMax, IM_COL32(95, 175, 255, 255));
        }
    }

    bool AnchorPresetIconButton(const UIAnchorPreset preset) {
        constexpr float buttonSize = 34.0f;

        ImGui::PushID(static_cast<int>(preset));
        const bool clicked = ImGui::Button("##UIAnchorPresetBtn", ImVec2(buttonSize, buttonSize));
        const ImVec2 rectMin = ImGui::GetItemRectMin();
        const ImVec2 rectMax = ImGui::GetItemRectMax();

        DrawAnchorPresetIcon(
            ImGui::GetWindowDrawList(),
            ImVec2((rectMin.x + rectMax.x) * 0.5f, (rectMin.y + rectMax.y) * 0.5f),
            buttonSize - 10.0f,
            UIAnchorPresetMin(preset),
            UIAnchorPresetMax(preset)
        );

        // Component-editor context now, so Tooltip (ImGuiDrawFunctions::),
        // not this file's own HoverTooltip.
        ImGuiDrawFunctions::Tooltip(Get(UIAnchorPresetLabelKey(preset)).c_str());
        ImGui::PopID();
        return clicked;
    }

    void DrawAnchorPresetPopup(ComponentUITransform& transform, bool& changed) {
        const std::string label = Get("editor.ui.transform.anchor_presets") + "##AnchorPresetButton";

        if (ImGui::Button(label.c_str())) ImGui::OpenPopup("##UIAnchorPresetPopup");

        if (ImGui::BeginPopup("##UIAnchorPresetPopup")) {
            ImGuiDrawFunctions::SmallMetaText("%s", Get("editor.ui.transform.anchor_presets_hint").c_str());
            Spacing();

            constexpr UIAnchorPreset grid[3][3] = {
                {UIAnchorPreset::TopLeft, UIAnchorPreset::TopCenter, UIAnchorPreset::TopRight},
                {UIAnchorPreset::MiddleLeft, UIAnchorPreset::Center, UIAnchorPreset::MiddleRight},
                {UIAnchorPreset::BottomLeft, UIAnchorPreset::BottomCenter, UIAnchorPreset::BottomRight},
            };

            for (const auto& row : grid) {
                for (int col = 0; col < 3; ++col) {
                    if (col > 0) ImGui::SameLine();
                    if (AnchorPresetIconButton(row[col])) {
                        ApplyAnchorPreset(transform, row[col]);
                        changed = true;
                        ImGui::CloseCurrentPopup();
                    }
                }
            }

            Spacing(2);
            ImGui::Separator();
            Spacing();

            if (AnchorPresetIconButton(UIAnchorPreset::HorizontalStretch)) {
                ApplyAnchorPreset(transform, UIAnchorPreset::HorizontalStretch);
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (AnchorPresetIconButton(UIAnchorPreset::VerticalStretch)) {
                ApplyAnchorPreset(transform, UIAnchorPreset::VerticalStretch);
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (AnchorPresetIconButton(UIAnchorPreset::FullStretch)) {
                ApplyAnchorPreset(transform, UIAnchorPreset::FullStretch);
                changed = true;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    void DrawPivotPresetPopup(ComponentUITransform& transform, bool& changed) {
        const std::string label = Get("editor.ui.transform.pivot_presets") + "##PivotPresetButton";

        if (ImGui::Button(label.c_str())) ImGui::OpenPopup("##UIPivotPresetPopup");

        if (ImGui::BeginPopup("##UIPivotPresetPopup")) {
            // `const`, not `constexpr` - we don't have Vector2's exact
            // definition and don't want to assume it's a literal type.
            const Vector2 grid[3][3] = {
                {{0.0f, 0.0f}, {0.5f, 0.0f}, {1.0f, 0.0f}},
                {{0.0f, 0.5f}, {0.5f, 0.5f}, {1.0f, 0.5f}},
                {{0.0f, 1.0f}, {0.5f, 1.0f}, {1.0f, 1.0f}},
            };

            for (int row = 0; row < 3; ++row) {
                for (int col = 0; col < 3; ++col) {
                    if (col > 0) ImGui::SameLine();
                    ImGui::PushID(row * 3 + col);
                    const bool clicked = ImGui::Button("##UIPivotPresetBtn", ImVec2(28.0f, 28.0f));
                    const ImVec2 rectMin = ImGui::GetItemRectMin();
                    const ImVec2 rectMax = ImGui::GetItemRectMax();
                    DrawAnchorPresetIcon(
                        ImGui::GetWindowDrawList(),
                        ImVec2((rectMin.x + rectMax.x) * 0.5f, (rectMin.y + rectMax.y) * 0.5f),
                        20.0f, grid[row][col], grid[row][col]
                    );
                    ImGui::PopID();

                    if (clicked) {
                        transform.pivot = grid[row][col];
                        changed = true;
                        ImGui::CloseCurrentPopup();
                    }
                }
            }

            ImGui::EndPopup();
        }
    }

    // Small interactive pivot diagram: a square representing the element's
    // own bounds, with a draggable dot for the pivot point (0..1 each axis).
    void DrawPivotDiagram(ComponentUITransform& transform, bool& changed) {
        constexpr float size = 64.0f;

        ImGui::InvisibleButton("##UIPivotDiagram", ImVec2(size, size));
        const ImVec2 boxMin = ImGui::GetItemRectMin();
        const ImVec2 boxMax = ImGui::GetItemRectMax();

        if (ImGui::IsItemActive()) {
            const ImVec2 mouse = ImGui::GetMousePos();
            const float px = std::clamp((mouse.x - boxMin.x) / size, 0.0f, 1.0f);
            const float py = std::clamp((mouse.y - boxMin.y) / size, 0.0f, 1.0f);

            if (px != transform.pivot.x || py != transform.pivot.y) {
                transform.pivot = {px, py};
                changed = true;
            }
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(boxMin, boxMax, IM_COL32(34, 34, 40, 255));
        drawList->AddRect(boxMin, boxMax, IM_COL32(110, 110, 120, 255));
        drawList->AddLine(ImVec2(boxMin.x + size * 0.5f, boxMin.y), ImVec2(boxMin.x + size * 0.5f, boxMax.y),
                           IM_COL32(68, 68, 76, 255));
        drawList->AddLine(ImVec2(boxMin.x, boxMin.y + size * 0.5f), ImVec2(boxMax.x, boxMin.y + size * 0.5f),
                           IM_COL32(68, 68, 76, 255));

        const ImVec2 dot(boxMin.x + transform.pivot.x * size, boxMin.y + transform.pivot.y * size);
        drawList->AddCircleFilled(dot, 5.0f, IM_COL32(90, 220, 255, 255));
        drawList->AddCircle(dot, 5.0f, IM_COL32(20, 20, 20, 255), 0, 1.5f);

        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGuiDrawFunctions::SmallMetaText("%s", Get("editor.ui.transform.pivot_diagram_hint").c_str());
        ImGui::Text("%.3f, %.3f", transform.pivot.x, transform.pivot.y);
        ImGui::EndGroup();
    }

    // ---------------------------------------------------------------------
    // Component field editors - called from inside DrawUIComponentEditor()'s
    // window, so they use ImGuiDrawFunctions::BeginSection/EndSection per
    // logical field group (matching how the real DrawComponentEditor breaks
    // Transform into separate "Position"/"Rotation"/"Scale" sections rather
    // than one big block), FieldWidth() before fields, and Tooltip() for
    // hints - not this file's own SectionHeader/HoverTooltip, which are for
    // the non-entity panels only (see the file comment at the top).
    // ---------------------------------------------------------------------

    void DrawUITransformInspector(ComponentUITransform& transform) {
        bool changed = false;
        constexpr float fieldWidth = 170.0f;

        ImGuiDrawFunctions::BeginSection("Anchors");

        ImGuiDrawFunctions::FieldWidth(fieldWidth);
        if (ImGui::DragFloat2(Get("editor.ui.transform.anchor_min").c_str(), &transform.anchorMin.x, 0.001f, 0.0f, 1.0f, "%.3f"))
            changed = true;
        ImGuiDrawFunctions::Tooltip(Get("editor.ui.transform.tooltip_anchor_min").c_str());

        ImGuiDrawFunctions::FieldWidth(fieldWidth);
        if (ImGui::DragFloat2(Get("editor.ui.transform.anchor_max").c_str(), &transform.anchorMax.x, 0.001f, 0.0f, 1.0f, "%.3f"))
            changed = true;
        ImGuiDrawFunctions::Tooltip(Get("editor.ui.transform.tooltip_anchor_max").c_str());

        // Clamp to [0,1] and enforce min <= max per axis, per spec.
        transform.anchorMin.x = std::clamp(transform.anchorMin.x, 0.0f, 1.0f);
        transform.anchorMin.y = std::clamp(transform.anchorMin.y, 0.0f, 1.0f);
        transform.anchorMax.x = std::clamp(transform.anchorMax.x, 0.0f, 1.0f);
        transform.anchorMax.y = std::clamp(transform.anchorMax.y, 0.0f, 1.0f);
        if (transform.anchorMax.x < transform.anchorMin.x) transform.anchorMax.x = transform.anchorMin.x;
        if (transform.anchorMax.y < transform.anchorMin.y) transform.anchorMax.y = transform.anchorMin.y;

        DrawAnchorPresetPopup(transform, changed);
        ImGui::SameLine();
        if (ImGui::Button((Get("editor.ui.transform.reset") + "##ResetUIAnchors").c_str())) {
            transform.anchorMin = {0.5f, 0.5f};
            transform.anchorMax = {0.5f, 0.5f};
            changed = true;
        }

        ImGuiDrawFunctions::EndSection();

        ImGuiDrawFunctions::BeginSection("Pivot");

        ImGuiDrawFunctions::FieldWidth(fieldWidth);
        if (ImGui::DragFloat2(Get("editor.ui.transform.pivot_xy").c_str(), &transform.pivot.x,
                               0.001f, 0.0f, 1.0f, "%.3f"))
            changed = true;
        transform.pivot.x = std::clamp(transform.pivot.x, 0.0f, 1.0f);
        transform.pivot.y = std::clamp(transform.pivot.y, 0.0f, 1.0f);

        DrawPivotPresetPopup(transform, changed);
        ImGui::SameLine();
        if (ImGui::Button((Get("editor.ui.transform.reset") + "##ResetUIPivot").c_str())) {
            transform.pivot = {0.5f, 0.5f};
            changed = true;
        }

        Spacing();
        DrawPivotDiagram(transform, changed);

        ImGuiDrawFunctions::EndSection();

        ImGuiDrawFunctions::BeginSection("Position");
        ImGuiDrawFunctions::FieldWidth(fieldWidth);
        if (ImGui::DragFloat2(Get("editor.ui.transform.position").c_str(), &transform.position.x,
                               1.0f, 0.0f, 0.0f, "%.1f"))
            changed = true;
        ImGuiDrawFunctions::Tooltip(Get("editor.ui.transform.tooltip_position").c_str());
        ImGui::SameLine();
        if (ImGui::Button((Get("editor.ui.transform.reset") + "##ResetUIPos").c_str())) {
            transform.position = {0.0f, 0.0f};
            changed = true;
        }
        ImGuiDrawFunctions::EndSection();

        ImGuiDrawFunctions::BeginSection("Scale");
        // Deliberately unbounded (v_min = v_max = 0.0f means "no limit" in
        // ImGui) - negative scale must stay possible, per spec.
        ImGuiDrawFunctions::FieldWidth(fieldWidth);
        if (ImGui::DragFloat2(Get("editor.ui.transform.scale").c_str(), &transform.scale.x, 0.01f, 0.0f, 0.0f, "%.3f"))
            changed = true;
        ImGuiDrawFunctions::Tooltip(Get("editor.ui.transform.tooltip_scale").c_str());
        ImGui::SameLine();
        if (ImGui::Button((Get("editor.ui.transform.reset") + "##ResetUIScale").c_str())) {
            transform.scale = {1.0f, 1.0f};
            changed = true;
        }
        ImGuiDrawFunctions::EndSection();

        ImGuiDrawFunctions::BeginSection("Rotation");
        // Unbounded for the same reason - rotation must be free to go past +-90.
        ImGuiDrawFunctions::FieldWidth(fieldWidth * 0.6f);
        if (ImGui::DragFloat(Get("editor.ui.transform.rotation").c_str(), &transform.rotation,
                              1.0f, 0.0f, 0.0f, "%.2f deg"))
            changed = true;
        ImGuiDrawFunctions::Tooltip(Get("editor.ui.transform.tooltip_rotation").c_str());
        ImGui::SameLine();
        if (ImGui::Button((Get("editor.ui.transform.reset") + "##ResetUIRot").c_str())) {
            transform.rotation = 0.0f;
            changed = true;
        }
        ImGuiDrawFunctions::EndSection();

        Spacing();
        if (FullWidthButton(Get("editor.ui.transform.reset_all").c_str())) {
            const ID owner = transform.ownerID; // preserve ownership across a full reset
            transform = ComponentUITransform{};
            transform.ownerID = owner;
            changed = true;
        }

        ImGuiDrawFunctions::BeginSection("Resolved Layout");
        ImGui::BeginDisabled();
        ImGui::Text("%s: %.3f, %.3f", Get("editor.ui.transform.resolved_position").c_str(),
                    transform.resolvedPosition.x, transform.resolvedPosition.y);
        ImGui::Text("%s: %.3f, %.3f", Get("editor.ui.transform.resolved_size").c_str(),
                    transform.resolvedSize.x, transform.resolvedSize.y);
        ImGui::EndDisabled();
        ImGuiDrawFunctions::Tooltip(Get("editor.ui.transform.tooltip_resolved").c_str());
        ImGuiDrawFunctions::EndSection();

        if (changed) uiHasUnsavedChanges = true;
    }

    void DrawUITextInspector(ComponentUIText& text) {
        ImGuiDrawFunctions::BeginSection("Text");

        ImGuiDrawFunctions::SmallMetaText("%s: %d", Get("editor.ui.text.char_count").c_str(),
                                           static_cast<int>(text.text.size()));

        const ImVec2 editorMin = ImGui::GetCursorScreenPos();
        const bool changed = ImGui::InputTextMultiline(
            "##UITextEditor",
            &text.text,
            ImVec2(-FLT_MIN, ImGui::GetTextLineHeightWithSpacing() * 6.0f),
            ImGuiInputTextFlags_AllowTabInput
        );

        if (text.text.empty() && !ImGui::IsItemActive()) {
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(editorMin.x + 6.0f, editorMin.y + 4.0f),
                IM_COL32(140, 140, 148, 255),
                Get("editor.ui.text.hint").c_str()
            );
        }

        Spacing();
        if (ImGui::SmallButton(Get("editor.ui.text.clear").c_str())) text.text.clear();

        ImGuiDrawFunctions::EndSection();

        if (changed) uiHasUnsavedChanges = true;
    }

    void DrawUISpriteInspector(ComponentUISprite& sprite) {
        ImGuiDrawFunctions::BeginSection("Preview");

        // ---- Preview, checkerboard backdrop so alpha is visible ----
        constexpr float previewSize = 96.0f;
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        constexpr float cell = 8.0f;
        drawList->PushClipRect(cursor, ImVec2(cursor.x + previewSize, cursor.y + previewSize), true);
        for (float y = 0.0f; y < previewSize; y += cell) {
            for (float x = 0.0f; x < previewSize; x += cell) {
                const bool dark = (static_cast<int>(x / cell) + static_cast<int>(y / cell)) % 2 == 0;
                drawList->AddRectFilled(
                    ImVec2(cursor.x + x, cursor.y + y),
                    ImVec2(cursor.x + x + cell, cursor.y + y + cell),
                    dark ? IM_COL32(58, 58, 64, 255) : IM_COL32(78, 78, 84, 255)
                );
            }
        }

        SDL_Texture* preview = sprite.texture.empty() ? nullptr : GetEditorTexture(sprite.texture);

        if (preview != nullptr) {
            // ASSUMPTION: SDL3's SDL_GetTextureSize(texture, &w, &h). If your
            // SDL3 revision exposes this differently (e.g. reading w/h fields
            // directly off SDL_Texture), swap this call for that.
            float texW = 0.0f, texH = 0.0f;
            SDL_GetTextureSize(preview, &texW, &texH);

            float drawW = previewSize, drawH = previewSize;
            if (texW > 0.0f && texH > 0.0f) {
                const float aspect = texW / texH;
                if (aspect >= 1.0f) drawH = previewSize / aspect;
                else drawW = previewSize * aspect;
            }

            ImGui::SetCursorScreenPos(ImVec2(cursor.x + (previewSize - drawW) * 0.5f,
                                              cursor.y + (previewSize - drawH) * 0.5f));
            ImGui::Image(reinterpret_cast<ImTextureID>(preview), ImVec2(drawW, drawH));
        } else if (!sprite.texture.empty()) {
            // Assigned but not resolvable -> missing-texture feedback.
            const auto warn = "!";
            const ImVec2 warnSize = ImGui::CalcTextSize(warn);
            drawList->AddText(ImVec2(cursor.x + (previewSize - warnSize.x) * 0.5f,
                                      cursor.y + (previewSize - warnSize.y) * 0.5f),
                               IM_COL32(255, 140, 90, 255), warn);
        }

        drawList->PopClipRect();
        drawList->AddRect(cursor, ImVec2(cursor.x + previewSize, cursor.y + previewSize), IM_COL32(120, 120, 128, 255));
        ImGui::SetCursorScreenPos(ImVec2(cursor.x, cursor.y + previewSize));

        ImGuiDrawFunctions::EndSection();

        ImGuiDrawFunctions::BeginSection("Texture");

        bool changed = false;
        if (DrawAssetField(Get("editor.ui.sprite.texture").c_str(), sprite.texture, AssetKind::Texture, 32.0f))
            changed = true;

        if (!sprite.texture.empty() && preview == nullptr) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.35f, 1.0f));
            ImGui::TextWrapped("%s", Get("editor.ui.sprite.missing_texture").c_str());
            ImGui::PopStyleColor();
        }

        ImGuiDrawFunctions::SmallMetaText("%s: %s", Get("editor.ui.sprite.path").c_str(),
                                           sprite.texture.empty() ? Get("editor.ui.none").c_str() : sprite.texture.c_str());

        ImGuiDrawFunctions::EndSection();

        if (changed) uiHasUnsavedChanges = true;
    }

    // ---------------------------------------------------------------------
    // Component list card + Add Component - port of the lambda inside the
    // real DrawEntityEditor() and its "Add Component" block.
    // ---------------------------------------------------------------------

    void DrawUIComponentCard(const char* label, UIEntityInspectorState& state, const UIComponentType type) {
        ImGui::PushID(static_cast<int>(type));

        const ImVec2 rowMin = ImGui::GetCursorScreenPos();
        const float rowW = ImGui::GetContentRegionAvail().x;
        const float rowH = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().FramePadding.y * 2.0f + 2.0f;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(rowMin, ImVec2(rowMin.x + rowW, rowMin.y + rowH), IM_COL32(40, 50, 65, 130), 3.0f);

        ImGui::Spacing();
        ImGui::Indent(6.0f);
        ImGui::TextUnformatted(label);
        ImGui::SameLine(rowW - 56.0f);

        if (ImGui::SmallButton("Edit")) {
            state.selectedComponent = static_cast<int>(type);
            state.editingComponent = true;
        }

        ImGui::Unindent(6.0f);
        ImGui::Spacing();
        ImGui::PopID();
    }

    void DrawUIComponentsSection(Entity& entity, UIEntityInspectorState& state) {
        ImGuiDrawFunctions::BeginSection("Components");

        if (UIEntityHasComponent(entity, UIComponentType::Transform))
            DrawUIComponentCard(Get(UIComponentDisplayNameKey(UIComponentType::Transform)).c_str(),
                                 state, UIComponentType::Transform);
        if (UIEntityHasComponent(entity, UIComponentType::Text))
            DrawUIComponentCard(Get(UIComponentDisplayNameKey(UIComponentType::Text)).c_str(),
                                 state, UIComponentType::Text);
        if (UIEntityHasComponent(entity, UIComponentType::Sprite))
            DrawUIComponentCard(Get(UIComponentDisplayNameKey(UIComponentType::Sprite)).c_str(),
                                 state, UIComponentType::Sprite);

        ImGuiDrawFunctions::EndSection();
    }

    void DrawUIAddComponentSection(Entity& entity, UIEntityInspectorState& state) {
        ImGuiDrawFunctions::BeginSection("Add Component");

        if (ImGui::Button(state.addingComponent ? "Cancel" : Get("entity.add_component").c_str()))
            state.addingComponent = !state.addingComponent;

        if (state.addingComponent) {
            ImGui::PushID("ui_add_component_combo");

            // Transform is never offered here, matching how CMP_TRANSFORM
            // is skipped in the real Add Component combo.
            std::vector<UIComponentType> addable;
            if (!UIEntityHasComponent(entity, UIComponentType::Text)) addable.push_back(UIComponentType::Text);
            if (!UIEntityHasComponent(entity, UIComponentType::Sprite)) addable.push_back(UIComponentType::Sprite);

            if (addable.empty())
                ImGuiDrawFunctions::SmallMetaText("%s", Get("editor.ui.component.all_added").c_str());
            else {
                if (state.componentToAdd < 0 || state.componentToAdd >= static_cast<int>(addable.size()))
                    state.componentToAdd = 0;

                std::vector<std::string> names;
                for (const UIComponentType type : addable) names.push_back(Get(UIComponentDisplayNameKey(type)));

                ImGuiDrawFunctions::FieldWidth(200.0f);
                if (ImGui::BeginCombo(Get("component.component").c_str(), names[state.componentToAdd].c_str())) {
                    for (int i = 0; i < static_cast<int>(addable.size()); ++i) {
                        const bool isSelected = state.componentToAdd == i;
                        if (ImGui::Selectable(names[i].c_str(), isSelected)) state.componentToAdd = i;
                        if (isSelected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                if (ImGui::Button(Get("common.add").c_str())) {
                    if (addable[state.componentToAdd] == UIComponentType::Text) entity.AddComponent<ComponentUIText>();
                    else if (addable[state.componentToAdd] == UIComponentType::Sprite) entity.AddComponent<ComponentUISprite>();
                    state.addingComponent = false;
                    state.componentToAdd = 0;
                    uiHasUnsavedChanges = true;
                }
            }

            ImGui::PopID();
        }

        ImGuiDrawFunctions::EndSection();
    }

    // ---------------------------------------------------------------------
    // Entity editor - port of ImGuiDrawFunctions::DrawEntityEditor(). Its
    // own window (Get("editor.ui.entity.title")), not inline in the main
    // panel - only DrawSelectedUIEntityInspector() (which just resolves the
    // live Entity* and calls this) is what's called inline, mirroring
    // exactly how DrawSelectedEntityInspector() calls DrawEntityEditor().
    // ---------------------------------------------------------------------

    bool DrawUIEntityEditor(Entity& entity, UIEntityInspectorState& state, bool* open) {
        bool deleteRequested = false;

        ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_FirstUseEver);
        // ASSUMPTION: kInspectorFlags (used for both entity/component windows
        // in your real code) is file-local to ImGuiDrawFunctions.cpp and not
        // reachable here - using default flags (none) as the safe fallback.
        if (!ImGui::Begin(Get("editor.ui.entity.title").c_str(), open)) {
            ImGui::End();
            return false;
        }

        {
            char hdr[128];
            std::snprintf(hdr, sizeof(hdr), "%s (#%u)",
                          entity.name.empty() ? "unnamed" : entity.name.c_str(),
                          static_cast<unsigned>(entity.id));
            ImGuiDrawFunctions::DrawInspectorHeader("Entity", hdr);
        }

        ImGuiDrawFunctions::BeginSection("Identity");
        ImGuiDrawFunctions::SmallMetaText("ID: %u", static_cast<unsigned>(entity.id));
        ImGuiDrawFunctions::FieldWidth(220.0f);
        ImGui::InputText(Get("entity.name").c_str(), &entity.name);
        ImGuiDrawFunctions::EndSection();

        DrawUIComponentsSection(entity, state);
        DrawUIAddComponentSection(entity, state);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::PushID("ui_entity_buttons");

        if (ImGuiDrawFunctions::DangerButton(Get("common.delete").c_str()) ||
            InputManager::GetKeyDown(SDL_SCANCODE_DELETE)) {
            deleteRequested = true;
            state.editingComponent = false;
            state.selectedComponent = -1;
            if (open) *open = false;
        }
        ImGuiDrawFunctions::Tooltip(Get("editor.tooltip.entity.common.delete").c_str());

        ImGui::SameLine();
        if (ImGui::Button(Get("common.close").c_str())) {
            state.editingComponent = false;
            state.selectedComponent = -1;
            if (open) *open = false;
        }

        ImGui::PopID();

        ImGui::End();
        return deleteRequested;
    }

    // ---------------------------------------------------------------------
    // Component editor - port of ImGuiDrawFunctions::DrawComponentEditor().
    // Own window too (componentName + a fixed ID suffix, mirroring
    // "##component_editor" - one shared window slot whose title/content
    // swap with state.selectedComponent, not one window per type).
    // ---------------------------------------------------------------------

    void DrawUIComponentEditor(Entity& entity, UIEntityInspectorState& state) {
        if (state.selectedComponent == -1) {
            state.editingComponent = false;
            return;
        }

        const auto componentType = static_cast<UIComponentType>(state.selectedComponent);
        const std::string componentName = Get(UIComponentDisplayNameKey(componentType));
        const std::string windowTitle = componentName + "##ui_component_editor";

        bool windowOpen = true;

        ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(windowTitle.c_str(), &windowOpen)) {
            ImGui::End();
            if (!windowOpen) {
                state.editingComponent = false;
                state.selectedComponent = -1;
            }
            return;
        }

        ImGui::PushID(state.selectedComponent);

        bool closeRequested = false;

        ImGuiDrawFunctions::DrawInspectorHeader("Component", componentName.c_str());

        if (componentType == UIComponentType::Transform) {
            if (auto* transform = entity.GetComponent<ComponentUITransform>()) {
                DrawUITransformInspector(*transform);
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                if (ImGuiDrawFunctions::DangerButton(Get("common.delete").c_str())) {
                    entity.RemoveComponent<ComponentUITransform>();
                    uiHasUnsavedChanges = true;
                    closeRequested = true;
                }
            }
            else ImGui::TextDisabled("Transform component missing");
        }
        else if (componentType == UIComponentType::Text) {
            if (auto* text = entity.GetComponent<ComponentUIText>()) {
                DrawUITextInspector(*text);
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                if (ImGuiDrawFunctions::DangerButton(Get("common.delete").c_str())) {
                    entity.RemoveComponent<ComponentUIText>();
                    uiHasUnsavedChanges = true;
                    closeRequested = true;
                }
            }
            else ImGui::TextDisabled("Text component missing");
        }
        else if (componentType == UIComponentType::Sprite) {
            if (ComponentUISprite* sprite = entity.GetComponent<ComponentUISprite>()) {
                DrawUISpriteInspector(*sprite);
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                if (ImGuiDrawFunctions::DangerButton(Get("common.delete").c_str())) {
                    entity.RemoveComponent<ComponentUISprite>();
                    uiHasUnsavedChanges = true;
                    closeRequested = true;
                }
            } else {
                ImGui::TextDisabled("Sprite component missing");
            }
        }

        if (!closeRequested) {
            ImGui::SameLine();
            if (ImGui::Button(Get("common.close").c_str())) closeRequested = true;
        }

        if (closeRequested || !windowOpen) {
            state.editingComponent = false;
            state.selectedComponent = -1;
        }

        ImGui::PopID();
        ImGui::End();
    }

    // ---------------------------------------------------------------------
    // Gate function - called inline from the main panel (mirrors
    // DrawSelectedEntityInspector()'s call site exactly: resolve the live
    // Entity*, hand off to the entity editor, then conditionally the
    // component editor). Draws nothing itself.
    // ---------------------------------------------------------------------

    void DrawSelectedUIEntityInspector(Level& level) {
        if (selectedUIEntityID == INVALID_ID) return;

        Entity* entity = level.GetEntity(selectedUIEntityID);
        if (entity == nullptr) {
            selectedUIEntityID = INVALID_ID;
            ResetUIInspectorState();
            return;
        }

        bool windowOpen = true;
        const bool deleteRequested = DrawUIEntityEditor(*entity, uiEntityInspectorState, &windowOpen);

        if (deleteRequested) {
            const ID idToDelete = entity->id;
            selectedUIEntityID = INVALID_ID;
            ResetUIInspectorState();
            level.DestroyEntity(idToDelete);
            uiHasUnsavedChanges = true;
            return;
        }

        if (!windowOpen) {
            selectedUIEntityID = INVALID_ID;
            ResetUIInspectorState();
            return;
        }

        if (uiEntityInspectorState.editingComponent && uiEntityInspectorState.selectedComponent != -1)
            DrawUIComponentEditor(*entity, uiEntityInspectorState);
    }

    // ---------------------------------------------------------------------
    // Canvas view controls - an inline toolbar section in the main panel
    // (same role as DrawMode()'s block in DrawEditorUI()), not a separate
    // window. Actual canvas content (checkerboard/grid/entities/gizmos)
    // is rendered full-screen by UIEditorDraw(), the same way
    // DrawGridDots()/DrawWalls()/DrawEntities() render the Map Editor's view.
    // ---------------------------------------------------------------------

    void DrawUICanvasViewControls() {
        if (ImGui::Button("-##UICanvasZoomOut"))
            uiCanvasZoom = std::clamp(uiCanvasZoom / 1.15f, MIN_UI_CANVAS_ZOOM, MAX_UI_CANVAS_ZOOM);
        ImGui::SameLine();
        ImGui::Text("%d%%", static_cast<int>(uiCanvasZoom * 100.0f + 0.5f));
        ImGui::SameLine();
        if (ImGui::Button("+##UICanvasZoomIn"))
            uiCanvasZoom = std::clamp(uiCanvasZoom * 1.15f, MIN_UI_CANVAS_ZOOM, MAX_UI_CANVAS_ZOOM);

        ImGui::SameLine(0.0f, 12.0f);
        if (ImGui::Button(Get("editor.ui.canvas.center").c_str())) {
            uiCanvasPan = {static_cast<float>(screenWidth) * 0.5f, static_cast<float>(screenHeight) * 0.5f};
            uiCanvasZoom = 1.0f;
        }
        HoverTooltip(Get("editor.ui.canvas.tooltip_center").c_str());

        Spacing();

        ImGui::Checkbox(Get("editor.ui.canvas.grid").c_str(), &showUIGrid);
        ImGui::SameLine();
        ImGui::Checkbox(Get("editor.ui.canvas.center_lines").c_str(), &showUICenterLines);
        ImGui::SameLine();
        ImGui::Checkbox(Get("editor.ui.canvas.safe_area").c_str(), &showUISafeArea);

        // Not user-editable: UI_vs.glsl resolves uPosition/uSize straight
        // against uScreenSize (the actual current screen), with no separate
        // design/reference resolution - so the canvas always previews at
        // whatever the real screen size is, read-only here.
        ImGuiDrawFunctions::SmallMetaText("%s: %d x %d", Get("editor.ui.canvas.previewing_at").c_str(),
                                           screenWidth, screenHeight);
    }

    // ---------------------------------------------------------------------
    // Hierarchy panel - its own window, matching DrawHierarchyPanel()'s
    // treatment exactly (search filter included).
    // ---------------------------------------------------------------------

    void DrawUIHierarchyPanel(Level& level) {
        ImGui::Begin(Get("editor.ui.hierarchy").c_str());

        PushAccentStyle();
        const bool addRequested = FullWidthButton(Get("editor.ui.hierarchy.add_entity").c_str());
        PopAccentStyle();
        HoverTooltip(Get("editor.ui.hierarchy.tooltip_add_entity").c_str());

        if (addRequested) {
            const ID newEntityID = level.CreateEntity(true);
            selectedUIEntityID = newEntityID;
            uiHasUnsavedChanges = true;
        }

        Spacing();

        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 28.0f);
        ImGui::InputText("##UIHierarchySearch", uiHierarchySearchBuf, sizeof(uiHierarchySearchBuf));
        HoverTooltip(Get("editor.ui.hierarchy.tooltip_filter").c_str());

        ImGui::SameLine(0.0f, 4.0f);
        if (ImGui::SmallButton("x")) uiHierarchySearchBuf[0] = '\0';
        HoverTooltip(Get("editor.ui.hierarchy.tooltip_clear_filter").c_str());

        const std::string searchLower = [&] {
            std::string s = uiHierarchySearchBuf;
            std::ranges::transform(s, s.begin(), [](const unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return s;
        }();
        const bool filtering = !searchLower.empty();

        auto matches = [&](const std::string& label) -> bool {
            if (!filtering) return true;
            std::string lower = label;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](const unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return lower.find(searchLower) != std::string::npos;
        };

        Spacing();
        ImGui::Separator();
        Spacing();

        int uiEntityCount = 0;
        for (Entity& entity : level.entities) if (entity.GetComponent<ComponentUITransform>() != nullptr) ++uiEntityCount;

        if (uiEntityCount == 0) {
            Spacing(2);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            ImGui::TextWrapped("%s", Get("editor.ui.hierarchy.empty").c_str());
            ImGui::PopStyleColor();
            ImGui::End();
            return;
        }

        ID entityPendingDelete = INVALID_ID;

        for (Entity& entity : level.entities) {
            const auto* uiTransform = entity.GetComponent<ComponentUITransform>();
            if (uiTransform == nullptr) continue;

            const std::string label = entity.name.empty()
            ? (Get("editor.ui.hierarchy.unnamed") + " #" + std::to_string(entity.id)) : entity.name;
            if (!matches(label)) continue;

            ImGui::PushID(static_cast<int>(entity.id));

            const auto* entityText = entity.GetComponent<ComponentUIText>();
            const auto* entitySprite = entity.GetComponent<ComponentUISprite>();
            const bool hasText = entityText != nullptr && !entityText->text.empty();
            const bool hasSprite = entitySprite != nullptr && !entitySprite->texture.empty();

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.85f, 1.00f, 1.00f));
            ImGui::TextUnformatted(hasText && hasSprite ? "[T+S]" : hasText ? "[T]" : hasSprite ? "[S]" : "[.]");
            ImGui::PopStyleColor();
            ImGui::SameLine(0.0f, 6.0f);

            const bool selected = selectedUIEntityID == entity.id;
            if (ImGui::Selectable(label.c_str(), selected))
                selectedUIEntityID = entity.id;

            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem(Get("editor.delete").c_str())) entityPendingDelete = entity.id;
                ImGui::EndPopup();
            }

            ImGui::PopID();
        }

        if (entityPendingDelete != INVALID_ID) {
            if (selectedUIEntityID == entityPendingDelete) selectedUIEntityID = INVALID_ID;
            level.DestroyEntity(entityPendingDelete);
            uiHasUnsavedChanges = true;
        }

        ImGui::End();
    }

    // ---------------------------------------------------------------------
    // Asset Browser panel - MapEditorUI.cpp's own DrawAssetBrowserPanel()
    // is anonymous-namespace-local there and only ever gets called from
    // DrawEditorUI(), so it never renders while in the UI Editor. This
    // mirrors that function's body closely enough to behave the same,
    // using the SAME window title/localisation key and the SAME shared
    // `assetBrowser`/`assetBrowserInitialized` externs, so it's the same
    // persistent window either editor draws it from, not a second one.
    // ---------------------------------------------------------------------

    void DrawUIAssetBrowserPanel() {
        if (!assetBrowserInitialized) {
            assetBrowser.SetRootDirectory(ProjectManager::GetAssetsPath());
            assetBrowserInitialized = true;
        }

        ImGui::Begin(Get("editor.asset_browser").c_str());

        if (ImGui::Button(Get("editor.asset_browser.refresh").c_str()))
            assetBrowser.Refresh();
        HoverTooltip(Get("editor.tooltip.asset_browser_refresh").c_str());

        ImGui::SameLine();
        ImGui::TextDisabled("%s", Get("editor.asset_browser.hint").c_str());

        Spacing();

        assetBrowser.Draw([](const std::string& textureFileName) -> ImTextureID {
            SDL_Texture* texture = GetEditorTexture(textureFileName);
            return texture != nullptr ? reinterpret_cast<ImTextureID>(texture) : ImTextureID{};
        });

        ImGui::End();

        assetBrowser.DrawTextEditorWindow(scriptEditorFont);
    }

    // ---------------------------------------------------------------------
    // Dockspace
    // ---------------------------------------------------------------------
    void DrawUIEditorDockSpace() {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();

        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);

        constexpr ImGuiWindowFlags flags =
                ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        ImGui::Begin("##UIEditorDockspaceHost", nullptr, flags);
        ImGui::PopStyleVar(3);

        const ImGuiID dockspaceID = ImGui::GetID("UIEditorDockspace");
        ImGui::DockSpace(dockspaceID, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

        ImGui::End();
    }
}

namespace MapEditorInternal {
    using namespace Localisation;

    // uiCanvasPan is the canvas-space point at the centre of the screen,
    // exactly like cameraPos is for WorldToScreen/ScreenToWorld - the UI
    // canvas is a full-screen view, not a bounded child panel.
    Vector2 UICanvasToScreen(const Vector2& canvasPos) {
        return {
            (canvasPos.x - uiCanvasPan.x) * uiCanvasZoom + static_cast<float>(screenWidth) * 0.5f,
            (canvasPos.y - uiCanvasPan.y) * uiCanvasZoom + static_cast<float>(screenHeight) * 0.5f
        };
    }

    Vector2 ScreenToUICanvas(const Vector2& screenPos) {
        return {
            (screenPos.x - static_cast<float>(screenWidth) * 0.5f) / uiCanvasZoom + uiCanvasPan.x,
            (screenPos.y - static_cast<float>(screenHeight) * 0.5f) / uiCanvasZoom + uiCanvasPan.y
        };
    }

    void DrawUIEditorUI() {
        DrawUIEditorDockSpace();

        Level& level = LevelManager::CurrentLevel();

        if (!uiCanvasCentered && screenWidth > 0 && screenHeight > 0) {
            uiCanvasPan = {static_cast<float>(screenWidth) * 0.5f, static_cast<float>(screenHeight) * 0.5f};
            uiCanvasCentered = true;
        }

        // Reset component-editor state on any selection change, regardless
        // of which file caused it (hierarchy click here, canvas click in
        // UIEditorInput.cpp, creation, deletion).
        if (selectedUIEntityID != lastKnownSelectedUIEntityID) {
            ResetUIInspectorState();
            lastKnownSelectedUIEntityID = selectedUIEntityID;
        }

        std::string panelTitle = Get("editor.ui.title");
        if (uiHasUnsavedChanges) panelTitle += "  *";

        ImGui::Begin(panelTitle.c_str());

        // ---- Canvas view (mirrors the "Mode" toolbar section) ----
        SectionHeader(Get("editor.ui.canvas_view").c_str());
        Spacing();
        DrawUICanvasViewControls();

        Spacing();
        ImGui::Separator();
        Spacing();

        // ---- Inspector (mirrors DrawEditorUI()'s own inline call exactly -
        // no header, just separator/spacing around it either side) ----
        DrawSelectedUIEntityInspector(level);

        Spacing();
        ImGui::Separator();
        Spacing();

        // ---- Actions ----
        SectionHeader(Get("editor.ui.actions").c_str());
        Spacing();

        PushAccentStyle();
        if (FullWidthButton(Get("editor.save").c_str())) if (Save(Editor::currentMap)) uiHasUnsavedChanges = false;
        PopAccentStyle();
        HoverTooltip(Get("editor.tooltip.save").c_str());

        Spacing();

        if (FullWidthButton(Get("editor.ui.back_to_map_editor").c_str())) currentState = STATE_MAP;

        ImGui::End();

        // ---- Other panels ----
        DrawUIHierarchyPanel(level);
        DrawUIAssetBrowserPanel();
    }
}

// namespace Editor