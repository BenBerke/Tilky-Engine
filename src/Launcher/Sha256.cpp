//
// Created by berke on 8/2/2026.
//
#include "Headers/Launcher/Sha256.hpp"

#include <cstring>
#include <fstream>
#include <vector>

namespace TilkySha256 {
    namespace {
        constexpr std::uint32_t K[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
        };

        constexpr std::uint32_t RotRight(const std::uint32_t value, const int bits) {
            return (value >> bits) | (value << (32 - bits));
        }

        // 64 KiB read chunks for HashFile - large enough to avoid excessive syscall
        // overhead on a multi-hundred-MB engine ZIP, small enough to keep memory use flat.
        constexpr std::size_t FILE_READ_CHUNK_SIZE = 1 << 16;
    }

    Hasher::Hasher() {
        state_[0] = 0x6a09e667;
        state_[1] = 0xbb67ae85;
        state_[2] = 0x3c6ef372;
        state_[3] = 0xa54ff53a;
        state_[4] = 0x510e527f;
        state_[5] = 0x9b05688c;
        state_[6] = 0x1f83d9ab;
        state_[7] = 0x5be0cd19;
    }

    void Hasher::Transform(const std::uint8_t block[64]) {
        std::uint32_t m[64];

        for (int i = 0, j = 0; i < 16; ++i, j += 4) {
            m[i] = (static_cast<std::uint32_t>(block[j]) << 24) |
                   (static_cast<std::uint32_t>(block[j + 1]) << 16) |
                   (static_cast<std::uint32_t>(block[j + 2]) << 8) |
                   static_cast<std::uint32_t>(block[j + 3]);
        }

        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = RotRight(m[i - 15], 7) ^ RotRight(m[i - 15], 18) ^ (m[i - 15] >> 3);
            const std::uint32_t s1 = RotRight(m[i - 2], 17) ^ RotRight(m[i - 2], 19) ^ (m[i - 2] >> 10);
            m[i] = m[i - 16] + s0 + m[i - 7] + s1;
        }

        std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];

        for (int i = 0; i < 64; ++i) {
            const std::uint32_t ep1 = RotRight(e, 6) ^ RotRight(e, 11) ^ RotRight(e, 25);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t t1 = h + ep1 + ch + K[i] + m[i];
            const std::uint32_t ep0 = RotRight(a, 2) ^ RotRight(a, 13) ^ RotRight(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = ep0 + maj;

            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    void Hasher::Update(const std::uint8_t* data, const std::size_t length) {
        for (std::size_t i = 0; i < length; ++i) {
            buffer_[bufferLength_++] = data[i];

            if (bufferLength_ == 64) {
                Transform(buffer_);
                bitLength_ += 512;
                bufferLength_ = 0;
            }
        }
    }

    std::array<std::uint8_t, 32> Hasher::Finalize() {
        std::uint32_t i = bufferLength_;

        // Pad whatever data is left in the buffer: a single 0x80 byte followed by
        // zeros, leaving the last 8 bytes of the final block for the bit-length.
        if (bufferLength_ < 56) {
            buffer_[i++] = 0x80;
            while (i < 56) buffer_[i++] = 0x00;
        } else {
            buffer_[i++] = 0x80;
            while (i < 64) buffer_[i++] = 0x00;
            Transform(buffer_);
            std::memset(buffer_, 0, 56);
        }

        bitLength_ += static_cast<std::uint64_t>(bufferLength_) * 8;
        buffer_[63] = static_cast<std::uint8_t>(bitLength_);
        buffer_[62] = static_cast<std::uint8_t>(bitLength_ >> 8);
        buffer_[61] = static_cast<std::uint8_t>(bitLength_ >> 16);
        buffer_[60] = static_cast<std::uint8_t>(bitLength_ >> 24);
        buffer_[59] = static_cast<std::uint8_t>(bitLength_ >> 32);
        buffer_[58] = static_cast<std::uint8_t>(bitLength_ >> 40);
        buffer_[57] = static_cast<std::uint8_t>(bitLength_ >> 48);
        buffer_[56] = static_cast<std::uint8_t>(bitLength_ >> 56);
        Transform(buffer_);

        // This implementation keeps state[] in host order and swaps to big-endian
        // only when writing out the final digest bytes.
        std::array<std::uint8_t, 32> hash{};
        for (int b = 0; b < 4; ++b) {
            for (int word = 0; word < 8; ++word) {
                hash[word * 4 + b] = static_cast<std::uint8_t>((state_[word] >> (24 - b * 8)) & 0xff);
            }
        }
        return hash;
    }

    std::string ToHex(const std::array<std::uint8_t, 32>& digest) {
        static constexpr char hexChars[] = "0123456789abcdef";

        std::string result;
        result.reserve(64);

        for (const std::uint8_t byte : digest) {
            result.push_back(hexChars[byte >> 4]);
            result.push_back(hexChars[byte & 0x0f]);
        }

        return result;
    }

    std::optional<std::string> HashFile(const fs::path& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return std::nullopt;

        Hasher hasher;
        std::vector<std::uint8_t> chunk(FILE_READ_CHUNK_SIZE);

        while (file) {
            file.read(reinterpret_cast<char*>(chunk.data()), static_cast<std::streamsize>(chunk.size()));
            const std::streamsize bytesRead = file.gcount();
            if (bytesRead > 0) hasher.Update(chunk.data(), static_cast<std::size_t>(bytesRead));
        }

        return ToHex(hasher.Finalize());
    }
}