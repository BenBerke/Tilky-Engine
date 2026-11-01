// An ld.lld stand-in for GCC on Windows (see "lld" in CMakeLists.txt).
//
// A long link line reaches the linker as a response file written by GCC's
// collect2, quoted the GNU way: a backslash before each space. lld's MinGW
// driver reads response files the Windows way, so every path with a space
// ("CLion Projects") breaks in two. This expands the arguments the GNU way,
// writes them to a new response file quoted the Windows way and runs the
// real lld on it.
//
// Built by CMake at configure time, never part of the engine. The real lld's
// path comes from LldWrapperConfig.h, which CMake writes next to it.

#include "LldWrapperConfig.h"

#include <windows.h>

#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
    // libiberty's buildargv: whitespace separates, a backslash escapes the
    // next character (also inside quotes), '...' and "..." group.
    std::vector<std::string> SplitGnu(const std::string& text) {
        std::vector<std::string> args;
        std::size_t i = 0;

        while (i < text.size()) {
            while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
            if (i >= text.size()) break;

            std::string arg;
            bool singleQuote = false, doubleQuote = false, escaped = false;

            for (; i < text.size(); ++i) {
                const char c = text[i];

                if (escaped) {
                    arg += c;
                    escaped = false;
                } else if (c == '\\') {
                    escaped = true;
                } else if (singleQuote) {
                    if (c == '\'') singleQuote = false;
                    else arg += c;
                } else if (doubleQuote) {
                    if (c == '"') doubleQuote = false;
                    else arg += c;
                } else if (c == '\'') {
                    singleQuote = true;
                } else if (c == '"') {
                    doubleQuote = true;
                } else if (std::isspace(static_cast<unsigned char>(c))) {
                    break;
                } else {
                    arg += c;
                }
            }

            args.push_back(std::move(arg));
        }

        return args;
    }

    // "@file" arguments are replaced by their contents, like libiberty's
    // expandargv; one that can't be read stays as it is.
    void Expand(const std::string& arg, std::vector<std::string>& out, int depth) {
        if (arg.size() > 1 && arg[0] == '@' && depth < 16) {
            std::ifstream in(arg.substr(1), std::ios::binary);

            if (in) {
                const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
                for (const std::string& inner : SplitGnu(text)) Expand(inner, out, depth + 1);
                return;
            }
        }

        out.push_back(arg);
    }

    // One argument the way CommandLineToArgvW (and lld's Windows response
    // file reader) splits it back.
    template <typename String>
    String QuoteWindows(const String& arg) {
        String out(1, '"');
        std::size_t backslashes = 0;

        for (const auto c : arg) {
            if (c == '\\') {
                ++backslashes;
                continue;
            }

            if (c == '"') {
                out.append(backslashes * 2 + 1, '\\');
            } else {
                out.append(backslashes, '\\');
            }

            out += c;
            backslashes = 0;
        }

        out.append(backslashes * 2, '\\');
        out += '"';
        return out;
    }

    std::string ToUtf8(const wchar_t* text) {
        const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
        if (size <= 1) return {};

        std::string result(static_cast<std::size_t>(size - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
        return result;
    }
}

int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) Expand(ToUtf8(argv[i]), args, 0);

    wchar_t tempDirectory[MAX_PATH + 1];
    wchar_t responseFile[MAX_PATH + 1];

    if (GetTempPathW(MAX_PATH + 1, tempDirectory) == 0 || GetTempFileNameW(tempDirectory, L"lld", 0, responseFile) == 0) {
        std::fprintf(stderr, "ld.lld wrapper: could not create a response file\n");
        return 1;
    }

    {
        std::ofstream out(responseFile, std::ios::binary | std::ios::trunc);
        for (const std::string& arg : args) out << QuoteWindows(arg) << '\n';

        if (!out) {
            std::fprintf(stderr, "ld.lld wrapper: could not write the response file\n");
            DeleteFileW(responseFile);
            return 1;
        }
    }

    const std::wstring realLld = TILKY_REAL_LLD;
    std::wstring commandLine = QuoteWindows(realLld) + L" " + QuoteWindows(L"@" + std::wstring(responseFile));

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};

    if (!CreateProcessW(realLld.c_str(), commandLine.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &process)) {
        std::fprintf(stderr, "ld.lld wrapper: could not start %ls (error %lu)\n", realLld.c_str(), GetLastError());
        DeleteFileW(responseFile);
        return 1;
    }

    WaitForSingleObject(process.hProcess, INFINITE);

    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    DeleteFileW(responseFile);
    return static_cast<int>(exitCode);
}
