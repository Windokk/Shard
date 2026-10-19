// asset_validator : checks a project without starting the engine.
//
//   ShardAssetValidator --project <project.json> [--game <library>] [--plugin <path>]... [--plugin-path <dir>]... [options]
//
//   * the project file reads, and names a resources folder and an asset database that exist
//   * the plugins : which load, which do not and why, the plugins.lock, the files two modules both provide
//   * every entry of the asset database has its file
//   * every world of the build list exists, is valid JSON and has the shape of a world (and the world files of the project)
//   * the checks plugins add (the "tool.validator" extension point, ToolPlugin::OnToolTask("validate"))
//
//   --strict    warnings (a plugin left out, a lock that differs) are problems too
//   --quiet     only the problems
//
// Exit code : 0 = nothing wrong, 1 = problems (or could not start).

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/core/diagnostics/logger.hpp"
#include "engine/game_module.hpp"
#include "engine/plugins/content.hpp"
#include "engine/plugins/manager.hpp"
#include "engine/world/engine.hpp"

namespace fs = std::filesystem;
using namespace Shard::Engine;
using nlohmann::json;

Debugging::Level minDebugLevel = Debugging::Level::Warning;

namespace {

    bool ReadText(const fs::path& path, std::string& out)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        std::stringstream ss;
        ss << in.rdbuf();
        out = ss.str();
        return true;
    }

    struct Report {
        std::vector<std::string> problems;
        std::vector<std::string> warnings;
        bool quiet = false;
        void Problem(std::string s) { problems.push_back(std::move(s)); }
        void Warning(std::string s) { warnings.push_back(std::move(s)); }
    };
}

int main(int argc, char** argv)
{
    PluginStartupOptions plugins;
    plugins.host = Plugins::Host::Tool;
    plugins.readOnly = true;
    std::string projectFile;
    bool strict = false, quiet = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--project" && i + 1 < argc) projectFile = argv[++i];
        else if (arg == "--game" && i + 1 < argc) plugins.gameModule = argv[++i];
        else if (arg == "--strict") strict = true;
        else if (arg == "--quiet") quiet = true;
        else if (arg == "--help" || arg == "-h") {
            std::cout << "ShardAssetValidator --project <project.json> [--game <library>] [--plugin <path>] [--plugin-path <dir>] [--registry <source>] [--strict] [--quiet]\n";
            return 0;
        }
        else if (const int taken = ParsePluginArgument(argc, argv, i, plugins); taken > 0) i += taken - 1;
        else { std::cerr << "[ERROR] Unknown argument : " << arg << std::endl; return 1; }
    }
    if (projectFile.empty()) { std::cerr << "[ERROR] --project is required" << std::endl; return 1; }

    Report report;
    report.quiet = quiet;

    // ---- The project file ----
    std::string text;
    if (!ReadText(projectFile, text)) { std::cerr << "[ERROR] cannot read " << projectFile << std::endl; return 1; }
    json project = json::parse(text, nullptr, false);
    if (project.is_discarded() || !project.is_object()) { std::cerr << "[ERROR] " << projectFile << " is not a JSON object" << std::endl; return 1; }
    const fs::path root = fs::absolute(projectFile).parent_path();

    fs::path resources, database;
    if (!project.contains("projectResources") || !project["projectResources"].is_string()) report.Problem("the project names no \"projectResources\"");
    else resources = root / project["projectResources"].get<std::string>();
    if (!project.contains("assetDatabase") || !project["assetDatabase"].is_string()) report.Problem("the project names no \"assetDatabase\"");
    else database = root / project["assetDatabase"].get<std::string>();
    std::error_code ec;
    if (!resources.empty() && !fs::is_directory(resources, ec)) report.Problem("the resources folder " + resources.generic_string() + " does not exist");
    if (!database.empty() && !fs::is_regular_file(database, ec)) report.Problem("the asset database " + database.generic_string() + " does not exist");

    // ---- Plugins ----
    plugins.project = projectFile;
    Core::SetEngine(nullptr);
    Debugging::SetLogger(&Debugging::Logger::GetInstance());
    Debugging::Logger::GetInstance().SetMinimumLevel(Debugging::Level::Warning);
    Plugins::PluginManager& manager = Plugins::PluginManager::Get();
    const bool started = StartPlugins(plugins);
    if (!started) report.Problem("the plugins could not start (see above)");
    for (const auto& record : manager.Plugins())
        if (record->state == Plugins::PluginState::Failed) report.Problem("plugin " + record->manifest.id + " failed : " + record->error);
    for (const Plugins::Rejection& rej : manager.Rejections()) {
        if (rej.kind == Plugins::Rejection::Kind::WrongHost || rej.kind == Plugins::Rejection::Kind::Duplicate || rej.kind == Plugins::Rejection::Kind::Disabled) continue;
        report.Warning("plugin " + rej.id + " " + rej.version + " is not loaded : " + rej.reason);
    }
    for (const std::string& line : manager.LockfileProblems()) report.Warning("plugins.lock : " + line);
    for (const Plugins::ContentConflict& c : manager.Content().FindConflicts())
        report.Warning(c.logicalPath + " is provided by " + c.keptFrom + " and " + c.rejectedFrom + " (no content.overrides)");
    for (const auto& record : manager.Plugins())
        for (const std::string& w : record->warnings) report.Warning(record->manifest.id + " : " + w);

    // ---- Asset database ----
    if (!database.empty() && !resources.empty() && fs::is_regular_file(database, ec)) {
        std::string dbText;
        ReadText(database, dbText);
        json db = json::parse(dbText, nullptr, false);
        if (db.is_discarded() || !db.is_object()) report.Problem("the asset database is not a JSON object");
        else {
            std::vector<std::string> names;
            for (auto it = db.begin(); it != db.end(); ++it) names.push_back(it.key());
            std::sort(names.begin(), names.end());
            int ids = 0;
            std::set<int> seen;
            for (const std::string& name : names) {
                if (!fs::exists(resources / name, ec)) report.Problem("the asset database lists " + name + ", which is not in the resources folder");
                if (db[name].is_object() && db[name].contains("id") && db[name]["id"].is_number_integer()) {
                    ++ids;
                    if (!seen.insert(db[name]["id"].get<int>()).second) report.Problem("the asset id " + std::to_string(db[name]["id"].get<int>()) + " is used twice (" + name + ")");
                }
            }
            if (!quiet) std::cout << "[VALIDATOR] asset database : " << names.size() << " entries, " << ids << " ids" << std::endl;
        }
    }

    // ---- Worlds ----
    std::set<std::string> worldFiles;
    if (project.contains("buildSettings") && project["buildSettings"].is_array()) {
        for (const json& w : project["buildSettings"]) {
            if (!w.is_string()) { report.Problem("buildSettings holds something that is not a world path"); continue; }
            if (!resources.empty() && !fs::exists(resources / w.get<std::string>(), ec)) report.Problem("the build list names " + w.get<std::string>() + ", which does not exist");
            worldFiles.insert(w.get<std::string>());
        }
    }
    if (!resources.empty() && fs::is_directory(resources, ec)) {
        for (fs::recursive_directory_iterator it(resources, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
            if (ec) break;
            if (it->is_regular_file(ec) && it->path().extension() == ".world")
                worldFiles.insert(fs::relative(it->path(), resources, ec).generic_string());
        }
    }
    int worldCount = 0;
    for (const std::string& rel : worldFiles) {
        std::string worldText;
        if (!ReadText(resources / rel, worldText)) continue;
        ++worldCount;
        json world = json::parse(worldText, nullptr, false);
        if (world.is_discarded()) { report.Problem(rel + " is not valid JSON"); continue; }
        for (const std::string& p : Plugins::ValidateWorldJson(world, rel)) report.Problem(p);
    }
    if (!quiet) std::cout << "[VALIDATOR] worlds : " << worldCount << std::endl;

    // ---- What plugins check ----
    for (const std::string& p : manager.RunToolValidators(projectFile)) report.Problem(p);

    manager.Shutdown();

    for (const std::string& w : report.warnings) {
        if (strict) std::cerr << "[PROBLEM] " << w << std::endl;
        else if (!quiet) std::cout << "[WARNING] " << w << std::endl;
    }
    for (const std::string& p : report.problems) std::cerr << "[PROBLEM] " << p << std::endl;
    const size_t total = report.problems.size() + (strict ? report.warnings.size() : 0);
    if (!quiet || total) std::cout << "[VALIDATOR] " << (total ? std::to_string(total) + " problem(s)" : std::string("no problems")) << std::endl;
    return total ? 1 : 0;
}
