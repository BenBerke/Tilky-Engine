//
// Created by berke on 10/31/2026.
//

#ifndef TILKY_ENGINE_SOUNDDECODERS_HPP
#define TILKY_ENGINE_SOUNDDECODERS_HPP

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Decodes the compressed sound formats (MP3 with dr_mp3, OGG Vorbis with
// stb_vorbis, FLAC with dr_flac) to 16-bit PCM for an OpenAL buffer. WAV
// stays with SDL_LoadWAV in SoundManager.
namespace SoundDecoders {
    struct PCM {
        std::vector<int16_t> samples; // interleaved
        int channels = 0;
        int sampleRate = 0;
    };

    // ".mp3", ".ogg" or ".flac", any case.
    [[nodiscard]] bool IsCompressedExtension(const std::string& extension);

    // False with `error` set when the file can't be read or decoded.
    bool Decode(const std::filesystem::path& path, PCM& out, std::string& error);
}

#endif //TILKY_ENGINE_SOUNDDECODERS_HPP
