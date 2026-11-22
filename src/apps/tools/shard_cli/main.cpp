#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "config.h"
#include "engine/plugins/archive.hpp"
#include "engine/plugins/crypto.hpp"
#include "engine/plugins/installer.hpp"
#include "engine/plugins/manifest.hpp"
#include "engine/plugins/package.hpp"

namespace fs = std::filesystem;
using namespace Shard::Engine::Plugins;
using nlohmann::json;

namespace {

    void Usage()
    {
        std::cout <<
            "shard plugin install <file.shardplugin> [--sha256 <hex>]\n"
            "shard plugin install-bundle <folder|archive> [--only <id,id,...>]\n"
            "shard plugin list [--json]\n"
            "shard plugin info <id> [--json]\n"
            "shard plugin remove <id>\n"
            "shard plugin doctor [--json]\n"
            "shard plugin new <id> [--dir <folder>] [--name <text>]\n"
            "shard plugin keygen <publisher> [--out <folder>]\n"
            "shard plugin sign <folder> --publisher <name> --key <file>\n"
            "shard plugin pack <folder> [--out <file>]\n"
            "shard plugin enable <id> --project <project.json>\n"
            "shard plugin disable <id> --project <project.json>\n"
            "\n"
            "options : --project <project.json>   plugins folder and trust file come from the project\n"
            "          --plugins <folder>         plugins folder (default : ./plugins)\n"
            "          --trust <file>             a trust file ; repeatable (default : plugin-trust.json next to the project, and next to this executable)\n"
            "          --allow-unsigned           install archives that are not signed by a trusted publisher\n"
            "          --json                     machine-readable output\n";
    }

    struct Cli {
        std::vector<std::string> positional;
        std::string project;
        std::string plugins;
        std::vector<std::string> trustFiles;
        std::string sha256;
        std::string dir;
        std::string name;
        std::string out;
        std::string publisher;
        std::string key;
        std::vector<std::string> only;
        bool allowUnsigned = false;
        bool json = false;
    };

    std::vector<std::string> Split(const std::string& text, char separator)
    {
        std::vector<std::string> out;
        std::stringstream ss(text);
        std::string item;
        while (std::getline(ss, item, separator)) if (!item.empty()) out.push_back(item);
        return out;
    }

    bool Parse(const std::vector<std::string>& args, Cli& cli, std::string& error)
    {
        for (size_t i = 0; i < args.size(); ++i) {
            const std::string& a = args[i];
            auto value = [&](std::string& out) {
                if (i + 1 >= args.size()) { error = a + " needs a value"; return false; }
                out = args[++i];
                return true;
            };
            std::string v;
            if (a == "--project") { if (!value(cli.project)) return false; }
            else if (a == "--plugins") { if (!value(cli.plugins)) return false; }
            else if (a == "--trust") { if (!value(v)) return false; cli.trustFiles.push_back(v); }
            else if (a == "--sha256") { if (!value(cli.sha256)) return false; }
            else if (a == "--dir") { if (!value(cli.dir)) return false; }
            else if (a == "--name") { if (!value(cli.name)) return false; }
            else if (a == "--out") { if (!value(cli.out)) return false; }
            else if (a == "--publisher") { if (!value(cli.publisher)) return false; }
            else if (a == "--key") { if (!value(cli.key)) return false; }
            else if (a == "--only") { if (!value(v)) return false; for (const std::string& id : Split(v, ',')) cli.only.push_back(id); }
            else if (a == "--allow-unsigned") cli.allowUnsigned = true;
            else if (a == "--json") cli.json = true;
            else if (a.compare(0, 2, "--") == 0) { error = "unknown option " + a; return false; }
            else cli.positional.push_back(a);
        }
        return true;
    }

    struct Context {
        std::string pluginsDir;
        TrustStore trust;
        InstallOptions options;
    };

    std::string g_ExeDir;

    bool BuildContext(const Cli& cli, Context& ctx, std::string& error)
    {
        fs::path root = fs::current_path();
        std::string pluginsFolder = "plugins";
        if (!cli.project.empty()) {
            std::ifstream in(cli.project);
            if (!in) { error = "cannot read " + cli.project; return false; }
            std::stringstream ss;
            ss << in.rdbuf();
            json project = json::parse(ss.str(), nullptr, false);
            if (project.is_discarded() || !project.is_object()) { error = cli.project + " is not a project file"; return false; }
            root = fs::absolute(cli.project).parent_path();
            if (project.contains("pluginsFolder") && project["pluginsFolder"].is_string() && !project["pluginsFolder"].get<std::string>().empty())
                pluginsFolder = project["pluginsFolder"].get<std::string>();
        }
        ctx.pluginsDir = cli.plugins.empty() ? (root / pluginsFolder).lexically_normal().generic_string() : fs::absolute(cli.plugins).generic_string();

        std::vector<std::string> trustFiles = cli.trustFiles;
        std::error_code ec;
        if (trustFiles.empty()) {
            const fs::path projectTrust = root / "plugin-trust.json";
            if (fs::exists(projectTrust, ec)) trustFiles.push_back(projectTrust.string());
        }
        if (!g_ExeDir.empty()) {
            const fs::path exeTrust = fs::path(g_ExeDir) / "plugin-trust.json";
            if (fs::exists(exeTrust, ec)) trustFiles.push_back(exeTrust.string());
        }
        for (const std::string& file : trustFiles) {
            auto loaded = ctx.trust.LoadFile(file);
            if (!loaded) { error = file + " : " + loaded.GetError().message; return false; }
        }

        ctx.options.installDirectory = ctx.pluginsDir;
        ctx.options.engineVersion = SemVer{ uint32_t(VERSION_MAJOR), uint32_t(VERSION_MINOR), uint32_t(VERSION_PATCH), {} };
        ctx.options.trust = &ctx.trust;
        ctx.options.allowUnsigned = cli.allowUnsigned;
        ctx.options.expectedSha256 = cli.sha256;
        ctx.options.alreadyAvailable = { "shard.core", "shard.assets", "shard.world", "shard.physics", "shard.audio", "shard.renderer" };
        return true;
    }

    int Fail(const std::string& message)
    {
        std::cerr << "[ERROR] " << message << std::endl;
        return 1;
    }

    json InstalledJson(const InstalledPlugin& p)
    {
        return json{ { "id", p.id }, { "version", p.version }, { "folder", p.folder }, { "source", p.source }, { "publisher", p.publisher } };
    }

    void PrintInstalled(const std::vector<InstalledPlugin>& installed, const Cli& cli)
    {
        if (cli.json) {
            json out = json::array();
            for (const InstalledPlugin& p : installed) out.push_back(InstalledJson(p));
            std::cout << out.dump(2) << "\n";
            return;
        }
        for (const InstalledPlugin& p : installed)
            std::cout << "installed " << p.id << " " << p.version << (p.publisher.empty() ? "" : " (by " + p.publisher + ")") << "\n";
    }

    int Install(const Cli& cli)
    {
        if (cli.positional.size() != 1) return Fail("usage : shard plugin install <file.shardplugin>");
        Context ctx;
        std::string error;
        if (!BuildContext(cli, ctx, error)) return Fail(error);
        auto installed = PluginInstaller::InstallArchive(cli.positional[0], ctx.options);
        if (!installed) return Fail(installed.GetError().message);
        PrintInstalled({ installed.Value() }, cli);
        return 0;
    }

    int InstallBundle(const Cli& cli)
    {
        if (cli.positional.size() != 1) return Fail("usage : shard plugin install-bundle <folder|archive> [--only id,id]");
        if (!cli.sha256.empty()) return Fail("--sha256 applies to one archive : use it with install");
        Context ctx;
        std::string error;
        if (!BuildContext(cli, ctx, error)) return Fail(error);
        auto installed = PluginInstaller::InstallBundle(cli.positional[0], cli.only, ctx.options);
        if (!installed) return Fail(installed.GetError().message);
        PrintInstalled(installed.Value(), cli);
        return 0;
    }

    struct Found {
        PluginManifest manifest;
        InstallMarker marker;
        bool installed = false;
        SignatureInfo signature;
    };

    std::vector<Found> Scan(const Context& ctx, std::vector<std::string>& problems)
    {
        std::vector<Found> out;
        std::error_code ec;
        if (!fs::is_directory(ctx.pluginsDir, ec)) return out;
        std::vector<fs::path> folders;
        for (fs::directory_iterator it(ctx.pluginsDir, ec), end; it != end; it.increment(ec)) {
            if (ec) break;
            if (it->is_directory(ec) && it->path().filename().string().compare(0, 1, ".") != 0) folders.push_back(it->path());
        }
        std::sort(folders.begin(), folders.end());
        for (const fs::path& folder : folders) {
            std::ifstream in(folder / "shard-plugin.json");
            if (!in) continue;
            std::stringstream ss;
            ss << in.rdbuf();
            auto manifest = ParseManifestText(ss.str());
            if (!manifest) { problems.push_back(folder.filename().string() + " : " + manifest.GetError().message); continue; }
            Found f;
            f.manifest = manifest.Value();
            f.manifest.rootDir = folder.generic_string();
            f.installed = ReadInstallMarker(folder.string(), f.marker);
            f.signature = VerifyPackage(folder.string(), ctx.trust);
            out.push_back(std::move(f));
        }
        return out;
    }

    json FoundJson(const Found& f)
    {
        return json{ { "id", f.manifest.id }, { "name", f.manifest.name }, { "version", f.manifest.version.ToString() }, { "kind", PluginKindName(f.manifest.kind) },
                     { "tier", int(f.manifest.tier) }, { "installed", f.installed }, { "signature", SignatureStatusName(f.signature.status) },
                     { "publisher", f.signature.publisher }, { "folder", f.manifest.rootDir } };
    }

    int List(const Cli& cli)
    {
        Context ctx;
        std::string error;
        if (!BuildContext(cli, ctx, error)) return Fail(error);
        std::vector<std::string> problems;
        const std::vector<Found> found = Scan(ctx, problems);
        if (cli.json) {
            json out = json::array();
            for (const Found& f : found) out.push_back(FoundJson(f));
            std::cout << out.dump(2) << "\n";
            return 0;
        }
        if (found.empty()) std::cout << "no plugin in " << ctx.pluginsDir << "\n";
        for (const Found& f : found)
            std::cout << f.manifest.id << " " << f.manifest.version.ToString() << "  [" << SignatureStatusName(f.signature.status) << "]" << (f.installed ? "" : "  (not installed by shard)") << "\n";
        for (const std::string& p : problems) std::cerr << "[WARNING] " << p << "\n";
        return 0;
    }

    int Info(const Cli& cli)
    {
        if (cli.positional.size() != 1) return Fail("usage : shard plugin info <id>");
        Context ctx;
        std::string error;
        if (!BuildContext(cli, ctx, error)) return Fail(error);
        std::vector<std::string> problems;
        for (const Found& f : Scan(ctx, problems)) {
            if (f.manifest.id != cli.positional[0]) continue;
            json j = FoundJson(f);
            j["manifest"] = json::parse(ManifestToJson(f.manifest).dump());
            j["source"] = f.marker.source;
            j["archiveSha256"] = f.marker.archiveSha256;
            if (cli.json) std::cout << j.dump(2) << "\n";
            else {
                std::cout << f.manifest.id << " " << f.manifest.version.ToString() << "\n";
                if (!f.manifest.name.empty()) std::cout << "  name      " << f.manifest.name << "\n";
                if (!f.manifest.description.empty()) std::cout << "  about     " << f.manifest.description << "\n";
                std::cout << "  engine    " << f.manifest.engineVersions.Text() << " (api " << f.manifest.apiMajor << "." << f.manifest.apiMinor << ")\n";
                std::cout << "  tier      " << int(f.manifest.tier) << "\n";
                std::cout << "  signature " << SignatureStatusName(f.signature.status) << (f.signature.publisher.empty() ? "" : " by " + f.signature.publisher) << "\n";
                if (f.installed) std::cout << "  source    " << f.marker.source << "\n";
                for (const DependencySpec& d : f.manifest.dependencies) std::cout << "  needs     " << d.id << " " << d.range.Text() << (d.optional ? " (optional)" : "") << "\n";
                std::cout << "  folder    " << f.manifest.rootDir << "\n";
            }
            return 0;
        }
        return Fail("no plugin '" + cli.positional[0] + "' in " + ctx.pluginsDir);
    }

    int Remove(const Cli& cli)
    {
        if (cli.positional.size() != 1) return Fail("usage : shard plugin remove <id>");
        Context ctx;
        std::string error;
        if (!BuildContext(cli, ctx, error)) return Fail(error);
        auto removed = PluginInstaller::Uninstall(ctx.pluginsDir, cli.positional[0]);
        if (!removed) return Fail(removed.GetError().message);
        std::cout << "removed " << cli.positional[0] << "\n";
        return 0;
    }

    bool ReadText(const fs::path& path, std::string& out)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        std::stringstream ss;
        ss << in.rdbuf();
        out = ss.str();
        return true;
    }

    std::string ReplaceAll(std::string text, const std::string& from, const std::string& to)
    {
        if (from.empty()) return text;
        for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) text.replace(at, from.size(), to);
        return text;
    }

    fs::path FindTemplate()
    {
        std::vector<fs::path> candidates;
        if (const char* env = std::getenv("SHARD_SDK_DIR")) candidates.push_back(fs::path(env) / "templates" / "plugin");
        if (!g_ExeDir.empty()) {
            candidates.push_back(fs::path(g_ExeDir) / ".." / "templates" / "plugin");
            candidates.push_back(fs::path(g_ExeDir) / "sdk" / "templates" / "plugin");
            candidates.push_back(fs::path(g_ExeDir) / ".." / "sdk" / "templates" / "plugin");
        }
#ifdef SHARD_SOURCE_DIR
        candidates.push_back(fs::path(SHARD_SOURCE_DIR) / "sdk" / "templates" / "plugin");
#endif
        std::error_code ec;
        for (const fs::path& c : candidates)
            if (fs::exists(c / "shard-plugin.json", ec)) return c.lexically_normal();
        return {};
    }

    int New(const Cli& cli)
    {
        if (cli.positional.size() != 1) return Fail("usage : shard plugin new <id> [--dir <folder>] [--name <text>]");
        const std::string& id = cli.positional[0];
        if (!IsValidPluginId(id)) return Fail("'" + id + "' is not a plugin id (reverse DNS, lowercase : com.acme.weather)");
        const fs::path source = FindTemplate();
        if (source.empty()) return Fail("the plugin template was not found (set SHARD_SDK_DIR to the SDK folder)");

        const fs::path target = fs::path(cli.dir.empty() ? "." : cli.dir) / id;
        std::error_code ec;
        if (fs::exists(target, ec)) return Fail(target.string() + " already exists");

        std::string targetName = id;
        std::replace(targetName.begin(), targetName.end(), '.', '_');
        const std::string name = cli.name.empty() ? id : cli.name;

        size_t files = 0;
        for (fs::recursive_directory_iterator it(source, ec), end; it != end; it.increment(ec)) {
            if (ec) break;
            const fs::path relative = fs::relative(it->path(), source, ec);
            const fs::path destination = target / relative;
            if (it->is_directory(ec)) { fs::create_directories(destination, ec); continue; }
            fs::create_directories(destination.parent_path(), ec);
            std::string text;
            if (!ReadText(it->path(), text)) return Fail("cannot read " + it->path().string());
            text = ReplaceAll(text, "com.example.template", id);
            text = ReplaceAll(text, "template_plugin", targetName);
            text = ReplaceAll(text, "Template plugin", name);
            std::ofstream out(destination, std::ios::binary | std::ios::trunc);
            if (!out) return Fail("cannot write " + destination.string());
            out << text;
            ++files;
        }
        if (files == 0) return Fail("the template at " + source.string() + " is empty");
        std::cout << "created " << target.generic_string() << " (" << files << " files)\n"
                  << "build : cmake -S " << target.generic_string() << " -B " << target.generic_string() << "/build -DCMAKE_PREFIX_PATH=<the SDK folder>\n";
        return 0;
    }

    int KeyGen(const Cli& cli)
    {
        if (cli.positional.size() != 1) return Fail("usage : shard plugin keygen <publisher> [--out <folder>]");
        const std::string base = (cli.out.empty() ? std::string(".") : cli.out) + "/" + cli.positional[0];
        std::array<uint8_t, 32> seed;
        if (!Crypto::RandomSeed(seed)) return Fail("the system gave no random numbers");
        Crypto::PublicKey pk;
        Crypto::SecretKey sk;
        Crypto::KeyPairFromSeed(seed, pk, sk);
        auto wrote = WriteKeyPair(base, sk, pk);
        if (!wrote) return Fail(wrote.GetError().message);
        std::cout << "wrote " << base << ".key (secret : keep it private) and " << base << ".pub\n"
                  << "people who trust you add it to their plugin-trust.json : { \"publishers\": { \"" << cli.positional[0] << "\": \"" << Crypto::ToHex(pk) << "\" } }\n";
        return 0;
    }

    int Sign(const Cli& cli)
    {
        if (cli.positional.size() != 1) return Fail("usage : shard plugin sign <folder> --publisher <name> --key <file>");
        if (cli.publisher.empty() || cli.key.empty()) return Fail("sign needs --publisher <name> and --key <file>");
        auto key = ReadSecretKey(cli.key);
        if (!key) return Fail(key.GetError().message);
        auto signedOk = SignPackage(cli.positional[0], cli.publisher, key.Value());
        if (!signedOk) return Fail(signedOk.GetError().message);
        std::cout << "signed " << cli.positional[0] << " as '" << cli.publisher << "'\n";
        return 0;
    }

    int Pack(const Cli& cli)
    {
        if (cli.positional.size() != 1) return Fail("usage : shard plugin pack <folder> [--out <file>]");
        const std::string& folder = cli.positional[0];
        std::string text;
        if (!ReadText(fs::path(folder) / "shard-plugin.json", text)) return Fail("no shard-plugin.json in " + folder);
        auto manifest = ParseManifestText(text);
        if (!manifest) return Fail("shard-plugin.json :\n" + manifest.GetError().message);
        const std::string out = cli.out.empty() ? manifest.Value().id + "-" + manifest.Value().version.ToString() + kArchiveExtension : cli.out;
        ZipWriter zip;
        auto added = zip.AddDirectory(folder, "", { kExtractedMarkerFile, kInstallMarkerFile });
        if (!added) return Fail(added.GetError().message);
        auto saved = zip.Save(out);
        if (!saved) return Fail(saved.GetError().message);
        const SignatureInfo sig = VerifyPackage(folder, TrustStore());
        std::cout << "packed " << added.Value() << " files into " << out << " (" << SignatureStatusName(sig.status) << ")\n";
        if (sig.status == SignatureInfo::Status::Unsigned) std::cout << "note : unsigned. Sign it first (shard plugin sign) if it is meant for other people\n";
        return 0;
    }

    int SetEnabled(const Cli& cli, bool enabled)
    {
        if (cli.positional.size() != 1) return Fail(std::string("usage : shard plugin ") + (enabled ? "enable" : "disable") + " <id> --project <project.json>");
        if (cli.project.empty()) return Fail("enabling and disabling is per project : pass --project <project.json>");
        const std::string& id = cli.positional[0];
        if (!IsValidPluginId(id)) return Fail("'" + id + "' is not a plugin id");
        std::ifstream in(cli.project);
        if (!in) return Fail("cannot read " + cli.project);
        std::stringstream ss;
        ss << in.rdbuf();
        in.close();
        nlohmann::ordered_json project = nlohmann::ordered_json::parse(ss.str(), nullptr, false);
        if (project.is_discarded() || !project.is_object()) return Fail(cli.project + " is not a project file");

        if (!project.contains("plugins") || !project["plugins"].is_array()) project["plugins"] = nlohmann::ordered_json::array();
        auto& list = project["plugins"];
        auto entry = list.end();
        for (auto it = list.begin(); it != list.end(); ++it)
            if (it->is_object() && it->value("id", std::string()) == id) entry = it;

        if (enabled) {
            if (entry != list.end()) {
                entry->erase("enabled");
                if (entry->size() == 1) list.erase(entry);
            }
        } else if (entry != list.end()) {
            (*entry)["enabled"] = false;
        } else {
            list.push_back(nlohmann::ordered_json{ { "id", id }, { "enabled", false } });
        }
        if (list.empty()) project.erase("plugins");

        std::ofstream out(cli.project, std::ios::trunc);
        if (!out) return Fail("cannot write " + cli.project);
        out << project.dump(2) << "\n";
        std::cout << (enabled ? "enabled " : "disabled ") << id << " in " << cli.project << "\n";
        return 0;
    }

    int Doctor(const Cli& cli)
    {
        Context ctx;
        std::string error;
        if (!BuildContext(cli, ctx, error)) return Fail(error);
        std::vector<std::string> problems;
        const std::vector<Found> found = Scan(ctx, problems);

        std::set<std::string> available(ctx.options.alreadyAvailable.begin(), ctx.options.alreadyAvailable.end());
        std::map<std::string, SemVer> versions;
        for (const Found& f : found) {
            available.insert(f.manifest.id);
            versions[f.manifest.id] = f.manifest.version;
            for (const ProvidedService& s : f.manifest.provides) available.insert("service:" + s.service);
        }

        for (const Found& f : found) {
            const PluginManifest& m = f.manifest;
            const std::string who = m.id + " " + m.version.ToString();
            if (m.apiMajor != kPluginApiMajor) problems.push_back(who + " : built for plugin API " + std::to_string(m.apiMajor) + ", this engine has " + std::to_string(kPluginApiMajor));
            if (!m.engineVersions.Satisfies(ctx.options.engineVersion)) problems.push_back(who + " : needs engine " + m.engineVersions.Text() + ", this is " + ctx.options.engineVersion.ToString());
            if (!m.binaries.empty()) {
                const std::string binary = m.BinaryForThisPlatform();
                if (binary.empty()) problems.push_back(who + " : no binary for " + CurrentPlatformKey());
                else if (!fs::exists(fs::path(m.rootDir) / binary)) problems.push_back(who + " : the binary " + binary + " is missing");
            }
            for (const DependencySpec& d : m.dependencies) {
                if (d.optional) continue;
                if (!available.count(d.id)) { problems.push_back(who + " : needs " + d.id + " " + d.range.Text() + ", which is not installed"); continue; }
                auto it = versions.find(d.id);
                if (it != versions.end() && !d.range.Satisfies(it->second))
                    problems.push_back(who + " : needs " + d.id + " " + d.range.Text() + ", " + it->second.ToString() + " is installed");
            }
            for (const DependencySpec& c : m.conflicts)
                if (versions.count(c.id)) problems.push_back(who + " : conflicts with " + c.id);
            switch (f.signature.status) {
                case SignatureInfo::Status::Invalid: problems.push_back(who + " : its files do not match its signature (" + f.signature.message + ")"); break;
                case SignatureInfo::Status::Revoked: problems.push_back(who + " : signed with a revoked key"); break;
                case SignatureInfo::Status::Untrusted:
                case SignatureInfo::Status::Unsigned:
                    if (f.installed || m.tier == TrustTier::Native) problems.push_back(who + " : " + SignatureStatusName(f.signature.status) + ", it will not load without consent");
                    break;
                default: break;
            }
        }

        if (cli.json) {
            json out = { { "plugins", found.size() }, { "problems", problems } };
            std::cout << out.dump(2) << "\n";
        } else {
            std::cout << found.size() << " plugins in " << ctx.pluginsDir << "\n";
            for (const std::string& p : problems) std::cout << "problem : " << p << "\n";
            if (problems.empty()) std::cout << "no problem found\n";
        }
        return problems.empty() ? 0 : 1;
    }
}

int main(int argc, char** argv)
{
    if (argc > 0) g_ExeDir = fs::absolute(argv[0]).parent_path().string();
    if (argc < 2 || std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0) {
        Usage();
        return argc < 2 ? 1 : 0;
    }
    if (std::strcmp(argv[1], "plugin") != 0 || argc < 3) {
        std::cerr << "[ERROR] Unknown command\n";
        Usage();
        return 1;
    }
    const std::string command = argv[2];
    Cli cli;
    std::string error;
    if (!Parse(std::vector<std::string>(argv + 3, argv + argc), cli, error)) return Fail(error);
    if (command == "install") return Install(cli);
    if (command == "install-bundle") return InstallBundle(cli);
    if (command == "list") return List(cli);
    if (command == "info") return Info(cli);
    if (command == "remove") return Remove(cli);
    if (command == "doctor") return Doctor(cli);
    if (command == "new") return New(cli);
    if (command == "keygen") return KeyGen(cli);
    if (command == "sign") return Sign(cli);
    if (command == "pack") return Pack(cli);
    if (command == "enable") return SetEnabled(cli, true);
    if (command == "disable") return SetEnabled(cli, false);
    std::cerr << "[ERROR] Unknown command : plugin " << command << "\n";
    Usage();
    return 1;
}
