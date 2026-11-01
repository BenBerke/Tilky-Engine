//
// Created by berke on 10/26/2026.
//

#ifndef TILKY_ENGINE_EVENTSYSTEM_HPP
#define TILKY_ENGINE_EVENTSYSTEM_HPP

#include <functional>
#include <optional>
#include <string>
#include <unordered_set>

#include "Headers/Objects/Event.hpp"
#include "Headers/Objects/RayHit.hpp"
#include "Headers/Runtime/PhysicsSystem.hpp"

struct Level;

namespace EventSystem {
    // What dispatches need from the scripting side, set by Start.
    struct Hooks {
        // Calls `functionName(instigator)` on every enabled script of the entity.
        std::function<void(Level& level, ID entityID, const std::string& functionName, ID instigatorID)> callFunction;
        // Destroys the entity at the end of the frame.
        std::function<void(ID entityID)> destroyEntity;
        // Copies the prefab into the level and starts its scripts. Returns
        // the new entity, INVALID_ID if the prefab can't be loaded.
        std::function<ID(Level& level, const std::string& prefab)> spawnPrefab;
        // Switches to the level at the end of the frame.
        std::function<void(const std::string& levelName)> loadLevel;
        // Pause / Resume / Quit dispatches, like Game.Pause and Game.Quit.
        std::function<void()> pause;
        std::function<void(const Level& level)> resume;
        std::function<void()> quit;

        // Set Field on a script: the entity's `scriptIndex`-th script
        // (inspector order). Get gives the public field's kind and current
        // value, nullopt when there is no such field or Set Field can't
        // change its type. Set writes it.
        std::function<std::optional<std::pair<FieldValueKind, FieldValue>>(
            Level& level, ID entityID, int scriptIndex, const std::string& field)> getScriptField;
        std::function<void(Level& level, ID entityID, int scriptIndex, const std::string& field,
                           FieldValueKind kind, const FieldValue& value)> setScriptField;
    };

    // The UI Button events, one per event payload.
    enum class UIButtonEvent : uint8_t { CLICK, RELEASE, HOVER_ENTER, HOVER_EXIT };

    // The text a Show Message dispatch put on screen.
    struct Message {
        uint64_t id; // unique for the session, to follow a message across frames
        std::string text;
        Vector3 color; // 0-255
        float alpha;   // 0..1, from the level's fade in/out
    };

    // Rebuild after anything that adds/removes walls, sectors, entities or
    // their events/dispatches (level load, topology rebuild, undo).
    void CreateRegistry(const Level& level);

    // Builds the registry, resets every event's triggerCount and takes the
    // level's current state (positions, sectors, enabled flags, conditions)
    // as the baseline, so nothing fires for how the level starts.
    void Start(Level& level, Hooks hooks);

    // Fires the On Start events of everything in the level. Run once the
    // scripts have started. Spawned entities' fire from Update.
    void FireOnStart(Level& level, const std::unordered_set<ID>* skipEntities = nullptr);

    // Runs the delayed dispatches that are due, advances fades, doors and
    // entity moves, then diffs this frame against the last and fires every
    // event that happened. Run after physics and the transform sector update.
    void Update(Level& level, const PhysicsSystem::Contacts& contacts);

    void Stop();

    // Acts on the wall/sector/entity/linedef/level the dispatch is attached
    // to (`ownerKind`, `ownerID`), now, whatever its delay. `instigatorID` is
    // the entity that caused the event (INVALID_ID if none).
    void ExecuteDispatch(Level& level, const Dispatch& dispatch, EventOwnerKind ownerKind, ID ownerID, ID instigatorID);

    // Checks the trigger limit, the tag filter against the instigator's tags,
    // the condition and the chance, then runs every dispatch listening on the
    // event's tag and its direct calls. `ownerKind` / `ownerID` is the object
    // the event is on (a call's Self).
    void TriggerEvent(Level& level, Event& event, EventOwnerKind ownerKind, ID ownerID, ID instigatorID);

    // A ray of `type` was cast by `casterID` (INVALID_ID if no entity cast
    // it). `hit` is what it stopped at, nullopt if nothing within `length`.
    // A GENERAL ray does nothing; any other fires Receive Ray on what it hit
    // and on every enabled linedef it passed, at the next Update.
    void ReportRay(Level& level, RayType type, ID casterID, const Vector3& origin, const Vector3& direction,
                   float length, const std::optional<RayHit>& hit);

    // Game.MessageBox.SetMessage: shows `text` like a Show Message dispatch,
    // styled by the level's message box. `color` is 0-255.
    void SetMessage(const Level& level, const std::string& text, const Vector3& color, float duration);

    // Clears the messages on screen and remembers the level's message style
    // as saved, for ResetMessageStyle. Run when a level starts, before its
    // scripts: messages shown from then on (Start callbacks too) stay.
    void BeginLevelMessages(const Level& level);

    // Game.MessageBox.Clear: removes every message on screen.
    void ClearMessages();

    // Game.MessageBox.Reset: the level's message style back to how
    // BeginLevelMessages found it.
    void ResetMessageStyle(Level& level);

    // Game.Signal: fires every Lua Signal event named `name`, now.
    void Signal(Level& level, const std::string& name, ID instigatorID);

    // Plays a sound once on a source of its own, like a Play Sound dispatch:
    // at `position`, or unpositioned (2D) without one. The source goes when
    // the sound ends, or with the level.
    void PlayOneShot(const std::string& sound, float gain, float pitch, const std::optional<Vector3>& position);

    // Fires the UI Button events of `kind` on the entity, with the player as
    // the instigator. The dispatches they run (relays included) count their
    // delays and cooldowns in real time, so they work while paused.
    void FireUIButtonEvent(Level& level, ID entityID, UIButtonEvent kind);

    // Fires the entity's Entity Destroyed events. Call before it is removed.
    void FireEntityDestroyed(Level& level, ID entityID);

    // The value of an engine field (EventFields.hpp) of the entity's
    // `componentIndex`-th component of that type, nullopt if there is no such
    // component or field. Changes nothing; the editor starts a Set Field's
    // value from it.
    [[nodiscard]] std::optional<FieldValue> ReadEntityField(Level& level, ID entityID, FieldComponent component,
                                                           int componentIndex, const std::string& field);

    // The Show Message texts to draw this frame, oldest first. Styled by the
    // level's WorldSettings::messageStyle.
    [[nodiscard]] std::vector<Message> CurrentMessages();
}

#endif //TILKY_ENGINE_EVENTSYSTEM_HPP
