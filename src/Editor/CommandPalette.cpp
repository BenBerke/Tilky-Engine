#include "Headers/Editor/CommandPalette.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

#include <imgui.h>

#include "Headers/Engine/Local/Local.hpp"

namespace CommandPalette {
    using namespace MapEditorInternal;

    namespace {
        struct Registered {
            const char* labelKey;
            KeyBindScope scope;
            std::function<void()> run;
        };

        std::vector<Registered> registered;

        // One row: a key binding, a shortcut, or a registered action.
        struct Command {
            std::string label;
            std::string group;
            std::string keys;
            KeyBindScope scope = KEYSCOPE_MAP;
            int keyBind = KEYBIND_COUNT;
            int combo = COMBO_COUNT;
            int registeredIndex = -1;
            int score = 0;
        };

        bool open = false;
        bool focusInput = false;
        char query[128] = {};
        int highlighted = 0;

        // Bindings that are held rather than pressed - camera flight,
        // panning and click modifiers. Pressing one once does nothing.
        bool IsHeldBinding(const int bind) {
            switch (bind) {
                case KEYBIND_PAN_UP: case KEYBIND_PAN_DOWN: case KEYBIND_PAN_LEFT: case KEYBIND_PAN_RIGHT:
                case KEYBIND_DISABLE_SNAP: case KEYBIND_MULTI_SELECT: case KEYBIND_RANGE_SELECT:
                case KEYBIND_CONSTRAIN_ANGLE: case KEYBIND_SPLIT_WALL: case KEYBIND_PAINT_SELECT:
                case KEYBIND_ASSIGN_GROUP_MODIFIER:
                case KEYBIND_RT_FLY_FORWARD: case KEYBIND_RT_FLY_BACK: case KEYBIND_RT_FLY_LEFT: case KEYBIND_RT_FLY_RIGHT:
                case KEYBIND_RT_FLY_UP: case KEYBIND_RT_FLY_DOWN: case KEYBIND_RT_RESET_SPEED:
                case KEYBIND_RT_DISABLE_SNAP: case KEYBIND_RT_ADD_TO_SELECTION:
                case KEYBIND_RT_TEXTURE_X_ONLY: case KEYBIND_RT_TEXTURE_Y_ONLY:
                case KEYBIND_RT_SELECT_BY_TEXTURE: case KEYBIND_RT_SELECT_CONNECTED: case KEYBIND_RT_CONNECTED_BY_HEIGHT:
                case KEYBIND_RT_FINE_SCROLL: case KEYBIND_RT_BRIGHTNESS_SCROLL:
                case KEYBIND_UI_BIG_NUDGE: case KEYBIND_UI_DISABLE_SNAP: case KEYBIND_UI_ADD_TO_SELECTION: case KEYBIND_UI_KEEP_ASPECT:
                    return true;
                default:
                    return false;
            }
        }

        // Every query character in order in `text`, ignoring case. Higher
        // for runs of matching characters and matches at word starts; -1
        // when it doesn't match.
        int FuzzyScore(const std::string& text, const char* pattern) {
            int score = 0;
            int run = 0;
            std::size_t position = 0;

            for (const char* p = pattern; *p != '\0'; ++p) {
                const char wanted = static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
                if (wanted == ' ') continue;

                bool found = false;

                for (; position < text.size(); ++position) {
                    if (static_cast<char>(std::tolower(static_cast<unsigned char>(text[position]))) != wanted) {
                        run = 0;
                        continue;
                    }

                    const bool wordStart = position == 0 || !std::isalnum(static_cast<unsigned char>(text[position - 1]));
                    score += 1 + run * 2 + (wordStart ? 3 : 0);
                    ++run;
                    ++position;
                    found = true;
                    break;
                }

                if (!found) return -1;
            }

            return score;
        }

        std::vector<Command> BuildCommands() {
            std::vector<Command> commands;

            const auto keys = [](const std::string& name) {
                return name == Localisation::Get("editor.user_settings.key_none") ? std::string() : name;
            };

            for (int bind = 0; bind < KEYBIND_COUNT; ++bind) {
                if (IsHeldBinding(bind)) continue;

                const EditorKeyBindInfo& info = KEY_BIND_INFO[bind];
                commands.push_back({Localisation::Get(info.labelKey), Localisation::Get(info.groupKey),
                                    keys(KeyBindName(static_cast<EditorKeyBind>(bind))), info.scope, bind});
            }

            for (int combo = 0; combo < COMBO_COUNT; ++combo) {
                if (combo == COMBO_COMMAND_PALETTE) continue;

                const EditorComboInfo& info = COMBO_INFO[combo];

                // The selection group shortcuts work in both editors; the
                // current one runs them.
                commands.push_back({Localisation::Get(info.labelKey), Localisation::Get(info.groupKey),
                                    keys(ComboName(static_cast<EditorCombo>(combo))), info.scope, KEYBIND_COUNT, combo});
            }

            for (int i = 0; i < static_cast<int>(registered.size()); ++i)
                commands.push_back({Localisation::Get(registered[i].labelKey), Localisation::Get("command_palette.group.commands"),
                                    {}, registered[i].scope, KEYBIND_COUNT, COMBO_COUNT, i});

            return commands;
        }

        const char* ScopeKey(const KeyBindScope scope) {
            switch (scope) {
                case KEYSCOPE_MAP: return "command_palette.scope.map";
                case KEYSCOPE_RUNTIME: return "command_palette.scope.runtime";
                case KEYSCOPE_UI: return "command_palette.scope.ui";
            }
            return "command_palette.scope.map";
        }

        // Group combos run in whichever of the two editors is open.
        bool RunsIn(const Command& command, const KeyBindScope current) {
            if (command.combo >= COMBO_SELECT_GROUP_1 && command.combo <= COMBO_SELECT_GROUP_LAST)
                return current == KEYSCOPE_MAP || current == KEYSCOPE_RUNTIME;

            return command.scope == current;
        }

        void Run(const Command& command, const KeyBindScope current, const SwitchEditor& switchTo) {
            if (!RunsIn(command, current) && switchTo) switchTo(command.scope);

            if (command.keyBind < KEYBIND_COUNT) PressKeyBind(static_cast<EditorKeyBind>(command.keyBind));
            else if (command.combo < COMBO_COUNT) PressCombo(static_cast<EditorCombo>(command.combo));
            else if (command.registeredIndex >= 0 && registered[command.registeredIndex].run) registered[command.registeredIndex].run();
        }
    }

    void Open() {
        open = true;
        focusInput = true;
        query[0] = '\0';
        highlighted = 0;
    }

    bool IsOpen() {
        return open;
    }

    void Register(const char* labelKey, const KeyBindScope scope, std::function<void()> run) {
        for (Registered& entry : registered) {
            if (std::strcmp(entry.labelKey, labelKey) != 0) continue;

            entry = {labelKey, scope, std::move(run)};
            return;
        }

        registered.push_back({labelKey, scope, std::move(run)});
    }

    void Draw(const KeyBindScope current, const SwitchEditor& switchTo) {
        if (!open) return;

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        constexpr float WIDTH = 560.0f;

        ImGui::SetNextWindowPos({viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + 48.0f}, ImGuiCond_Always, {0.5f, 0.0f});
        ImGui::SetNextWindowSize({WIDTH, 0.0f}, ImGuiCond_Always);

        if (focusInput) ImGui::SetNextWindowFocus();

        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                           ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse;

        if (!ImGui::Begin("##CommandPalette", nullptr, flags)) {
            ImGui::End();
            return;
        }

        if (focusInput) {
            ImGui::SetKeyboardFocusHere();
            focusInput = false;
        }

        ImGui::SetNextItemWidth(-1.0f);
        const bool submitted = ImGui::InputTextWithHint("##CommandPaletteQuery", Localisation::Get("command_palette.hint").c_str(),
                                                        query, sizeof(query), ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::IsItemEdited()) highlighted = 0;

        std::vector<Command> commands = BuildCommands();

        std::vector<Command> shown;
        for (Command& command : commands) {
            command.score = query[0] == '\0' ? 0 : FuzzyScore(command.label + "  " + command.group, query);
            if (command.score >= 0) shown.push_back(std::move(command));
        }

        // Best match first; this editor's actions ahead of the others'.
        std::ranges::stable_sort(shown, [current](const Command& a, const Command& b) {
            if (a.score != b.score) return a.score > b.score;
            return RunsIn(a, current) && !RunsIn(b, current);
        });

        const int count = static_cast<int>(shown.size());

        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) highlighted = std::min(highlighted + 1, count - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) highlighted = std::max(highlighted - 1, 0);
        highlighted = std::clamp(highlighted, 0, std::max(count - 1, 0));

        int toRun = submitted && count > 0 ? highlighted : -1;

        constexpr int VISIBLE_ROWS = 14;
        const float listHeight = ImGui::GetTextLineHeightWithSpacing() * static_cast<float>(std::min(std::max(count, 1), VISIBLE_ROWS)) + 4.0f;

        if (ImGui::BeginChild("##CommandPaletteList", {0.0f, listHeight})) {
            if (count == 0) ImGui::TextDisabled("%s", Localisation::Get("command_palette.no_match").c_str());

            for (int i = 0; i < count; ++i) {
                const Command& command = shown[i];
                const bool here = RunsIn(command, current);

                ImGui::PushID(i);

                if (!here) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

                if (ImGui::Selectable(command.label.c_str(), i == highlighted)) toRun = i;
                if (i == highlighted && (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow))) ImGui::SetScrollHereY();

                // Group, editor and keys, right-aligned on the same row.
                const std::string detail = command.group + "  |  " + Localisation::Get(ScopeKey(command.scope)) +
                                           (command.keys.empty() ? std::string() : "  |  " + command.keys);
                const float detailWidth = ImGui::CalcTextSize(detail.c_str()).x;

                ImGui::SameLine(std::max(ImGui::GetContentRegionMax().x - detailWidth, ImGui::CalcTextSize(command.label.c_str()).x + 16.0f));
                ImGui::TextDisabled("%s", detail.c_str());

                if (!here) ImGui::PopStyleColor();

                ImGui::PopID();
            }
        }
        ImGui::EndChild();

        ImGui::TextDisabled("%s", Localisation::Get("command_palette.footer").c_str());

        const bool lostFocus = !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

        ImGui::End();

        if (toRun >= 0) {
            open = false;
            Run(shown[toRun], current, switchTo);
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_Escape) || lostFocus) open = false;
    }
}
