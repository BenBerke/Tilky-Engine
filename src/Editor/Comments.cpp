#include "Comments.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <string>

#include <imgui.h>
#include "misc/cpp/imgui_stdlib.h"

#include "EditorInternal.hpp"
#include "EventLinks.hpp"
#include "Headers/Engine/InputManager.hpp"
#include "Headers/Engine/Local/Local.hpp"
#include "Headers/Map/MapQueries.hpp"

namespace Comments {
    bool showIcons = true;

    namespace {
        // A pin floats this far above the floor under it in 3D.
        constexpr float PIN_LIFT = 8.0f;

        constexpr float ICON_HALF_SIZE = 7.0f;
        constexpr ImU32 ICON_FILL = IM_COL32(250, 210, 70, 235);
        constexpr ImU32 ICON_PIN_FILL = IM_COL32(120, 200, 255, 235);
        constexpr ImU32 ICON_OUTLINE = IM_COL32(40, 30, 10, 255);

        // Set by Add, cleared once the panel has focused that comment.
        ID focusCommentID = INVALID_ID;

        // The comment whose text field is being typed into, with the
        // comments as they were before, so one typing session is one step.
        ID editingCommentID = INVALID_ID;
        std::optional<MapEditorInternal::CommentsSnapshot> editBefore;

        const char* TargetKey(const EditorCommentTarget target) {
            switch (target) {
                case EditorCommentTarget::PIN: return "comments.target.pin";
                case EditorCommentTarget::WALL: return "tag_explorer.kind.wall";
                case EditorCommentTarget::SECTOR: return "tag_explorer.kind.sector";
                case EditorCommentTarget::LINEDEF: return "tag_explorer.kind.linedef";
                case EditorCommentTarget::ENTITY: return "tag_explorer.kind.entity";
            }
            return "comments.target.pin";
        }

        std::string CommentLabel(const Level& level, const EditorComment& comment) {
            if (comment.target == EditorCommentTarget::PIN) {
                char buffer[96];
                std::snprintf(buffer, sizeof(buffer), "%s (%.1f, %.1f)", Localisation::Get(TargetKey(comment.target)).c_str(),
                              comment.position.x, comment.position.y);
                return buffer;
            }

            return EventLinks::OwnerLabel(level, OwnerKindFor(comment.target), comment.targetID);
        }
    }

    EditorCommentTarget TargetFor(const EventOwnerKind kind) {
        switch (kind) {
            case EventOwnerKind::LINE: return EditorCommentTarget::WALL;
            case EventOwnerKind::SECTOR: return EditorCommentTarget::SECTOR;
            case EventOwnerKind::LINEDEF: return EditorCommentTarget::LINEDEF;
            case EventOwnerKind::ENTITY: return EditorCommentTarget::ENTITY;
            case EventOwnerKind::LEVEL: break;
        }
        return EditorCommentTarget::PIN;
    }

    EventOwnerKind OwnerKindFor(const EditorCommentTarget target) {
        switch (target) {
            case EditorCommentTarget::WALL: return EventOwnerKind::LINE;
            case EditorCommentTarget::SECTOR: return EventOwnerKind::SECTOR;
            case EditorCommentTarget::LINEDEF: return EventOwnerKind::LINEDEF;
            case EditorCommentTarget::ENTITY: return EventOwnerKind::ENTITY;
            case EditorCommentTarget::PIN: break;
        }
        return EventOwnerKind::LEVEL;
    }

    bool Anchor(const Level& level, const EditorComment& comment, Vector3& point) {
        if (comment.target != EditorCommentTarget::PIN)
            return EventLinks::AnchorPoint(level, OwnerKindFor(comment.target), comment.targetID, point);

        float floor = 0.0f;
        const int sectorIndex = MapQueries::FindSectorContainingPoint(level.sectors, comment.position);

        if (sectorIndex >= 0 && !level.sectors[sectorIndex].floors.empty()) {
            const Sector& sector = level.sectors[sectorIndex];
            floor = MapQueries::SurfaceHeightAt(sector, sector.floors.front().floor, comment.position);
        }

        point = {comment.position.x, floor + PIN_LIFT, comment.position.y};
        return true;
    }

    ID Add(Level& level, const EditorCommentTarget target, const ID targetID, const Vector2& position) {
        MapEditorInternal::CommentsSnapshot before = MapEditorInternal::CaptureCommentsSnapshot();

        EditorComment comment;
        comment.id = level.nextCommentID++;
        comment.target = target;
        comment.targetID = target == EditorCommentTarget::PIN ? INVALID_ID : targetID;
        comment.position = position;

        level.comments.push_back(comment);

        MapEditorInternal::PushCommentsUndo(std::move(before), "comments.history.add");
        MapEditorInternal::MarkUnsavedChanges();

        focusCommentID = comment.id;
        return comment.id;
    }

    void DrawIcons(const Level& level, const Projector& project, const bool mouseOnView) {
        if (!showIcons || level.comments.empty()) return;

        ImDrawList* drawList = ImGui::GetBackgroundDrawList();
        const Vector2 mouse = InputManager::GetMousePosition();
        const EditorComment* hovered = nullptr;

        for (const EditorComment& comment : level.comments) {
            Vector3 anchor;
            Vector2 screen;
            if (!Anchor(level, comment, anchor) || !project(anchor, screen)) continue;

            // A small note: a filled square with two text lines, blue for pins.
            const ImVec2 min = {screen.x - ICON_HALF_SIZE, screen.y - ICON_HALF_SIZE};
            const ImVec2 max = {screen.x + ICON_HALF_SIZE, screen.y + ICON_HALF_SIZE};

            drawList->AddRectFilled(min, max, comment.target == EditorCommentTarget::PIN ? ICON_PIN_FILL : ICON_FILL, 2.0f);
            drawList->AddRect(min, max, ICON_OUTLINE, 2.0f);
            drawList->AddLine({min.x + 3.0f, screen.y - 2.0f}, {max.x - 3.0f, screen.y - 2.0f}, ICON_OUTLINE);
            drawList->AddLine({min.x + 3.0f, screen.y + 2.0f}, {max.x - 5.0f, screen.y + 2.0f}, ICON_OUTLINE);

            if (mouseOnView && std::abs(mouse.x - screen.x) <= ICON_HALF_SIZE && std::abs(mouse.y - screen.y) <= ICON_HALF_SIZE)
                hovered = &comment;
        }

        if (hovered != nullptr) {
            ImGui::BeginTooltip();
            ImGui::TextDisabled("%s", CommentLabel(level, *hovered).c_str());
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22.0f);
            ImGui::TextUnformatted(hovered->text.empty() ? Localisation::Get("comments.empty").c_str() : hovered->text.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }

    void DrawPanel(Level& level, const char* windowName, bool* open, const PickCallback& onPick) {
        ImGui::SetNextWindowSize(ImVec2(340.0f, 420.0f), ImGuiCond_FirstUseEver);

        if (focusCommentID != INVALID_ID) ImGui::SetNextWindowFocus();

        if (!ImGui::Begin(windowName, open)) {
            ImGui::End();
            return;
        }

        if (ImGui::Checkbox(Localisation::Get("comments.show_icons").c_str(), &showIcons)) MapEditorInternal::SaveUserSettings();

        ImGui::TextDisabled("%s", MapEditorInternal::FormatKeyBinds(Localisation::Get("comments.hint")).c_str());
        ImGui::Separator();

        if (level.comments.empty()) ImGui::TextDisabled("%s", Localisation::Get("comments.none").c_str());

        ID deleteID = INVALID_ID;

        for (EditorComment& comment : level.comments) {
            ImGui::PushID(static_cast<int>(comment.id));

            if (ImGui::Selectable(CommentLabel(level, comment).c_str()) && onPick) onPick(comment);

            if (comment.target == EditorCommentTarget::PIN) {
                // Moving a pin by typing is one step per drag.
                static MapEditorInternal::CommentsSnapshot dragBefore;

                ImGui::SetNextItemWidth(-1.0f);
                const bool changed = ImGui::DragFloat2("##Position", &comment.position.x, 0.25f, 0.0f, 0.0f, "%.1f");
                if (ImGui::IsItemActivated()) dragBefore = MapEditorInternal::CaptureCommentsSnapshot();
                if (changed) MapEditorInternal::MarkUnsavedChanges();
                if (ImGui::IsItemDeactivatedAfterEdit()) MapEditorInternal::PushCommentsUndo(std::move(dragBefore), "comments.history.move_pin");
            }

            if (focusCommentID == comment.id) {
                ImGui::SetKeyboardFocusHere();
                ImGui::SetScrollHereY();
                focusCommentID = INVALID_ID;
            }

            const std::string textBefore = comment.text;

            ImGui::InputTextMultiline("##Text", &comment.text, ImVec2(-1.0f, ImGui::GetTextLineHeight() * 4.0f));

            // The comments as they were when typing began become the step
            // once typing ends with a change.
            if (ImGui::IsItemActivated()) {
                MapEditorInternal::CommentsSnapshot before = MapEditorInternal::CaptureCommentsSnapshot();
                for (EditorComment& saved : before.comments)
                    if (saved.id == comment.id) saved.text = textBefore;

                editBefore = std::move(before);
                editingCommentID = comment.id;
            }

            if (comment.text != textBefore) MapEditorInternal::MarkUnsavedChanges();

            if (ImGui::IsItemDeactivated() && editingCommentID == comment.id) {
                if (ImGui::IsItemDeactivatedAfterEdit() && editBefore.has_value())
                    MapEditorInternal::PushCommentsUndo(std::move(*editBefore), "comments.history.edit");

                editBefore.reset();
                editingCommentID = INVALID_ID;
            }

            if (ImGui::SmallButton(Localisation::Get("comments.delete").c_str())) deleteID = comment.id;

            ImGui::Separator();
            ImGui::PopID();
        }

        if (deleteID != INVALID_ID) {
            MapEditorInternal::CommentsSnapshot before = MapEditorInternal::CaptureCommentsSnapshot();
            std::erase_if(level.comments, [deleteID](const EditorComment& comment) { return comment.id == deleteID; });

            MapEditorInternal::PushCommentsUndo(std::move(before), "comments.history.delete");
            MapEditorInternal::MarkUnsavedChanges();
        }

        ImGui::End();
    }
}
