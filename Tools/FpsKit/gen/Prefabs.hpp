// Every prefab of the Classic FPS Kit: effects, projectiles, enemies, weapons,
// pickups, decorations, level logic and the player.
#pragma once

#include <set>
#include <tuple>

#include "Kit.hpp"

// ── Effects: a picture that plays once and removes itself ───────────────────

inline Str MakeEffect(const Str& name, const Str& artName, const Str& letters, const float frameSeconds,
                      const Str& sound = "", const float blastRadius = 0.0f, const float blastDamage = 0.0f) {
    const json& art = Art("effects", artName);
    const auto frames = ArtFrames(art, letters, frameSeconds);
    const Str book = Book("Animations/Effects/" + name + ".fpk", frames, FlipbookLoopMode::Once);
    return MakePrefab("Prefabs/Effects/" + name + ".prefab", false, [&](Level&, Entity& e) {
        auto* sprite = AddSprite(e, art["w"], art["h"], false);
        SetSpriteFrame(sprite, frames[0]);
        e.AddComponent<ComponentFlipbook>()->flipbookFileName = book;

        e.events.push_back(Ev(PayloadOnStart{}, {Self("Remove"), Self("Sound")}));
        e.dispatches.push_back(Dp("Remove", PayloadDispatchEntityDestroy{}, TotalTime(frames)));
        if (!sound.empty()) e.dispatches.push_back(Dp("Sound", Play(sound)));

        if (blastRadius > 0.0f) {
            ComponentScript& blast = AddLua(e, "Scripts/Combat/Explosion");
            blast.publicValues["radius"] = blastRadius;
            blast.publicValues["damage"] = blastDamage;
        }
    });
}

inline void MakeEffects() {
    MakeEffect("Fireball Hit", "Fireball", "CDE", 0.12f, "World/fireball explode");
    MakeEffect("Red Ball Hit", "Plasma Ball Red", "CDE", 0.12f, "World/fireball explode");
    MakeEffect("Green Ball Hit", "Plasma Ball Green", "CDE", 0.12f, "World/fireball explode");
    MakeEffect("Plasma Hit", "Plasma Hit", "ABCDE", 0.07f);
    MakeEffect("Spider Plasma Hit", "Spider Hit", "ABCDE", 0.07f);
    MakeEffect("Tracer Hit", "Tracer Hit", "ABC", 0.12f, "Weapons/rocket explode");
    MakeEffect("Rocket Explosion", "Rocket", "BCD", 0.14f, "Weapons/rocket explode", 32.0f, 128.0f);
    MakeEffect("Slug Explosion", "Rocket", "BCD", 0.14f, "Weapons/rocket explode", 16.0f, 40.0f);
    MakeEffect("SKAG Explosion", "SKAG Hit", "ABCDEF", 0.14f, "Weapons/rocket explode", 90.0f, 600.0f);
    MakeEffect("Barrel Explosion", "Barrel Explosion", "ABCDE", 0.14f, "World/barrel explode", 32.0f, 128.0f);
    MakeEffect("Grenade Explosion", "Rocket", "BCD", 0.14f, "Weapons/rocket explode", 44.0f, 120.0f);
    MakeEffect("Teleport Fog", "Teleport Fog", "ABCDEFGHIJ", 0.1f);
    MakeEffect("Item Fog", "Item Fog", "ABCDE", 0.14f, "Items/item respawn");
}

// ── Particle effects: prefabs nothing ever spawns ───────────────────────────
//
// Fx.lua and Weapon.lua fire these with Game.EmitParticles: the prefab's emitters go
// off at a point and no entity is made. They're ordinary prefabs, so open one in the
// editor to change how they look.

inline std::vector<Str> FxPics(const Str& folder, const Str& name, const int count) {
    std::vector<Str> pictures;
    for (int i = 1; i <= count; ++i) pictures.push_back("Sprites/FX/" + folder + "/" + name + " " + std::to_string(i) + ".png");
    return pictures;
}

inline std::vector<Str> ArtPics(const json& art, const Str& letters) {
    std::vector<Str> pictures;
    for (const char letter : letters) pictures.push_back(art["path"].get<Str>() + "/" + letter + ".png");
    return pictures;
}

inline void MakeParticleEffect(const Str& name, const std::function<void(Entity&)>& fill) {
    MakePrefab("Prefabs/Effects/" + name + ".prefab", false, [&](Level&, Entity& e) { fill(e); });
}

inline void MakeParticleEffects() {
    using C = ParticleCollision;
    constexpr float kStainBlood = 0.85f;

    MakeParticleEffect("Blood", [](Entity& e) {
        // Doom's own puff of blood, at the hit.
        auto* puff = e.AddComponent<ComponentParticleEmitter>();
        puff->textures = ArtPics(Art("effects", "Blood"), "ABC");
        puff->animate = true;
        puff->burstMin = puff->burstMax = 1;
        puff->speedMin = puff->speedMax = 0.0f;
        puff->spread = 0.0f;
        puff->gravity = 20.0f;
        puff->lifetimeMin = puff->lifetimeMax = 0.3f;
        puff->sizeMin = puff->sizeMax = 5.0f;

        // Droplets, thrown on past the target. Those that land leave stains.
        auto* drops = e.AddComponent<ComponentParticleEmitter>();
        drops->textures = FxPics("Blood", "Drop", 4);
        drops->burstMin = 6;
        drops->burstMax = 11;
        drops->spread = 32.0f;
        drops->speedMin = 40.0f;
        drops->speedMax = 130.0f;
        drops->gravity = 300.0f;
        drops->lifetimeMin = 1.0f;
        drops->lifetimeMax = 1.6f;
        drops->sizeMin = 0.5f;
        drops->sizeMax = 1.2f;
        drops->collision = C::DIE;
        drops->stainTextures = FxPics("Blood", "Small Stain", 3);
        drops->stainChance = 0.55f;
        drops->stainSizeMin = 3.0f;
        drops->stainSizeMax = 7.0f;
        drops->stainColor = {1.0f, 1.0f, 1.0f, kStainBlood};
        drops->noStainTag = Tag("Liquid");
    });

    MakeParticleEffect("Blood Burst", [](Entity& e) {
        auto* drops = e.AddComponent<ComponentParticleEmitter>();
        drops->textures = FxPics("Blood", "Drop", 4);
        drops->burstMin = 45;
        drops->burstMax = 65;
        drops->spread = 180.0f;
        drops->speedMin = 40.0f;
        drops->speedMax = 170.0f;
        drops->gravity = 300.0f;
        drops->lifetimeMin = 1.0f;
        drops->lifetimeMax = 2.0f;
        drops->sizeMin = 0.6f;
        drops->sizeMax = 1.8f;
        drops->collision = C::DIE;
        drops->stainTextures = FxPics("Blood", "Stain", 5);
        drops->stainChance = 0.5f;
        drops->stainSizeMin = 5.0f;
        drops->stainSizeMax = 13.0f;
        drops->stainColor = {1.0f, 1.0f, 1.0f, kStainBlood};
        drops->noStainTag = Tag("Liquid");
    });

    MakeParticleEffect("Gib Chunks", [](Entity& e) {
        auto* chunks = e.AddComponent<ComponentParticleEmitter>();
        chunks->textures = FxPics("Gibs", "Chunk", 6);
        chunks->burstMin = 8;
        chunks->burstMax = 12;
        chunks->spread = 65.0f;
        chunks->speedMin = 50.0f;
        chunks->speedMax = 135.0f;
        chunks->gravity = 300.0f;
        chunks->lifetimeMin = 6.0f;
        chunks->lifetimeMax = 11.0f;
        chunks->sizeMin = 1.6f;
        chunks->sizeMax = 3.6f;
        chunks->collision = C::BOUNCE;
        chunks->bounciness = 0.3f;
        chunks->stainTextures = FxPics("Blood", "Small Stain", 3);
        chunks->stainChance = 0.6f;
        chunks->stainSizeMin = 3.0f;
        chunks->stainSizeMax = 6.0f;
        chunks->stainColor = {1.0f, 1.0f, 1.0f, kStainBlood};
        chunks->noStainTag = Tag("Liquid");
    });

    MakeParticleEffect("Puff", [](Entity& e) {
        auto* puff = e.AddComponent<ComponentParticleEmitter>();
        puff->textures = ArtPics(Art("effects", "Puff"), "ABCD");
        puff->animate = true;
        puff->burstMin = puff->burstMax = 1;
        puff->speedMin = puff->speedMax = 0.0f;
        puff->spread = 0.0f;
        puff->gravity = -14.0f;
        puff->lifetimeMin = puff->lifetimeMax = 0.4f;
        puff->sizeMin = puff->sizeMax = 4.0f;

        auto* sparks = e.AddComponent<ComponentParticleEmitter>();
        sparks->textures = {"Sprites/FX/Debris/Spark.png"};
        sparks->burstMin = 2;
        sparks->burstMax = 5;
        sparks->spread = 55.0f;
        sparks->speedMin = 40.0f;
        sparks->speedMax = 110.0f;
        sparks->gravity = 240.0f;
        sparks->lifetimeMin = 0.15f;
        sparks->lifetimeMax = 0.35f;
        sparks->sizeMin = 0.3f;
        sparks->sizeMax = 0.6f;
        sparks->collision = C::DIE;
    });

    MakeParticleEffect("Explosion Debris", [](Entity& e) {
        auto* rocks = e.AddComponent<ComponentParticleEmitter>();
        rocks->textures = FxPics("Debris", "Rock", 3);
        rocks->burstMin = 12;
        rocks->burstMax = 18;
        rocks->spread = 180.0f;
        rocks->speedMin = 50.0f;
        rocks->speedMax = 190.0f;
        rocks->gravity = 300.0f;
        rocks->lifetimeMin = 1.5f;
        rocks->lifetimeMax = 3.5f;
        rocks->sizeMin = 0.6f;
        rocks->sizeMax = 1.6f;
        rocks->collision = C::BOUNCE;
        rocks->bounciness = 0.35f;

        auto* sparks = e.AddComponent<ComponentParticleEmitter>();
        sparks->textures = {"Sprites/FX/Debris/Spark.png"};
        sparks->burstMin = 16;
        sparks->burstMax = 26;
        sparks->spread = 180.0f;
        sparks->speedMin = 70.0f;
        sparks->speedMax = 220.0f;
        sparks->gravity = 160.0f;
        sparks->lifetimeMin = 0.3f;
        sparks->lifetimeMax = 0.8f;
        sparks->sizeMin = 0.4f;
        sparks->sizeMax = 0.9f;
        sparks->collision = C::DIE;

        auto* smoke = e.AddComponent<ComponentParticleEmitter>();
        smoke->textures = FxPics("Debris", "Smoke", 3);
        smoke->burstMin = 6;
        smoke->burstMax = 9;
        smoke->spread = 180.0f;
        smoke->speedMin = 10.0f;
        smoke->speedMax = 45.0f;
        smoke->gravity = -18.0f;
        smoke->drag = 1.8f;
        smoke->lifetimeMin = 0.8f;
        smoke->lifetimeMax = 1.6f;
        smoke->sizeMin = 8.0f;
        smoke->sizeMax = 14.0f;
        smoke->sizeEnd = 2.2f;
        smoke->colorStart = {0.7f, 0.7f, 0.7f, 0.6f};
        smoke->colorEnd = {0.5f, 0.5f, 0.5f, 0.0f};
    });

    const auto casing = [](const Str& name, const Str& picture, const float size) {
        MakeParticleEffect(name, [&](Entity& e) {
            auto* shell = e.AddComponent<ComponentParticleEmitter>();
            shell->textures = {"Sprites/FX/Casings/" + picture + ".png"};
            shell->burstMin = shell->burstMax = 1;
            shell->spread = 22.0f;
            shell->speedMin = 28.0f;
            shell->speedMax = 48.0f;
            shell->gravity = 280.0f;
            shell->lifetimeMin = 3.0f;
            shell->lifetimeMax = 5.0f;
            shell->sizeMin = size;
            shell->sizeMax = size * 1.15f;
            shell->collision = C::BOUNCE;
            shell->bounciness = 0.45f;
        });
    };
    casing("Bullet Casing", "Bullet", 0.9f);
    casing("Shell Casing", "Shell", 1.4f);

    for (const Str& liquid : {Str("Water"), Str("Nukage"), Str("Lava")}) {
        MakeParticleEffect("Splash " + liquid, [&](Entity& e) {
            auto* drops = e.AddComponent<ComponentParticleEmitter>();
            drops->textures = FxPics("Splash", liquid + " Drop", 3);
            drops->burstMin = 12;
            drops->burstMax = 20;
            drops->spread = 28.0f;
            drops->speedMin = 30.0f;
            drops->speedMax = 95.0f;
            drops->gravity = 280.0f;
            drops->lifetimeMin = 0.4f;
            drops->lifetimeMax = 0.8f;
            drops->sizeMin = 0.5f;
            drops->sizeMax = 1.3f;
            drops->collision = C::DIE;
        });
    }
}

// ── Projectiles ─────────────────────────────────────────────────────────────

inline Str MakeProjectile(const Str& name, const Str& artName, const Str& letters, const float speed, const int damageMin,
                          const int damageMax, const Str& impact, const float homing = 0.0f) {
    const json& art = Art("effects", artName);
    const auto frames = ArtFrames(art, letters, 0.12f);
    const Str book = Book("Animations/Projectiles/" + name + ".fpk", frames, FlipbookLoopMode::Loop);
    return MakePrefab("Prefabs/Projectiles/" + name + ".prefab", false, [&](Level&, Entity& e) {
        auto* sprite = AddSprite(e, art["w"], art["h"], false);
        SetSpriteFrame(sprite, frames[0]);
        e.AddComponent<ComponentFlipbook>()->flipbookFileName = book;

        ComponentScript& script = AddLua(e, "Scripts/Combat/Projectile");
        script.publicValues["speed"] = speed;
        script.publicValues["damageMin"] = damageMin;
        script.publicValues["damageMax"] = damageMax;
        script.publicValues["homing"] = homing;
        script.publicValues["impactPrefab"] = PrefabRef("Prefabs/Effects/" + impact + ".prefab");
    });
}

inline void MakeProjectiles() {
    MakeProjectile("Fireball", "Fireball", "AB", 87.5f, 3, 24, "Fireball Hit");
    MakeProjectile("Red Ball", "Plasma Ball Red", "AB", 131.0f, 8, 64, "Red Ball Hit");
    MakeProjectile("Green Ball", "Plasma Ball Green", "AB", 87.5f, 5, 40, "Green Ball Hit");
    MakeProjectile("Tracer", "Tracer", "AB", 87.5f, 10, 80, "Tracer Hit", 90.0f);
    MakeProjectile("Slug Ball", "Slug Ball", "AB", 175.0f, 8, 64, "Slug Explosion");
    MakeProjectile("Spider Plasma", "Spider Plasma", "AB", 218.0f, 5, 40, "Spider Plasma Hit");
    MakeProjectile("Rocket", "Rocket", "A", 175.0f, 20, 160, "Rocket Explosion");
    MakeProjectile("Plasma", "Plasma", "AB", 218.0f, 5, 40, "Plasma Hit");
    MakeProjectile("SKAG Ball", "SKAG Ball", "AB", 218.0f, 100, 800, "SKAG Explosion");

    // Bounces and goes off on a fuse, so it has its own script: Grenade.lua.
    std::vector<Frame> spin;
    for (int i = 1; i <= 4; ++i) spin.push_back(Pic("Sprites/FX/Grenade/Spin " + std::to_string(i) + ".png", 0.07f));
    const Str book = Book("Animations/Projectiles/Grenade.fpk", spin, FlipbookLoopMode::Loop);
    MakePrefab("Prefabs/Projectiles/Grenade.prefab", false, [&](Level&, Entity& e) {
        auto* sprite = AddSprite(e, 10, 10, false);
        SetSpriteFrame(sprite, spin[0]);
        e.AddComponent<ComponentFlipbook>()->flipbookFileName = book;
        ComponentScript& script = AddLua(e, "Scripts/Combat/Grenade");
        script.publicValues["explosionPrefab"] = PrefabRef("Prefabs/Effects/Grenade Explosion.prefab");
    });
}

// ── Enemies ─────────────────────────────────────────────────────────────────

struct EnemySpec {
    Str name;
    float health, speed, radius, height, painChance;
    Str walk;
    float walkSeconds;
    Str attack;
    float attackSeconds;
    Str pain;
    Str die;

    // What it does when its Monster AI decides to attack.
    int meleeDamage = 0;
    int bulletDamage = 0;            // hitscan, no projectile
    float bulletChance = 60.0f;      // percent that a shot hits
    Str projectile;                  // a prefab in Prefabs/Projectiles
    int shots = 1;
    float fan = 0.0f;
    float muzzle = 8.0f;
    Str spawnsEnemy;                 // instead of attacking, spits this out
    float shootDelay = 0.3f;         // from the decision to the shot

    Str sight, active, painSound, death, attackSound, meleeSound;
    Str drop;                        // a prefab dropped on death
    bool flies = false;
    bool bleeds = true;
    float alpha = 1.0f;
    float missileScale = 4.0f;       // Doom's 1.0 at our map scale
    float missileMin = 0.0f;
};

inline void MakeEnemy(const EnemySpec& s) {
    const json& art = Art("enemies", s.name == "Stealth Worm" ? "Flesh Worm" : s.name);
    const Str folder = "Animations/Enemies/" + s.name + "/";

    const auto walk = ArtFrames(art, s.walk, s.walkSeconds);
    const auto attack = ArtFrames(art, s.attack, s.attackSeconds);
    const auto die = ArtFrames(art, s.die, 0.14f);
    const Frame pain = ArtFrame(art, s.pain.substr(0, 1), 0.14f);

    const Str walkBook = Book(folder + "Walk.fpk", walk, FlipbookLoopMode::Loop);
    const Str attackBook = Book(folder + "Attack.fpk", attack, FlipbookLoopMode::Once);
    const Str dieBook = Book(folder + "Die.fpk", die, FlipbookLoopMode::Once);

    MakePrefab("Prefabs/Enemies/" + s.name + ".prefab", false, [&](Level& level, Entity& e) {
        e.tagIds = {Tag("Enemy")};
        if (s.bleeds) e.tagIds.push_back(Tag("Bleeds"));

        auto* sprite = AddSprite(e, art["w"], art["h"], true);
        SetSpriteFrame(sprite, walk[0]);
        sprite->color.w = s.alpha;
        e.AddComponent<ComponentFlipbook>()->flipbookFileName = walkBook;

        AddCapsule(level, e, s.radius, s.height);
        auto* body = AddBody(e, false, 10.0f);
        if (s.flies) body->gravityScale = 0.0f;

        auto* path = e.AddComponent<ComponentPathfinder>();
        path->speed = s.speed;
        path->deadzone = 2.5f;
        path->floatSpeed = 35.0f;
        path->SetFlag(ComponentPathfinder::IS_AIR_ENTITY, s.flies);

        auto* ai = e.AddComponent<ComponentMonsterAI>();
        ai->health = s.health;
        ai->painChance = s.painChance;
        ai->walkFlipbook = walkBook;
        ai->attackFlipbook = attackBook;
        ai->dieFlipbook = dieBook;
        ai->nearRange = 16.0f;
        ai->meleeRange = 11.0f;
        ai->missileDistanceOffset = 16.0f;
        ai->missileDistanceScale = s.missileScale;
        ai->missileMinRange = s.missileMin;
        ai->attackPause = TotalTime(attack);
        ai->hasMelee = s.meleeDamage > 0;
        ai->hasMissile = s.bulletDamage > 0 || !s.projectile.empty() || !s.spawnsEnemy.empty();

        // ── Counting ──
        e.events.push_back(Ev(PayloadOnStart{}, {Self("Count Me")}));
        e.dispatches.push_back(Dp("Count Me", AddVar("TotalKills", 1), 0.05f));

        // ── Noises ──
        if (!s.sight.empty()) {
            e.events.push_back(Ev(PayloadMonsterTargetSpotted{}, {Self("Sight Sound")}));
            e.dispatches.push_back(Dp("Sight Sound", Play(s.sight)));
        }
        if (!s.active.empty()) {
            e.events.push_back(Ev(PayloadMonsterActive{}, {Self("Active Sound")}));
            e.dispatches.push_back(Dp("Active Sound", Play(s.active)));
        }

        // ── Pain: flinch for a moment ──
        e.events.push_back(Ev(PayloadMonsterPain{}, {Self("Flinch")}));
        PayloadDispatchEntitySetSprite flinch;
        flinch.textures = pain.textures;
        e.dispatches.push_back(Dp("Flinch", flinch));
        if (!s.painSound.empty()) e.dispatches.push_back(Dp("Flinch", Play(s.painSound)));

        // ── Death: the body stays on the floor, but nothing bumps into it or shoots it ──
        // (Corpse.lua also leaves a pool of blood under whatever bleeds, through Fx.lua.)
        AddLua(e, "Scripts/Enemies/Corpse");
        e.events.push_back(Ev(PayloadMonsterDied{}, {Self("Die")}));
        e.dispatches.push_back(Dp("Die", AddVar("Kills", 1)));
        if (!s.death.empty()) e.dispatches.push_back(Dp("Die", Play(s.death)));
        e.dispatches.push_back(Dp("Die", PayloadDispatchEntityCallFunction{"BecomeCorpse"}));
        if (!s.drop.empty()) e.dispatches.push_back(Dp("Die", Spawn(s.drop)));

        // ── Attacks: the Monster AI decides, these events do the rest ──
        // Being hit by another monster's bite or bullet.
        PayloadDispatchEntityMonsterAI hurt;
        hurt.action = MonsterAIAction::HURT;
        hurt.damage = 12.0f;
        e.dispatches.push_back(Dp("Hit By Monster", hurt));

        const auto hurtPlayerSteps = [&](const Str& name, const int damage, const float delay) {
            // Sets how much this hit does, then the player's Take Damage reads it.
            e.dispatches.push_back(Dp(name, SetVar("IncomingDamage", static_cast<float>(damage)), delay, {Then(Inst("Take Damage"))}));
        };

        if (s.meleeDamage > 0) {
            PayloadMonsterMeleeReady onPlayer, onEnemy;
            onPlayer.filter = Only("Player");
            onEnemy.filter = Only("Enemy");
            e.events.push_back(Ev(onPlayer, {Self("Bite")}));
            e.events.push_back(Ev(onEnemy, {Self("Bite Sound"), Inst("Hit By Monster")}));
            hurtPlayerSteps("Bite", s.meleeDamage, s.attackSeconds);
            if (!s.meleeSound.empty()) {
                e.dispatches.push_back(Dp("Bite", Play(s.meleeSound), s.attackSeconds));
                e.dispatches.push_back(Dp("Bite Sound", Play(s.meleeSound), s.attackSeconds));
            }
        }

        if (s.bulletDamage > 0) {
            PayloadMonsterMissileReady any, onPlayer, onEnemy;
            onPlayer.filter = Only("Player");
            onEnemy.filter = Only("Enemy");
            e.events.push_back(Ev(any, {Self("Shot Sound")}));
            e.events.push_back(Ev(onPlayer, {Self("Shoot")}, {}, 0, s.bulletChance));
            e.events.push_back(Ev(onEnemy, {Inst("Hit By Monster")}, {}, 0, s.bulletChance));
            hurtPlayerSteps("Shoot", s.bulletDamage, s.shootDelay);
            e.dispatches.push_back(Dp("Shot Sound", Play(s.attackSound), s.shootDelay));
        }

        if (!s.projectile.empty()) {
            ComponentScript& fire = AddLua(e, "Scripts/Enemies/FireProjectile");
            fire.publicValues["projectilePrefab"] = PrefabRef("Prefabs/Projectiles/" + s.projectile + ".prefab");
            fire.publicValues["shots"] = s.shots;
            fire.publicValues["fanDegrees"] = s.fan;
            fire.publicValues["delay"] = s.shootDelay;
            fire.publicValues["muzzleHeight"] = s.muzzle;
            fire.publicValues["fireSound"] = s.attackSound.empty() ? Str() : Sound(s.attackSound);
        }

        if (!s.spawnsEnemy.empty()) {
            e.events.push_back(Ev(PayloadMonsterMissileReady{}, {Self("Spit")}, {}, 0, 50.0f));
            Dispatch spit = Dp("Spit", Spawn("Prefabs/Enemies/" + s.spawnsEnemy + ".prefab", {0, 6, 0}), s.shootDelay);
            spit.cooldown = 3.0f;
            e.dispatches.push_back(spit);
        }
    });
}

inline void MakeEnemies() {
    const Str clip = "Prefabs/Pickups/Ammo/Clip.prefab";
    const Str shotgun = "Prefabs/Weapons/Pickups/Shotgun.prefab";
    const Str minigun = "Prefabs/Weapons/Pickups/Minigun.prefab";

    {EnemySpec s{"Zombie", 20, 17.5f, 5, 14, 78, "ABCD", 0.114f, "EF", 0.2f, "G", "HIJKL"};
     s.bulletDamage = 9; s.sight = "Monsters/zombie sight 1"; s.active = "Monsters/zombie active"; s.painSound = "Monsters/zombie pain";
     s.death = "Monsters/zombie death 1"; s.attackSound = "Weapons/pistol"; s.drop = clip; MakeEnemy(s);}
    {EnemySpec s{"Shotgun Zombie", 30, 17.5f, 5, 14, 66, "ABCD", 0.086f, "EF", 0.2f, "G", "HIJKL"};
     s.bulletDamage = 18; s.sight = "Monsters/zombie sight 2"; s.active = "Monsters/zombie active"; s.painSound = "Monsters/zombie pain";
     s.death = "Monsters/zombie death 2"; s.attackSound = "Weapons/shotgun"; s.drop = shotgun; MakeEnemy(s);}
    {EnemySpec s{"Minigun Zombie", 70, 17.5f, 5, 14, 66, "ABCD", 0.086f, "EF", 0.114f, "G", "HIJKLMN"};
     s.bulletDamage = 8; s.bulletChance = 70; s.shootDelay = 0.1f; s.sight = "Monsters/zombie sight 3"; s.active = "Monsters/zombie active";
     s.painSound = "Monsters/zombie pain"; s.death = "Monsters/zombie death 3"; s.attackSound = "Weapons/pistol"; s.drop = minigun; MakeEnemy(s);}
    {EnemySpec s{"Serpentipede", 60, 23, 5, 14, 78, "ABCD", 0.086f, "EFG", 0.17f, "H", "IJKLM"};
     s.meleeDamage = 12; s.projectile = "Fireball"; s.sight = "Monsters/serpentipede sight 1"; s.active = "Monsters/serpentipede active";
     s.painSound = "Monsters/zombie pain"; s.death = "Monsters/serpentipede death 1"; s.attackSound = "World/fireball shoot"; s.meleeSound = "Monsters/claw"; MakeEnemy(s);}
    {EnemySpec s{"Flesh Worm", 150, 44, 7.5f, 14, 70, "ABCD", 0.057f, "EFG", 0.114f, "H", "IJKLMN"};
     s.meleeDamage = 22; s.sight = "Monsters/worm sight"; s.active = "Monsters/worm active"; s.painSound = "Monsters/worm pain";
     s.death = "Monsters/worm death"; s.meleeSound = "Monsters/worm attack"; MakeEnemy(s);}
    {EnemySpec s{"Stealth Worm", 150, 44, 7.5f, 14, 70, "ABCD", 0.057f, "EFG", 0.114f, "H", "IJKLMN"};
     s.meleeDamage = 22; s.alpha = 0.3f; s.sight = "Monsters/worm sight"; s.active = "Monsters/worm active"; s.painSound = "Monsters/worm pain";
     s.death = "Monsters/worm death"; s.meleeSound = "Monsters/worm attack"; MakeEnemy(s);}
    {EnemySpec s{"Trilobite", 400, 23, 7.75f, 15.5f, 50, "A", 0.3f, "BCD", 0.14f, "E", "GHIJKL"};
     s.meleeDamage = 30; s.projectile = "Green Ball"; s.flies = true; s.bleeds = false; s.muzzle = 9; s.sight = "Monsters/trilobite sight"; s.active = "Monsters/worm active";
     s.painSound = "Monsters/worm pain"; s.death = "Monsters/trilobite death"; s.attackSound = "World/fireball shoot"; s.meleeSound = "Monsters/claw"; MakeEnemy(s);}
    {EnemySpec s{"Deadflare", 100, 30, 4, 12, 100, "AB", 0.1f, "CD", 0.15f, "E", "FGHIJK"};
     s.meleeDamage = 15; s.flies = true; s.bleeds = false; s.active = "Monsters/worm active"; s.painSound = "Monsters/worm pain"; s.death = "Monsters/deadflare death"; s.meleeSound = "Monsters/deadflare attack"; MakeEnemy(s);}
    {EnemySpec s{"Pain Bringer", 500, 23, 6, 16, 20, "ABCD", 0.086f, "EFG", 0.2f, "H", "IJKLMNO"};
     s.meleeDamage = 40; s.projectile = "Red Ball"; s.sight = "Monsters/pain bringer sight"; s.active = "Monsters/worm active"; s.painSound = "Monsters/worm pain";
     s.death = "Monsters/pain bringer death"; s.attackSound = "World/fireball shoot"; s.meleeSound = "Monsters/claw"; MakeEnemy(s);}
    {EnemySpec s{"Pain Lord", 1000, 23, 6, 16, 20, "ABCD", 0.086f, "EFG", 0.2f, "H", "IJKLMNO"};
     s.meleeDamage = 40; s.projectile = "Red Ball"; s.sight = "Monsters/pain lord sight"; s.active = "Monsters/worm active"; s.painSound = "Monsters/worm pain";
     s.death = "Monsters/pain lord death"; s.attackSound = "World/fireball shoot"; s.meleeSound = "Monsters/claw"; MakeEnemy(s);}
    {EnemySpec s{"Hangman", 300, 44, 5, 14, 39, "ABCDEF", 0.057f, "JK", 0.3f, "L", "MNOPQ"};
     s.meleeDamage = 30; s.projectile = "Tracer"; s.missileMin = 49; s.sight = "Monsters/hangman sight"; s.active = "Monsters/hangman active"; s.painSound = "Monsters/zombie pain";
     s.death = "Monsters/hangman death"; s.attackSound = "Monsters/hangman shoot"; s.meleeSound = "Monsters/hangman punch"; s.bleeds = false; MakeEnemy(s);}
    {EnemySpec s{"Combat Slug", 600, 17.5f, 12, 16, 31, "ABCDEF", 0.114f, "GHI", 0.28f, "J", "KLMNOPQRST"};
     s.projectile = "Slug Ball"; s.shots = 3; s.fan = 12; s.sight = "Monsters/slug sight"; s.active = "Monsters/zombie active"; s.painSound = "Monsters/slug pain";
     s.death = "Monsters/slug death"; s.attackSound = "Monsters/slug shoot"; MakeEnemy(s);}
    {EnemySpec s{"Technospider", 500, 35, 16, 16, 50, "ABCDEF", 0.086f, "AGH", 0.1f, "I", "JKLMNOP"};
     s.projectile = "Spider Plasma"; s.shootDelay = 0.15f; s.bleeds = false; s.missileScale = 3.0f; s.sight = "Monsters/spider sight"; s.active = "Monsters/spider active"; s.painSound = "Monsters/zombie pain";
     s.death = "Monsters/spider death"; s.attackSound = "Weapons/plasma"; MakeEnemy(s);}
    {EnemySpec s{"Matribite", 400, 23, 7.75f, 15.5f, 50, "ABC", 0.17f, "DEF", 0.14f, "G", "HIJKLM"};
     s.spawnsEnemy = "Deadflare"; s.flies = true; s.bleeds = false; s.sight = "Monsters/matribite sight"; s.painSound = "Monsters/matribite pain";
     s.death = "Monsters/matribite death"; MakeEnemy(s);}
    {EnemySpec s{"Assault Tripod", 4000, 47, 10, 27, 8, "ABCD", 0.086f, "EF", 0.17f, "G", "HIJKLMNOP"};
     s.projectile = "Rocket"; s.muzzle = 16; s.missileScale = 2.0f; s.bleeds = false; s.sight = "Monsters/tripod sight"; s.painSound = "Monsters/zombie pain";
     s.death = "Monsters/tripod death"; s.attackSound = "Weapons/rocket launcher"; MakeEnemy(s);}
    {EnemySpec s{"Large Technospider", 3000, 35, 24, 25, 16, "ABCDEF", 0.086f, "AGH", 0.1f, "I", "JKLMNOPQRS"};
     s.bulletDamage = 15; s.bulletChance = 80; s.shootDelay = 0.1f; s.bleeds = false; s.missileScale = 2.0f; s.sight = "Monsters/large spider sight"; s.painSound = "Monsters/zombie pain";
     s.death = "Monsters/large spider death"; s.attackSound = "Weapons/shotgun"; MakeEnemy(s);}
}

// ── Weapons in the player's hands ───────────────────────────────────────────
// UI entities with no picture of their own: a UI entity has no box in the world
// for rays to hit. They all show on the HUD's Weapon View (see AddWeaponView).

struct WeaponSpec {
    Str name;
    int attackType;      // 0 Bullets, 1 Melee, 2 Projectile
    int slot;
    Str owned, ammo;
    int ammoPerShot;
    bool fallback;
    std::vector<std::pair<Str, float>> fireFrames; // picture, Doom tics
    float fireTics, shotTics;
    Str triggerSound, fireSound, hitSound;
    int pellets;
    float spread;
    int damageMin, damageMax;
    float range;
    float berserk;
    Str projectile;
    Str readyFrames = "Ready";
};

inline Str MakeWeaponView(const WeaponSpec& s) {
    const Str folder = "Sprites/Weapons/" + s.name + "/";
    std::vector<Frame> fire;
    for (const auto& [picture, tics] : s.fireFrames) fire.push_back(Pic(folder + picture + ".png", tics / 35.0f));

    std::vector<Frame> ready;
    if (s.readyFrames == "Ready") ready.push_back(Pic(folder + "Ready.png", 1.0f));
    else { ready.push_back(Pic(folder + "Ready1.png", 0.11f)); ready.push_back(Pic(folder + "Ready2.png", 0.11f)); }

    const Str readyBook = Book("Animations/Weapons/" + s.name + " Ready.fpk", ready, FlipbookLoopMode::Loop);
    const Str fireBook = Book("Animations/Weapons/" + s.name + " Fire.fpk", fire, FlipbookLoopMode::Once);

    // How it feels to fire: recoil degrees, shake, picture kick, room flash, casing.
    struct Feel { float recoil, shake, kick, flash; Str casing; };
    static const std::map<Str, Feel> kFeel = {
        {"Pistol", {0.5f, 0.0f, 0.03f, 40.0f, "Bullet Casing"}},
        {"Shotgun", {1.6f, 0.35f, 0.08f, 70.0f, "Shell Casing"}},
        {"Super Shotgun", {2.8f, 0.7f, 0.12f, 90.0f, "Shell Casing"}},
        {"Minigun", {0.3f, 0.12f, 0.03f, 45.0f, "Bullet Casing"}},
        {"Rocket Launcher", {1.3f, 0.3f, 0.07f, 60.0f, ""}},
        {"Plasma Rifle", {0.2f, 0.05f, 0.02f, 30.0f, ""}},
        {"SKAG 1337", {3.0f, 1.0f, 0.12f, 120.0f, ""}},
    };

    return MakePrefab("Prefabs/Weapons/In Hand/" + s.name + ".prefab", true, [&](Level&, Entity& e) {
        ComponentScript& w = AddLua(e, "Scripts/Weapons/Weapon");
        auto& v = w.publicValues;
        v["attackType"] = s.attackType;
        v["slot"] = s.slot;
        v["ownedVariable"] = s.owned;
        v["ammoVariable"] = s.ammo;
        v["ammoPerShot"] = s.ammoPerShot;
        v["useAsFallback"] = s.fallback;
        v["shotDelay"] = s.shotTics / 35.0f;
        v["fireTime"] = s.fireTics / 35.0f;
        v["readyFlipbook"] = AssetRef(readyBook);
        v["fireFlipbook"] = AssetRef(fireBook);
        v["triggerSound"] = s.triggerSound.empty() ? Str() : Sound(s.triggerSound);
        v["fireSound"] = s.fireSound.empty() ? Str() : Sound(s.fireSound);
        v["hitSound"] = s.hitSound.empty() ? Str() : Sound(s.hitSound);
        v["pellets"] = s.pellets;
        v["spread"] = s.spread;
        v["damageMin"] = s.damageMin;
        v["damageMax"] = s.damageMax;
        v["range"] = s.range;
        v["berserkMultiplier"] = s.berserk;
        if (const auto feel = kFeel.find(s.name); feel != kFeel.end()) {
            v["recoil"] = feel->second.recoil;
            v["shake"] = feel->second.shake;
            v["spriteKick"] = feel->second.kick;
            v["flashLight"] = feel->second.flash;
            if (!feel->second.casing.empty()) v["casingPrefab"] = PrefabRef("Prefabs/Effects/" + feel->second.casing + ".prefab");
        }
        if (!s.projectile.empty()) v["projectilePrefab"] = PrefabRef("Prefabs/Projectiles/" + s.projectile + ".prefab");
    });
}

inline std::vector<Str> MakeWeaponViews() {
    std::vector<WeaponSpec> specs;
    specs.push_back({"Fist", 1, 1, "HasFist", "", 1, true, {{"B", 4}, {"C", 4}, {"D", 5}, {"C", 4}, {"B", 5}}, 22, 8, "", "Weapons/punch", "", 1, 0, 2, 20, 16, 10});
    {WeaponSpec s{"Chainsaw", 1, 1, "HasChainsaw", "", 1, true, {{"Fire1", 4}, {"Fire2", 4}}, 8, 0, "", "Weapons/chainsaw swing", "Weapons/chainsaw hit", 1, 5, 2, 20, 16, 1};
     s.readyFrames = "Ready12"; specs.push_back(s);}
    specs.push_back({"Pistol", 0, 2, "HasPistol", "Bullets", 1, true, {{"Flash", 6}, {"C", 4}, {"B", 5}}, 15, 0, "", "Weapons/pistol", "", 1, 2.8f, 5, 15, 512, 1});
    specs.push_back({"Shotgun", 0, 3, "HasShotgun", "Shells", 1, true, {{"Flash1", 4}, {"Flash2", 3}, {"B", 5}, {"C", 5}, {"D", 5}, {"C", 5}, {"B", 5}}, 37, 0, "", "Weapons/shotgun", "", 7, 5.6f, 5, 15, 512, 1});
    specs.push_back({"Super Shotgun", 0, 3, "HasSuperShotgun", "Shells", 2, true, {{"Flash1", 5}, {"Flash2", 5}, {"B", 7}, {"C", 7}, {"D", 7}, {"E", 7}, {"F", 7}, {"G", 6}, {"H", 6}}, 57, 0, "", "Weapons/super shotgun", "", 20, 11.2f, 5, 15, 512, 1});
    specs.push_back({"Minigun", 0, 4, "HasMinigun", "Bullets", 1, true, {{"Flash1", 2}, {"Flash2", 2}}, 4, 0, "", "Weapons/pistol", "", 1, 5.6f, 5, 15, 512, 1});
    specs.push_back({"Rocket Launcher", 2, 5, "HasRocketLauncher", "Rockets", 1, false, {{"Flash1", 3}, {"Flash2", 4}, {"Flash3", 4}, {"Flash4", 4}, {"B", 5}}, 20, 8, "", "Weapons/rocket launcher", "", 1, 0, 0, 0, 0, 1, "Rocket"});
    specs.push_back({"Plasma Rifle", 2, 6, "HasPlasmaRifle", "Cells", 1, true, {{"Flash1", 2}, {"Flash2", 2}}, 4, 0, "", "Weapons/plasma", "", 1, 0, 0, 0, 0, 1, "Plasma"});
    specs.push_back({"SKAG 1337", 2, 7, "HasSKAG", "Cells", 40, false, {{"B", 20}, {"Flash1", 8}, {"Flash2", 8}, {"C", 14}}, 50, 26, "Weapons/skag charge", "Weapons/rocket launcher", "", 1, 0, 0, 0, 0, 1, "SKAG Ball"});

    std::vector<Str> references;
    for (const WeaponSpec& s : specs) references.push_back(MakeWeaponView(s));
    return references;
}

// ── Pickups ─────────────────────────────────────────────────────────────────

struct PickupSpec {
    Str folder;                     // under Prefabs/
    Str name;                       // the prefab's name
    Str artName;                    // in the manifest's props
    Condition condition;            // only picked up when this holds
    std::vector<Dispatch> effects;  // what it does; named "Pick Up"
    std::vector<DirectCall> extraCalls;
    Str message;
    Str sound = "Items/item pickup";
    bool countsAsItem = false;
};

inline void MakePickup(const PickupSpec& s) {
    const json& art = Art("props", s.artName);
    std::vector<Frame> frames;
    for (const auto& [letter, rotated] : art["frames"].items()) frames.push_back(ArtFrame(art, letter, 0.15f));

    Str book;
    if (frames.size() > 1) book = Book("Animations/Pickups/" + s.name + ".fpk", frames, FlipbookLoopMode::Loop);

    MakePrefab("Prefabs/" + s.folder + "/" + s.name + ".prefab", false, [&](Level& level, Entity& e) {
        e.tagIds = {Tag("Pickup")};
        auto* sprite = AddSprite(e, art["w"], art["h"], false);
        SetSpriteFrame(sprite, frames[0]);
        if (!book.empty()) e.AddComponent<ComponentFlipbook>()->flipbookFileName = book;

        AddCapsule(level, e, 5.0f, 12.0f, true);

        // Walking onto it: only the player, and only if it would do something.
        PayloadEntityCollide touch;
        touch.filter = Only("Player");
        std::vector<DirectCall> calls = s.extraCalls;
        calls.push_back(Self("Pick Up"));
        calls.push_back(Tagged("HUD", "Pickup Flash"));
        e.events.push_back(Ev(touch, calls, s.condition, 1));

        for (const Dispatch& effect : s.effects) e.dispatches.push_back(effect);
        if (s.countsAsItem) {
            e.events.push_back(Ev(PayloadOnStart{}, {Self("Count Me")}));
            e.dispatches.push_back(Dp("Count Me", AddVar("TotalItems", 1), 0.05f));
            e.dispatches.push_back(Dp("Pick Up", AddVar("Items", 1)));
        }
        if (!s.sound.empty()) e.dispatches.push_back(Dp("Pick Up", Play(s.sound)));
        if (!s.message.empty()) e.dispatches.push_back(Dp("Pick Up", Msg(s.message)));
        // A moment later, so the follow-up steps above still get to run.
        e.dispatches.push_back(Dp("Pick Up", PayloadDispatchEntityDestroy{}, 0.05f));
    });
}

// "Add <amount> to <var>" then, next frame, bring it back to <cap> if it went over.
inline Dispatch AddCapped(const Str& var, const float amount, const float cap) {
    return Dp("Pick Up", AddVar(var, amount), 0.0f, {ThenIf(Inst("Cap " + var + " " + std::to_string(static_cast<int>(cap))), If(var, GT, cap))});
}

inline void MakePickups() {
    const auto ammo = [](const Str& name, const Str& var, const float amount, const Str& maxVar, const Str& message, const Str& art = "") {
        PickupSpec s{"Pickups/Ammo", name, art.empty() ? name : art, IfVars(var, LT, maxVar), {Dp("Pick Up", AddVar(var, amount))}, {}, message};
        MakePickup(s);
    };
    ammo("Clip", "Bullets", 10, "MaxBullets", "Picked up a clip.");
    ammo("Ammo Box", "Bullets", 50, "MaxBullets", "Picked up a box of bullets.");
    ammo("Shells", "Shells", 4, "MaxShells", "Picked up 4 shotgun shells.");
    ammo("Shell Box", "Shells", 20, "MaxShells", "Picked up a box of shells.");
    ammo("Rocket", "Rockets", 1, "MaxRockets", "Picked up a rocket.");
    ammo("Rocket Box", "Rockets", 5, "MaxRockets", "Picked up a box of rockets.");
    ammo("Cell", "Cells", 20, "MaxCells", "Picked up an energy cell.");
    ammo("Cell Pack", "Cells", 100, "MaxCells", "Picked up an energy cell pack.");
    ammo("Grenade", "Grenades", 1, "MaxGrenades", "Picked up a grenade.");
    ammo("Grenade Box", "Grenades", 5, "MaxGrenades", "Picked up a box of grenades.");

    MakePickup({"Pickups/Ammo", "Backpack", "Backpack", {}, {
        Dp("Pick Up", SetVar("HasBackpack", 1)), Dp("Pick Up", SetVar("MaxBullets", 400)), Dp("Pick Up", SetVar("MaxShells", 100)),
        Dp("Pick Up", SetVar("MaxRockets", 100)), Dp("Pick Up", SetVar("MaxCells", 600)), Dp("Pick Up", SetVar("MaxGrenades", 40)),
        Dp("Pick Up", AddVar("Bullets", 10)), Dp("Pick Up", AddVar("Shells", 4)), Dp("Pick Up", AddVar("Rockets", 1)), Dp("Pick Up", AddVar("Cells", 20)), Dp("Pick Up", AddVar("Grenades", 1))},
        {}, "Picked up a backpack full of ammo!"});

    MakePickup({"Pickups/Health", "Stimpack", "Stimpack", If("Health", LT, 100), {AddCapped("Health", 10, 100)}, {}, "Picked up a stimpack."});
    MakePickup({"Pickups/Health", "Medikit", "Medikit", If("Health", LT, 100), {AddCapped("Health", 25, 100)}, {}, "Picked up a medikit."});
    MakePickup({"Pickups/Health", "Health Bonus", "Health Bonus", If("Health", LT, 200), {AddCapped("Health", 1, 200)}, {}, "Picked up a health bonus.", "Items/item pickup", true});
    MakePickup({"Pickups/Health", "Soul Sphere", "Soul Sphere", {}, {AddCapped("Health", 100, 200)}, {}, "Soul sphere!", "Items/powerup pickup", true});
    MakePickup({"Pickups/Health", "Mega Sphere", "Mega Sphere", {}, {Dp("Pick Up", SetVar("Health", 200)), Dp("Pick Up", SetVar("Armor", 200)), Dp("Pick Up", SetVar("ArmorType", 2))}, {}, "Mega sphere!", "Items/powerup pickup", true});

    MakePickup({"Pickups/Armor", "Armor Bonus", "Armor Bonus", If("Armor", LT, 200), {AddCapped("Armor", 1, 200),
        Dp("Pick Up", AddVar("ArmorType", 0), 0.0f, {ThenIf(Inst("Green Armor Type"), If("ArmorType", EQ, 0))})}, {}, "Picked up an armor bonus.", "Items/item pickup", true});
    MakePickup({"Pickups/Armor", "Green Armor", "Green Armor", If("Armor", LT, 100), {Dp("Pick Up", SetVar("Armor", 100)), Dp("Pick Up", SetVar("ArmorType", 1))}, {}, "Picked up the armor."});
    MakePickup({"Pickups/Armor", "Blue Armor", "Blue Armor", If("Armor", LT, 200), {Dp("Pick Up", SetVar("Armor", 200)), Dp("Pick Up", SetVar("ArmorType", 2))}, {}, "Picked up the mega armor!"});

    const auto key = [](const Str& name, const Str& color, const Str& what) {
        MakePickup({"Pickups/Keys", name, name, {}, {}, {Inst("Get " + color + " Key")}, "Picked up a " + what + ".", "Items/item pickup"});
    };
    key("Red Key", "Red", "red keycard");
    key("Blue Key", "Blue", "blue keycard");
    key("Yellow Key", "Yellow", "yellow keycard");
    key("Red Skull", "Red", "red skull key");
    key("Blue Skull", "Blue", "blue skull key");
    key("Yellow Skull", "Yellow", "yellow skull key");

    MakePickup({"Pickups/Powerups", "Berserk", "Berserk", {}, {Dp("Pick Up", SetVar("Berserk", 1)), Dp("Pick Up", SetVar("Health", 100))}, {}, "Berserk!", "Items/powerup pickup", true});
    MakePickup({"Pickups/Powerups", "Invulnerability", "Invulnerability", {}, {}, {Inst("Get Invulnerability")}, "Invulnerability!", "Items/powerup pickup", true});
    MakePickup({"Pickups/Powerups", "Radiation Suit", "Radiation Suit", {}, {}, {Inst("Get Radiation Suit")}, "Radiation suit.", "Items/powerup pickup"});

    const auto weapon = [](const Str& name, const Str& owned, const Str& ammoVar, const float ammoAmount, const Str& message) {
        std::vector<Dispatch> effects{Dp("Pick Up", SetVar(owned, 1))};
        if (!ammoVar.empty()) effects.push_back(Dp("Pick Up", AddVar(ammoVar, ammoAmount)));
        MakePickup({"Weapons/Pickups", name, name, {}, effects, {}, message, "Weapons/weapon pickup"});
    };
    weapon("Chainsaw", "HasChainsaw", "", 0, "A chainsaw! Find some meat!");
    weapon("Pistol", "HasPistol", "Bullets", 20, "You got the pistol!");
    weapon("Shotgun", "HasShotgun", "Shells", 8, "You got the shotgun!");
    weapon("Super Shotgun", "HasSuperShotgun", "Shells", 8, "You got the super shotgun!");
    weapon("Minigun", "HasMinigun", "Bullets", 20, "You got the minigun!");
    weapon("Rocket Launcher", "HasRocketLauncher", "Rockets", 2, "You got the rocket launcher!");
    weapon("Plasma Rifle", "HasPlasmaRifle", "Cells", 40, "You got the plasma rifle!");
    weapon("SKAG 1337", "HasSKAG", "Cells", 40, "Oh yes. You got the SKAG 1337!");
}

// ── Decorations and hazards ─────────────────────────────────────────────────

inline void MakeDecorations() {
    // No collider: you walk through these.
    const std::set<Str> soft = {"Candle", "Skull Pile", "Blood Pool", "Small Blood Pool", "Gore Pile",
                                "Hanging Body", "Hanging Legs", "Hanging Torso", "Hanging Twitch"};
    // Not decorations, or made by hand below.
    const std::set<Str> skip = {"Barrel"};

    for (const auto& [name, art] : gManifest["props"].items()) {
        if (art["path"].get<Str>().find("Sprites/Decorations") != 0 || skip.contains(name)) continue;

        std::vector<Frame> frames;
        for (const auto& [letter, rotated] : art["frames"].items()) frames.push_back(ArtFrame(art, letter, 0.14f));
        Str book;
        if (frames.size() > 1) book = Book("Animations/Decorations/" + name + ".fpk", frames, FlipbookLoopMode::Loop);

        MakePrefab("Prefabs/Decorations/" + name + ".prefab", false, [&](Level& level, Entity& e) {
            auto* sprite = AddSprite(e, art["w"], art["h"], false);
            SetSpriteFrame(sprite, frames[0]);
            if (!book.empty()) e.AddComponent<ComponentFlipbook>()->flipbookFileName = book;
            if (!soft.contains(name)) {
                const float radius = std::clamp(art["w"].get<float>() * 0.25f * 0.4f, 2.0f, 6.0f);
                AddCapsule(level, e, radius, std::max(art["h"].get<float>() * 0.25f, 10.0f));
                AddBody(e, true);
            }
        });
    }

    // The barrel uses a Monster AI only so it can be hurt: it can't see, hear or walk.
    const json& art = Art("props", "Barrel");
    const auto frames = ArtFrames(art, "AB", 0.2f);
    const Str book = Book("Animations/Hazards/Barrel.fpk", frames, FlipbookLoopMode::Loop);
    MakePrefab("Prefabs/Hazards/Explosive Barrel.prefab", false, [&](Level& level, Entity& e) {
        // Kickable: QuickActions.lua frees it and shoves it instead of hurting it.
        e.tagIds = {Tag("Kickable")};
        auto* sprite = AddSprite(e, art["w"], art["h"], false);
        SetSpriteFrame(sprite, frames[0]);
        e.AddComponent<ComponentFlipbook>()->flipbookFileName = book;
        AddCapsule(level, e, 3.0f, 10.5f);
        AddBody(e, true)->friction = 170.0f;

        auto* ai = e.AddComponent<ComponentMonsterAI>();
        ai->health = 20.0f;
        ai->painChance = 0.0f;
        ai->sightFov = 0.0f;
        ai->sightRange = 0.01f;
        ai->nearRange = 0.0f;
        ai->hasMelee = false;
        ai->hasMissile = false;
        ai->hearing = false;
        ai->infights = false;
        ai->drivePathfinder = false;

        e.events.push_back(Ev(PayloadMonsterDied{}, {Self("Explode")}));
        e.dispatches.push_back(Dp("Explode", Spawn("Prefabs/Effects/Barrel Explosion.prefab")));
        e.dispatches.push_back(Dp("Explode", PayloadDispatchEntityDestroy{}));
    });
}

// ── Level logic ─────────────────────────────────────────────────────────────

inline void MakeLevelLogic() {
    // A flat picture of a switch that goes on a wall (Snap to Wall in the inspector).
    // You press it with Use, then something happens. It has no collider, so it doesn't
    // stick out of the wall: the Use ray hits an entity's Transform box without one, so
    // the box's depth is what you can reach it through.
    const auto useSwitch = [](const Str& name, const float widthPx, const float heightPx, const Str& offTexture, const Str& onTexture, const Str& sound,
                              const std::function<void(Entity&, std::vector<DirectCall>&)>& extra, const uint32_t maxTriggers) {
        MakePrefab("Prefabs/Level Logic/" + name + ".prefab", false, [&](Level&, Entity& e) {
            auto* sprite = AddSprite(e, widthPx, heightPx, false);
            sprite->isStatic = true;
            sprite->textureFileNames.fill("Textures/Walls/" + offTexture + ".png");
            e.GetComponent<ComponentTransform>()->scale.z = 16.0f;

            PayloadReceiveRay use;
            use.rayType = RayType::INTERACT;
            use.filter = Only("Player");

            std::vector<DirectCall> calls{Self("Press")};
            extra(e, calls);
            e.events.push_back(Ev(use, calls, {}, maxTriggers));

            PayloadDispatchEntitySetSprite on;
            on.textures.fill("Textures/Walls/" + onTexture + ".png");
            e.dispatches.push_back(Dp("Press", on));
            e.dispatches.push_back(Dp("Press", Play(sound)));
        });
    };

    useSwitch("Exit Switch", 48, 108, "SW1EXIT", "SW2EXIT", "World/switch exit", [](Entity&, std::vector<DirectCall>& calls) {
        calls.push_back(Tagged("Intermission", "Show"));
    }, 1);
    useSwitch("End Game Switch", 48, 108, "SW1EXIT", "SW2EXIT", "World/switch exit", [](Entity&, std::vector<DirectCall>& calls) {
        calls.push_back(Tagged("Victory", "Show"));
    }, 1);
    // Fires the event tag "Switch 1": anything that listens on it reacts.
    useSwitch("Switch", 64, 128, "SW1COMP", "SW2COMP", "World/switch on", [](Entity&, std::vector<DirectCall>&) {}, 0);
    {
        // Same as above, plus the link tag on its one event.
        const Str reference = "Prefabs/Level Logic/Switch.prefab";
        Level prefab;
        Str error;
        LevelSerialization::LoadPrefabFromFile(gAssets / reference, prefab, &error);
        prefab.entities[0].events[0].useTag = true;
        prefab.entities[0].events[0].tagId = LinkTag("Switch 1");
        LevelSerialization::SavePrefabToFile(gAssets / reference, prefab, &error);
        Prefab::Invalidate(reference);
    }

    // Invisible triggers. They show in the editor, not in the game.
    const auto trigger = [](const Str& name, const float radius, const float height,
                            const std::function<void(Entity&)>& fill) {
        MakePrefab("Prefabs/Level Logic/" + name + ".prefab", false, [&](Level& level, Entity& e) {
            e.GetComponent<ComponentTransform>()->scale = {1, 1, 1};
            AddCapsule(level, e, radius, height, true);
            AddBody(e, true);
            fill(e);
        });
    };

    PayloadEntityCollide playerTouch;
    playerTouch.filter = Only("Player");

    trigger("Exit Trigger", 16, 32, [&](Entity& e) {
        e.events.push_back(Ev(playerTouch, {Self("Press"), Tagged("Intermission", "Show")}, {}, 1));
        e.dispatches.push_back(Dp("Press", Play("World/switch exit")));
    });
    trigger("Secret Spot", 12, 32, [&](Entity& e) {
        e.events.push_back(Ev(PayloadOnStart{}, {Self("Count Me")}));
        e.dispatches.push_back(Dp("Count Me", AddVar("TotalSecrets", 1), 0.05f));
        e.events.push_back(Ev(playerTouch, {Self("Found")}, {}, 1));
        e.dispatches.push_back(Dp("Found", AddVar("Secrets", 1)));
        e.dispatches.push_back(Dp("Found", Msg("A secret is revealed!")));
        e.dispatches.push_back(Dp("Found", Play("World/radio")));
    });
    trigger("Walk Over Trigger", 16, 32, [&](Entity& e) {
        // Fires the event tag "Trigger 1" once, when the player walks here.
        Event linked = Ev(playerTouch, {}, {}, 1);
        linked.useTag = true;
        linked.tagId = LinkTag("Trigger 1");
        e.events.push_back(linked);
    });
    trigger("Teleport Trigger", 8, 16, [&](Entity& e) {
        ComponentScript& teleporter = AddLua(e, "Scripts/Level Logic/Teleporter");
        teleporter.publicValues["fogPrefab"] = PrefabRef("Prefabs/Effects/Teleport Fog.prefab");
    });
    MakePrefab("Prefabs/Level Logic/Teleport Destination.prefab", false, [&](Level&, Entity& e) {
        e.GetComponent<ComponentTransform>()->scale = {1, 1, 1};
    });
}

// ── The player ──────────────────────────────────────────────────────────────

inline void MakePlayer(const std::vector<Str>& weaponViews) {
    MakePrefab("Prefabs/Player/Player.prefab", false, [&](Level& level, Entity& e) {
        e.tagIds = {Tag("Player")};
        auto* transform = e.GetComponent<ComponentTransform>();
        transform->scale = {1, 1, 1};

        level.ActivateCamera(*e.AddComponent<ComponentCamera>());
        auto* controller = e.AddComponent<ComponentPlayerController>();
        level.ActivatePlayerController(*controller);
        // Doom's pace at this kit's scale, with its quick start and short slide.
        controller->speed = 100.0f;
        controller->runningSpeed = 132.0f;
        controller->acceleration = 520.0f;
        controller->deceleration = 280.0f;
        controller->airControl = 0.25f;
        controller->eyeHeight = 10.25f;
        controller->useReach = 20.0f;
        controller->jumpPower = 40.0f; // Set to 0 for Doom's no jumping.

        AddCapsule(level, e, 4.0f, 14.0f);
        AddBody(e, false, 10.0f);

        AddLua(e, "Scripts/Combat/DamageRules");
        {
            ComponentScript& fx = AddLua(e, "Scripts/Combat/Fx");
            const auto effect = [](const Str& name) { return PrefabRef("Prefabs/Effects/" + name + ".prefab"); };
            fx.publicValues["bloodPrefab"] = effect("Blood");
            fx.publicValues["bloodBurstPrefab"] = effect("Blood Burst");
            fx.publicValues["gibPrefab"] = effect("Gib Chunks");
            fx.publicValues["puffPrefab"] = effect("Puff");
            fx.publicValues["debrisPrefab"] = effect("Explosion Debris");
            fx.publicValues["splashWaterPrefab"] = effect("Splash Water");
            fx.publicValues["splashNukagePrefab"] = effect("Splash Nukage");
            fx.publicValues["splashLavaPrefab"] = effect("Splash Lava");
        }
        AddLua(e, "Scripts/Player/PlayerHealth");
        AddLua(e, "Scripts/Player/ViewFeel");
        AddLua(e, "Scripts/World/LiquidSplashes");
        {
            // The boot and the throwing hand: six frames each, in UI/Actions.
            const auto action = [](const Str& name, const float seconds) {
                std::vector<Frame> frames;
                for (int i = 1; i <= 6; ++i) frames.push_back(Pic("UI/Actions/" + name + " " + std::to_string(i) + ".png", seconds));
                return AssetRef(Book("Animations/Actions/" + name + ".fpk", frames, FlipbookLoopMode::Once));
            };
            ComponentScript& quick = AddLua(e, "Scripts/Player/QuickActions");
            quick.publicValues["kickFlipbook"] = action("Kick", 0.06f);
            quick.publicValues["throwFlipbook"] = action("Throw", 0.07f);
            quick.publicValues["grenadePrefab"] = PrefabRef("Prefabs/Projectiles/Grenade.prefab");
        }
        ComponentScript& holder = AddLua(e, "Scripts/Weapons/WeaponHolder");
        for (size_t i = 0; i < weaponViews.size(); ++i)
            holder.publicValues["weapon" + std::to_string(i + 1)] = PrefabRef(weaponViews[i]);

        const auto add = [&](Dispatch d) { e.dispatches.push_back(std::move(d)); };

        // ── Every level start: forget what belongs to the last level ──
        e.events.push_back(Ev(PayloadOnStart{}, {Self("New Level")}));
        for (const char* v : {"Kills", "TotalKills", "Items", "TotalItems", "Secrets", "TotalSecrets", "Time", "MenuState",
                              "HasRedKey", "HasBlueKey", "HasYellowKey", "Berserk", "Invulnerable", "RadSuit"})
            add(Dp("New Level", SetVar(v, 0)));

        // ── Dead last time (or a new game)? Start again with a pistol ──
        e.events.push_back(Ev(PayloadOnStart{}, {Self("Pistol Start")}, If("Health", LE, 0)));
        const std::vector<std::pair<const char*, float>> fresh = {
            {"Health", 100}, {"Armor", 0}, {"ArmorType", 0}, {"Bullets", 50}, {"Shells", 0}, {"Rockets", 0}, {"Cells", 0}, {"Grenades", 2},
            {"MaxBullets", 200}, {"MaxShells", 50}, {"MaxRockets", 50}, {"MaxCells", 300}, {"MaxGrenades", 20}, {"HasBackpack", 0}, {"HasFist", 1},
            {"HasPistol", 1}, {"HasChainsaw", 0}, {"HasShotgun", 0}, {"HasSuperShotgun", 0}, {"HasMinigun", 0},
            {"HasRocketLauncher", 0}, {"HasPlasmaRifle", 0}, {"HasSKAG", 0}};
        for (const auto& [v, value] : fresh) add(Dp("Pistol Start", SetVar(v, value)));

        // ── Time on the intermission screen ──
        PayloadTimer second;
        second.interval = 1.0f;
        second.firstDelay = 1.0f;
        e.events.push_back(Ev(second, {Self("Tick")}));
        add(Dp("Tick", AddVar("Time", 1)));

        // ── Ammo can't go over its maximum ──
        for (const auto& [v, normal, backpack] : std::vector<std::tuple<Str, float, float>>{
                 {"Bullets", 200, 400}, {"Shells", 50, 100}, {"Rockets", 50, 100}, {"Cells", 300, 600}, {"Grenades", 20, 40}}) {
            e.events.push_back(Ev(PayloadConditionMet{}, {Self("Cap " + v)}, IfVars(v, GT, "Max" + v)));
            add(Dp("Cap " + v, SetVar(v, normal), 0.0f, {ThenIf(Self("Cap " + v + " Backpack"), If("HasBackpack", EQ, 1))}));
            add(Dp("Cap " + v + " Backpack", SetVar(v, backpack)));
        }

        // ── Called by health and armor pickups, so they stop at their limit ──
        add(Dp("Cap Health 100", SetVar("Health", 100)));
        add(Dp("Cap Health 200", SetVar("Health", 200)));
        add(Dp("Cap Armor 200", SetVar("Armor", 200)));
        add(Dp("Green Armor Type", SetVar("ArmorType", 1)));

        // ── Called by key pickups. The tag is what locked doors check ──
        for (const char* color : {"Red", "Blue", "Yellow"}) {
            PayloadDispatchChangeTag addTag;
            addTag.tag = Tag(Str(color) + "Key");
            add(Dp(Str("Get ") + color + " Key", addTag));
            add(Dp(Str("Get ") + color + " Key", SetVar(Str("Has") + color + "Key", 1)));
        }

        // ── Called by powerups: they run here because the pickup is gone by the time they end ──
        add(Dp("Get Invulnerability", SetVar("Invulnerable", 1), 0.0f, {Then(Self("End Invulnerability"), 30.0f)}));
        add(Dp("End Invulnerability", SetVar("Invulnerable", 0)));
        add(Dp("Get Radiation Suit", SetVar("RadSuit", 1), 0.0f, {Then(Self("End Radiation Suit"), 60.0f)}));
        add(Dp("End Radiation Suit", SetVar("RadSuit", 0)));

        // ── Called by enemies and toxic floors. See PlayerHealth.lua ──
        add(Dp("Take Damage", PayloadDispatchEntityCallFunction{"TakeIncomingDamage"}));

        // ── Dying ──
        e.events.push_back(Ev(PayloadConditionMet{}, {Self("Die"), Tagged("Death Screen", "Show")}, If("Health", LE, 0)));
        add(Dp("Die", Play("Player/death")));
        add(Dp("Die", SetVar("MenuState", 2)));
        add(Dp("Die", SetF(FieldComponent::PLAYER_CONTROLLER, "speed", Num(0))));
        add(Dp("Die", SetF(FieldComponent::PLAYER_CONTROLLER, "runningSpeed", Num(0))));
        add(Dp("Die", SetF(FieldComponent::PLAYER_CONTROLLER, "jumpPower", Num(0))));
        {
            auto sink = SetF(FieldComponent::PLAYER_CONTROLLER, "eyeHeight", Num(2.5f));
            sink.overTime = true;
            sink.speed = 12.0f;
            add(Dp("Die", sink));
        }
    });
}
