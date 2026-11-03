// The Classic FPS Kit's levels, drawn from rectangles with the same code the
// editor's sector drawing and Make Door / Make Lift buttons run.
#pragma once

#include "Kit.hpp"
#include "Layouts.hpp"
#include "src/Editor/EditorInternal.hpp"

struct Room {
    Str name;
    float x0, z0, x1, z1;
    float floor, ceiling;
    Str wall, flat, ceilingFlat;
    float light = 200.0f;
    Str top, bottom;    // what its openings show above and below, if not `wall`
    bool sky = false;
    bool exactLight = false; // a lamp: isn't dimmed with the indoor rooms
    ID id = INVALID_ID;
};

struct LevelB {
    Level& level;
    const LayoutIDs& layouts;
    std::vector<Room> rooms;

    Room& R(const Str& name, float x0, float z0, float x1, float z1, float floor, float ceiling,
            const Str& wall, const Str& flat, const Str& ceilingFlat, float light = 200.0f) {
        rooms.push_back({name, x0, z0, x1, z1, floor, ceiling, wall, flat, ceilingFlat, light});
        return rooms.back();
    }

    // The room a detail is drawn inside. Details copy its heights and textures.
    Room OuterRoom(const Str& name) {
        for (const Room& r : rooms) if (r.name == name) return r;
        Fail(level.name + ": no room " + name);
        return {};
    }

    // A bright panel set into the ceiling, with the pool of light it throws. Doom's
    // lighting is all like this: a sector drawn just to be brighter than its room.
    void Lamp(const Str& outerName, float x0, float z0, float x1, float z1, const Str& flat = "TLITE6_1", float light = 255.0f) {
        Room r = OuterRoom(outerName);
        r.name = outerName + " Lamp";
        r.x0 = x0; r.z0 = z0; r.x1 = x1; r.z1 = z1;
        r.ceiling += 4.0f;
        r.ceilingFlat = flat;
        r.wall = "METAL";
        r.top.clear();
        r.bottom.clear();
        r.light = light;
        r.exactLight = true;
        rooms.push_back(r);
    }

    // A solid block: floor and ceiling at the same height. Outdoors, give it a
    // height, so the sky shows above it.
    void Pillar(const Str& outerName, float x0, float z0, float x1, float z1, const Str& texture, const float height = 0.0f) {
        Room r = OuterRoom(outerName);
        r.name = outerName + " Pillar";
        r.x0 = x0; r.z0 = z0; r.x1 = x1; r.z1 = z1;
        r.floor = height > 0.0f ? r.floor + height : r.ceiling;
        r.wall = r.top = r.bottom = texture;
        r.flat = r.ceilingFlat = "FLAT5";
        rooms.push_back(r);
    }

    // Keeps a sector's light changing from the moment the level starts.
    void LightFx(const Str& name, const PayloadDispatchSectorLightEffect::Type type, const float dim) {
        Sector* sector = Find(name);
        if (sector == nullptr) return;
        PayloadDispatchSectorLightEffect effect;
        effect.type = type;
        effect.dimLight = {dim, dim, dim};
        effect.brightTime = 1.2f;
        effect.dimTime = 0.25f;
        sector->events.push_back(Ev(PayloadOnStart{}, {Self("Light")}));
        sector->dispatches.push_back(Dp("Light", effect));
    }

    static SurfaceTexture Tex(const Str& path) {
        SurfaceTexture texture;
        TextureFrame frame;
        frame.texture = path;
        texture.frames.push_back(frame);
        return texture;
    }
    static SurfaceTexture WallTex(const Str& name) { return name.empty() ? SurfaceTexture{} : Tex("Textures/Walls/" + name + ".png"); }
    static SurfaceTexture FlatTex(const Str& name) { return name.empty() ? SurfaceTexture{} : Tex("Textures/Flats/" + name + ".png"); }

    // Draws the rooms, then fills them in. Rooms inside other rooms must come
    // after them, and must not cover the outer room's low corner.
    void Build() {
        for (const Room& r : rooms) {
            const auto result = MapTopology::ApplyDrawnGeometry(level,
                {{r.x0, r.z0}, {r.x1, r.z0}, {r.x1, r.z1}, {r.x0, r.z1}, {r.x0, r.z0}}, {});
            if (!result.success) Fail(level.name + ": room " + r.name + ": " + result.message);
        }

        std::map<ID, const Room*> byID;
        for (Room& r : rooms) {
            const int index = MapQueries::FindSectorContainingPoint(level.sectors, {r.x0 + 0.5f, r.z0 + 0.5f}, -1);
            if (index < 0) { Fail(level.name + ": no sector for room " + r.name); continue; }
            Sector& sector = level.sectors[index];
            r.id = sector.id;
            byID[r.id] = &r;
            sector.name = r.name;
            // Indoors stays dim, so that lamps and doorways stand out. Outdoors is daylight.
            const float light = r.sky || r.exactLight ? r.light : r.light * 0.75f;
            sector.light = {light, light, light};
            SectorFloor& floor = sector.floors[0];
            floor.floor.height = r.floor;
            floor.floor.texture = FlatTex(r.flat);
            floor.ceiling.height = r.ceiling;
            floor.ceiling.texture = FlatTex(r.ceilingFlat);
            if (r.sky) floor.ceiling.color.w = 0.0f;
        }

        for (Wall& wall : level.walls) {
            wall.color = {1, 1, 1, 1};
            const auto front = byID.find(wall.frontSector);
            const auto back = byID.find(wall.backSector);
            const Room* a = front != byID.end() ? front->second : nullptr;
            const Room* b = back != byID.end() ? back->second : nullptr;

            if (a == nullptr || b == nullptr) {
                const Room* only = a != nullptr ? a : b;
                if (only != nullptr) wall.top.texture = WallTex(only->wall);
                continue;
            }

            // An opening: above it the lower ceiling's room decides, below it the
            // higher floor's, and otherwise the wall of the taller side shows.
            const Room* lowCeiling = a->ceiling <= b->ceiling ? a : b;
            const Room* highCeiling = a->ceiling <= b->ceiling ? b : a;
            const Room* highFloor = a->floor >= b->floor ? a : b;
            const Room* lowFloor = a->floor >= b->floor ? b : a;
            wall.top.texture = WallTex(!lowCeiling->top.empty() ? lowCeiling->top : highCeiling->wall);
            wall.bottom.texture = WallTex(!highFloor->bottom.empty() ? highFloor->bottom : lowFloor->wall);
        }
        MapQueries::RebuildSectorRuntimeLinks(level);
    }

    Sector* Find(const Str& name) {
        for (Sector& sector : level.sectors) if (sector.name == name) return &sector;
        Fail(level.name + ": no sector " + name);
        return nullptr;
    }

    float FloorAt(const float x, const float z) {
        const int index = MapQueries::FindSectorContainingPoint(level.sectors, {x, z}, -1);
        if (index < 0) { Fail(level.name + ": something placed outside the map at " + std::to_string(x) + "," + std::to_string(z)); return 0.0f; }
        return level.sectors[index].floors[0].floor.height;
    }

    // Places a prefab. yaw 0 faces +z (north on the map); 180 faces south.
    ID Put(const Str& prefab, const float x, const float z, const float yaw = 180.0f, const float aboveFloor = 0.0f) {
        const ID id = Prefab::Instantiate(level, "Prefabs/" + prefab + ".prefab");
        if (id == INVALID_ID) { Fail("can't place " + prefab); return id; }
        auto* transform = level.GetEntity(id)->GetComponent<ComponentTransform>();
        transform->position = {x, FloorAt(x, z) + aboveFloor, z};
        transform->rotation = Quaternion::FromEulerDegrees(0.0f, yaw, 0.0f);
        return id;
    }

    // Places a prefab flat on the wall at (x, z), snapped to it the way the inspector's
    // Snap to Wall does, on the side (fromX, fromZ) is on. The snap is saved with the
    // level, so the editor shows it and keeps the prefab on the wall.
    ID PutOnWall(const Str& prefab, const float x, const float z, const float fromX, const float fromZ, const float aboveFloor = 0.0f) {
        const Wall* wall = nullptr;
        for (const Wall& each : level.walls) {
            const float dx = each.end.x - each.start.x, dz = each.end.y - each.start.y;
            const float lengthSq = dx * dx + dz * dz;
            if (lengthSq <= 0.0f) continue;
            const float t = std::clamp(((x - each.start.x) * dx + (z - each.start.y) * dz) / lengthSq, 0.0f, 1.0f);
            const float ox = each.start.x + dx * t - x, oz = each.start.y + dz * t - z;
            if (ox * ox + oz * oz < 0.0001f) { wall = &each; break; }
        }
        if (wall == nullptr) { Fail(level.name + ": no wall at " + std::to_string(x) + "," + std::to_string(z)); return INVALID_ID; }

        const ID id = Put(prefab, fromX, fromZ, 0.0f, aboveFloor);
        if (id == INVALID_ID) return id;

        const float dx = wall->end.x - wall->start.x, dz = wall->end.y - wall->start.y;
        const float length = std::sqrt(dx * dx + dz * dz);
        const Vector2 normal = {-dz / length, dx / length};
        const float side = (fromX - x) * normal.x + (fromZ - z) * normal.y < 0.0f ? -1.0f : 1.0f;

        WallSnapData snap;
        snap.wallID = wall->id;
        snap.side = side;
        level.wallSnaps[id] = snap;

        auto* transform = level.GetEntity(id)->GetComponent<ComponentTransform>();
        transform->position.x = x + normal.x * side * snap.gap;
        transform->position.z = z + normal.y * side * snap.gap;
        transform->rotation = Quaternion::FromAxisAngle(0.0f, 1.0f, 0.0f, std::atan2(normal.x * side, normal.y * side));
        return id;
    }

    void Note(const float x, const float z, const Str& text) {
        EditorComment comment;
        comment.id = level.nextCommentID++;
        comment.target = EditorCommentTarget::PIN;
        comment.position = {x, z};
        comment.text = text;
        level.comments.push_back(std::move(comment));
    }

    PayloadDispatchSectorDoor* DoorOf(Sector& sector) {
        for (Dispatch& d : sector.dispatches)
            if (auto* door = std::get_if<PayloadDispatchSectorDoor>(&d.payload)) return door;
        return nullptr;
    }

    // The same as the sector inspector's Make Door button, plus sounds.
    void MakeDoor(const Str& name, const Str& requiredTag = "", const Str& lockedMessage = "") {
        Sector* sector = Find(name);
        if (sector == nullptr) return;
        MapEditorInternal::MakeDoor(level, sector->id, 0, 2.0f, 3.0f);
        auto* door = DoorOf(*sector);
        door->openSound = Sound("World/door open");
        door->closeSound = Sound("World/door close");
        door->openSpeed = 30.0f;
        door->closeSpeed = 30.0f;
        if (!requiredTag.empty()) {
            door->locked = true;
            door->requiredTag = Tag(requiredTag);
            door->lockedSound = Sound("Player/no way");
            door->lockedMessage = lockedMessage;
        }
    }

    // A door that listens on an event tag and stays open: monster closets, boss gates.
    void MakeTrapDoor(const Str& name, const Str& linkTag) {
        Sector* sector = Find(name);
        if (sector == nullptr) return;
        sector->floors[0].ceiling.height = sector->floors[0].floor.height;
        PayloadDispatchSectorDoor door;
        door.type = PayloadDispatchSectorDoor::OPEN_STAY;
        door.lip = 0.0f;
        door.openSound = Sound("World/door open");
        door.openSpeed = 40.0f;
        door.pathfindersCanOpen = false;
        Dispatch dispatch;
        dispatch.payload = door;
        dispatch.useTag = true;
        dispatch.tagId = LinkTag(linkTag);
        sector->dispatches.push_back(dispatch);
    }

    // The same as the sector inspector's Make Lift button: down when used or stepped on.
    void MakeLift(const Str& name) {
        Sector* sector = Find(name);
        if (sector == nullptr) return;
        MapEditorInternal::MakeLiftSettings settings;
        settings.motion = MapEditorInternal::MakeLiftSettings::DOWN_WAIT_UP;
        settings.speed = 30.0f;
        MapEditorInternal::MakeLift(level, sector->id, 0, settings);
        for (Dispatch& d : sector->dispatches) {
            if (auto* lift = std::get_if<PayloadDispatchSectorLift>(&d.payload)) {
                lift->startSound = Sound("World/lift start");
                lift->stopSound = Sound("World/lift stop");
            }
        }
    }

    // A floor that hurts unless you wear the Radiation Suit. It works through the
    // same "Take Damage" dispatch enemies use.
    void MakeToxic(const Str& name, const float damage) {
        Sector* sector = Find(name);
        if (sector == nullptr) return;
        PayloadSectorEntityStay stay;
        stay.filter = Only("Player");
        stay.interval = 1.0f;
        sector->events.push_back(Ev(stay, {Self("Set Toxic Damage")}, If("RadSuit", EQ, 0)));
        sector->dispatches.push_back(Dp("Set Toxic Damage", SetVar("IncomingDamage", damage), 0.0f, {Then(Inst("Take Damage"))}));
    }

    // Splashes when things land in or wade through it (LiquidSplashes.lua), and takes no
    // blood stains. kind is Water, Nukage or Lava.
    void MakeLiquid(const Str& name, const Str& kind) {
        Sector* sector = Find(name);
        if (sector == nullptr) return;
        sector->tagIds.push_back(Tag("Liquid"));
        sector->tagIds.push_back(Tag(kind));
    }

    // One looping track, faded when the next level has a different one. The volumes
    // bring every track to about the same loudness, under the sound effects.
    void Music(const Str& track, const float volume) {
        auto& music = level.worldSettings.music;
        music.tracks = {"Music/" + track + ".ogg"};
        music.mode = MusicSettings::LOOP;
        music.volume = volume;
        music.fade = 1.0f;
    }

    void Finish(const bool withHud = true, const Str& skyTexture = "") {
        auto& world = level.worldSettings;
        if (withHud) world.uiLayouts = {layouts.hud, layouts.intermission, layouts.victory, layouts.death, layouts.pause};

        world.messageStyle.font = "Fonts/Classic HUD.ttf";
        world.messageStyle.fontSize = 34.0f;
        world.messageStyle.shadow = true;
        world.messageStyle.anchor = MessageAnchor::TOP_LEFT;
        world.messageStyle.align = MessageTextAlign::LEFT;
        world.messageStyle.offset = {12.0f, 28.0f}; // below the FPS counter of the editor's Play mode

        // Doom's sound falloff, at this kit's scale: full volume up close, silent far away.
        level.listenerSettings.distanceModel = AL_LINEAR_DISTANCE_CLAMPED;
        level.listenerSettings.oneShotReferenceDistance = 40.0f;
        level.listenerSettings.oneShotMaxDistance = 350.0f;

        // Far things fall into the dark, as in Doom. The engine's default fade starts
        // further away than most of these rooms are long. Daylight fades less.
        auto& renderer = level.rendererSettings;
        renderer.distanceLightStart = skyTexture.empty() ? 32.0f : 96.0f;
        renderer.distanceLightEnd = skyTexture.empty() ? 380.0f : 520.0f;
        renderer.distanceLightDarkest = skyTexture.empty() ? 0.2f : 0.4f;

        if (!skyTexture.empty()) {
            level.sky.mode = SkyMode::Cylinder;
            level.sky.texture = "Textures/Sky/" + skyTexture + ".png";
        }

        // Links every placed prefab up with what it overrides.
        for (Entity& entity : level.entities) {
            if (entity.prefabLink.prefab.empty()) continue;
            if (const Level* prefab = Prefab::Get(entity.prefabLink.prefab)) Prefab::RefreshOverrides(level, entity, *prefab);
        }

        Str error;
        const fs::path file = gAssets / "Levels" / (level.name + ".bson");
        fs::create_directories(file.parent_path());
        if (!LevelSerialization::SaveLevelToFile(file, level, nullptr, &error)) Fail(level.name + ": " + error);
        std::cout << level.name << ": " << level.sectors.size() << " sectors, " << level.walls.size() << " walls, "
                  << level.entities.size() << " entities\n";
    }
};

// Runs fill on a new, current level and saves it.
inline void BuildLevel(const Str& name, const LayoutIDs& layouts, const std::function<void(LevelB&)>& fill) {
    LevelManager::loadedLevels.clear();
    LevelManager::loadedLevels.emplace_back();
    LevelManager::currentLevelIndex = 0;
    Level& level = LevelManager::loadedLevels[0];
    level.name = name;
    LevelB b{level, layouts};
    fill(b);
}

// ── Main Menu ───────────────────────────────────────────────────────────────

inline void LevelMainMenu(LevelB& b) {
    b.R("Backdrop", -32, -32, 32, 32, 0, 40, "STARTAN1", "FLOOR0_1", "CEIL3_1", 120);
    b.Build();
    const ID camera = b.level.CreateEntity(false);
    Entity& e = *b.level.GetEntity(camera);
    e.name = "Menu Camera";
    e.GetComponent<ComponentTransform>()->position = {0, 10, 0};
    b.level.ActivateCamera(*e.AddComponent<ComponentCamera>());

    b.level.worldSettings.lockCursorOnStart = false;
    b.level.worldSettings.hideCursorOnStart = false;
    b.level.worldSettings.uiLayouts = {b.layouts.mainMenu};

    b.Note(0, 20, "MAIN MENU. This level only holds a camera: the menu itself is the UI layout 'UI Layouts/Main Menu'. "
                  "New Game loads the next level in Project Settings > Levels, so keep this level at the top of that list.");
    b.Music("Post Apocalyptic Wastelands", 0.84f);
    b.Finish(false);
}

// ── Level 1: every basic idea, one after the other ──────────────────────────

inline void Level1(LevelB& b) {
    const Str wall = "STARTAN2", floor = "FLOOR0_1", ceil = "CEIL3_1";

    b.R("Start", -48, 0, 48, 96, 0, 48, wall, floor, ceil);
    {Room& r = b.R("Door 1", -16, 96, 16, 104, 0, 48, "DOORTRAK", "FLAT5", "FLAT5"); r.top = "DOOR1";}
    b.R("Corridor", -16, 104, 16, 168, 0, 40, "STARTAN3", "FLOOR0_3", "CEIL3_3", 150);
    {Room& r = b.R("Step 1", -16, 168, 16, 176, 4, 44, "STARTAN3", "STEP1", ceil, 170); r.bottom = "STEP1";}
    {Room& r = b.R("Step 2", -16, 176, 16, 184, 8, 48, "STARTAN3", "STEP1", ceil, 180); r.bottom = "STEP1";}
    {Room& r = b.R("Step 3", -16, 184, 16, 192, 12, 52, "STARTAN3", "STEP1", ceil, 190); r.bottom = "STEP1";}
    {Room& r = b.R("Hall", -80, 192, 80, 288, 16, 88, "BROWN1", "FLOOR4_8", "CEIL1_1", 220); r.bottom = "STEP1";}
    {Room& r = b.R("Lift", -16, 288, 16, 320, 56, 128, "SUPPORT3", "STEP2", "CEIL3_5", 200); r.bottom = "PLAT1";}
    b.R("Ledge", -56, 320, 56, 376, 56, 112, "BROWN96", "FLOOR5_1", "CEIL3_5", 170);
    {Room& r = b.R("Door 2", -16, 376, 16, 384, 56, 112, "DOORTRAK", "FLAT5", "FLAT5"); r.top = "BIGDOOR2";}
    b.R("Exit Room", -32, 384, 32, 432, 56, 104, "EXITSTON", "FLOOR0_6", "CEIL4_1", 240);

    // Pillars and lamps: rooms drawn inside rooms. These are the sectors that stop a
    // level looking like a row of boxes.
    // A secret: this door looks like the hall's wall.
    {Room& r = b.R("Secret Door", 80, 232, 88, 248, 16, 72, "DOORTRAK", "FLAT5", "FLAT5"); r.top = "BROWN1";}
    b.R("Secret Closet", 88, 224, 120, 256, 16, 64, "STONE2", "FLOOR7_1", "CEIL3_1", 120);

    b.Lamp("Start", -12, 40, 12, 64);
    b.Pillar("Start", -40, 40, -32, 48, "SUPPORT3");
    b.Pillar("Start", 32, 40, 40, 48, "SUPPORT3");
    b.Lamp("Corridor", -6, 116, 6, 124);
    b.Lamp("Corridor", -6, 148, 6, 156);
    b.Lamp("Hall", -16, 228, 16, 252, "TLITE6_5");
    b.Pillar("Hall", -44, 216, -36, 224, "SUPPORT3");
    b.Pillar("Hall", 36, 216, 44, 224, "SUPPORT3");
    b.Pillar("Hall", -44, 256, -36, 264, "SUPPORT3");
    b.Pillar("Hall", 36, 256, 44, 264, "SUPPORT3");
    b.Lamp("Ledge", -12, 340, 12, 360);
    b.Pillar("Ledge", -40, 332, -32, 340, "BROWN144");
    b.Pillar("Ledge", 32, 332, 40, 340, "BROWN144");
    b.Lamp("Exit Room", -8, 400, 8, 416, "TLITE6_6");
    b.Build();

    b.LightFx("Corridor", PayloadDispatchSectorLightEffect::FLICKER, 60.0f);
    b.MakeDoor("Secret Door");
    b.MakeDoor("Door 1");
    b.MakeDoor("Door 2");
    b.MakeLift("Lift");

    b.Put("Player/Player", 0, 24, 0);
    b.Put("Pickups/Ammo/Clip", -24, 72);
    b.Put("Pickups/Ammo/Grenade", -12, 72);
    b.Put("Pickups/Health/Stimpack", 24, 72);
    b.Put("Decorations/Tall Lamp", -40, 88);
    b.Put("Decorations/Tall Lamp", 40, 88);

    // A trail of bonuses, which count as Items on the Intermission.
    for (const float z : {108.0f, 118.0f, 128.0f}) b.Put("Pickups/Health/Health Bonus", 10, z);
    for (const float x : {-70.0f, -60.0f, -50.0f}) b.Put("Pickups/Armor/Armor Bonus", x, 276);
    b.Put("Pickups/Armor/Green Armor", 104, 240);
    b.Put("Level Logic/Secret Spot", 96, 240);

    b.Put("Enemies/Zombie", -6, 140);
    b.Put("Enemies/Zombie", 6, 156);

    b.Put("Weapons/Pickups/Shotgun", 0, 200);
    b.Put("Pickups/Ammo/Shells", -12, 200);
    b.Put("Pickups/Ammo/Shells", 12, 200);
    b.Put("Enemies/Zombie", -56, 260);
    b.Put("Enemies/Shotgun Zombie", 0, 270);
    b.Put("Enemies/Zombie", 56, 260);
    b.Put("Hazards/Explosive Barrel", -30, 276);
    b.Put("Hazards/Explosive Barrel", 30, 276);
    b.Put("Decorations/Column", -72, 200);
    b.Put("Decorations/Column", 72, 200);
    b.Put("Decorations/Column", -72, 280);
    b.Put("Decorations/Column", 72, 280);
    b.Put("Pickups/Armor/Green Armor", -70, 240);
    b.Put("Pickups/Ammo/Clip", 70, 240);

    b.Put("Enemies/Serpentipede", -40, 350);
    b.Put("Enemies/Serpentipede", 40, 350);
    b.Put("Pickups/Health/Medikit", 0, 350);
    b.PutOnWall("Level Logic/Exit Switch", 0, 432, 0, 412);

    b.Note(0, 8, "WELCOME! These yellow notes are comments: they only exist in the editor. "
                 "Press Play to try the level. WASD moves, the mouse looks, E uses things, left click shoots, Esc pauses.");
    b.Note(0, 48, "THE PLAYER is a prefab: Prefabs/Player/Player. To start a level somewhere else, just move it. "
                  "Every level needs exactly one.");
    b.Note(-24, 60, "PICKUPS are prefabs too. Drag any from Prefabs/Pickups onto the map. They need no setup.");
    b.Note(0, 92, "A DOOR is a thin sector whose ceiling sits on its floor. Select it and look at the Make Door button in the "
                  "sector inspector: that's how this one was made.");
    b.Note(10, 130, "ENEMIES: drag from Prefabs/Enemies. They wake when they see or hear you and open doors by themselves.");
    b.Note(24, 180, "STAIRS are just small sectors, each a little higher than the last. The player can climb 6 units per step.");
    b.Note(-40, 210, "A sector's floor and ceiling heights are the whole idea of map making. "
                     "This hall is a big rectangle with a higher ceiling.");
    b.Note(-40, 228, "PILLARS AND LAMPS are small sectors drawn inside this one. A pillar's floor and ceiling are the same height. "
                     "A lamp is just brighter, with a light texture on a slightly higher ceiling.");
    b.Note(74, 240, "A SECRET: this bit of wall is a door. Its Secret Spot prefab counts it on the Intermission.");
    b.Note(-10, 112, "FLICKERING LIGHT: select this corridor. Its On Start event runs a Light Effect dispatch.");
    b.Note(30, 300, "A LIFT: this sector's floor starts up high. Face its wall and press E, it comes down, step on and ride up. "
                    "Made with Make Lift in the sector inspector.");
    b.Note(0, 428, "THE EXIT SWITCH shows the Intermission and then loads the next level in Project Settings > Levels. "
                   "Drag Prefabs/Level Logic/Exit Switch into any level, then click Snap to Wall in its inspector and right-click a wall.");
    b.Music("Rocky Musicloop", 0.37f);
    b.Finish();
}

// ── Level 2: keys, a secret, toxic floor, raising stairs, a teleporter ──────

inline void Level2(LevelB& b) {
    const Str tech = "TEKWALL4", gray = "GRAY7";

    b.R("Hub", -96, 0, 96, 128, 0, 72, gray, "FLOOR0_6", "CEIL5_1", 190);
    {Room& r = b.R("Toxic Pool", -32, 48, 32, 96, -4, 72, gray, "NUKAGE1", "CEIL5_1", 230); r.bottom = "STEP3";}

    // West: the unlocked way, with the red key.
    {Room& r = b.R("West Door", -104, 48, -96, 80, 0, 56, "DOORTRAK", "FLAT5", "FLAT5"); r.top = "DOOR1";}
    b.R("West Room", -200, 16, -104, 112, 0, 64, tech, "FLOOR1_6", "TLITE6_1", 170);

    // North: behind the red door, stairs that rise when you press a switch.
    {Room& r = b.R("Red Door", -16, 128, 16, 136, 0, 56, "DOORTRAK", "FLAT5", "FLAT5"); r.top = "DOORRED";}
    b.R("North Room", -72, 136, 72, 216, 0, 96, "BROWNGRN", "FLOOR4_6", "CEIL3_2", 160);
    b.R("Stair 1", -16, 216, 16, 228, 0, 96, "BROWNGRN", "STEP2", "CEIL3_2", 160);
    b.R("Stair 2", -16, 228, 16, 240, 0, 96, "BROWNGRN", "STEP2", "CEIL3_2", 160);
    b.R("Stair 3", -16, 240, 16, 252, 0, 96, "BROWNGRN", "STEP2", "CEIL3_2", 160);
    b.R("Stair 4", -16, 252, 16, 264, 0, 96, "BROWNGRN", "STEP2", "CEIL3_2", 160);
    b.R("Key Platform", -40, 264, 40, 312, 24, 96, "BROWNGRN", "FLOOR5_1", "CEIL3_2", 200);
    b.R("Pad Room", 40, 272, 72, 304, 24, 64, "COMPBLUE", "GATE1", "TLITE6_5", 240);

    // East: behind the blue door, the way out.
    {Room& r = b.R("Blue Door", 96, 48, 104, 80, 0, 56, "DOORTRAK", "FLAT5", "FLAT5"); r.top = "DOORBLU";}
    b.R("East Hall", 104, 16, 200, 112, 0, 64, tech, "FLOOR3_3", "TLITE6_6", 180);
    {Room& r = b.R("Exit Door", 200, 48, 208, 80, 0, 56, "DOORTRAK", "FLAT5", "FLAT5"); r.top = "EXITDOOR";}
    b.R("Exit Room", 208, 32, 256, 96, 0, 56, "EXITSTON", "FLOOR0_7", "CEIL4_2", 240);

    // South: a secret behind a wall that looks like any other.
    {Room& r = b.R("Secret Door", -16, -8, 16, 0, 0, 56, "DOORTRAK", "FLAT5", "FLAT5"); r.top = gray;}
    b.R("Secret Closet", -24, -48, 24, -8, 0, 48, "STONE2", "FLOOR7_1", "CEIL3_1", 120);

    b.Lamp("Hub", -12, 106, 12, 122, "TLITE6_5");
    b.Pillar("Hub", -64, 4, -56, 12, "METAL2");
    b.Pillar("Hub", 56, 4, 64, 12, "METAL2");
    b.Pillar("Hub", -64, 104, -56, 112, "METAL2");
    b.Pillar("Hub", 56, 104, 64, 112, "METAL2");
    b.Pillar("West Room", -170, 40, -162, 48, "SUPPORT2");
    b.Pillar("West Room", -170, 80, -162, 88, "SUPPORT2");
    b.Lamp("North Room", -16, 160, 16, 190, "TLITE6_5");
    b.Pillar("East Hall", 130, 42, 138, 50, "SUPPORT2");
    b.Pillar("East Hall", 166, 42, 174, 50, "SUPPORT2");
    b.Pillar("East Hall", 130, 78, 138, 86, "SUPPORT2");
    b.Pillar("East Hall", 166, 78, 174, 86, "SUPPORT2");
    b.Lamp("Exit Room", 224, 56, 240, 72, "TLITE6_6");
    b.Build();

    b.LightFx("East Hall", PayloadDispatchSectorLightEffect::FLICKER, 50.0f);
    b.MakeDoor("West Door");
    b.MakeDoor("Red Door", "RedKey", "You need a red key to open this door.");
    b.MakeDoor("Blue Door", "BlueKey", "You need a blue key to open this door.");
    b.MakeDoor("Exit Door");
    b.MakeDoor("Secret Door");
    b.MakeToxic("Toxic Pool", 5.0f);
    b.MakeLiquid("Toxic Pool", "Nukage");

    // The switch's event tag "Switch 1" reaches the first stair, which raises the rest.
    {
        Sector* stair = b.Find("Stair 1");
        PayloadDispatchSectorRaiseStairs raise;
        raise.stepHeight = 5.0f;
        raise.speed = 12.0f;
        Dispatch dispatch;
        dispatch.payload = raise;
        dispatch.useTag = true;
        dispatch.tagId = LinkTag("Switch 1");
        stair->dispatches.push_back(dispatch);
    }

    b.Put("Player/Player", 0, 16, 0);
    b.Put("Pickups/Powerups/Radiation Suit", 0, 40);
    b.Put("Pickups/Ammo/Ammo Box", 0, 72);   // in the pool
    b.Put("Enemies/Shotgun Zombie", -80, 110);
    b.Put("Enemies/Shotgun Zombie", 80, 110);
    b.Put("Decorations/Short Green Torch", -90, 8);
    b.Put("Decorations/Short Green Torch", 90, 8);

    b.Put("Enemies/Serpentipede", -180, 30);
    b.Put("Enemies/Serpentipede", -180, 100);
    b.Put("Enemies/Zombie", -130, 96);
    b.Put("Pickups/Keys/Red Key", -190, 64);
    b.Put("Weapons/Pickups/Super Shotgun", -150, 64);
    b.Put("Pickups/Ammo/Shell Box", -160, 30);
    b.Put("Pickups/Ammo/Grenade Box", -146, 30);

    b.Put("Enemies/Flesh Worm", -50, 200);
    b.Put("Enemies/Flesh Worm", 50, 200);
    b.PutOnWall("Level Logic/Switch", -72, 180, -60, 180);
    b.Put("Pickups/Health/Medikit", 60, 150);
    b.Put("Pickups/Keys/Blue Key", 0, 300);
    b.Put("Enemies/Serpentipede", -30, 290);
    {
        // The teleporter on the platform takes you back to the hub.
        const ID pad = b.Put("Level Logic/Teleport Trigger", 56, 288);
        const ID destination = b.Put("Level Logic/Teleport Destination", 60, 24, 270);
        b.level.GetEntity(destination)->name = "Back To The Hub";
        b.level.scripts.Get(pad)->publicValues["destination"] = EntityRefValue{destination, ""};
    }

    b.Put("Enemies/Minigun Zombie", 180, 40);
    b.Put("Enemies/Trilobite", 150, 90, 180, 16);
    b.Put("Pickups/Ammo/Shell Box", 120, 100);
    b.Put("Pickups/Armor/Armor Bonus", 120, 30);
    b.Put("Pickups/Armor/Armor Bonus", 130, 30);
    b.Put("Pickups/Armor/Armor Bonus", 140, 30);
    b.PutOnWall("Level Logic/Exit Switch", 256, 64, 240, 64);

    b.Put("Pickups/Health/Soul Sphere", 0, -30);
    b.Put("Level Logic/Secret Spot", 0, -24);

    b.Note(0, 110, "THIS HUB shows how keys work: the west room has the red key, the red door hides the blue key, the blue door hides the exit.");
    b.Note(-100, 40, "An unlocked door. Enemies can open these too.");
    b.Note(-190, 80, "KEYS: Prefabs/Pickups/Keys. Taking one adds a tag to the player, and the HUD shows it.");
    b.Note(30, 140, "A LOCKED DOOR is a normal door with Locked ticked on its Door dispatch, and the player's key tag as the Required Tag.");
    b.Note(0, 60, "TOXIC FLOOR: select this sector. Its Entity Stay event hurts the player every second, unless RadSuit is on. "
                  "Copy these two lines to any sector. NUKAGE1 is a flat from Textures/Flats.");
    b.Note(-40, 170, "THIS SWITCH fires the event tag 'Switch 1'. The first stair has a Raise Stairs dispatch listening on the same tag. "
                     "Stairs follow neighbouring sectors with the same floor texture.");
    b.Note(56, 260, "A TELEPORTER: Prefabs/Level Logic/Teleport Trigger. Its Destination field holds the Teleport Destination at the hub. "
                    "Drag it there in the inspector.");
    b.Note(0, -4, "A SECRET: this door looks like the wall around it, and the Secret Spot prefab behind it counts it on the Intermission.");
    b.Music("Heavy Battle 2", 0.35f);
    b.Finish();
}

// ── Level 3: outdoors, a sky, and a trap ────────────────────────────────────

inline void Level3(LevelB& b) {
    {Room& r = b.R("Courtyard", -160, 0, 160, 320, 0, 136, "ROCK2", "GRASS1", "", 255); r.sky = true;}
    // A building: a solid block (floor and ceiling at the same height, so nothing
    // fits inside) with a room drawn inside it, and a door cut through to the room.
    {Room& r = b.R("Building Walls", -56, 104, 56, 216, 64, 64, "BRICK7", "FLAT5", "FLAT5", 200); r.top = "BRICK7"; r.bottom = "BRICK7";}
    b.R("Building", -48, 112, 48, 208, 0, 64, "BRICK7", "FLOOR5_2", "CEIL5_2", 170);
    {Room& r = b.R("Building Door", -16, 104, 16, 112, 0, 52, "DOORTRAK", "FLAT5", "FLAT5"); r.top = "BIGDOOR1";}

    // Monster closets in the outer walls. They open when you take the key.
    const auto closet = [&](const Str& name, float x0, float z0, float x1, float z1, const Str& gateName, float gx0, float gz0, float gx1, float gz1) {
        b.R(name, x0, z0, x1, z1, 0, 64, "BRICK9", "RROCK09", "RROCK09", 110);
        Room& gate = b.R(gateName, gx0, gz0, gx1, gz1, 0, 64, "ROCK2", "RROCK09", "RROCK09", 150);
        gate.top = "ROCK2";
    };
    closet("West Closet", -216, 136, -168, 184, "West Gate", -168, 144, -160, 176);
    closet("East Closet", 168, 136, 216, 184, "East Gate", 160, 144, 168, 176);
    closet("North Closet", -24, 328, 24, 376, "North Gate", -16, 320, 16, 328);

    // A ledge reached by a lift, with the rocket launcher on it.
    {Room& r = b.R("Ledge", -160, 280, -96, 320, 48, 136, "ROCK2", "RROCK13", "", 255); r.sky = true; r.bottom = "ROCK3";}
    {Room& r = b.R("Courtyard Lift", -120, 256, -96, 280, 48, 136, "ROCK2", "STEP2", "", 255); r.sky = true; r.bottom = "PLAT1";}

    // The way out needs the yellow key.
    {Room& r = b.R("Yellow Door", 120, 320, 152, 328, 0, 56, "DOORTRAK", "FLAT5", "FLAT5"); r.top = "DOORYEL";}
    b.R("Exit Room", 104, 328, 168, 376, 0, 56, "EXITSTON", "FLOOR0_7", "CEIL4_2", 240);

    // Rock outcrops, a path up to the building, and a lamp inside it.
    b.Pillar("Courtyard", 84, 140, 100, 156, "ROCK3", 40);
    b.Pillar("Courtyard", -80, 226, -64, 238, "ROCK3", 28);
    b.Pillar("Courtyard", 70, 270, 90, 285, "ROCK3", 56);
    {Room r = b.OuterRoom("Courtyard"); r.name = "Path"; r.x0 = -20; r.z0 = 10; r.x1 = 20; r.z1 = 100; r.flat = "FLOOR7_1"; b.rooms.push_back(r);}
    b.Lamp("Building", -12, 150, 12, 170, "TLITE6_5");
    b.Lamp("Exit Room", 124, 344, 148, 360, "TLITE6_6");
    b.Build();

    b.LightFx("Building", PayloadDispatchSectorLightEffect::FLICKER, 50.0f);
    b.MakeDoor("Building Door");
    b.MakeDoor("Yellow Door", "YellowKey", "You need a yellow key to open this door.");
    b.MakeLift("Courtyard Lift");
    b.MakeTrapDoor("West Gate", "Trap");
    b.MakeTrapDoor("East Gate", "Trap");
    b.MakeTrapDoor("North Gate", "Trap");

    b.Put("Player/Player", 0, 24, 0);
    b.Put("Decorations/Burnt Tree", -110, 60);
    b.Put("Decorations/Burnt Tree", 120, 90);
    b.Put("Decorations/Big Tree", 110, 240);
    b.Put("Decorations/Stump", -130, 150);
    b.Put("Decorations/Skulls On Pole", -60, 40);
    b.Put("Decorations/Skull On Pole", 60, 40);

    b.Put("Enemies/Serpentipede", -100, 200);
    b.Put("Enemies/Serpentipede", 100, 200);
    b.Put("Enemies/Serpentipede", 0, 280);
    b.Put("Enemies/Shotgun Zombie", -60, 90);
    b.Put("Enemies/Shotgun Zombie", 60, 90);
    b.Put("Enemies/Deadflare", 0, 60, 180, 20);
    b.Put("Pickups/Ammo/Shell Box", 30, 30);
    b.Put("Pickups/Ammo/Grenade Box", 0, 30);
    b.Put("Pickups/Ammo/Ammo Box", -30, 30);

    b.Put("Weapons/Pickups/Minigun", -30, 130);
    b.Put("Pickups/Keys/Yellow Key", 0, 190);
    {
        // Touching the key sets the trap: the trigger's event tag opens the gates.
        const ID trigger = b.Put("Level Logic/Walk Over Trigger", 0, 186);
        Entity* e = b.level.GetEntity(trigger);
        e->events[0].tagId = LinkTag("Trap");
        e->name = "Trap Trigger";
    }
    b.Put("Pickups/Health/Medikit", 30, 190);
    b.Put("Pickups/Armor/Blue Armor", -30, 190);

    b.Put("Enemies/Flesh Worm", -192, 160, 90);
    b.Put("Enemies/Flesh Worm", -200, 150, 90);
    b.Put("Enemies/Pain Bringer", 192, 160, 270);
    b.Put("Enemies/Serpentipede", 200, 145, 270);
    b.Put("Enemies/Minigun Zombie", 0, 350);
    b.Put("Enemies/Serpentipede", 12, 360);

    b.Put("Weapons/Pickups/Rocket Launcher", -130, 300);
    b.Put("Pickups/Ammo/Rocket Box", -145, 300);
    b.Put("Pickups/Health/Medikit", -115, 300);
    b.Put("Enemies/Zombie", -150, 310);

    b.PutOnWall("Level Logic/Exit Switch", 136, 376, 136, 358);

    b.Note(0, 10, "OUTDOORS is a sector with no ceiling picture: its ceiling colour's alpha is 0, so the level's Sky shows through. "
                  "The Sky is in World Settings > Rendering. Keep the ceiling higher than anything near it.");
    b.Note(-80, 150, "A BUILDING is three sectors. A block whose floor and ceiling are the same height is solid: that's the walls. "
                    "A room drawn inside the block is the inside. A door sector joins the room to the courtyard.");
    b.Note(30, 170, "A TRAP: the Walk Over Trigger next to the key fires the event tag 'Trap'. "
                    "The three gates (thin sectors around the courtyard) have Door dispatches listening on it.");
    b.Note(-200, 190, "A MONSTER CLOSET is a room behind a shut door. Enemies can't see through it, so they wait until it opens.");
    b.Note(-108, 250, "Another lift: this one holds the way up to the rocket launcher.");
    b.Music("Swordfight III", 0.56f);
    b.Finish(true, "SKY2");
}

// ── Level 4: the last fight ─────────────────────────────────────────────────

inline void Level4(LevelB& b) {
    const Str rock = "SP_HOT1", marble = "MARBLE1";

    b.R("Antechamber", -48, 0, 48, 80, 0, 56, marble, "FLOOR7_2", "CEIL5_2", 170);
    {Room& r = b.R("Arena Door", -16, 80, 16, 88, 0, 56, "DOORTRAK", "FLAT5", "FLAT5"); r.top = "SPCDOOR1";}
    {Room& r = b.R("Arena", -176, 88, 176, 376, 0, 160, rock, "FLAT5_7", "", 200); r.sky = true;}
    {Room& r = b.R("Lava Pit", -64, 200, 64, 280, -8, 160, rock, "LAVA1", "", 255); r.sky = true; r.bottom = "FIREBLU1";}
    {Room& r = b.R("Island", -16, 224, 16, 256, 0, 160, rock, "FLOOR5_3", "", 230); r.sky = true; r.bottom = "MARBLE3";}
    {Room& r = b.R("Exit Gate", -16, 376, 16, 384, 0, 56, "DOORTRAK", "FLAT5", "FLAT5"); r.top = "EXITDOOR";}
    b.R("Exit Room", -32, 384, 32, 432, 0, 56, "EXITSTON", "FLOOR0_7", "CEIL4_2", 240);

    b.Lamp("Antechamber", -10, 28, 10, 50, "TLITE6_5");
    b.Pillar("Arena", -130, 200, -110, 220, "SP_HOT1", 64);
    b.Pillar("Arena", 110, 200, 130, 220, "SP_HOT1", 64);
    b.Pillar("Arena", -130, 260, -110, 280, "SP_HOT1", 64);
    b.Pillar("Arena", 110, 260, 130, 280, "SP_HOT1", 64);
    b.Lamp("Exit Room", -8, 400, 8, 416, "TLITE6_6");
    b.Build();

    b.LightFx("Lava Pit", PayloadDispatchSectorLightEffect::FIRE_FLICKER, 170.0f);
    b.MakeDoor("Arena Door");
    b.MakeTrapDoor("Exit Gate", "Boss Dead");
    b.MakeToxic("Lava Pit", 10.0f);
    b.MakeLiquid("Lava Pit", "Lava");

    b.Put("Player/Player", 0, 16, 0);
    b.Put("Weapons/Pickups/Plasma Rifle", -30, 60);
    b.Put("Weapons/Pickups/SKAG 1337", 30, 60);
    b.Put("Pickups/Ammo/Cell Pack", -30, 40);
    b.Put("Pickups/Ammo/Grenade Box", 14, 60);
    b.Put("Pickups/Ammo/Cell Pack", 30, 40);
    b.Put("Pickups/Ammo/Rocket Box", 0, 60);
    b.Put("Pickups/Powerups/Berserk", -40, 16);
    b.Put("Pickups/Health/Mega Sphere", 40, 16);

    b.Put("Decorations/Red Torch", -160, 100);
    b.Put("Decorations/Red Torch", 160, 100);
    b.Put("Decorations/Red Torch", -160, 364);
    b.Put("Decorations/Red Torch", 160, 364);
    b.Put("Decorations/Heart Pillar", -100, 180);
    b.Put("Decorations/Heart Pillar", 100, 180);
    b.Put("Decorations/Heart Pillar", -100, 300);
    b.Put("Decorations/Heart Pillar", 100, 300);

    b.Put("Pickups/Powerups/Invulnerability", 0, 240);
    b.Put("Pickups/Ammo/Cell Pack", -150, 240);
    b.Put("Pickups/Ammo/Cell Pack", 150, 240);
    b.Put("Pickups/Ammo/Rocket Box", -150, 130);
    b.Put("Pickups/Ammo/Rocket Box", 150, 130);
    b.Put("Pickups/Health/Medikit", -150, 340);
    b.Put("Pickups/Health/Medikit", 150, 340);

    b.Put("Enemies/Hangman", -120, 330);
    b.Put("Enemies/Hangman", 120, 330);
    b.Put("Enemies/Combat Slug", -130, 180, 90);
    b.Put("Enemies/Technospider", 130, 180, 270);
    b.Put("Enemies/Matribite", 0, 340, 180, 20);
    {
        // When the boss dies, the event tag "Boss Dead" opens the exit gate.
        const ID boss = b.Put("Enemies/Assault Tripod", 0, 310);
        Entity* e = b.level.GetEntity(boss);
        Event died = Ev(PayloadMonsterDied{}, {Self("Die")});
        died.useTag = true;
        died.tagId = LinkTag("Boss Dead");
        for (Event& event : e->events)
            if (std::holds_alternative<PayloadMonsterDied>(event.payload)) { event.useTag = true; event.tagId = died.tagId; }
    }

    b.PutOnWall("Level Logic/End Game Switch", 0, 432, 0, 412);

    b.Note(0, 30, "THE BOSS LEVEL. Everything you need is here. Take it, then open the door.");
    b.Note(30, 310, "THE BOSS is an ordinary enemy prefab with one change: its Monster Died event also fires the event tag 'Boss Dead'. "
                    "Look at its events: this level changed that one list, and everything else still follows the prefab.");
    b.Note(0, 372, "This gate has a Door dispatch listening on 'Boss Dead', so it opens when the boss falls.");
    b.Note(0, 428, "THE END GAME SWITCH shows the Victory screen instead of moving to the next level. Use it in your last level.");
    b.Music("Boss Battle 10", 0.37f);
    b.Finish(true, "SKY3");
}

// ── Starting points ─────────────────────────────────────────────────────────

inline void LevelBlank(LevelB& b) {
    b.R("First Room", -64, 0, 64, 128, 0, 56, "STARTAN3", "FLOOR0_1", "CEIL3_1");
    b.Build();
    b.Put("Player/Player", 0, 32, 0);
    b.Note(0, 8, "A CLEAN START. It already has a player and the full interface. Make a copy of this level in the Asset Browser, "
                 "draw more sectors from this room and drag in prefabs. Then drag your copy into place in Project Settings > Levels.");
    b.Music("Rocky Musicloop", 0.37f);
    b.Finish();
}

inline void LevelGallery(LevelB& b) {
    b.R("Gallery", -320, 0, 320, 560, 0, 112, "METAL2", "FLOOR4_8", "TLITE6_4", 230);
    b.R("Water Pool", -40, 500, 40, 548, -4, 112, "METAL2", "FWATER1", "TLITE6_4", 230);
    b.Build();
    b.MakeLiquid("Water Pool", "Water");
    b.Put("Player/Player", 0, 20, 0);

    const auto row = [&](const fs::path& folder, const Str& prefixPath, float z, const float spacing, const Str& title) {
        std::vector<Str> names;
        for (const auto& file : fs::directory_iterator(gAssets / "Prefabs" / folder))
            if (file.path().extension() == ".prefab") names.push_back(file.path().stem().string());
        std::sort(names.begin(), names.end());
        float x = -304.0f;
        b.Note(-310, z - 12, title);
        for (const Str& name : names) {
            if (x > 304.0f) { x = -304.0f; z += 26.0f; }
            const ID id = b.Put(prefixPath + "/" + name, x, z);
            if (Entity* e = b.level.GetEntity(id); e != nullptr) {
                if (auto* ai = e->GetComponent<ComponentMonsterAI>()) ai->isActive = false; // standing still for you
                if (auto* path = e->GetComponent<ComponentPathfinder>()) path->isActive = false;
            }
            x += spacing;
        }
    };
    row("Enemies", "Enemies", 40, 36, "ENEMIES. They stand still in this gallery only: their Monster AI is switched off here.");
    row("Weapons/Pickups", "Weapons/Pickups", 110, 24, "WEAPON PICKUPS");
    row("Pickups/Ammo", "Pickups/Ammo", 150, 24, "AMMO");
    row("Pickups/Health", "Pickups/Health", 180, 24, "HEALTH");
    row("Pickups/Armor", "Pickups/Armor", 210, 24, "ARMOR");
    row("Pickups/Keys", "Pickups/Keys", 240, 24, "KEYS");
    row("Pickups/Powerups", "Pickups/Powerups", 270, 24, "POWERUPS");
    row("Hazards", "Hazards", 300, 24, "HAZARDS");
    row("Decorations", "Decorations", 340, 22, "DECORATIONS. Solid ones have a collider, the others you walk through.");

    b.Note(0, 492, "A WATER POOL. To make a sector splash, give it the tags Liquid and Water, Nukage or Lava.");
    b.Note(0, 8, "EVERY PREFAB IN THE KIT, in rows. Play to walk around them. To use one, drag it from the Prefabs folder onto your own map.");
    b.Finish();
}
