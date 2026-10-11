//
// Created by berke on 10/10/2026.
//
#include "Headers/Launcher/ArchiveInstaller.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>

#include <cpr/cpr.h>
#include <minizip-ng/mz.h>
#include <minizip-ng/mz_os.h>
#include <minizip-ng/mz_strm.h>
#include <minizip-ng/mz_zip.h>
#include <minizip-ng/mz_zip_rw.h>
#include <spdlog/spdlog.h>

#include "Headers/Launcher/Sha256.hpp"

namespace ArchiveInstaller {
    namespace {
        constexpr long CONNECT_TIMEOUT_MS = 8000;

        std::string LowercaseHex(std::string s) {
            std::ranges::transform(s, s.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }
    }

    bool Download(const std::string& url, const fs::path& destination, const long timeoutMs,
                  const DownloadProgress& progress, std::string& error) {
        std::ofstream outFile(destination, std::ios::binary | std::ios::trunc);
        if (!outFile.is_open()) {
            error = "could not open .part file for writing: " + destination.string();
            return false;
        }

        cpr::Session session;
        session.SetUrl(cpr::Url{url});
        session.SetTimeout(cpr::Timeout{timeoutMs});
        session.SetConnectTimeout(cpr::ConnectTimeout{CONNECT_TIMEOUT_MS});
        session.SetRedirect(cpr::Redirect{true}); // GitHub Release assets redirect to blob storage

        session.SetProgressCallback(cpr::ProgressCallback{
            [&progress](const cpr::cpr_pf_arg_t downloadTotal, const cpr::cpr_pf_arg_t downloadNow,
                        cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, intptr_t) -> bool {
                const float fraction = downloadTotal > 0
                    ? static_cast<float>(static_cast<double>(downloadNow) / static_cast<double>(downloadTotal))
                    : 0.0f;
                return !progress || progress(fraction); // false aborts the transfer (libcurl convention)
            }
        });

        const cpr::Response response = session.Download(outFile);
        outFile.close();

        if (response.error.code != cpr::ErrorCode::OK) {
            error = "download error: " + response.error.message;
            return false;
        }

        if (response.status_code != 200) {
            error = "download failed with HTTP status " + std::to_string(response.status_code);
            return false;
        }

        return true;
    }

    bool VerifySha256(const fs::path& file, const std::string& expectedHex, std::string& error) {
        const std::optional<std::string> actualHash = TilkySha256::HashFile(file);
        if (!actualHash) {
            error = "could not read the downloaded file to verify its hash";
            return false;
        }

        if (expectedHex.empty()) {
            spdlog::warn("No sha256 given for {} - skipping hash verification", file.string());
            return true;
        }

        if (LowercaseHex(expectedHex) != *actualHash) {
            error = "hash mismatch: expected " + expectedHex + ", got " + *actualHash;
            return false;
        }

        return true;
    }

    bool ExtractZip(const fs::path& zipFile, const fs::path& destinationDirectory,
                    const ExtractProgress& progress, std::string& error) {
        struct ExtractProgressContext {
            const ExtractProgress* progress;
            int32_t totalEntries;
            int32_t extractedEntries;
        };

        // Pre-count entries so the per-entry callback below can report "N of M" as
        // a fraction - mz_zip_reader_save_all() only calls back once an entry has
        // finished extracting, it doesn't report byte-level progress.
        int32_t totalEntries = 0;
        {
            void* countReader = mz_zip_reader_create();
            if (countReader != nullptr && mz_zip_reader_open_file(countReader, zipFile.string().c_str()) == MZ_OK) {
                int32_t navResult = mz_zip_reader_goto_first_entry(countReader);
                while (navResult == MZ_OK) {
                    ++totalEntries;
                    navResult = mz_zip_reader_goto_next_entry(countReader);
                }
                mz_zip_reader_close(countReader);
            }
            if (countReader != nullptr) mz_zip_reader_delete(&countReader);
        }

        void* reader = mz_zip_reader_create();
        if (reader == nullptr) {
            error = "could not create ZIP reader";
            return false;
        }

        ExtractProgressContext progressCtx{&progress, totalEntries, 0};

        mz_zip_reader_set_entry_cb(reader, &progressCtx,
            [](void*, void* userdata, mz_zip_file*, const char*) -> int32_t {
                auto* ctx = static_cast<ExtractProgressContext*>(userdata);
                ctx->extractedEntries++;
                const float fraction = ctx->totalEntries > 0
                    ? static_cast<float>(ctx->extractedEntries) / static_cast<float>(ctx->totalEntries)
                    : 0.0f;
                if (*ctx->progress) (*ctx->progress)(fraction);
                return MZ_OK;
            }
        );

        const int32_t openResult = mz_zip_reader_open_file(reader, zipFile.string().c_str());
        int32_t extractResult = MZ_STREAM_ERROR;

        if (openResult == MZ_OK) {
            extractResult = mz_zip_reader_save_all(reader, destinationDirectory.string().c_str());
            mz_zip_reader_close(reader);
        }

        mz_zip_reader_delete(&reader);

        if (openResult != MZ_OK) {
            error = "could not open the downloaded ZIP (it may be corrupt) - minizip-ng error " + std::to_string(openResult);
            return false;
        }

        if (extractResult != MZ_OK) {
            error = "ZIP extraction failed - minizip-ng error " + std::to_string(extractResult);
            return false;
        }

        return true;
    }
}
