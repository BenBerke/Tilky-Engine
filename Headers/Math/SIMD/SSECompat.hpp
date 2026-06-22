//
// Created by berke on 6/17/2026.
//

#ifndef TILKY_ENGINE_SSECOMPAT_HPP
#define TILKY_ENGINE_SSECOMPAT_HPP

/// Work around for the IDE mistakenly reporting _mm_shuffle as a bug

#include <xmmintrin.h>
#include <emmintrin.h>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86)
    #include <immintrin.h>
#elif defined(__aarch64__) || defined(_M_ARM64) || defined(__ARM_NEON) || defined(__ARM_NEON__)
    #include "sse2neon.h"
#else
    #error "No SIMD backend available for this platform"
#endif

#if defined(TILKY_CLANGD)
    #define TILKY_MM_SHUFFLE_PS(a, b, imm) (a)
#else
    #define TILKY_MM_SHUFFLE_PS(a, b, imm) _mm_shuffle_ps((a), (b), (imm))
#endif

#endif //TILKY_ENGINE_SSECOMPAT_HPP