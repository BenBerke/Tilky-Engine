//
// Created by berke on 10/20/2026.
//
#include "Headers/Launcher/ProjectTemplates.hpp"

#include <algorithm>
#include <fstream>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "Headers/Launcher/ArchiveInstaller.hpp"
#include "Headers/Project/ProjectManager.hpp"

using json = nlohmann::json;

namespace ProjectTemplates {
    namespace {
        // Templates are extracted here first, never straight into the projects
        // folder, so a failed extraction can't show up as a broken project.
        fs::path WorkFolder() {
            return ProjectManager::GetEngineFolder() / "TemplateWork";
        }
    }

    fs::path GetTemplatesFolder(const std::string& engineVersion) {
        const fs::path engineDirectory = engineVersion.empty()
            ? ProjectManager::GetEngineBasePath()
            : ProjectManager::GetEngineVersionDirectory(engineVersion);
        return engineDirectory / "LauncherAssets" / "Templates";
    }

    std::vector<TemplateInfo> DiscoverTemplates(const std::string& engineVersion) {
        std::vector<TemplateInfo> result;

        // Engine versions released before templates existed have no Templates
        // folder at all; offer the ones bundled with this launcher instead.
        std::error_code ec;
        fs::path folder = GetTemplatesFolder(engineVersion);
        if (!fs::is_directory(folder, ec)) folder = GetTemplatesFolder(std::string());
        if (!fs::is_directory(folder, ec)) return result;

        for (const fs::directory_entry& entry : fs::directory_iterator(folder, ec)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".zip") continue;

            TemplateInfo info;
            info.id = entry.path().stem().string();
            info.name = info.id;
            info.zipFile = entry.path();

            const fs::path metaFile = folder / (info.id + ".json");
            if (fs::exists(metaFile, ec)) {
                try {
                    const json meta = json::parse(std::ifstream(metaFile));
                    info.name = meta.value("name", info.id);
                    info.description = meta.value("description", std::string());
                } catch (const std::exception& e) {
                    spdlog::warn("Couldn't read template metadata {}: {}", metaFile.string(), e.what());
                }
            }

            const fs::path thumbnail = folder / (info.id + ".png");
            if (fs::exists(thumbnail, ec)) info.thumbnailFile = thumbnail;

            result.push_back(std::move(info));
        }

        std::ranges::sort(result, [](const TemplateInfo& a, const TemplateInfo& b) { return a.name < b.name; });
        return result;
    }

    bool CreateProjectFromTemplate(const TemplateInfo& info, const std::string& projectName,
                                   const std::string& engineVersion, fs::path& outProjectFolder,
                                   std::string& error) {
        const fs::path destination = ProjectManager::GetDefaultProjectsFolder() / projectName;
        const fs::path workDir = WorkFolder() / (projectName + "-creating");

        std::error_code ec;
        if (fs::exists(destination, ec)) {
            error = "a folder called " + projectName + " already exists";
            return false;
        }

        const auto cleanUp = [&workDir] {
            std::error_code cleanupEc;
            fs::remove_all(workDir, cleanupEc);
        };

        try {
            cleanUp(); // a leftover from an earlier failed attempt
            fs::create_directories(workDir);
        } catch (const std::exception& e) {
            error = std::string("could not create the work folder: ") + e.what();
            return false;
        }

        if (!ArchiveInstaller::ExtractZip(info.zipFile, workDir, nullptr, error)) {
            cleanUp();
            return false;
        }

        const fs::path tilkyFile = workDir / "project.tilky";
        try {
            json data = json::parse(std::ifstream(tilkyFile));
            data["name"] = projectName;
            data["engineVersion"] = engineVersion;
            data.erase("lastOpenLevel");

            std::ofstream out(tilkyFile, std::ios::trunc);
            out << data.dump(4);
            if (!out) throw std::runtime_error("write failed");
        } catch (const std::exception& e) {
            error = "the template has no usable project.tilky (" + std::string(e.what()) + ")";
            cleanUp();
            return false;
        }

        try {
            fs::create_directories(destination.parent_path());
        } catch (const std::exception& e) {
            error = std::string("could not prepare the projects folder: ") + e.what();
            cleanUp();
            return false;
        }

        fs::rename(workDir, destination, ec);
        if (ec) {
            // rename can't cross drives; copy instead in that case.
            std::error_code copyEc;
            fs::copy(workDir, destination, fs::copy_options::recursive, copyEc);
            if (copyEc) {
                std::error_code removeEc;
                fs::remove_all(destination, removeEc);
                cleanUp();
                error = "could not move the project into " + destination.parent_path().string() + ": " + copyEc.message();
                return false;
            }
            cleanUp();
        }

        spdlog::info("Created project {} from template {}", destination.string(), info.id);
        outProjectFolder = destination;
        return true;
    }
}
