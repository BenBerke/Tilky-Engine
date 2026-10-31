//
// Created by berke on 10/31/2026.
//
// The decoder libraries are single files: their implementations live in
// this translation unit only.

#include "Headers/Runtime/Sound/SoundDecoders.hpp"

#define DR_MP3_IMPLEMENTATION
#include <dr_mp3.h>

#define DR_FLAC_IMPLEMENTATION
#include <dr_flac.h>

// stb_vorbis is a .c file; its macros (L, C, R...) stay in here.
#include <stb_vorbis.c>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>

namespace {
    std::string Lower(std::string text) {
        std::ranges::transform(text, text.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    // Read through std::filesystem so paths with any characters work; the
    // libraries' own file opening uses the narrow C runtime.
    bool ReadFile(const std::filesystem::path& path, std::vector<unsigned char>& bytes, std::string& error) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            error = "couldn't open the file";
            return false;
        }

        bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        if (bytes.empty()) {
            error = "the file is empty";
            return false;
        }

        return true;
    }

    bool DecodeMP3(const std::vector<unsigned char>& bytes, SoundDecoders::PCM& out, std::string& error) {
        drmp3_config config{};
        drmp3_uint64 frames = 0;
        drmp3_int16* samples = drmp3_open_memory_and_read_pcm_frames_s16(bytes.data(), bytes.size(), &config, &frames, nullptr);
        if (!samples) {
            error = "not a valid MP3";
            return false;
        }

        out.channels = static_cast<int>(config.channels);
        out.sampleRate = static_cast<int>(config.sampleRate);
        out.samples.assign(samples, samples + frames * config.channels);
        drmp3_free(samples, nullptr);
        return true;
    }

    bool DecodeFLAC(const std::vector<unsigned char>& bytes, SoundDecoders::PCM& out, std::string& error) {
        unsigned int channels = 0, sampleRate = 0;
        drflac_uint64 frames = 0;
        drflac_int16* samples = drflac_open_memory_and_read_pcm_frames_s16(bytes.data(), bytes.size(), &channels, &sampleRate, &frames, nullptr);
        if (!samples) {
            error = "not a valid FLAC";
            return false;
        }

        out.channels = static_cast<int>(channels);
        out.sampleRate = static_cast<int>(sampleRate);
        out.samples.assign(samples, samples + frames * channels);
        drflac_free(samples, nullptr);
        return true;
    }

    bool DecodeOGG(const std::vector<unsigned char>& bytes, SoundDecoders::PCM& out, std::string& error) {
        int channels = 0, sampleRate = 0;
        short* samples = nullptr;
        const int frames = stb_vorbis_decode_memory(bytes.data(), static_cast<int>(bytes.size()), &channels, &sampleRate, &samples);
        if (frames < 0 || !samples) {
            error = "not a valid OGG Vorbis";
            return false;
        }

        out.channels = channels;
        out.sampleRate = sampleRate;
        out.samples.assign(samples, samples + static_cast<size_t>(frames) * static_cast<size_t>(channels));
        std::free(samples);
        return true;
    }
}

namespace SoundDecoders {
    bool IsCompressedExtension(const std::string& extension) {
        const std::string lower = Lower(extension);
        return lower == ".mp3" || lower == ".ogg" || lower == ".flac";
    }

    bool Decode(const std::filesystem::path& path, PCM& out, std::string& error) {
        std::vector<unsigned char> bytes;
        if (!ReadFile(path, bytes, error)) return false;

        const std::string extension = Lower(path.extension().string());
        out = {};

        bool ok = false;
        if (extension == ".mp3") ok = DecodeMP3(bytes, out, error);
        else if (extension == ".flac") ok = DecodeFLAC(bytes, out, error);
        else if (extension == ".ogg") ok = DecodeOGG(bytes, out, error);
        else error = "not an MP3, OGG or FLAC file";

        if (ok && out.samples.empty()) {
            error = "it has no sound in it";
            return false;
        }

        return ok;
    }
}
