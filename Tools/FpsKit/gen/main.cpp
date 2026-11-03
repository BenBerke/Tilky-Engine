// Generates the Classic FPS Kit's prefabs, flipbooks, UI layouts and levels with
// the engine's own code. Art and sounds come from ../extract.py first.
//
// usage: fpskit <template project folder> <art_manifest.json>
#include "Kit.hpp"
#include "Prefabs.hpp"
#include "Layouts.hpp"
#include "Levels.hpp"

namespace {
    const std::vector<Str> kLevels = {"Level 1 - Outpost", "Level 2 - Keys", "Level 3 - Courtyard", "Level 4 - The Tripod"};

    void DefineVariables() {
        GlobalEventVariables::Clear();
        const std::vector<std::pair<const char*, float>> variables = {
            // What the player carries between levels.
            {"Health", 100}, {"Armor", 0}, {"ArmorType", 0},
            {"Bullets", 50}, {"Shells", 0}, {"Rockets", 0}, {"Cells", 0}, {"Grenades", 2},
            {"MaxBullets", 200}, {"MaxShells", 50}, {"MaxRockets", 50}, {"MaxCells", 300}, {"MaxGrenades", 20}, {"HasBackpack", 0},
            {"HasFist", 1}, {"HasPistol", 1}, {"HasChainsaw", 0}, {"HasShotgun", 0}, {"HasSuperShotgun", 0},
            {"HasMinigun", 0}, {"HasRocketLauncher", 0}, {"HasPlasmaRifle", 0}, {"HasSKAG", 0},
            // Reset at each level start.
            {"HasRedKey", 0}, {"HasBlueKey", 0}, {"HasYellowKey", 0},
            {"Berserk", 0}, {"Invulnerable", 0}, {"RadSuit", 0},
            {"Kills", 0}, {"TotalKills", 0}, {"Items", 0}, {"TotalItems", 0}, {"Secrets", 0}, {"TotalSecrets", 0}, {"Time", 0},
            // Plumbing.
            {"CurrentAmmo", 0},      // what the HUD's big ammo number shows
            {"IncomingDamage", 0},   // how much the next "Take Damage" does
            {"MenuState", 0},        // 0 playing, 1 pause menu open, 2 any other screen
            {"GoreLevel", 1},        // Options menu: 0 off, 1 normal, 2 extra
            // The screen between levels: what's shown so far, counting up.
            {"TallyKills", 0}, {"TallyItems", 0}, {"TallySecrets", 0}, {"TallyMinutes", 0}, {"TallySeconds", 0},
            {"TallyDone", 0}, {"TallySkip", 0},
        };
        for (const auto& [name, value] : variables) GlobalEventVariables::Create(name, value);
    }

    void WriteProjectFile(const fs::path& project) {
        json data = {
            {"assetsFolder", "Assets"},
            {"engineVersion", ""},
            {"name", "Classic FPS Kit"},
            {"uiReferenceHeight", 1080.0}
        };
        data["levels"] = json::array({"Main Menu"});
        for (const Str& level : kLevels) data["levels"].push_back(level);
        data["levels"].push_back("Blank Base Level");
        data["levels"].push_back("Prefab Gallery");
        std::ofstream(project / "project.tilky") << data.dump(4);
    }

    void WriteSettings(const fs::path& project) {
        json settings = {
            {"formatVersion", 1},
            {"tagToId", TagRegistry::GetAll()},
            {"nextTagId", TagRegistry::GetNextId()},
            {"eventTags", TagRegistry::GetEventTagIds()}
        };
        GlobalEventVariables::WriteToProjectSettings(settings);
        std::ofstream(project / "ProjectSettings.json") << settings.dump(4);
    }

    // Removes everything this tool writes, so a rename leaves nothing behind.
    void ClearGenerated(const fs::path& assets) {
        for (const char* folder : {"Prefabs", "Animations", "UI Layouts", "Levels"}) fs::remove_all(assets / folder);
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cout << "usage: fpskit <template project folder> <art_manifest.json>\n";
        return 1;
    }
    const fs::path project = fs::absolute(argv[1]);
    gAssets = project / "Assets";
    std::ifstream(argv[2]) >> gManifest;

    ClearGenerated(gAssets);
    WriteProjectFile(project);
    if (!ProjectManager::LoadProjectMetaData(project / "project.tilky")) {
        std::cout << "couldn't load the project\n";
        return 2;
    }

    TagRegistry::Clear();
    TagRegistry::EnsureBuiltInTags();
    DefineVariables();
    WriteSettings(project);
    UILayouts::Rescan();

    // Tags used by name in several places.
    for (const char* tag : {"Player", "Enemy", "Pickup", "Bleeds", "RedKey", "BlueKey", "YellowKey",
                            "HUD", "Pause Menu", "Death Screen", "Intermission", "Victory", "Level List",
                            "Options Menu", "Gore Setting", "Kickable", "Liquid", "Water", "Nukage", "Lava"}) Tag(tag);

    MakeEffects();
    MakeParticleEffects();
    MakeProjectiles();
    MakePickups();    // before enemies: they drop some
    MakeEnemies();
    MakeDecorations();
    MakeLevelLogic();
    MakePlayer(MakeWeaponViews());

    const LayoutIDs layouts = MakeLayouts(kLevels);

    BuildLevel("Main Menu", layouts, LevelMainMenu);
    BuildLevel(kLevels[0], layouts, Level1);
    BuildLevel(kLevels[1], layouts, Level2);
    BuildLevel(kLevels[2], layouts, Level3);
    BuildLevel(kLevels[3], layouts, Level4);
    BuildLevel("Blank Base Level", layouts, LevelBlank);
    BuildLevel("Prefab Gallery", layouts, LevelGallery);

    WriteSettings(project);

    std::cout << (gErrors == 0 ? "OK\n" : "FAILED: " + std::to_string(gErrors) + " errors\n");
    return gErrors == 0 ? 0 : 3;
}
