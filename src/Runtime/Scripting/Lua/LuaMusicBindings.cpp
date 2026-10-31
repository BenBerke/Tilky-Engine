#include "../../../../Headers/Runtime/Scripting/Lua/LuaScripting.hpp"

#include "Headers/Runtime/Scripting/Lua/LuaBindingMetadata.hpp"
#include "Headers/Runtime/Sound/MusicPlayer.hpp"

#include <sol/sol.hpp>

#include <string>

// Lua's Music table - the level's music list (World Settings > Music),
// played by MusicPlayer.
namespace {
    using namespace LuaBindingMetadata;

    void RegisterMusicMetadata() {
        RegisterType(GlobalTable("Music",
            "The level's music: the list of tracks in World Settings > Music, played one after another. "
            "It keeps playing through Game.LoadLevel when the next level has the same list.",
            {},
            {
                Method("Start", {}, {}, "Resumes a paused track; otherwise plays the list from its first track (Shuffle: in a new order). Does nothing while already playing."),
                Method("Stop", {}, {}, "Stops the music, fading out if the level has a fade. The next Start plays the list from the beginning."),
                Method("Pause", {}, {}, "Holds the track where it is; Start carries on from there."),
                Method("SwitchToIndex", {Param("index", "integer")}, {}, "Plays track `index` (1-based) now, fading if the level has a fade; the list carries on from there. Raises an error if there is no such track."),
                Method("SetVolume", {Param("volume", "number")}, {}, "The music's volume: 1 as the files are, 0 silent."),
            }));
    }
}

void LuaScriptSystem::RegisterMusicBindings(sol::state& lua) {
    RegisterMusicMetadata();

    sol::table music = lua.create_named_table("Music");

    music.set_function("Start", [] { MusicPlayer::Start(); });
    music.set_function("Stop", [] { MusicPlayer::Stop(); });
    music.set_function("Pause", [] { MusicPlayer::Pause(); });

    music.set_function("SwitchToIndex", [](const int index) {
        if (!MusicPlayer::SwitchToIndex(index - 1))
            throw sol::error("Music.SwitchToIndex: there is no track " + std::to_string(index) + " (the level has " +
                             std::to_string(MusicPlayer::TrackCount()) + ")");
    });

    music.set_function("SetVolume", [](const float volume) { MusicPlayer::SetVolume(volume); });
}
