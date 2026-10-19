#include "Headers/Engine/FolderWatcher.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <system_error>
#include <utility>

#include <spdlog/spdlog.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

#ifdef _WIN32
struct FolderWatcher::Win32State {
    static constexpr DWORD NOTIFY_FILTER =
        FILE_NOTIFY_CHANGE_FILE_NAME |
        FILE_NOTIFY_CHANGE_DIR_NAME |
        FILE_NOTIFY_CHANGE_LAST_WRITE |
        FILE_NOTIFY_CHANGE_SIZE |
        FILE_NOTIFY_CHANGE_CREATION;

    HANDLE directory = INVALID_HANDLE_VALUE;
    HANDLE changeEvent = nullptr;
    HANDLE stopEvent = nullptr;
    OVERLAPPED overlapped = {};

    // Big enough that a burst of changes (a folder of images copied in)
    // rarely overflows it. An overflow still counts as a change.
    alignas(DWORD) std::array<BYTE, 64 * 1024> buffer{};

    bool IssueRead() {
        overlapped = {};
        overlapped.hEvent = changeEvent;

        return ReadDirectoryChangesW(
            directory,
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            TRUE,
            NOTIFY_FILTER,
            nullptr,
            &overlapped,
            nullptr
        ) != 0;
    }

    ~Win32State() {
        if (directory != INVALID_HANDLE_VALUE) CloseHandle(directory);
        if (changeEvent != nullptr) CloseHandle(changeEvent);
        if (stopEvent != nullptr) CloseHandle(stopEvent);
    }
};
#else
struct FolderWatcher::Win32State {};
#endif

FolderWatcher::FolderWatcher(std::set<std::string> extensions) : extensions(std::move(extensions)) {}

FolderWatcher::~FolderWatcher() {
    Stop();
}

std::uint64_t FolderWatcher::Generation(const fs::path& folder) {
    if (folder != watchedFolder) Start(folder);

    if (!watching) return ++generation;
    return generation;
}

void FolderWatcher::Start(const fs::path& folder) {
    Stop();

    watchedFolder = folder;
    ++generation;

#ifdef _WIN32
    auto state = std::make_unique<Win32State>();

    state->directory = CreateFileW(
        folder.wstring().c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr
    );

    state->changeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    state->stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    // The first read is issued here, not on the thread, so no change made
    // after Generation() returns can slip past before the thread is up.
    if (state->directory == INVALID_HANDLE_VALUE ||
        state->changeEvent == nullptr || state->stopEvent == nullptr ||
        !state->IssueRead()) {
        spdlog::warn("FolderWatcher could not watch '{}' (error {}); rescanning every time", folder.string(), GetLastError());
        return;
    }

    win32 = std::move(state);
    watching = true;
    thread = std::thread(&FolderWatcher::Watch, this);
#endif
}

void FolderWatcher::Stop() {
#ifdef _WIN32
    if (thread.joinable()) {
        SetEvent(win32->stopEvent);
        thread.join();
    }
#endif

    win32.reset();
    watching = false;
    watchedFolder.clear();
}

bool FolderWatcher::IsCounted(const fs::path& relative, const unsigned long action) const {
    std::string extension = relative.extension().string();
    std::ranges::transform(extension, extension.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (extensions.contains(extension)) return true;

#ifdef _WIN32
    // A folder's "modified" only means something inside it changed, and that
    // something gets its own entry.
    if (action == FILE_ACTION_MODIFIED) return false;
#else
    (void)action;
#endif

    // Moving or deleting a folder moves or deletes the images in it, with no
    // entry of their own. A removed folder can't be checked any more, so a
    // name without an extension is taken to be one.
    if (extension.empty()) return true;

    std::error_code ec;
    return fs::is_directory(watchedFolder / relative, ec);
}

void FolderWatcher::Watch() {
#ifdef _WIN32
    Win32State& state = *win32;
    const HANDLE handles[2] = {state.changeEvent, state.stopEvent};

    while (true) {
        const DWORD woken = WaitForMultipleObjects(2, handles, FALSE, INFINITE);

        if (woken != WAIT_OBJECT_0) {
            CancelIoEx(state.directory, &state.overlapped);

            DWORD ignored = 0;
            GetOverlappedResult(state.directory, &state.overlapped, &ignored, TRUE);
            return;
        }

        DWORD bytes = 0;
        const bool completed = GetOverlappedResult(state.directory, &state.overlapped, &bytes, FALSE) != 0;

        // No entries means the buffer overflowed: something changed, but
        // not what.
        bool changed = !completed || bytes == 0;

        for (DWORD offset = 0; !changed && offset < bytes;) {
            const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(state.buffer.data() + offset);
            const fs::path relative(std::wstring(info->FileName, info->FileNameLength / sizeof(WCHAR)));

            if (IsCounted(relative, info->Action)) changed = true;

            if (info->NextEntryOffset == 0) break;
            offset += info->NextEntryOffset;
        }

        if (changed) ++generation;

        ResetEvent(state.changeEvent);

        if (!state.IssueRead()) {
            spdlog::warn("FolderWatcher stopped watching '{}' (error {}); rescanning every time", watchedFolder.string(), GetLastError());
            watching = false;
            ++generation;

            // Parked until Stop(), which joins this thread.
            WaitForSingleObject(state.stopEvent, INFINITE);
            return;
        }
    }
#endif
}
