// The Classic FPS Kit's UI layouts: HUD, pause menu, death screen, intermission,
// victory and main menu. They're laid out on Doom's own 320 x 200 screen and built
// from pictures, as Doom's are: Assets/UI/Words, Digits, Faces and Pictures.
#pragma once

#include "Kit.hpp"

struct LayoutIDs {
    uint64_t hud = 0, pause = 0, death = 0, intermission = 0, victory = 0, mainMenu = 0;
};

inline const Str kHudFont = "Fonts/Classic HUD.ttf";

// Doom's 320 x 200 screen, centred on a wide one: its 320 x 240 shape is 4:3, because
// Doom's pixels were taller than wide. Everything in these layouts is placed with this.
inline Vector2 D(const float x, const float y) { return {0.125f + x * 0.75f / 320.0f, y / 200.0f}; }

// A spot on the status bar, in the bar picture's pixels. The bar is the bottom 32 of those 200.
inline Vector2 Bar(const float x, const float y) { return D(x, 168.0f + y); }

inline std::pair<int, int> WordSize(const Str& word) {
    const json& size = Art("words", word);
    return size.is_array() ? std::pair<int, int>{size[0].get<int>(), size[1].get<int>()} : std::pair<int, int>{100, 19};
}
inline Str WordPicture(const Str& word) { return "UI/Words/" + word + ".png"; }

// A layout being filled in. Every Add* returns the entity's ID, because adding
// the next one can move entities in memory.
struct LayoutB {
    Level* level = nullptr;
    uint64_t id = 0;

    Entity& At(const ID entityID) { return *level->GetEntity(entityID); }

    ID Add(const Str& name, Vector2 anchorMin, Vector2 anchorMax, const std::vector<Str>& tags = {}, const bool enabled = true) {
        const ID entityID = level->CreateEntity(true);
        Entity& e = At(entityID);
        e.name = name;
        for (const Str& tag : tags) e.tagIds.push_back(Tag(tag));
        e.enabled = enabled;
        auto* ui = e.GetComponent<ComponentUITransform>();
        ui->anchorMin = anchorMin;
        ui->anchorMax = anchorMax;
        ui->pivot = {0.0f, 0.0f};
        return entityID;
    }

    ID Image(const Str& name, Vector2 min, Vector2 max, const Str& texture, Vector4 color = {1, 1, 1, 1},
             const std::vector<Str>& tags = {}, const bool enabled = true) {
        const ID entityID = Add(name, min, max, tags, enabled);
        auto* sprite = At(entityID).AddComponent<ComponentUISprite>();
        sprite->texture = texture;
        sprite->color = color;
        return entityID;
    }

    ID Label(const Str& name, Vector2 at, const Str& text, const float size, Vector4 color = {1, 1, 1, 1},
             const Str& font = kHudFont, const std::vector<Str>& tags = {}, const bool enabled = true) {
        const ID entityID = Add(name, at, at, tags, enabled);
        auto* label = At(entityID).AddComponent<ComponentUIText>();
        label->text = text;
        label->fontSize = size;
        label->color = color;
        label->font = font;
        return entityID;
    }

    // A word from UI/Words at Doom's size, or `scale` times it.
    ID Word(const Str& name, const float x, const float y, const Str& word, const std::vector<Str>& tags = {},
            const bool enabled = true, const float scale = 1.0f) {
        const auto [w, h] = WordSize(word);
        return Image(name, D(x, y), D(x + w * scale, y + h * scale), WordPicture(word), {1, 1, 1, 1}, tags, enabled);
    }

    ID WordCentred(const Str& name, const float y, const Str& word, const std::vector<Str>& tags = {},
                   const bool enabled = true, const float scale = 1.0f) {
        return Word(name, (320.0f - WordSize(word).first * scale) * 0.5f, y, word, tags, enabled, scale);
    }

    // A menu row: a word that runs its dispatches named "Go" when clicked, or when
    // MenuCursor.lua picks it. It acts when the button is let go, so nothing is still
    // being held when a new level locks the mouse.
    ID Button(const Str& name, const float x, const float y, const Str& word, const std::vector<Dispatch>& go,
              std::vector<DirectCall> extraCalls = {}, const std::vector<Str>& tags = {}, const bool enabled = true) {
        const ID entityID = Word(name, x, y, word, tags, enabled);
        Entity& e = At(entityID);
        auto* button = e.AddComponent<ComponentUIButton>();
        button->normalColor = {1.0f, 1.0f, 1.0f, 1.0f};
        button->hoverColor = {1.3f, 1.3f, 1.3f, 1.0f};
        button->pressedColor = {0.7f, 0.7f, 0.7f, 1.0f};

        extraCalls.insert(extraCalls.begin(), Self("Go"));
        PayloadLuaSignal picked;
        picked.name = "Menu Pick " + name;
        e.events.push_back(Ev(PayloadUIButtonRelease{}, extraCalls));
        e.events.push_back(Ev(picked, extraCalls));
        e.dispatches.push_back(Dp("Go", Play("Weapons/pistol")));
        for (const Dispatch& dispatch : go) e.dispatches.push_back(dispatch);
        return entityID;
    }

    // The blinking skull. It moves itself next to the selected button of `tag`.
    ID Cursor(const Str& tag, const bool enabled = true) {
        const ID entityID = Image(tag + " Cursor", D(0, 0), D(20, 19), "UI/Pictures/M_SKULL1.png", {1, 1, 1, 1}, {tag}, enabled);
        ComponentScript& script = AddLua(At(entityID), "Scripts/UI/MenuCursor");
        script.publicValues["menuTag"] = tag;
        return entityID;
    }

    // One number shown with pictures: a holder running PictureNumber.lua, plus one
    // image per digit. `right` is the x where the ones digit ends.
    void PictureNumber(const Str& name, const Str& variable, const Str& pictures, const float right, const float y,
                       const float digitWidth, const float digitHeight, const int digitCount,
                       const std::vector<Str>& tags = {}, const bool enabled = true, const int minimumDigits = 1,
                       std::vector<ID>* created = nullptr) {
        for (int place = 1; place <= digitCount; ++place) {
            const float x = right - digitWidth * place;
            const ID digit = Image(name + " Digit " + std::to_string(place), D(x, y), D(x + digitWidth, y + digitHeight),
                                   pictures + "0.png", {1, 1, 1, 1}, tags, enabled);
            if (created) created->push_back(digit);
        }
        const ID holder = Add(name + " Number", {0, 0}, {0, 0}, tags, enabled);
        ComponentScript& script = AddLua(At(holder), "Scripts/UI/PictureNumber");
        script.publicValues["variable"] = variable;
        script.publicValues["digitPictures"] = pictures;
        script.publicValues["digitName"] = name + " Digit";
        script.publicValues["digitCount"] = digitCount;
        script.publicValues["minimumDigits"] = minimumDigits;
        if (created) created->push_back(holder);
    }

    // The dispatches that let tagged menu entities be shown and hidden together.
    void Showable(const ID entityID) {
        Entity& e = At(entityID);
        e.dispatches.push_back(Dp("Show", PayloadDispatchEntityEnable{}));
        e.dispatches.push_back(Dp("Hide", PayloadDispatchEntityDisable{}));
    }
};

inline void BuildLayout(const Str& fileName, uint64_t& outID, const std::function<void(LayoutB&)>& fill) {
    const fs::path file = gAssets / "UI Layouts" / (fileName + ".uilayout");
    fs::create_directories(file.parent_path());
    Str error;
    if (!UILayouts::Create(file, &outID, &error)) Fail("layout " + fileName + ": " + error);
    UILayouts::Rescan();
    Level* level = UILayouts::Get(outID);
    if (level == nullptr) { Fail("layout " + fileName + " didn't load"); return; }
    {
        LevelManager::ScopedCurrentLevel scope(*level);
        LayoutB layout{level, outID};
        fill(layout);
    }
    if (!UILayouts::Save(outID, &error)) Fail("layout " + fileName + ": " + error);
}

// ── HUD ─────────────────────────────────────────────────────────────────────

inline void BuildHud(LayoutB& h) {
    h.Image("Crosshair", {0.5f, 0.5f}, {0.5f, 0.5f}, "", {1, 1, 1, 0.7f});
    {
        auto* ui = h.At(h.level->entities.back().id).GetComponent<ComponentUITransform>();
        ui->pivot = {0.5f, 0.5f};
        ui->scale = {3.0f, 3.0f};
    }

    // ── Screen flashes. See-through until something calls them ──
    const auto flash = [&](const Str& name, const Vector4 color, const float seconds, const float fadeSpeed) {
        const ID entityID = h.Image(name, {0, 0}, {1, 1}, "", {color.x, color.y, color.z, 0.0f}, {"HUD"});
        auto set = SetF(FieldComponent::UI_SPRITE, "color", Vec(color.x, color.y, color.z, color.w));
        set.revert = true;
        set.revertAfter = seconds;
        set.overTime = true; // without this, speed is ignored and the flash just pops on and off
        set.speed = fadeSpeed;
        h.At(entityID).dispatches.push_back(Dp(name, set));
        return entityID;
    };
    const ID hurt = flash("Hurt Flash", {0.8f, 0.05f, 0.0f, 0.14f}, 0.05f, 0.8f);
    PayloadLuaSignal hurtSignal;
    hurtSignal.name = "PlayerHurt";
    h.At(hurt).events.push_back(Ev(hurtSignal, {Self("Hurt Flash")}));
    flash("Pickup Flash", {1.0f, 0.85f, 0.4f, 0.06f}, 0.03f, 0.5f);

    const ID suit = h.Image("Radiation Suit Tint", {0, 0}, {1, 1}, "", {0.1f, 1.0f, 0.2f, 0.0f});
    h.At(suit).events.push_back(Ev(PayloadConditionMet{}, {Self("Suit On")}, If("RadSuit", EQ, 1)));
    h.At(suit).events.push_back(Ev(PayloadConditionMet{}, {Self("Suit Off")}, If("RadSuit", EQ, 0)));
    h.At(suit).dispatches.push_back(Dp("Suit On", SetF(FieldComponent::UI_SPRITE, "color", Vec(0.1f, 0.8f, 0.2f, 0.06f))));
    h.At(suit).dispatches.push_back(Dp("Suit Off", SetF(FieldComponent::UI_SPRITE, "color", Vec(0.1f, 1.0f, 0.2f, 0.0f))));

    // ── The bar itself, with Doom's border flat beside it on a wide screen ──
    h.Image("Bar Side Left", {0.0f, 0.84f}, {0.125f, 1.0f}, "UI/Pictures/BARSIDE.png");
    h.Image("Bar Side Right", {0.875f, 0.84f}, {1.0f, 1.0f}, "UI/Pictures/BARSIDE.png");
    h.Image("Status Bar", Bar(0, 0), Bar(320, 32), "UI/Pictures/STBAR.png");
    h.Image("Arms Panel", Bar(104, 0), Bar(142, 32), "UI/Pictures/STARMS.png");

    // ── The big numbers, where Doom puts them ──
    h.PictureNumber("Ammo", "CurrentAmmo", "UI/Digits/Big ", 44, 171, 14, 16, 3);
    h.PictureNumber("Health", "Health", "UI/Digits/Big ", 90, 171, 14, 16, 3);
    h.Image("Health Percent", Bar(90, 3), Bar(104, 19), "UI/Digits/Big percent.png");
    h.PictureNumber("Armor", "Armor", "UI/Digits/Big ", 221, 171, 14, 16, 3);
    h.Image("Armor Percent", Bar(221, 3), Bar(235, 19), "UI/Digits/Big percent.png");

    // ── Which weapon slots the player owns: grey numbers turn yellow ──
    const std::vector<std::tuple<Str, Str, float, float>> arms = {
        {"2", "HasPistol", 111, 4}, {"3", "HasShotgun", 123, 4}, {"4", "HasMinigun", 135, 4},
        {"5", "HasRocketLauncher", 111, 14}, {"6", "HasPlasmaRifle", 123, 14}, {"7", "HasSKAG", 135, 14}};
    for (const auto& [digit, variable, x, y] : arms) {
        const Str startPicture = variable == "HasPistol" ? "Yellow " : "Gray ";
        const ID entityID = h.Image("Arm " + digit, Bar(x, y), Bar(x + 4, y + 6), "UI/Digits/" + startPicture + digit + ".png");
        Entity& e = h.At(entityID);
        e.events.push_back(Ev(PayloadConditionMet{}, {Self("Own")}, If(variable, EQ, 1)));
        if (digit == "3") e.events.push_back(Ev(PayloadConditionMet{}, {Self("Own")}, If("HasSuperShotgun", EQ, 1)));
        e.events.push_back(Ev(PayloadConditionMet{}, {Self("Lost")}, If(variable, EQ, 0)));
        e.dispatches.push_back(Dp("Own", SetF(FieldComponent::UI_SPRITE, "texture", Text("UI/Digits/Yellow " + digit + ".png"))));
        e.dispatches.push_back(Dp("Lost", SetF(FieldComponent::UI_SPRITE, "texture", Text("UI/Digits/Gray " + digit + ".png"))));
    }

    // ── The face: Face.lua picks the picture ──
    {
        const ID entityID = h.Image("Face", Bar(143, 0), Bar(176, 32), "UI/Faces/STFST00.png");
        AddLua(h.At(entityID), "Scripts/UI/Face");
    }

    // ── Keys, in Doom's order ──
    const std::vector<std::tuple<Str, Str, Str, float>> keys = {
        {"Blue", "HasBlueKey", "STKEYS0", 3}, {"Yellow", "HasYellowKey", "STKEYS1", 13}, {"Red", "HasRedKey", "STKEYS2", 23}};
    for (const auto& [color, variable, picture, y] : keys) {
        const ID entityID = h.Image(color + " Key Icon", Bar(239, y), Bar(246, y + 5), "UI/Pictures/" + picture + ".png");
        Entity& e = h.At(entityID);
        e.GetComponent<ComponentUISprite>()->isActive = false;
        e.events.push_back(Ev(PayloadConditionMet{}, {Self("Got")}, If(variable, EQ, 1)));
        e.events.push_back(Ev(PayloadConditionMet{}, {Self("Lost")}, If(variable, EQ, 0)));
        e.dispatches.push_back(Dp("Got", SetF(FieldComponent::UI_SPRITE, "isActive", Flag(true))));
        e.dispatches.push_back(Dp("Lost", SetF(FieldComponent::UI_SPRITE, "isActive", Flag(false))));
    }

    // ── The small ammo table: how many, out of how many ──
    const std::vector<std::tuple<Str, float>> table = {{"Bullets", 173}, {"Shells", 179}, {"Rockets", 185}, {"Cells", 191}};
    for (const auto& [variable, y] : table) {
        h.PictureNumber(variable + " Count", variable, "UI/Digits/Yellow ", 288, y, 4, 6, 3);
        h.PictureNumber(variable + " Max", "Max" + variable, "UI/Digits/Yellow ", 314, y, 4, 6, 3);
    }
}

// ── Menus shown over the game ───────────────────────────────────────────────

inline constexpr float kMenuX = 97.0f;   // Doom's own column
inline float MenuY(const float top, const int row) { return top + static_cast<float>(row) * 20.0f; }

inline void BuildPause(LayoutB& p) {
    const std::vector<Str> tags = {"Pause Menu"};
    std::vector<ID> shown;

    // Esc: opens when nothing is open, closes when this menu is open. MenuState
    // changes a frame later, so one key press can't do both.
    const ID control = p.Add("Pause Control", {0, 0}, {0, 0});
    {
        Entity& e = p.At(control);
        PayloadKeyPress esc;
        esc.input = KeyEventInput::KEY;
        esc.key = SDL_SCANCODE_ESCAPE;
        e.events.push_back(Ev(esc, {Self("Open"), Tagged("Pause Menu", "Show")}, If("MenuState", EQ, 0)));
        e.events.push_back(Ev(esc, {Self("Close"), Tagged("Pause Menu", "Hide")}, If("MenuState", EQ, 1)));
        e.dispatches.push_back(Dp("Open", PayloadDispatchPause{}, 0.0f, {Then(Self("Mark Open"))}));
        e.dispatches.push_back(Dp("Open", Play("World/fast door open")));
        e.dispatches.push_back(Dp("Mark Open", SetVar("MenuState", 1)));
        e.dispatches.push_back(Dp("Close", PayloadDispatchResume{}, 0.0f, {Then(Self("Mark Closed"))}));
        e.dispatches.push_back(Dp("Close", Play("World/fast door close")));
        e.dispatches.push_back(Dp("Mark Closed", SetVar("MenuState", 0)));
    }

    shown.push_back(p.Image("Dim", {0, 0}, {1, 0.84f}, "", {0, 0, 0, 0.45f}, tags, false));
    {
        const json& logo = Art("pictures", "M_DOOM");
        const float w = logo.is_array() ? logo[0].get<float>() : 159.0f, h = logo.is_array() ? logo[1].get<float>() : 37.0f;
        shown.push_back(p.Image("Logo", D((320 - w) * 0.5f, 10), D((320 + w) * 0.5f, 10 + h), "UI/Pictures/M_DOOM.png", {1, 1, 1, 1}, tags, false));
    }

    shown.push_back(p.Button("Resume", kMenuX, MenuY(64, 0), "Resume",
        {Dp("Go", PayloadDispatchResume{}), Dp("Go", SetVar("MenuState", 0))}, {Tagged("Pause Menu", "Hide")}, tags, false));

    shown.push_back(p.Button("Restart Level", kMenuX, MenuY(64, 1), "Restart Level",
        {Dp("Go", PayloadDispatchResume{}), Dp("Go", SetVar("Health", 0)), Dp("Go", PayloadDispatchEntityCallFunction{"RestartThisLevel"})}, {Tagged("Pause Menu", "Hide")}, tags, false));
    AddLua(p.At(shown.back()), "Scripts/UI/RestartLevel");

    {
        PayloadDispatchLoadLevel menu;
        menu.levelName = "Main Menu";
        shown.push_back(p.Button("Main Menu", kMenuX, MenuY(64, 2), "Main Menu",
            {Dp("Go", PayloadDispatchResume{}), Dp("Go", menu)}, {Tagged("Pause Menu", "Hide")}, tags, false));
    }
    shown.push_back(p.Button("Quit", kMenuX, MenuY(64, 3), "Quit Game", {Dp("Go", PayloadDispatchQuit{})}, {}, tags, false));
    shown.push_back(p.Cursor("Pause Menu", false));

    for (const ID entityID : shown) p.Showable(entityID);
}

inline void BuildDeath(LayoutB& d) {
    const std::vector<Str> tags = {"Death Screen"};
    std::vector<ID> shown;

    // Stays enabled: a disabled entity only runs its Enable dispatch.
    // A moment to watch yourself fall, then stop the game and give back the cursor.
    const ID control = d.Add("Death Control", {0, 0}, {0, 0}, tags);
    d.At(control).dispatches.push_back(Dp("Show", PayloadDispatchPause{}, 1.2f));

    shown.push_back(d.Image("Dim", {0, 0}, {1, 0.84f}, "", {0.2f, 0, 0, 0.35f}, tags, false));
    shown.push_back(d.WordCentred("Title", 30, "You Died", tags, false, 2.0f));

    shown.push_back(d.Button("Try Again", 110, MenuY(90, 0), "Try Again",
        {Dp("Go", PayloadDispatchResume{}), Dp("Go", PayloadDispatchEntityCallFunction{"RestartThisLevel"})}, {Tagged("Death Screen", "Hide")}, tags, false));
    AddLua(d.At(shown.back()), "Scripts/UI/RestartLevel");

    PayloadDispatchLoadLevel menu;
    menu.levelName = "Main Menu";
    shown.push_back(d.Button("Main Menu", 110, MenuY(90, 1), "Main Menu",
        {Dp("Go", PayloadDispatchResume{}), Dp("Go", menu)}, {Tagged("Death Screen", "Hide")}, tags, false));
    shown.push_back(d.Cursor("Death Screen", false));

    for (const ID entityID : shown) d.Showable(entityID);
}

// Doom's screen between levels: its background, the score counting up, and a click or
// key to go on. IntermissionTally.lua does the counting; Scripts/UI has the rest.
inline void BuildIntermission(LayoutB& i) {
    const std::vector<Str> tags = {"Intermission"};
    std::vector<ID> shown;

    // Stays enabled: a disabled entity only runs its Enable dispatch.
    const ID control = i.Add("Intermission Control", {0, 0}, {0, 0}, tags);
    i.At(control).dispatches.push_back(Dp("Show", PayloadDispatchPause{}));
    i.At(control).dispatches.push_back(Dp("Show", SetVar("MenuState", 2)));

    shown.push_back(i.Image("Black", {0, 0}, {1, 1}, "", {0, 0, 0, 1}, tags, false));
    shown.push_back(i.Image("Background", D(0, 0), D(320, 200), "UI/Pictures/INTERPIC.png", {1, 1, 1, 1}, tags, false));

    shown.push_back(i.Label("Level Title", D(50, 3), "", 72, {0.92f, 0.92f, 0.92f, 1}, kHudFont, tags, false));
    shown.push_back(i.Word("Finished", 50, 22, "Finished", tags, false));

    const float digitW = static_cast<float>(WordSize("Tally Digit").first), digitH = static_cast<float>(WordSize("Tally Digit").second);
    const auto stat = [&](const Str& word, const float y, const Str& variable) {
        shown.push_back(i.Word(word, 50, y, word, tags, false));
        i.PictureNumber(word + " Tally", variable, "UI/Digits/Tally ", 255, y, digitW, digitH, 3, tags, false, 1, &shown);
        shown.push_back(i.Image(word + " Percent", D(255, y), D(255 + digitW, y + digitH), "UI/Digits/Tally percent.png", {1, 1, 1, 1}, tags, false));
    };
    stat("Kills", 56, "TallyKills");
    stat("Items", 80, "TallyItems");
    stat("Secret", 104, "TallySecrets");

    shown.push_back(i.Word("Time", 50, 138, "Time", tags, false));
    const float secondsRight = 255.0f + digitW, colonRight = secondsRight - digitW * 2.0f;
    i.PictureNumber("Minutes Tally", "TallyMinutes", "UI/Digits/Tally ", colonRight - digitW, 138, digitW, digitH, 2, tags, false, 1, &shown);
    shown.push_back(i.Image("Clock Colon", D(colonRight - digitW, 138), D(colonRight, 138 + digitH), "UI/Digits/Tally colon.png", {1, 1, 1, 1}, tags, false));
    i.PictureNumber("Seconds Tally", "TallySeconds", "UI/Digits/Tally ", secondsRight, 138, digitW, digitH, 2, tags, false, 2, &shown);

    {
        const ID prompt = i.WordCentred("Continue Prompt", 172, "Continue", tags, false);
        i.At(prompt).GetComponent<ComponentUISprite>()->isActive = false;
        shown.push_back(prompt);
    }

    const ID tally = i.Add("Tally", {0, 0}, {0, 0}, tags, false);
    AddLua(i.At(tally), "Scripts/UI/IntermissionTally");
    shown.push_back(tally);

    // The whole screen is the button. See-through, and on top of the rest.
    {
        const ID entityID = i.Image("Next Level", {0, 0}, {1, 1}, "", {1, 1, 1, 0}, tags, false);
        Entity& e = i.At(entityID);
        auto* button = e.AddComponent<ComponentUIButton>();
        button->hoverColor = button->normalColor;
        button->pressedColor = button->normalColor;

        PayloadDispatchLoadLevel next;
        next.nextLevel = true;
        const std::vector<DirectCall> goCalls = {Self("Go"), Tagged("Intermission", "Hide")};
        PayloadLuaSignal picked;
        picked.name = "Menu Pick Next Level";
        e.events.push_back(Ev(PayloadUIButtonRelease{}, goCalls, If("TallyDone", EQ, 1)));
        e.events.push_back(Ev(PayloadUIButtonRelease{}, {Self("Skip")}, If("TallyDone", EQ, 0)));
        e.events.push_back(Ev(picked, goCalls));
        e.dispatches.push_back(Dp("Go", PayloadDispatchResume{}));
        e.dispatches.push_back(Dp("Go", next));
        e.dispatches.push_back(Dp("Skip", SetVar("TallySkip", 1)));
        shown.push_back(entityID);
    }

    for (const ID entityID : shown) i.Showable(entityID);
}

inline void BuildVictory(LayoutB& v) {
    const std::vector<Str> tags = {"Victory"};
    std::vector<ID> shown;

    // Stays enabled: a disabled entity only runs its Enable dispatch.
    const ID control = v.Add("Victory Control", {0, 0}, {0, 0}, tags);
    v.At(control).dispatches.push_back(Dp("Show", PayloadDispatchPause{}));
    v.At(control).dispatches.push_back(Dp("Show", SetVar("MenuState", 2)));

    shown.push_back(v.Image("Black", {0, 0}, {1, 1}, "", {0, 0, 0, 1}, tags, false));
    shown.push_back(v.Image("Background", D(0, 0), D(320, 200), "UI/Pictures/BOSSBACK.png", {1, 1, 1, 1}, tags, false));
    shown.push_back(v.WordCentred("Title", 18, "You Win", tags, false, 2.0f));
    shown.push_back(v.Label("Text", D(40, 76), "The invasion is over. For now.", 44, {1, 0.85f, 0.2f, 1}, kHudFont, tags, false));
    shown.push_back(v.Label("Text 2", D(40, 100), "Change this text in the", 44, {1, 0.85f, 0.2f, 1}, kHudFont, tags, false));
    shown.push_back(v.Label("Text 3", D(40, 112), "Victory UI layout.", 44, {1, 0.85f, 0.2f, 1}, kHudFont, tags, false));

    PayloadDispatchLoadLevel menu;
    menu.levelName = "Main Menu";
    shown.push_back(v.Button("Main Menu", 110, 170, "Main Menu",
        {Dp("Go", PayloadDispatchResume{}), Dp("Go", menu)}, {Tagged("Victory", "Hide")}, tags, false));
    shown.push_back(v.Cursor("Victory", false));

    for (const ID entityID : shown) v.Showable(entityID);
}

// "Level 1 - Outpost" -> the word picture "Outpost".
inline Str LevelWord(const Str& levelName) {
    const auto dash = levelName.find(" - ");
    return dash == Str::npos ? levelName : levelName.substr(dash + 3);
}

inline void BuildMainMenu(LayoutB& m, const std::vector<Str>& levels) {
    m.Image("Black", {0, 0}, {1, 1}, "", {0, 0, 0, 1});
    m.Image("Background", D(0, 0), D(320, 200), "UI/Pictures/TITLEPIC.png");

    constexpr float x = 28.0f, top = 92.0f;
    const std::vector<Str> items = {"Main Items"};
    const std::vector<Str> list = {"Level List"};

    // "Health 0" is how a new game asks for a pistol start: the Player prefab
    // checks for it when the next level begins.
    PayloadDispatchLoadLevel next;
    next.nextLevel = true;
    m.Showable(m.Button("New Game", x, MenuY(top, 0), "New Game", {Dp("Go", SetVar("Health", 0)), Dp("Go", next)}, {}, items));
    m.Showable(m.Button("Select Level", x, MenuY(top, 1), "Select Level", {},
                        {Tagged("Main Items", "Hide"), Tagged("Level List", "Show")}, items));
    m.Showable(m.Button("Quit", x, MenuY(top, 2), "Quit Game", {Dp("Go", PayloadDispatchQuit{})}, {}, items));
    m.Showable(m.Cursor("Main Items"));

    // Choosing a level replaces the main list, as Doom's episode menu does.
    m.Showable(m.Word("Choose Level", x, MenuY(top, -1), "Choose Level", list, false));
    int row = 0;
    for (const Str& name : levels) {
        PayloadDispatchLoadLevel load;
        load.levelName = name;
        m.Showable(m.Button(name, x, MenuY(top, row++), LevelWord(name), {Dp("Go", SetVar("Health", 0)), Dp("Go", load)}, {}, list, false));
    }
    m.Showable(m.Button("Back", x, MenuY(top, row), "Back", {},
                        {Tagged("Level List", "Hide"), Tagged("Main Items", "Show")}, list, false));
    m.Showable(m.Cursor("Level List", false));
}

inline LayoutIDs MakeLayouts(const std::vector<Str>& playableLevels) {
    LayoutIDs ids;
    BuildLayout("HUD", ids.hud, BuildHud);
    BuildLayout("Pause Menu", ids.pause, BuildPause);
    BuildLayout("Death Screen", ids.death, BuildDeath);
    BuildLayout("Intermission", ids.intermission, BuildIntermission);
    BuildLayout("Victory", ids.victory, BuildVictory);
    BuildLayout("Main Menu", ids.mainMenu, [&](LayoutB& m) { BuildMainMenu(m, playableLevels); });
    return ids;
}
