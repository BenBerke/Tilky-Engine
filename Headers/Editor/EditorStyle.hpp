//
// Created by berke on 10/12/2026.
//

#ifndef TILKY_ENGINE_EDITORSTYLE_HPP
#define TILKY_ENGINE_EDITORSTYLE_HPP

#include "imgui.h"

// The look shared by the level editor and the runtime editor: one set of
// sizes and roundings, and a dark and a light palette built around the same
// blue accent. Only looks - nothing here changes what any widget does.
namespace EditorStyle {
    // The accent of whichever palette was applied last, for widgets that
    // colour themselves (selected toggles, accent buttons).
    inline ImVec4 accentColor = ImVec4(0.29f, 0.56f, 0.93f, 1.00f);

    inline ImVec4 Accent() { return accentColor; }

    // The accent, lightened (amount > 0) or darkened (amount < 0).
    inline ImVec4 AccentShade(const float amount) {
        const auto shade = [amount](const float c) {
            return amount >= 0.0f ? c + (1.0f - c) * amount : c * (1.0f + amount);
        };
        return ImVec4(shade(accentColor.x), shade(accentColor.y), shade(accentColor.z), accentColor.w);
    }

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

    inline void ApplyDark() {
        ImGui::StyleColorsDark();

        ImGuiStyle& style = ImGui::GetStyle();
        ApplyMetrics(style);

        ImVec4* colors = style.Colors;

        const ImVec4 accent = ImVec4(0.29f, 0.56f, 0.93f, 1.00f);
        accentColor = accent;
        const ImVec4 accentBright = ImVec4(0.42f, 0.66f, 1.00f, 1.00f);
        const auto withAlpha = [](ImVec4 color, const float alpha) {
            color.w = alpha;
            return color;
        };

        colors[ImGuiCol_Text] = ImVec4(0.91f, 0.92f, 0.94f, 1.00f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.52f, 0.54f, 0.58f, 1.00f);
        colors[ImGuiCol_WindowBg] = ImVec4(0.115f, 0.12f, 0.13f, 1.00f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.105f, 0.115f, 0.98f);
        colors[ImGuiCol_Border] = ImVec4(0.22f, 0.23f, 0.25f, 1.00f);
        colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

        colors[ImGuiCol_FrameBg] = ImVec4(0.17f, 0.18f, 0.20f, 1.00f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.21f, 0.22f, 0.25f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.26f, 0.30f, 1.00f);

        colors[ImGuiCol_TitleBg] = ImVec4(0.085f, 0.09f, 0.10f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.10f, 0.105f, 0.115f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.085f, 0.09f, 0.10f, 0.85f);
        colors[ImGuiCol_MenuBarBg] = ImVec4(0.09f, 0.095f, 0.105f, 1.00f);

        colors[ImGuiCol_ScrollbarBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.27f, 0.28f, 0.31f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.34f, 0.35f, 0.39f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.41f, 0.43f, 0.47f, 1.00f);

        colors[ImGuiCol_CheckMark] = accentBright;
        colors[ImGuiCol_SliderGrab] = accent;
        colors[ImGuiCol_SliderGrabActive] = accentBright;

        colors[ImGuiCol_Button] = ImVec4(0.20f, 0.21f, 0.24f, 1.00f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.28f, 0.32f, 1.00f);
        colors[ImGuiCol_ButtonActive] = withAlpha(accent, 0.75f);

        colors[ImGuiCol_Header] = withAlpha(accent, 0.28f);
        colors[ImGuiCol_HeaderHovered] = withAlpha(accent, 0.40f);
        colors[ImGuiCol_HeaderActive] = withAlpha(accent, 0.55f);

        colors[ImGuiCol_Separator] = ImVec4(0.22f, 0.23f, 0.25f, 1.00f);
        colors[ImGuiCol_SeparatorHovered] = withAlpha(accent, 0.70f);
        colors[ImGuiCol_SeparatorActive] = accent;

        colors[ImGuiCol_ResizeGrip] = withAlpha(accent, 0.15f);
        colors[ImGuiCol_ResizeGripHovered] = withAlpha(accent, 0.55f);
        colors[ImGuiCol_ResizeGripActive] = withAlpha(accent, 0.85f);

        colors[ImGuiCol_InputTextCursor] = colors[ImGuiCol_Text];

        colors[ImGuiCol_Tab] = ImVec4(0.14f, 0.145f, 0.16f, 1.00f);
        colors[ImGuiCol_TabHovered] = withAlpha(accent, 0.45f);
        colors[ImGuiCol_TabSelected] = ImVec4(0.20f, 0.21f, 0.24f, 1.00f);
        colors[ImGuiCol_TabSelectedOverline] = accent;
        colors[ImGuiCol_TabDimmed] = ImVec4(0.11f, 0.115f, 0.125f, 1.00f);
        colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.16f, 0.17f, 0.19f, 1.00f);
        colors[ImGuiCol_TabDimmedSelectedOverline] = withAlpha(accent, 0.50f);

        colors[ImGuiCol_DockingPreview] = withAlpha(accent, 0.55f);
        colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.09f, 0.095f, 0.105f, 1.00f);

        colors[ImGuiCol_PlotLines] = ImVec4(0.62f, 0.64f, 0.68f, 1.00f);
        colors[ImGuiCol_PlotLinesHovered] = accentBright;
        colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.66f, 0.22f, 1.00f);
        colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.75f, 0.30f, 1.00f);

        colors[ImGuiCol_TableHeaderBg] = ImVec4(0.15f, 0.155f, 0.17f, 1.00f);
        colors[ImGuiCol_TableBorderStrong] = ImVec4(0.22f, 0.23f, 0.25f, 1.00f);
        colors[ImGuiCol_TableBorderLight] = ImVec4(0.17f, 0.18f, 0.20f, 1.00f);
        colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);

        colors[ImGuiCol_TextLink] = accentBright;
        colors[ImGuiCol_TextSelectedBg] = withAlpha(accent, 0.35f);
        colors[ImGuiCol_DragDropTarget] = ImVec4(0.98f, 0.76f, 0.26f, 0.90f);
        colors[ImGuiCol_NavCursor] = accentBright;
        colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
        colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.45f);
        colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.50f);
    }

    inline void ApplyLight() {
        ImGui::StyleColorsLight();

        ImGuiStyle& style = ImGui::GetStyle();
        ApplyMetrics(style);

        ImVec4* colors = style.Colors;

        const ImVec4 accent = ImVec4(0.20f, 0.45f, 0.85f, 1.00f);
        accentColor = accent;
        const auto withAlpha = [](ImVec4 color, const float alpha) {
            color.w = alpha;
            return color;
        };

        colors[ImGuiCol_Text] = ImVec4(0.07f, 0.07f, 0.09f, 1.00f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.45f, 0.45f, 0.47f, 1.00f);
        colors[ImGuiCol_WindowBg] = ImVec4(0.94f, 0.94f, 0.95f, 1.00f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.98f, 0.98f, 0.99f, 0.98f);
        colors[ImGuiCol_Border] = ImVec4(0.75f, 0.75f, 0.78f, 1.00f);
        colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

        colors[ImGuiCol_FrameBg] = ImVec4(0.86f, 0.86f, 0.88f, 1.00f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.80f, 0.85f, 0.95f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.72f, 0.80f, 0.95f, 1.00f);

        colors[ImGuiCol_TitleBg] = ImVec4(0.87f, 0.87f, 0.89f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.82f, 0.84f, 0.89f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.90f, 0.90f, 0.91f, 0.85f);
        colors[ImGuiCol_MenuBarBg] = ImVec4(0.89f, 0.89f, 0.91f, 1.00f);

        colors[ImGuiCol_ScrollbarBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.70f, 0.70f, 0.73f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.60f, 0.60f, 0.63f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.50f, 0.50f, 0.53f, 1.00f);

        colors[ImGuiCol_CheckMark] = accent;
        colors[ImGuiCol_SliderGrab] = withAlpha(accent, 0.85f);
        colors[ImGuiCol_SliderGrabActive] = accent;

        colors[ImGuiCol_Button] = ImVec4(0.83f, 0.83f, 0.86f, 1.00f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.74f, 0.81f, 0.95f, 1.00f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.62f, 0.73f, 0.95f, 1.00f);

        colors[ImGuiCol_Header] = withAlpha(accent, 0.22f);
        colors[ImGuiCol_HeaderHovered] = withAlpha(accent, 0.32f);
        colors[ImGuiCol_HeaderActive] = withAlpha(accent, 0.45f);

        colors[ImGuiCol_Separator] = ImVec4(0.75f, 0.75f, 0.78f, 1.00f);
        colors[ImGuiCol_SeparatorHovered] = withAlpha(accent, 0.70f);
        colors[ImGuiCol_SeparatorActive] = accent;

        colors[ImGuiCol_ResizeGrip] = withAlpha(accent, 0.15f);
        colors[ImGuiCol_ResizeGripHovered] = withAlpha(accent, 0.55f);
        colors[ImGuiCol_ResizeGripActive] = withAlpha(accent, 0.85f);

        colors[ImGuiCol_Tab] = ImVec4(0.86f, 0.86f, 0.88f, 1.00f);
        colors[ImGuiCol_TabHovered] = withAlpha(accent, 0.30f);
        colors[ImGuiCol_TabSelected] = ImVec4(0.96f, 0.96f, 0.97f, 1.00f);
        colors[ImGuiCol_TabSelectedOverline] = accent;
        colors[ImGuiCol_TabDimmed] = ImVec4(0.89f, 0.89f, 0.91f, 1.00f);
        colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.93f, 0.93f, 0.95f, 1.00f);
        colors[ImGuiCol_TabDimmedSelectedOverline] = withAlpha(accent, 0.50f);

        colors[ImGuiCol_DockingPreview] = withAlpha(accent, 0.45f);
        colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.88f, 0.88f, 0.90f, 1.00f);

        colors[ImGuiCol_PlotLines] = ImVec4(0.30f, 0.30f, 0.33f, 1.00f);
        colors[ImGuiCol_PlotHistogram] = ImVec4(0.80f, 0.55f, 0.10f, 1.00f);

        colors[ImGuiCol_TableHeaderBg] = ImVec4(0.88f, 0.88f, 0.90f, 1.00f);
        colors[ImGuiCol_TableBorderStrong] = ImVec4(0.75f, 0.75f, 0.78f, 1.00f);
        colors[ImGuiCol_TableBorderLight] = ImVec4(0.84f, 0.84f, 0.86f, 1.00f);
        colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.00f, 0.00f, 0.00f, 0.03f);

        colors[ImGuiCol_TextLink] = accent;
        colors[ImGuiCol_TextSelectedBg] = withAlpha(accent, 0.30f);
        colors[ImGuiCol_DragDropTarget] = ImVec4(0.90f, 0.70f, 0.10f, 0.90f);
        colors[ImGuiCol_NavCursor] = accent;
        colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.20f, 0.20f, 0.20f, 0.35f);
    }
}

#endif //TILKY_ENGINE_EDITORSTYLE_HPP
