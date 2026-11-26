// See Headers/Engine/AssetFS.hpp.

#include "Headers/Engine/AssetFS.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

extern "C" volatile const std::uint8_t tilkyPackKeySlot[64];

namespace AssetFS {
namespace {
    // ---- Pack layout ----
    //
    // Header (plain, HEADER_SIZE bytes):
    //   "TPAK", u32 version, u8 salt[16], u64 index offset, u64 index size
    // File data: each file encrypted with nonce(salt, its index + 1).
    // Index (encrypted with nonce(salt, 0)):
    //   "TIDX", u32 file count, per file: string path, u64 offset, u64 size,
    //   u32 flags (0); u32 set count, per set: string name, u32 count, strings
    //   (whatever names the set's user keys by). Strings are a u32 length and
    //   the bytes.

    constexpr char PACK_MAGIC[4] = {'T', 'P', 'A', 'K'};
    constexpr char INDEX_MAGIC[4] = {'T', 'I', 'D', 'X'};
    constexpr std::uint32_t PACK_VERSION = 1;
    constexpr std::size_t HEADER_SIZE = 48;

    // Key slot layout (PackKeySlot.cpp): noise[16], masked key[32], check[4], spare[12].
    constexpr std::size_t SLOT_NOISE = 0;
    constexpr std::size_t SLOT_KEY = 16;
    constexpr std::size_t SLOT_CHECK = 48;
    constexpr std::size_t SLOT_SIZE = 64;

    // ---- ChaCha20 (RFC 8439) ----

    std::uint32_t Rotl(const std::uint32_t v, const int c) { return (v << c) | (v >> (32 - c)); }

    void QuarterRound(std::uint32_t* x, const int a, const int b, const int c, const int d) {
        x[a] += x[b]; x[d] = Rotl(x[d] ^ x[a], 16);
        x[c] += x[d]; x[b] = Rotl(x[b] ^ x[c], 12);
        x[a] += x[b]; x[d] = Rotl(x[d] ^ x[a], 8);
        x[c] += x[d]; x[b] = Rotl(x[b] ^ x[c], 7);
    }

    std::uint32_t Load32(const std::uint8_t* p) {
        return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
               (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
    }

    void Block(const std::uint8_t key[32], const std::uint8_t nonce[12], const std::uint32_t counter, std::uint8_t out[64]) {
        std::uint32_t state[16] = {0x61707865, 0x3320646e, 0x79622d32, 0x6b206574};
        for (int i = 0; i < 8; ++i) state[4 + i] = Load32(key + i * 4);
        state[12] = counter;
        for (int i = 0; i < 3; ++i) state[13 + i] = Load32(nonce + i * 4);

        std::uint32_t x[16];
        std::memcpy(x, state, sizeof(x));
        for (int round = 0; round < 10; ++round) {
            QuarterRound(x, 0, 4, 8, 12);
            QuarterRound(x, 1, 5, 9, 13);
            QuarterRound(x, 2, 6, 10, 14);
            QuarterRound(x, 3, 7, 11, 15);
            QuarterRound(x, 0, 5, 10, 15);
            QuarterRound(x, 1, 6, 11, 12);
            QuarterRound(x, 2, 7, 8, 13);
            QuarterRound(x, 3, 4, 9, 14);
        }
        for (int i = 0; i < 16; ++i) {
            const std::uint32_t v = x[i] + state[i];
            out[i * 4 + 0] = static_cast<std::uint8_t>(v);
            out[i * 4 + 1] = static_cast<std::uint8_t>(v >> 8);
            out[i * 4 + 2] = static_cast<std::uint8_t>(v >> 16);
            out[i * 4 + 3] = static_cast<std::uint8_t>(v >> 24);
        }
    }

    // XORs the keystream from byte `offset` of the stream into `data`.
    void Crypt(const Key& key, const std::uint8_t nonce[12], const std::uint64_t offset, std::uint8_t* data, const std::size_t size) {
        std::uint8_t stream[64];
        std::uint64_t position = offset;
        std::size_t done = 0;
        while (done < size) {
            Block(key.data(), nonce, static_cast<std::uint32_t>(position / 64), stream);
            const std::size_t start = static_cast<std::size_t>(position % 64);
            const std::size_t count = (std::min)(size - done, static_cast<std::size_t>(64) - start);
            for (std::size_t i = 0; i < count; ++i) data[done + i] ^= stream[start + i];
            done += count;
            position += count;
        }
    }

    using Salt = std::array<std::uint8_t, 16>;

    // Index: stream 0; file i: stream i + 1.
    std::array<std::uint8_t, 12> Nonce(const Salt& salt, const std::uint32_t stream) {
        std::array<std::uint8_t, 12> nonce{};
        std::memcpy(nonce.data(), salt.data(), 12);
        for (int i = 0; i < 4; ++i) nonce[8 + i] ^= static_cast<std::uint8_t>(stream >> (i * 8));
        nonce[0] ^= salt[12];
        nonce[1] ^= salt[13];
        nonce[2] ^= salt[14];
        nonce[3] ^= salt[15];
        return nonce;
    }

    // ---- Key slot ----

    std::uint32_t SlotCheck(const std::uint8_t* noise) {
        std::uint32_t hash = 2166136261u;
        for (int i = 0; i < 16; ++i) {
            hash ^= noise[i];
            hash *= 16777619u;
        }
        return hash ^ 0x5bd1e995u;
    }

    // The 32 bytes the key is XORed with: a ChaCha20 block keyed by the noise.
    std::array<std::uint8_t, 32> SlotMask(const std::uint8_t* noise) {
        Key maskKey{};
        std::memcpy(maskKey.data(), noise, 16);
        std::memcpy(maskKey.data() + 16, noise, 16);
        const std::uint8_t nonce[12] = {'t', 'i', 'l', 'k', 'y', 'k', 'e', 'y', 0, 0, 0, 0};
        std::uint8_t block[64];
        Block(maskKey.data(), nonce, 7, block);
        std::array<std::uint8_t, 32> mask{};
        std::memcpy(mask.data(), block, 32);
        return mask;
    }

    std::array<std::uint8_t, SLOT_SIZE> ReadSlot() {
        std::array<std::uint8_t, SLOT_SIZE> slot{};
        for (std::size_t i = 0; i < SLOT_SIZE; ++i) slot[i] = tilkyPackKeySlot[i];
        return slot;
    }

    bool KeyFromSlot(Key& key) {
        const std::array<std::uint8_t, SLOT_SIZE> slot = ReadSlot();
        std::uint32_t check = 0;
        std::memcpy(&check, slot.data() + SLOT_CHECK, 4);
        if (check != SlotCheck(slot.data() + SLOT_NOISE)) return false;

        const std::array<std::uint8_t, 32> mask = SlotMask(slot.data() + SLOT_NOISE);
        for (std::size_t i = 0; i < 32; ++i) key[i] = slot[SLOT_KEY + i] ^ mask[i];
        return true;
    }

    // ---- Paths ----

    std::string Lower(std::string text) {
        std::ranges::transform(text, text.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    std::string FirstPart(const std::string& relative) {
        const std::size_t slash = relative.find('/');
        return slash == std::string::npos ? relative : relative.substr(0, slash);
    }

    // ---- The mounted pack ----

    struct Entry {
        std::string path; // relative to the root, generic
        std::uint64_t offset = 0;
        std::uint64_t size = 0;
    };

    struct Pack {
        bool mounted = false;
        fs::path file;
        fs::path root; // lexically normal, no trailing separator
        Key key{};
        Salt salt{};
        std::vector<Entry> entries;
        std::unordered_map<std::string, std::size_t> byPath;
        std::unordered_map<std::string, std::size_t> byLowerPath;
        std::unordered_set<std::string> lowerFolders; // every folder holding a file
        std::unordered_set<std::string> lowerTopNames;
        std::vector<FileSet> sets;
    };

    Pack pack;

    fs::path NormalRoot(const fs::path& root) {
        fs::path normal = root.lexically_normal();
        if (!normal.has_filename() && normal.has_parent_path() && normal != normal.root_path()) normal = normal.parent_path();
        return normal;
    }

    // The path relative to the root if the pack answers for it.
    bool PackRelative(const fs::path& path, std::string& relative) {
        if (!pack.mounted) return false;

        const fs::path rel = path.lexically_normal().lexically_relative(pack.root);
        if (rel.empty()) return false;

        std::string text = rel.generic_string();
        while (text.ends_with('/')) text.pop_back();
        if (text.empty() || text == "." || text.starts_with("..")) return false;

        if (!pack.lowerTopNames.contains(Lower(FirstPart(text)))) return false;
        relative = std::move(text);
        return true;
    }

    const Entry* FindEntry(const std::string& relative) {
        if (const auto it = pack.byPath.find(relative); it != pack.byPath.end()) return &pack.entries[it->second];
        if (const auto it = pack.byLowerPath.find(Lower(relative)); it != pack.byLowerPath.end()) return &pack.entries[it->second];
        return nullptr;
    }

    template <typename Buffer>
    bool ReadEntry(const Entry& entry, Buffer& out) {
        std::ifstream file(pack.file, std::ios::binary);
        if (!file) return false;
        file.seekg(static_cast<std::streamoff>(entry.offset));

        out.resize(static_cast<std::size_t>(entry.size));
        if (entry.size > 0 && !file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(entry.size))) return false;

        const std::size_t index = static_cast<std::size_t>(&entry - pack.entries.data());
        const auto nonce = Nonce(pack.salt, static_cast<std::uint32_t>(index + 1));
        Crypt(pack.key, nonce.data(), 0, reinterpret_cast<std::uint8_t*>(out.data()), out.size());
        return true;
    }

    template <typename Buffer>
    bool ReadDisk(const fs::path& path, Buffer& out) {
        std::ifstream file(path, std::ios::binary);
        if (!file) return false;
        file.seekg(0, std::ios::end);
        const std::streamoff size = file.tellg();
        if (size < 0) return false;
        file.seekg(0, std::ios::beg);
        out.resize(static_cast<std::size_t>(size));
        if (size > 0 && !file.read(reinterpret_cast<char*>(out.data()), size)) return false;
        return true;
    }

    // ---- Index reading/writing ----

    struct Reader {
        const std::string& data;
        std::size_t position = 0;

        template <typename T>
        bool Read(T& value) {
            if (data.size() - position < sizeof(T)) return false;
            std::memcpy(&value, data.data() + position, sizeof(T));
            position += sizeof(T);
            return true;
        }

        bool ReadString(std::string& text) {
            std::uint32_t size = 0;
            if (!Read(size) || data.size() - position < size) return false;
            text.assign(data, position, size);
            position += size;
            return true;
        }
    };

    template <typename T>
    void Write(std::string& out, const T& value) {
        out.append(reinterpret_cast<const char*>(&value), sizeof(T));
    }

    void WriteString(std::string& out, const std::string& text) {
        Write(out, static_cast<std::uint32_t>(text.size()));
        out += text;
    }

    void SetError(std::string* error, std::string text) {
        if (error != nullptr) *error = std::move(text);
    }
}

// ============================================================================
// The running game
// ============================================================================

MountResult MountExportedPack(const fs::path& root, std::string* error) {
    Key key{};
    if (!KeyFromSlot(key)) return MountResult::NotExported;

    Pack mounted;
    mounted.root = NormalRoot(root);
    mounted.file = mounted.root / PACK_FILE_NAME;
    mounted.key = key;

    std::ifstream file(mounted.file, std::ios::binary);
    if (!file) {
        SetError(error, "The game's data file is missing: " + mounted.file.string());
        return MountResult::Failed;
    }

    char header[HEADER_SIZE] = {};
    if (!file.read(header, HEADER_SIZE) || std::memcmp(header, PACK_MAGIC, 4) != 0) {
        SetError(error, "The game's data file is damaged: " + mounted.file.string());
        return MountResult::Failed;
    }

    std::uint32_t version = 0;
    std::uint64_t indexOffset = 0;
    std::uint64_t indexSize = 0;
    std::memcpy(&version, header + 4, 4);
    std::memcpy(mounted.salt.data(), header + 8, 16);
    std::memcpy(&indexOffset, header + 24, 8);
    std::memcpy(&indexSize, header + 32, 8);

    if (version != PACK_VERSION) {
        SetError(error, "The game's data file is from another engine version: " + mounted.file.string());
        return MountResult::Failed;
    }

    file.seekg(0, std::ios::end);
    const std::uint64_t fileSize = static_cast<std::uint64_t>(file.tellg());
    if (indexOffset < HEADER_SIZE || indexOffset > fileSize || indexSize > fileSize - indexOffset) {
        SetError(error, "The game's data file is damaged: " + mounted.file.string());
        return MountResult::Failed;
    }

    std::string index(static_cast<std::size_t>(indexSize), '\0');
    file.seekg(static_cast<std::streamoff>(indexOffset));
    if (!file.read(index.data(), static_cast<std::streamsize>(indexSize))) {
        SetError(error, "The game's data file is damaged: " + mounted.file.string());
        return MountResult::Failed;
    }
    const auto indexNonce = Nonce(mounted.salt, 0);
    Crypt(key, indexNonce.data(), 0, reinterpret_cast<std::uint8_t*>(index.data()), index.size());

    // A name the file system can't take (a damaged byte) would throw when
    // the game first turns it into a path.
    const auto convertible = [](const std::string& name) {
        try {
            (void)fs::path(name);
            return true;
        }
        catch (const std::exception&) {
            return false;
        }
    };

    Reader reader{index};
    char magic[4] = {};
    std::uint32_t fileCount = 0;
    bool valid = reader.Read(magic) && std::memcmp(magic, INDEX_MAGIC, 4) == 0 && reader.Read(fileCount);

    // Every file must lie between the header and the index (a damaged size
    // would otherwise be trusted when the file is read).
    for (std::uint32_t i = 0; valid && i < fileCount; ++i) {
        Entry entry;
        std::uint32_t flags = 0;
        valid = reader.ReadString(entry.path) && reader.Read(entry.offset) && reader.Read(entry.size) && reader.Read(flags) &&
                !entry.path.empty() && entry.offset >= HEADER_SIZE && entry.offset <= indexOffset &&
                entry.size <= indexOffset - entry.offset && convertible(entry.path);
        if (valid) mounted.entries.push_back(std::move(entry));
    }

    std::uint32_t setCount = 0;
    valid = valid && reader.Read(setCount);
    for (std::uint32_t i = 0; valid && i < setCount; ++i) {
        FileSet set;
        std::uint32_t count = 0;
        valid = reader.ReadString(set.name) && reader.Read(count);
        for (std::uint32_t j = 0; valid && j < count; ++j) {
            std::string member;
            valid = reader.ReadString(member) && convertible(member);
            if (valid) set.files.push_back(std::move(member));
        }
        if (valid) mounted.sets.push_back(std::move(set));
    }

    if (!valid) {
        SetError(error, "The game's data file doesn't belong to this executable, or is damaged: " + mounted.file.string());
        return MountResult::Failed;
    }

    for (std::size_t i = 0; i < mounted.entries.size(); ++i) {
        const std::string& path = mounted.entries[i].path;
        mounted.byPath.emplace(path, i);
        mounted.byLowerPath.emplace(Lower(path), i);
        mounted.lowerTopNames.insert(Lower(FirstPart(path)));

        for (std::size_t slash = path.find('/'); slash != std::string::npos; slash = path.find('/', slash + 1))
            mounted.lowerFolders.insert(Lower(path.substr(0, slash)));
    }

    mounted.mounted = true;
    pack = std::move(mounted);
    return MountResult::Mounted;
}

bool IsPacked() { return pack.mounted; }

bool CoversPath(const fs::path& path) {
    std::string relative;
    return PackRelative(path, relative);
}

bool Exists(const fs::path& path) {
    if (std::string relative; PackRelative(path, relative)) return FindEntry(relative) != nullptr || pack.lowerFolders.contains(Lower(relative));
    std::error_code ec;
    return fs::exists(path, ec);
}

bool IsFile(const fs::path& path) {
    if (std::string relative; PackRelative(path, relative)) return FindEntry(relative) != nullptr;
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

bool IsDirectory(const fs::path& path) {
    if (std::string relative; PackRelative(path, relative)) return pack.lowerFolders.contains(Lower(relative));
    std::error_code ec;
    return fs::is_directory(path, ec);
}

bool WriteTime(const fs::path& path, fs::file_time_type& out) {
    if (std::string relative; PackRelative(path, relative)) {
        out = fs::file_time_type{};
        return FindEntry(relative) != nullptr;
    }
    std::error_code ec;
    out = fs::last_write_time(path, ec);
    return !ec;
}

bool ReadFile(const fs::path& path, std::string& out) {
    if (std::string relative; PackRelative(path, relative)) {
        const Entry* entry = FindEntry(relative);
        return entry != nullptr && ReadEntry(*entry, out);
    }
    return ReadDisk(path, out);
}

bool ReadFile(const fs::path& path, std::vector<unsigned char>& out) {
    if (std::string relative; PackRelative(path, relative)) {
        const Entry* entry = FindEntry(relative);
        return entry != nullptr && ReadEntry(*entry, out);
    }
    return ReadDisk(path, out);
}

std::vector<fs::path> ListPackedFiles(const fs::path& folder, const bool recursive) {
    std::vector<fs::path> files;
    if (!pack.mounted) return files;

    std::string relative;
    if (!PackRelative(folder, relative)) {
        // The root itself: everything in the pack.
        const fs::path rel = folder.lexically_normal().lexically_relative(pack.root);
        const std::string text = rel.generic_string();
        if (text != "." && !text.empty()) return files;
        relative.clear();
    }

    const std::string prefix = relative.empty() ? std::string() : Lower(relative) + "/";
    for (const Entry& entry : pack.entries) {
        const std::string lower = Lower(entry.path);
        if (!lower.starts_with(prefix)) continue;
        if (!recursive && lower.find('/', prefix.size()) != std::string::npos) continue;
        files.push_back(pack.root / fs::path(entry.path));
    }
    return files;
}

const std::vector<FileSet>& PackedSets() { return pack.sets; }

// ============================================================================
// Writing a pack
// ============================================================================

Key NewKey() {
    std::random_device device;
    Key key{};
    for (std::uint8_t& byte : key) byte = static_cast<std::uint8_t>(device());
    return key;
}

bool WritePack(const fs::path& sourceRoot, const fs::path& pakPath, const Key& key, const std::vector<FileSet>& sets, std::string& error) {
    std::vector<std::string> paths;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(sourceRoot, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file()) paths.push_back(it->path().lexically_relative(sourceRoot).generic_string());
    if (ec) {
        error = "Couldn't list " + sourceRoot.string() + ": " + ec.message();
        return false;
    }
    std::ranges::sort(paths);

    Salt salt{};
    {
        std::random_device device;
        for (std::uint8_t& byte : salt) byte = static_cast<std::uint8_t>(device());
    }

    std::ofstream out(pakPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "Couldn't create " + pakPath.string();
        return false;
    }

    out.write(std::string(HEADER_SIZE, '\0').data(), HEADER_SIZE);

    std::vector<Entry> entries;
    std::uint64_t offset = HEADER_SIZE;
    std::vector<std::uint8_t> bytes;

    for (std::size_t i = 0; i < paths.size(); ++i) {
        if (!ReadDisk(sourceRoot / fs::path(paths[i]), bytes)) {
            error = "Couldn't read " + (sourceRoot / fs::path(paths[i])).string();
            return false;
        }

        const auto nonce = Nonce(salt, static_cast<std::uint32_t>(i + 1));
        Crypt(key, nonce.data(), 0, bytes.data(), bytes.size());
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));

        entries.push_back({paths[i], offset, bytes.size()});
        offset += bytes.size();
    }

    std::string index(INDEX_MAGIC, 4);
    Write(index, static_cast<std::uint32_t>(entries.size()));
    for (const Entry& entry : entries) {
        WriteString(index, entry.path);
        Write(index, entry.offset);
        Write(index, entry.size);
        Write(index, static_cast<std::uint32_t>(0));
    }

    Write(index, static_cast<std::uint32_t>(sets.size()));
    for (const FileSet& set : sets) {
        std::vector<std::string> members = set.files;
        std::ranges::sort(members);
        members.erase(std::unique(members.begin(), members.end()), members.end());

        WriteString(index, set.name);
        Write(index, static_cast<std::uint32_t>(members.size()));
        for (const std::string& member : members) WriteString(index, member);
    }

    const auto indexNonce = Nonce(salt, 0);
    Crypt(key, indexNonce.data(), 0, reinterpret_cast<std::uint8_t*>(index.data()), index.size());
    out.write(index.data(), static_cast<std::streamsize>(index.size()));

    char header[HEADER_SIZE] = {};
    std::memcpy(header, PACK_MAGIC, 4);
    std::memcpy(header + 4, &PACK_VERSION, 4);
    std::memcpy(header + 8, salt.data(), 16);
    const std::uint64_t indexSize = index.size();
    std::memcpy(header + 24, &offset, 8);
    std::memcpy(header + 32, &indexSize, 8);
    out.seekp(0);
    out.write(header, HEADER_SIZE);

    out.close();
    if (!out) {
        error = "Couldn't write " + pakPath.string();
        return false;
    }
    return true;
}

bool PatchKeyIntoExecutable(const fs::path& executable, const Key& key, std::string& error, std::size_t* slotOffset,
                            std::size_t* slotSize) {
    std::string bytes;
    if (!ReadDisk(executable, bytes)) {
        error = "Couldn't read " + executable.string();
        return false;
    }

    // The marker, from this (never patched) program's own slot.
    const std::array<std::uint8_t, SLOT_SIZE> ownSlot = ReadSlot();
    const std::string_view marker(reinterpret_cast<const char*>(ownSlot.data()), 16);

    const std::size_t at = bytes.find(marker);
    if (at == std::string::npos || bytes.size() - at < SLOT_SIZE) {
        error = "The runtime " + executable.string() + " has no key slot (is it from an older engine?)";
        return false;
    }
    if (bytes.find(marker, at + 1) != std::string::npos) {
        error = "The runtime " + executable.string() + " has more than one key slot";
        return false;
    }

    std::array<std::uint8_t, 16> noise{};
    std::random_device device;
    do {
        for (std::uint8_t& byte : noise) byte = static_cast<std::uint8_t>(device());
    } while (std::memcmp(noise.data(), marker.data(), 16) == 0);

    const std::array<std::uint8_t, 32> mask = SlotMask(noise.data());
    const std::uint32_t check = SlotCheck(noise.data());

    std::memcpy(bytes.data() + at + SLOT_NOISE, noise.data(), 16);
    for (std::size_t i = 0; i < 32; ++i) bytes[at + SLOT_KEY + i] = static_cast<char>(key[i] ^ mask[i]);
    std::memcpy(bytes.data() + at + SLOT_CHECK, &check, 4);

    std::ofstream out(executable, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) {
        error = "Couldn't write " + executable.string();
        return false;
    }

    if (slotOffset != nullptr) *slotOffset = at;
    if (slotSize != nullptr) *slotSize = SLOT_SIZE;
    return true;
}
}
