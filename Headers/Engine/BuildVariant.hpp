#ifndef TILKY_ENGINE_BUILDVARIANT_HPP
#define TILKY_ENGINE_BUILDVARIANT_HPP

// Which executable this code was linked into. Most runtime sources are
// compiled once and shared by Tilky_Engine and the standalone game, so they
// can't test TILKY_STANDALONE themselves; they ask here instead.
// BuildVariant.cpp is the one source compiled separately for each target.
//
// Code that needs ImGui or editor-only symbols can't use this (the game
// doesn't link them) and stays in the per-target sources (see
// TILKY_VARIANT_SOURCES in CMakeLists.txt) behind #ifdef TILKY_STANDALONE.
namespace BuildVariant {
    [[nodiscard]] bool IsStandalone();
}

#endif // TILKY_ENGINE_BUILDVARIANT_HPP
