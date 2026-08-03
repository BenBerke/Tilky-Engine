//
// Created by berke on 8/2/2026.
//
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace fs = std::filesystem;

// Minimal, dependency-free SHA-256 (FIPS 180-4). Added for EngineVersionManager's
// download-integrity check.
//
// CPR's default SSL backend is OpenSSL on Linux but NOT on Windows (vcpkg's cpr port
// only pulls openssl on the "linux" platform - see the cpr feature block in
// vcpkg's ports/cpr/vcpkg.json), so a hashing routine that depends on OpenSSL being
// linked in would silently be unavailable on Windows builds. This implementation has
// no third-party dependency and works identically on every platform we ship for.
//
// Ported from the public-domain reference implementation by Brad Conte
// (https://github.com/B-Con/crypto-algorithms) and verified against the NIST FIPS
// 180-2 test vectors (including the 1,000,000 x 'a' long-message vector) plus a
// streaming file-hash test before being added to this codebase.
namespace TilkySha256 {
    // Streaming hasher: feed it data in any number of Update() calls, then call
    // Finalize() exactly once to get the 32-byte digest.
    class Hasher {
    public:
        Hasher();

        void Update(const std::uint8_t* data, std::size_t length);

        // Finalizes and returns the digest. Do not call Update() after this.
        std::array<std::uint8_t, 32> Finalize();

    private:
        void Transform(const std::uint8_t block[64]);

        std::uint8_t buffer_[64]{};
        std::uint32_t bufferLength_ = 0;
        std::uint64_t bitLength_ = 0;
        std::uint32_t state_[8]{};
    };

    // Lowercase hex string, e.g. "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855".
    std::string ToHex(const std::array<std::uint8_t, 32>& digest);

    // Hashes a file's contents on disk, reading it in 64 KiB chunks so a large engine
    // ZIP is never fully loaded into memory. Returns std::nullopt if the file could
    // not be opened for reading.
    std::optional<std::string> HashFile(const fs::path& path);
}