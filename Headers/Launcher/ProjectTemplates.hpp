#ifndef TILKY_ENGINE_PROJECTTEMPLATES_HPP
#define TILKY_ENGINE_PROJECTTEMPLATES_HPP

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Project templates bundled with every engine version, offered by the
// launcher's Create Project window next to the built-in Blank project.
//
// Each engine version carries the templates made for it in
// <engine>/LauncherAssets/Templates, three files per template sharing one id:
//
//   fps-starter.zip   project.tilky + Assets/ at the ZIP root
//   fps-starter.json  { "name": "FPS Starter", "description": "..." }
//   fps-starter.png   optional thumbnail
//
// The ZIPs are built from the editable folders under Templates/ in the
// repository (see the "Project templates" section of CMakeLists.txt).
// Templates are read from the engine version the new project is pinned to,
// because save formats change between versions.
namespace ProjectTemplates {
    struct TemplateInfo {
        std::string id;          // file stem, e.g. "fps-starter"
        std::string name;        // shown in the launcher; the id if the .json doesn't say
        std::string description;
        fs::path zipFile;
        fs::path thumbnailFile;  // empty if the template has no .png
    };

    // <engine>/LauncherAssets/Templates for an installed engine version. An
    // empty version means the engine copy next to the running launcher, the
    // same fallback OpenProject() uses for unpinned projects.
    fs::path GetTemplatesFolder(const std::string& engineVersion);

    // Every template in GetTemplatesFolder(engineVersion), sorted by name.
    // A version with no Templates folder (released before templates existed)
    // gets the templates bundled next to the running launcher.
    std::vector<TemplateInfo> DiscoverTemplates(const std::string& engineVersion);

    // Creates <default projects folder>/<projectName> from a template: extracts
    // the ZIP into a work folder, checks it holds a project.tilky, sets "name"
    // and "engineVersion", drops "lastOpenLevel" and keeps every other field,
    // then moves the project into place. An existing folder is never touched.
    // Blocking; templates are local files, so this is quick enough for the UI
    // thread. On failure returns false and sets `error`.
    bool CreateProjectFromTemplate(const TemplateInfo& info, const std::string& projectName,
                                   const std::string& engineVersion, fs::path& outProjectFolder,
                                   std::string& error);
}

#endif //TILKY_ENGINE_PROJECTTEMPLATES_HPP
