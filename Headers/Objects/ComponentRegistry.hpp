//
// Created by berke on 6/18/2026.
//

#ifndef TILKY_ENGINE_COMPONENTREGISTRY_HPP
#define TILKY_ENGINE_COMPONENTREGISTRY_HPP

#pragma once

// Struct name / enum name / name of the vector in Level.hpp / translation json
#define TILKY_NORMAL_COMPONENTS(X) \
X(ComponentTransform,        CMP_TRANSFORM,         transforms,         "component.transform") \
X(ComponentSprite,           CMP_SPRITE,            sprites,            "component.sprite") \
X(ComponentDecal,            CMP_DECAL,             decals,             "component.decal") \
X(ComponentAudioSource,      CMP_AUDIO_SOURCE,      audioSources,       "component.audio_source") \
X(ComponentScript,           CMP_SCRIPT,            scripts,            "component.script") \
X(ComponentPlayerController, CMP_PLAYER_CONTROLLER, playerControllers,  "component.player_controller") \
X(ComponentCamera,           CMP_CAMERA,            cameras,            "component.camera") \
X(ComponentCollider,         CMP_COLLIDER,          colliders,          "component.collider") \
X(ComponentRigidbody,        CMP_RIGIDBODY,         rigidbodies,        "component.rigidbody")

#define TILKY_UI_COMPONENTS(X) \
X(ComponentUITransform,      CMP_UI_TRANSFORM,      ui_transforms,      "component.ui_transform") \
X(ComponentUISprite,         CMP_UI_SPRITE,         ui_sprites,         "component.ui_sprite") \
X(ComponentUIText,           CMP_UI_TEXT,           ui_texts,           "component.ui_text")

#define TILKY_COMPONENTS(X) \
TILKY_NORMAL_COMPONENTS(X) \
TILKY_UI_COMPONENTS(X)

#endif //TILKY_ENGINE_COMPONENTREGISTRY_HPP