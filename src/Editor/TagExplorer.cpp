#include "TagExplorer.hpp"
#include "EventLinks.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <imgui.h>

#include "Headers/Engine/Local/Local.hpp"
#include "Headers/Objects/Level.hpp"
#include "Headers/TagRegistry.hpp"

namespace TagExplorer {
    namespace {
        enum class UseRole : uint8_t {
            CARRIES,       // the object has the tag
            EVENT_LINK,    // one of its events is linked by the tag
            DISPATCH_LINK, // one of its dispatches is linked by the tag
            NAMED_BY,      // an event's or dispatch's settings name the tag
        };

        struct Use {
            EventOwnerKind kind = EventOwnerKind::LEVEL;
            ID id = INVALID_ID;
            UseRole role = UseRole::CARRIES;
            const char* payloadKey = nullptr; // the event or dispatch, for the link and named roles
        };

        using UseIndex = std::unordered_map<TagId, std::vector<Use>>;

        // Every tag a payload's settings name: its filter's tags, and the
        // `target` / `tag` fields of the payloads that point at a tag.
        template<typename Payload>
        void PayloadTags(const Payload& payload, std::vector<TagId>& out) {
            std::visit([&out](const auto& p) {
                using P = std::decay_t<decltype(p)>;

                if constexpr (requires { p.filter.tags; })
                    out.insert(out.end(), p.filter.tags.begin(), p.filter.tags.end());

                if constexpr (requires { p.target; })
                    if constexpr (std::is_same_v<std::decay_t<decltype(P::target)>, TagId>) out.push_back(p.target);

                if constexpr (requires { p.tag; })
                    if constexpr (std::is_same_v<std::decay_t<decltype(P::tag)>, TagId>) out.push_back(p.tag);
            }, payload);
        }

        void AddUse(UseIndex& index, const TagId tag, const Use& use) {
            if (tag == 0) return;

            std::vector<Use>& uses = index[tag];

            const bool duplicate = std::ranges::any_of(uses, [&use](const Use& other) {
                return other.kind == use.kind && other.id == use.id && other.role == use.role && other.payloadKey == use.payloadKey;
            });

            if (!duplicate) uses.push_back(use);
        }

        void IndexObject(UseIndex& index, const EventOwnerKind kind, const ID id, const std::vector<TagId>* carried,
                         const std::vector<Event>& events, const std::vector<Dispatch>& dispatches) {
            if (carried != nullptr)
                for (const TagId tag : *carried) AddUse(index, tag, {kind, id, UseRole::CARRIES});

            std::vector<TagId> named;

            for (const Event& event : events) {
                const char* key = GetTranslationKey(event.payload);
                AddUse(index, event.tagId, {kind, id, UseRole::EVENT_LINK, key});

                named.clear();
                PayloadTags(event.payload, named);
                for (const TagId tag : named) AddUse(index, tag, {kind, id, UseRole::NAMED_BY, key});
            }

            for (const Dispatch& dispatch : dispatches) {
                const char* key = GetTranslationKey(dispatch.payload);
                AddUse(index, dispatch.tagId, {kind, id, UseRole::DISPATCH_LINK, key});

                named.clear();
                PayloadTags(dispatch.payload, named);
                for (const TagId tag : named) AddUse(index, tag, {kind, id, UseRole::NAMED_BY, key});
            }
        }

        UseIndex BuildIndex(const Level& level) {
            UseIndex index;

            for (const Wall& wall : level.walls)
                IndexObject(index, EventOwnerKind::LINE, wall.id, &wall.tagIds, wall.events, wall.dispatches);

            for (const Sector& sector : level.sectors)
                IndexObject(index, EventOwnerKind::SECTOR, sector.id, &sector.tagIds, sector.events, sector.dispatches);

            for (const Linedef& linedef : level.linedefs)
                IndexObject(index, EventOwnerKind::LINEDEF, linedef.id, nullptr, linedef.events, linedef.dispatches);

            for (const Entity& entity : level.entities)
                IndexObject(index, EventOwnerKind::ENTITY, entity.id, &entity.tagIds, entity.events, entity.dispatches);

            IndexObject(index, EventOwnerKind::LEVEL, INVALID_ID, nullptr, level.worldSettings.events, level.worldSettings.dispatches);

            return index;
        }

        const char* RoleKey(const UseRole role) {
            switch (role) {
                case UseRole::CARRIES: return "tag_explorer.role.carries";
                case UseRole::EVENT_LINK: return "tag_explorer.role.event";
                case UseRole::DISPATCH_LINK: return "tag_explorer.role.dispatch";
                case UseRole::NAMED_BY: return "tag_explorer.role.named";
            }
            return "tag_explorer.role.carries";
        }

        std::string Lowercase(std::string text) {
            for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return text;
        }

        char searchText[128] = {};
        bool unusedOnly = false;
    }

    void Draw(const Level& level, const char* windowName, bool* open, const PickCallback& onPick) {
        ImGui::SetNextWindowSize(ImVec2(320.0f, 420.0f), ImGuiCond_FirstUseEver);

        if (!ImGui::Begin(windowName, open)) {
            ImGui::End();
            return;
        }

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##TagExplorerSearch", Localisation::Get("tag_explorer.search").c_str(), searchText, sizeof(searchText));
        ImGui::Checkbox(Localisation::Get("tag_explorer.unused_only").c_str(), &unusedOnly);
        ImGui::Separator();

        const UseIndex index = BuildIndex(level);
        const std::string search = Lowercase(searchText);

        // By name, so the list doesn't reshuffle as tags are used.
        std::vector<std::pair<std::string, TagId>> tags(TagRegistry::GetAll().begin(), TagRegistry::GetAll().end());
        std::ranges::sort(tags);

        if (ImGui::BeginChild("##TagExplorerList")) {
            if (tags.empty()) ImGui::TextDisabled("%s", Localisation::Get("tag_explorer.no_tags").c_str());

            for (const auto& [name, tag] : tags) {
                if (!search.empty() && Lowercase(name).find(search) == std::string::npos) continue;

                const auto it = index.find(tag);
                const std::size_t count = it != index.end() ? it->second.size() : 0;
                if (unusedOnly && count > 0) continue;

                ImGui::PushID(static_cast<int>(tag));

                const std::string header = name + "  (" + std::to_string(count) + ")";

                if (count == 0) {
                    ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", Localisation::Get("tag_explorer.unused").c_str());
                }
                else if (ImGui::TreeNode(header.c_str())) {
                    int row = 0;

                    for (const Use& use : it->second) {
                        ImGui::PushID(row++);

                        std::string label = EventLinks::OwnerLabel(level, use.kind, use.id) + "  -  " + Localisation::Get(RoleKey(use.role));
                        if (use.payloadKey != nullptr) label += ": " + Localisation::Get(use.payloadKey);

                        if (ImGui::Selectable(label.c_str()) && onPick) onPick(use.kind, use.id);

                        ImGui::PopID();
                    }

                    ImGui::TreePop();
                }

                ImGui::PopID();
            }
        }
        ImGui::EndChild();

        ImGui::End();
    }
}
