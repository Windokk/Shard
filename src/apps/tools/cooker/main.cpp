// ShardCooker : packs content into .shardpak files (compressed unless --store is given).
//   ShardCooker pack    <folder> <out.shardpak> [--prefix <p>]  every file of a folder in one pak
//   ShardCooker plugin  <plugin folder> [--out <folder>]        a plugin ready to ship : its content folder becomes <mount>.shardpak,
//                                                               the manifest and the other files are copied as they are
//   ShardCooker project <project.json> [--out <folder>]         a project ready to ship : its resources folder becomes resources.shardpak,
//                                                               the project file, asset database, plugins... are copied, and the content
//                                                               folder of each plugin of the project is cooked too
//                                                               Textures used by materials become block compressed mips (.stx) and models
//                                                               their finished vertices (.smesh), unless --no-assets is given.
//   ShardCooker list    <pak>                                   files, sizes, crc
//   ShardCooker verify  <pak>                                   checks every crc
//   ShardCooker unpack  <pak> <folder>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <thread>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/assets/pak/pak.hpp"
#include "engine/assets/serialization/material/material_asset_refs.hpp"
#include "engine/assets/vfs/filesystem.hpp"
#include "engine/renderer/rhi/resources/mesh/mesh_cooked.hpp"
#include "engine/renderer/rhi/resources/texture/texture.hpp"

namespace fs = std::filesystem;
namespace Pak = Shard::Engine::Pak;
using Pak::PakReader;
using Pak::PakWriter;

namespace {
    bool gStore = false;
    bool gNoAssets = false;

    int Usage()
    {
        std::cerr << "usage :\n"
                     "  ShardCooker pack    <folder> <out.shardpak> [--prefix <p>] [--store]\n"
                     "  ShardCooker plugin  <plugin folder> [--out <folder>] [--store]\n"
                     "  ShardCooker project <project.json> [--out <folder>] [--store] [--no-assets]\n"
                     "  ShardCooker list    <pak>\n"
                     "  ShardCooker verify  <pak>\n"
                     "  ShardCooker unpack  <pak> <folder>\n";
        return 2;
    }

    std::string Option(std::vector<std::string>& args, const std::string& name)
    {
        for (size_t i = 0; i + 1 < args.size(); ++i) {
            if (args[i] != name) continue;
            std::string value = args[i + 1];
            args.erase(args.begin() + std::ptrdiff_t(i), args.begin() + std::ptrdiff_t(i) + 2);
            return value;
        }
        return {};
    }

    bool Flag(std::vector<std::string>& args, const std::string& name)
    {
        auto it = std::find(args.begin(), args.end(), name);
        if (it == args.end()) return false;
        args.erase(it);
        return true;
    }

    std::string Percent(uint64_t stored, uint64_t original)
    {
        if (original == 0) return "";
        char text[32];
        std::snprintf(text, sizeof(text), " (%.0f%%)", 100.0 * double(stored) / double(original));
        return text;
    }

    // Packs a folder, prints "<pak> : N files, A -> B bytes". Returns the number of files, -1 on failure.
    // `overlay` can replace or add entries once the folder is in (the asset cooking)
    long PackFolder(const fs::path& folder, const fs::path& pak, const std::string& prefix = {}, const std::function<void(PakWriter&)>& overlay = {})
    {
        PakWriter writer;
        writer.SetCompression(!gStore);
        std::vector<std::string> skipped;
        const size_t added = writer.AddFolder(folder.string(), prefix, &skipped);
        if (overlay) overlay(writer);
        for (const std::string& s : skipped) std::cerr << "[WARN] skipped (unsafe name) : " << s << "\n";
        std::string error;
        if (!writer.Write(pak.string(), &error)) { std::cerr << "[ERROR] " << error << "\n"; return -1; }
        std::error_code ec;
        const uint64_t onDisk = fs::file_size(pak, ec);
        std::cout << pak.string() << " : " << added << " files, " << writer.OriginalBytes() << " -> " << onDisk << " bytes" << Percent(onDisk, writer.OriginalBytes()) << "\n";
        return long(added);
    }

    // Copies a tree to `out`, except the sub folders in `skipFolders` (relative, '/')
    bool CopyTree(const fs::path& root, const fs::path& out, const std::vector<std::string>& skipFolders, size_t& copied)
    {
        std::error_code ec;
        for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec)) continue;
            const fs::path rel = fs::relative(it->path(), root, ec);
            const std::string name = rel.generic_string();
            // Hidden folders are the editor's own (.cache holds GBs of thumbnails and texture caches, .git...) : never shipped
            bool skip = false;
            for (const fs::path& part : rel)
                if (!part.empty() && part.string()[0] == '.' && part.string() != "." && part.string() != "..") skip = true;
            if (skip) continue;
            for (const std::string& folder : skipFolders)
                if (name.compare(0, folder.size() + 1, folder + "/") == 0) skip = true;
            if (skip) continue;
            fs::create_directories((out / rel).parent_path(), ec);
            fs::copy_file(it->path(), out / rel, fs::copy_options::overwrite_existing, ec);
            if (ec) { std::cerr << "[ERROR] cannot copy " << name << " : " << ec.message() << "\n"; return false; }
            ++copied;
        }
        return true;
    }

    std::string ContentMountOf(const fs::path& manifestFile)
    {
        std::ifstream in(manifestFile);
        nlohmann::json manifest = in ? nlohmann::json::parse(in, nullptr, false) : nlohmann::json();
        std::string mount = "content";
        if (manifest.is_object() && manifest.contains("content") && manifest["content"].is_object()) mount = manifest["content"].value("mount", mount);
        return mount;
    }

    bool SamePath(const fs::path& a, const fs::path& b)
    {
        std::error_code ec;
        return fs::weakly_canonical(a, ec) == fs::weakly_canonical(b, ec);
    }

    // The manifest, binaries... copied ; <mount>/ packed. Used for a plugin and for each plugin folder of a project.
    int CookPluginFolder(const fs::path& root, const fs::path& out)
    {
        const fs::path manifestFile = root / "shard-plugin.json";
        if (!fs::is_regular_file(manifestFile)) { std::cerr << "[ERROR] no shard-plugin.json in " << root.string() << "\n"; return 1; }
        const std::string mount = ContentMountOf(manifestFile);
        std::error_code ec;
        fs::create_directories(out, ec);
        size_t copied = 0;
        if (!CopyTree(root, out, { mount }, copied)) return 1;
        if (fs::is_directory(root / mount) && PackFolder(root / mount, out / (mount + Pak::kExtension)) < 0) return 1;
        return 0;
    }

    int CookPlugin(const std::string& dir, std::string out)
    {
        const fs::path root(dir);
        if (out.empty()) out = (root.parent_path() / (root.filename().string() + "-cooked")).string();
        if (SamePath(out, root)) { std::cerr << "[ERROR] --out must not be the plugin folder\n"; return 1; }
        return CookPluginFolder(root, out);
    }

    std::string Lower(std::string s)
    {
        for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    struct AssetStats {
        size_t textures = 0, meshes = 0, kept = 0, failed = 0;
        uint64_t sourceBytes = 0, cookedBytes = 0;
    };

    bool WriteStaged(const fs::path& file, const std::vector<uint8_t>& bytes)
    {
        std::error_code ec;
        fs::create_directories(file.parent_path(), ec);
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        out.close();
        return bool(out);
    }

    // Textures and models become what the engine loads directly (block compressed mips, finished vertices). The cooked data is stored next to
    // the file (<file>.lin.stx / .srgb.stx / .smesh) and the original keeps its name as an empty placeholder, so every name in the project stays valid.
    // Only the textures materials use are cooked, in the variant(s) their slots need : anything else is left as it is.
    void CookAssets(const fs::path& resources, const fs::path& staging, PakWriter& writer, AssetStats& stats)
    {
        using Shard::Engine::Filesystem::Path;
        namespace Rendering = Shard::Engine::Rendering;
        namespace Serialization = Shard::Engine::Serialization;
        std::error_code ec;

        struct Use { bool srgb = false, linear = false; };
        std::map<std::string, Use> uses;
        std::vector<fs::path> models, images;
        for (fs::recursive_directory_iterator it(resources, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec)) continue;
            const std::string ext = Lower(it->path().extension().string());
            if (ext == ".mat" || ext == ".material") {
                const auto refs = Serialization::PeekMaterialAssetRefs(Path(it->path().generic_string()));
                for (const auto& ref : refs.textures) {
                    Use& use = uses[Lower(fs::path(ref.pathInProject).generic_string())];
                    (Serialization::IsColorTextureSlot(ref.slot) ? use.srgb : use.linear) = true;
                }
            } else if (ext == ".fbx") {
                models.push_back(it->path());
            } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp") {
                images.push_back(it->path());
            }
        }

        const unsigned threads = std::max(1u, std::thread::hardware_concurrency());
        for (const fs::path& file : images) {
            const std::string rel = fs::relative(file, resources, ec).generic_string();
            const auto found = uses.find(Lower(rel));
            if (found == uses.end()) { ++stats.kept; continue; }

            bool allCooked = true;
            uint64_t cooked = 0;
            for (const bool srgb : { false, true }) {
                if (!(srgb ? found->second.srgb : found->second.linear)) continue;
                std::vector<uint8_t> bytes;
                const std::string name = rel + (srgb ? ".srgb.stx" : ".lin.stx");
                if (!Rendering::CookTexture(Path(file.generic_string()), srgb, threads, bytes) || !WriteStaged(staging / name, bytes) ||
                    !writer.AddFile(name, (staging / name).string())) {
                    allCooked = false;
                    continue;
                }
                cooked += bytes.size();
            }
            if (!allCooked) { ++stats.failed; std::cerr << "[WARN] texture kept as it is : " << rel << "\n"; continue; }
            stats.sourceBytes += fs::file_size(file, ec);
            stats.cookedBytes += cooked;
            writer.Add(rel, "");
            ++stats.textures;
        }

        for (const fs::path& file : models) {
            const std::string rel = fs::relative(file, resources, ec).generic_string();
            const Rendering::MeshCPUData data = Rendering::DecodeMeshFile(Path(file.generic_string()));
            const std::vector<uint8_t> bytes = data.success ? Rendering::SerializeMeshData(data) : std::vector<uint8_t>();
            const std::string name = rel + ".smesh";
            if (bytes.empty() || !WriteStaged(staging / name, bytes) || !writer.AddFile(name, (staging / name).string())) {
                ++stats.failed;
                std::cerr << "[WARN] model kept as it is : " << rel << "\n";
                continue;
            }
            stats.sourceBytes += fs::file_size(file, ec);
            stats.cookedBytes += bytes.size();
            writer.Add(rel, "");
            ++stats.meshes;
        }
    }

    int CookProject(const std::string& projectFile, std::string out)
    {
        std::ifstream in(projectFile);
        if (!in) { std::cerr << "[ERROR] cannot read " << projectFile << "\n"; return 1; }
        nlohmann::json project = nlohmann::json::parse(in, nullptr, false);
        if (project.is_discarded() || !project.is_object() || !project.contains("projectResources") || !project["projectResources"].is_string()) {
            std::cerr << "[ERROR] " << projectFile << " is not a project (no projectResources)\n";
            return 1;
        }
        const fs::path root = fs::path(projectFile).parent_path().empty() ? fs::path(".") : fs::path(projectFile).parent_path();
        if (out.empty()) out = (root.filename().string() + "-cooked");
        if (SamePath(out, root)) { std::cerr << "[ERROR] --out must not be the project folder\n"; return 1; }

        const std::string resources = fs::path(project["projectResources"].get<std::string>()).generic_string();
        if (!fs::is_directory(root / resources)) { std::cerr << "[ERROR] resources folder '" << resources << "' not found\n"; return 1; }
        const std::string pluginsFolder = project.value("pluginsFolder", std::string());

        std::error_code ec;
        fs::create_directories(out, ec);

        // Plugins of the project : each one is cooked on its own, not copied raw
        std::vector<std::string> skip = { resources };
        std::vector<fs::path> pluginDirs;
        if (!pluginsFolder.empty() && fs::is_directory(root / pluginsFolder)) {
            for (const auto& entry : fs::directory_iterator(root / pluginsFolder, ec))
                if (entry.is_directory(ec) && fs::is_regular_file(entry.path() / "shard-plugin.json")) pluginDirs.push_back(entry.path());
            for (const fs::path& p : pluginDirs) skip.push_back(fs::relative(p, root, ec).generic_string());
        }

        size_t copied = 0;
        if (!CopyTree(root, out, skip, copied)) return 1;
        const fs::path staging = fs::path(out) / ".cook-staging";
        AssetStats assets;
        const bool cookAssets = !gNoAssets;
        const long packed = PackFolder(root / resources, fs::path(out) / (resources + Pak::kExtension), {}, [&](PakWriter& writer) {
            if (cookAssets) CookAssets(root / resources, staging, writer, assets);
        });
        fs::remove_all(staging, ec);
        if (packed < 0) return 1;
        if (cookAssets)
            std::cout << "assets : " << assets.textures << " textures and " << assets.meshes << " models cooked (" << assets.sourceBytes << " -> " << assets.cookedBytes
                      << " bytes), " << assets.kept << " images left as they are, " << assets.failed << " failed\n";
        for (const fs::path& p : pluginDirs)
            if (CookPluginFolder(p, fs::path(out) / fs::relative(p, root, ec)) != 0) return 1;
        std::cout << out << " : project cooked, " << copied << " other files copied, " << pluginDirs.size() << " plugins\n";
        return 0;
    }

    int Pack(const std::string& folder, const std::string& out, const std::string& prefix)
    {
        if (!fs::is_directory(folder)) { std::cerr << "[ERROR] not a folder : " << folder << "\n"; return 1; }
        return PackFolder(folder, out, prefix) < 0 ? 1 : 0;
    }

    int List(const std::string& file)
    {
        PakReader reader;
        std::string error;
        if (!reader.Open(file, &error)) { std::cerr << "[ERROR] " << error << "\n"; return 1; }
        uint64_t original = 0, stored = 0;
        for (const auto& e : reader.Entries()) {
            char line[96];
            std::snprintf(line, sizeof(line), "%10llu %10llu  %08x  %c  ", static_cast<unsigned long long>(e.size),
                          static_cast<unsigned long long>(e.storedSize), e.crc, e.compressed ? 'z' : ' ');
            std::cout << line << e.path << "\n";
            original += e.size;
            stored += e.storedSize;
        }
        std::cout << reader.Entries().size() << " files, " << original << " -> " << stored << " bytes" << Percent(stored, original) << " (size, stored, crc, z = compressed)\n";
        return 0;
    }

    int Verify(const std::string& file)
    {
        PakReader reader;
        std::string error;
        if (!reader.Open(file, &error)) { std::cerr << "[ERROR] " << error << "\n"; return 1; }
        const std::vector<std::string> bad = reader.Verify();
        for (const std::string& b : bad) std::cerr << "[ERROR] corrupt : " << b << "\n";
        if (bad.empty()) std::cout << "ok, " << reader.Entries().size() << " files\n";
        return bad.empty() ? 0 : 1;
    }

    int Unpack(const std::string& file, const std::string& folder)
    {
        PakReader reader;
        std::string error;
        if (!reader.Open(file, &error) || !Pak::Unpack(reader, folder, &error)) { std::cerr << "[ERROR] " << error << "\n"; return 1; }
        std::cout << folder << " : " << reader.Entries().size() << " files\n";
        return 0;
    }
}

int main(int argc, char** argv)
{
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) return Usage();
    const std::string command = args[0];
    args.erase(args.begin());
    gStore = Flag(args, "--store");
    gNoAssets = Flag(args, "--no-assets");
    const std::string prefix = Option(args, "--prefix");
    const std::string out = Option(args, "--out");

    if (command == "pack" && args.size() == 2) return Pack(args[0], args[1], prefix);
    if (command == "plugin" && args.size() == 1) return CookPlugin(args[0], out);
    if (command == "project" && args.size() == 1) return CookProject(args[0], out);
    if (command == "list" && args.size() == 1) return List(args[0]);
    if (command == "verify" && args.size() == 1) return Verify(args[0]);
    if (command == "unpack" && args.size() == 2) return Unpack(args[0], args[1]);
    return Usage();
}
