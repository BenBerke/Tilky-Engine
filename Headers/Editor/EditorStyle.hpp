//
// Created by berke on 10/12/2026.
//

#ifndef TILKY_ENGINE_EDITORSTYLE_HPP
#define TILKY_ENGINE_EDITORSTYLE_HPP

#include "imgui.h"

#include <array>
#include <string>
#include <string_view>

// The editor's UI designs, shared by the level editor and the runtime editor.
// Only looks - nothing here changes what any widget does.
//
// To add a design: write a Palette (or an apply function of its own, like
// Classic) and add one line to Designs(). The User Settings dropdown lists
// whatever is in Designs(), and the saved setting is the design's `id`.
namespace EditorStyle {
    // The handful of colours a design picks; every ImGui colour is derived
    // from these in ApplyPalette.
    struct Palette {
        bool light = false;

        ImVec4 text;
        ImVec4 textDisabled;
        ImVec4 windowBg;
        ImVec4 popupBg;
        ImVec4 border;

        ImVec4 frame;
        ImVec4 frameHovered;
        ImVec4 frameActive;

        ImVec4 titleBg;
        ImVec4 titleBgActive;
        ImVec4 menuBar;

        ImVec4 scrollGrab;
        ImVec4 scrollGrabHovered;
        ImVec4 scrollGrabActive;

        ImVec4 button;
        ImVec4 buttonHovered;

        ImVec4 tab;
        ImVec4 tabSelected;
        ImVec4 tabDimmed;
        ImVec4 tabDimmedSelected;

        ImVec4 tableHeader;
        ImVec4 tableBorderLight;
        ImVec4 tableRowAlt;
        ImVec4 dockingEmpty;

        // Selection, checkmarks, active toggles, section header bars.
        ImVec4 accent;
        // A lighter accent for small marks (checkmarks, links) on dark designs.
        ImVec4 accentBright;
        // Opacity of the accent behind selected/hovered rows and headers.
        float headerAlpha = 0.28f;

        ImVec4 plotLines;
        ImVec4 plotHistogram;
        ImVec4 dragDropTarget;
        ImVec4 modalDim;
    };

    struct Design {
        const char* id;       // saved in the user settings
        const char* labelKey; // Localisation key for the dropdown
        void (*apply)();
    };

    inline constexpr std::string_view DEFAULT_DESIGN = "dark";

    // The accent of whichever design was applied last, for widgets that
    // colour themselves (selected toggles, accent buttons, count badges).
    inline ImVec4 accentColor = ImVec4(0.29f, 0.56f, 0.93f, 1.00f);

    // The id of the design applied last. The runtime editor reapplies it,
    // so it looks like the editor it was opened from.
    inline std::string activeDesign = std::string(DEFAULT_DESIGN);

    inline ImVec4 Accent() { return accentColor; }

    // The accent, lightened (amount > 0) or darkened (amount < 0).
    inline ImVec4 AccentShade(const float amount) {
        const auto shade = [amount](const float c) {
            return amount >= 0.0f ? c + (1.0f - c) * amount : c * (1.0f + amount);
        };
        return ImVec4(shade(accentColor.x), shade(accentColor.y), shade(accentColor.z), accentColor.w);
    }

    inline ImVec4 WithAlpha(ImVec4 color, const float alpha) {
        color.w = alpha;
        return color;
    }

    // Sizes and roundings shared by every design except Classic.
    inline void ApplyMetrics(ImGuiStyle& style) {
        style.WindowPadding = ImVec2(10.0f, 8.0f);
        style.FramePadding = ImVec2(7.0f, 4.0f);
        style.CellPadding = ImVec2(6.0f, 3.0f);
        style.ItemSpacing = ImVec2(8.0f, 5.0f);
        style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
        style.IndentSpacing = 16.0f;
        style.ScrollbarSize = 12.0f;
        style.GrabMinSize = 10.0f;

        style.WindowBorderSize = 1.0f;
        style.ChildBorderSize = 1.0f;
        style.PopupBorderSize = 1.0f;
        style.FrameBorderSize = 0.0f;
        style.TabBorderSize = 0.0f;

        style.WindowRounding = 6.0f;
        style.ChildRounding = 4.0f;
        style.FrameRounding = 4.0f;
        style.PopupRounding = 5.0f;
        style.ScrollbarRounding = 6.0f;
        style.GrabRounding = 3.0f;
        style.TabRounding = 4.0f;

        style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
        style.SeparatorTextBorderSize = 1.0f;
        style.SeparatorTextPadding = ImVec2(10.0f, 4.0f);
    }

    inline void ApplyPalette(const Palette& p) {
        ImGuiStyle& style = ImGui::GetStyle();

        // Start from defaults so nothing from the previous design lingers.
        style = ImGuiStyle();
        if (p.light) ImGui::StyleColorsLight(&style);
        else ImGui::StyleColorsDark(&style);
        ApplyMetrics(style);

        accentColor = p.accent;

        ImVec4* colors = style.Colors;
        const ImVec4 none = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

        colors[ImGuiCol_Text] = p.text;
        colors[ImGuiCol_TextDisabled] = p.textDisabled;
        colors[ImGuiCol_WindowBg] = p.windowBg;
        colors[ImGuiCol_ChildBg] = none;
        colors[ImGuiCol_PopupBg] = p.popupBg;
        colors[ImGuiCol_Border] = p.border;
        colors[ImGuiCol_BorderShadow] = none;

        colors[ImGuiCol_FrameBg] = p.frame;
        colors[ImGuiCol_FrameBgHovered] = p.frameHovered;
        colors[ImGuiCol_FrameBgActive] = p.frameActive;

        colors[ImGuiCol_TitleBg] = p.titleBg;
        colors[ImGuiCol_TitleBgActive] = p.titleBgActive;
        colors[ImGuiCol_TitleBgCollapsed] = WithAlpha(p.titleBg, 0.85f);
        colors[ImGuiCol_MenuBarBg] = p.menuBar;

        colors[ImGuiCol_ScrollbarBg] = none;
        colors[ImGuiCol_ScrollbarGrab] = p.scrollGrab;
        colors[ImGuiCol_ScrollbarGrabHovered] = p.scrollGrabHovered;
        colors[ImGuiCol_ScrollbarGrabActive] = p.scrollGrabActive;

        colors[ImGuiCol_CheckMark] = p.accentBright;
        colors[ImGuiCol_SliderGrab] = p.accent;
        colors[ImGuiCol_SliderGrabActive] = p.accentBright;

        colors[ImGuiCol_Button] = p.button;
        colors[ImGuiCol_ButtonHovered] = p.buttonHovered;
        colors[ImGuiCol_ButtonActive] = WithAlpha(p.accent, 0.75f);

        colors[ImGuiCol_Header] = WithAlpha(p.accent, p.headerAlpha);
        colors[ImGuiCol_HeaderHovered] = WithAlpha(p.accent, p.headerAlpha + 0.12f);
        colors[ImGuiCol_HeaderActive] = WithAlpha(p.accent, p.headerAlpha + 0.27f);

        colors[ImGuiCol_Separator] = p.border;
        colors[ImGuiCol_SeparatorHovered] = WithAlpha(p.accent, 0.70f);
        colors[ImGuiCol_SeparatorActive] = p.accent;

        colors[ImGuiCol_ResizeGrip] = WithAlpha(p.accent, 0.15f);
        colors[ImGuiCol_ResizeGripHovered] = WithAlpha(p.accent, 0.55f);
        colors[ImGuiCol_ResizeGripActive] = WithAlpha(p.accent, 0.85f);

        colors[ImGuiCol_InputTextCursor] = p.text;

        colors[ImGuiCol_Tab] = p.tab;
        colors[ImGuiCol_TabHovered] = WithAlpha(p.accent, 0.45f);
        colors[ImGuiCol_TabSelected] = p.tabSelected;
        colors[ImGuiCol_TabSelectedOverline] = p.accent;
        colors[ImGuiCol_TabDimmed] = p.tabDimmed;
        colors[ImGuiCol_TabDimmedSelected] = p.tabDimmedSelected;
        colors[ImGuiCol_TabDimmedSelectedOverline] = WithAlpha(p.accent, 0.50f);

        colors[ImGuiCol_DockingPreview] = WithAlpha(p.accent, 0.55f);
        colors[ImGuiCol_DockingEmptyBg] = p.dockingEmpty;

        colors[ImGuiCol_PlotLines] = p.plotLines;
        colors[ImGuiCol_PlotLinesHovered] = p.accentBright;
        colors[ImGuiCol_PlotHistogram] = p.plotHistogram;
        colors[ImGuiCol_PlotHistogramHovered] = p.dragDropTarget;

        colors[ImGuiCol_TableHeaderBg] = p.tableHeader;
        colors[ImGuiCol_TableBorderStrong] = p.border;
        colors[ImGuiCol_TableBorderLight] = p.tableBorderLight;
        colors[ImGuiCol_TableRowBg] = none;
        colors[ImGuiCol_TableRowBgAlt] = p.tableRowAlt;

        colors[ImGuiCol_TextLink] = p.accentBright;
        colors[ImGuiCol_TextSelectedBg] = WithAlpha(p.accent, 0.35f);
        colors[ImGuiCol_DragDropTarget] = p.dragDropTarget;
        colors[ImGuiCol_NavCursor] = p.accentBright;
        colors[ImGuiCol_ModalWindowDimBg] = p.modalDim;
    }

    // ---- Designs ------------------------------------------------------------

    inline const Palette DARK_PALETTE = {
        .light = false,
        .text = ImVec4(0.91f, 0.92f, 0.94f, 1.00f),
        .textDisabled = ImVec4(0.52f, 0.54f, 0.58f, 1.00f),
        .windowBg = ImVec4(0.115f, 0.12f, 0.13f, 1.00f),
        .popupBg = ImVec4(0.10f, 0.105f, 0.115f, 0.98f),
        .border = ImVec4(0.22f, 0.23f, 0.25f, 1.00f),
        .frame = ImVec4(0.17f, 0.18f, 0.20f, 1.00f),
        .frameHovered = ImVec4(0.21f, 0.22f, 0.25f, 1.00f),
        .frameActive = ImVec4(0.24f, 0.26f, 0.30f, 1.00f),
        .titleBg = ImVec4(0.085f, 0.09f, 0.10f, 1.00f),
        .titleBgActive = ImVec4(0.10f, 0.105f, 0.115f, 1.00f),
        .menuBar = ImVec4(0.09f, 0.095f, 0.105f, 1.00f),
        .scrollGrab = ImVec4(0.27f, 0.28f, 0.31f, 1.00f),
        .scrollGrabHovered = ImVec4(0.34f, 0.35f, 0.39f, 1.00f),
        .scrollGrabActive = ImVec4(0.41f, 0.43f, 0.47f, 1.00f),
        .button = ImVec4(0.20f, 0.21f, 0.24f, 1.00f),
        .buttonHovered = ImVec4(0.26f, 0.28f, 0.32f, 1.00f),
        .tab = ImVec4(0.14f, 0.145f, 0.16f, 1.00f),
        .tabSelected = ImVec4(0.20f, 0.21f, 0.24f, 1.00f),
        .tabDimmed = ImVec4(0.11f, 0.115f, 0.125f, 1.00f),
        .tabDimmedSelected = ImVec4(0.16f, 0.17f, 0.19f, 1.00f),
        .tableHeader = ImVec4(0.15f, 0.155f, 0.17f, 1.00f),
        .tableBorderLight = ImVec4(0.17f, 0.18f, 0.20f, 1.00f),
        .tableRowAlt = ImVec4(1.00f, 1.00f, 1.00f, 0.03f),
        .dockingEmpty = ImVec4(0.09f, 0.095f, 0.105f, 1.00f),
        .accent = ImVec4(0.29f, 0.56f, 0.93f, 1.00f),
        .accentBright = ImVec4(0.42f, 0.66f, 1.00f, 1.00f),
        .headerAlpha = 0.28f,
        .plotLines = ImVec4(0.62f, 0.64f, 0.68f, 1.00f),
        .plotHistogram = ImVec4(0.90f, 0.66f, 0.22f, 1.00f),
        .dragDropTarget = ImVec4(0.98f, 0.76f, 0.26f, 0.90f),
        .modalDim = ImVec4(0.00f, 0.00f, 0.00f, 0.50f),
    };

    inline const Palette LIGHT_PALETTE = {
        .light = true,
        .text = ImVec4(0.07f, 0.07f, 0.09f, 1.00f),
        .textDisabled = ImVec4(0.45f, 0.45f, 0.47f, 1.00f),
        .windowBg = ImVec4(0.94f, 0.94f, 0.95f, 1.00f),
        .popupBg = ImVec4(0.98f, 0.98f, 0.99f, 0.98f),
        .border = ImVec4(0.75f, 0.75f, 0.78f, 1.00f),
        .frame = ImVec4(0.86f, 0.86f, 0.88f, 1.00f),
        .frameHovered = ImVec4(0.80f, 0.85f, 0.95f, 1.00f),
        .frameActive = ImVec4(0.72f, 0.80f, 0.95f, 1.00f),
        .titleBg = ImVec4(0.87f, 0.87f, 0.89f, 1.00f),
        .titleBgActive = ImVec4(0.82f, 0.84f, 0.89f, 1.00f),
        .menuBar = ImVec4(0.89f, 0.89f, 0.91f, 1.00f),
        .scrollGrab = ImVec4(0.70f, 0.70f, 0.73f, 1.00f),
        .scrollGrabHovered = ImVec4(0.60f, 0.60f, 0.63f, 1.00f),
        .scrollGrabActive = ImVec4(0.50f, 0.50f, 0.53f, 1.00f),
        .button = ImVec4(0.83f, 0.83f, 0.86f, 1.00f),
        .buttonHovered = ImVec4(0.74f, 0.81f, 0.95f, 1.00f),
        .tab = ImVec4(0.86f, 0.86f, 0.88f, 1.00f),
        .tabSelected = ImVec4(0.96f, 0.96f, 0.97f, 1.00f),
        .tabDimmed = ImVec4(0.89f, 0.89f, 0.91f, 1.00f),
        .tabDimmedSelected = ImVec4(0.93f, 0.93f, 0.95f, 1.00f),
        .tableHeader = ImVec4(0.88f, 0.88f, 0.90f, 1.00f),
        .tableBorderLight = ImVec4(0.84f, 0.84f, 0.86f, 1.00f),
        .tableRowAlt = ImVec4(0.00f, 0.00f, 0.00f, 0.03f),
        .dockingEmpty = ImVec4(0.88f, 0.88f, 0.90f, 1.00f),
        .accent = ImVec4(0.20f, 0.45f, 0.85f, 1.00f),
        .accentBright = ImVec4(0.20f, 0.45f, 0.85f, 1.00f),
        .headerAlpha = 0.22f,
        .plotLines = ImVec4(0.30f, 0.30f, 0.33f, 1.00f),
        .plotHistogram = ImVec4(0.80f, 0.55f, 0.10f, 1.00f),
        .dragDropTarget = ImVec4(0.90f, 0.70f, 0.10f, 0.90f),
        .modalDim = ImVec4(0.20f, 0.20f, 0.20f, 0.35f),
    };

    // Deep navy with a teal accent.
    inline const Palette MIDNIGHT_PALETTE = {
        .light = false,
        .text = ImVec4(0.88f, 0.91f, 0.96f, 1.00f),
        .textDisabled = ImVec4(0.48f, 0.53f, 0.62f, 1.00f),
        .windowBg = ImVec4(0.07f, 0.085f, 0.125f, 1.00f),
        .popupBg = ImVec4(0.06f, 0.075f, 0.11f, 0.98f),
        .border = ImVec4(0.15f, 0.19f, 0.27f, 1.00f),
        .frame = ImVec4(0.11f, 0.14f, 0.20f, 1.00f),
        .frameHovered = ImVec4(0.14f, 0.18f, 0.26f, 1.00f),
        .frameActive = ImVec4(0.17f, 0.22f, 0.31f, 1.00f),
        .titleBg = ImVec4(0.05f, 0.06f, 0.095f, 1.00f),
        .titleBgActive = ImVec4(0.065f, 0.08f, 0.12f, 1.00f),
        .menuBar = ImVec4(0.055f, 0.065f, 0.10f, 1.00f),
        .scrollGrab = ImVec4(0.18f, 0.23f, 0.32f, 1.00f),
        .scrollGrabHovered = ImVec4(0.24f, 0.30f, 0.41f, 1.00f),
        .scrollGrabActive = ImVec4(0.30f, 0.37f, 0.50f, 1.00f),
        .button = ImVec4(0.13f, 0.17f, 0.25f, 1.00f),
        .buttonHovered = ImVec4(0.17f, 0.22f, 0.32f, 1.00f),
        .tab = ImVec4(0.09f, 0.11f, 0.16f, 1.00f),
        .tabSelected = ImVec4(0.13f, 0.17f, 0.25f, 1.00f),
        .tabDimmed = ImVec4(0.07f, 0.085f, 0.125f, 1.00f),
        .tabDimmedSelected = ImVec4(0.11f, 0.14f, 0.20f, 1.00f),
        .tableHeader = ImVec4(0.10f, 0.125f, 0.18f, 1.00f),
        .tableBorderLight = ImVec4(0.11f, 0.14f, 0.20f, 1.00f),
        .tableRowAlt = ImVec4(1.00f, 1.00f, 1.00f, 0.025f),
        .dockingEmpty = ImVec4(0.055f, 0.065f, 0.10f, 1.00f),
        .accent = ImVec4(0.13f, 0.62f, 0.64f, 1.00f),
        .accentBright = ImVec4(0.35f, 0.85f, 0.86f, 1.00f),
        .headerAlpha = 0.30f,
        .plotLines = ImVec4(0.60f, 0.66f, 0.76f, 1.00f),
        .plotHistogram = ImVec4(0.90f, 0.66f, 0.22f, 1.00f),
        .dragDropTarget = ImVec4(0.98f, 0.76f, 0.26f, 0.90f),
        .modalDim = ImVec4(0.00f, 0.00f, 0.00f, 0.55f),
    };

    // The look the editor had before the designs existed: stock Dear ImGui
    // dark colours and sizes.
    inline void ApplyClassic() {
        ImGuiStyle& style = ImGui::GetStyle();
        style = ImGuiStyle();
        ImGui::StyleColorsDark(&style);
        accentColor = style.Colors[ImGuiCol_CheckMark];
    }

    // Dropdown order.
    inline const std::array<Design, 4>& Designs() {
        static const std::array<Design, 4> designs = {{
            {"dark", "editor.design.dark", [] { ApplyPalette(DARK_PALETTE); }},
            {"light", "editor.design.light", [] { ApplyPalette(LIGHT_PALETTE); }},
            {"midnight", "editor.design.midnight", [] { ApplyPalette(MIDNIGHT_PALETTE); }},
            {"classic", "editor.design.classic", ApplyClassic},
        }};
        return designs;
    }

    inline const Design* FindDesign(const std::string_view id) {
        for (const Design& design : Designs())
            if (id == design.id) return &design;
        return nullptr;
    }

    inline const std::string& ActiveDesign() { return activeDesign; }

    // Applies the design with this id, or the default one for an unknown
    // id. Without an ImGui context it only records the choice; the next
    // Apply(ActiveDesign()) once a context exists puts it on screen.
    inline void Apply(const std::string_view id) {
        const Design* design = FindDesign(id);
        if (design == nullptr) design = FindDesign(DEFAULT_DESIGN);

        activeDesign = design->id;

        if (ImGui::GetCurrentContext() != nullptr) design->apply();
    }
}

#endif //TILKY_ENGINE_EDITORSTYLE_HPP
