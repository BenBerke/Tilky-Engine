//
// Created by berke on 5/16/2026.
//
#include "imgui.h"
#include "src/Editor/EditorInternal.hpp"
#include "Headers/Engine/Local/Local.hpp"

namespace {
    void Spacing(const int count = 1) {
        for (int i = 0; i < count; ++i) {
            ImGui::Spacing();
        }
    }
}

namespace MapEditorInternal {
    using namespace Localisation;
    void DrawUIEditorUI() {
        ImGui::Begin(Get("editor.ui.title").c_str());



        ImGui::End();

    }
}

 // namespace Editor
