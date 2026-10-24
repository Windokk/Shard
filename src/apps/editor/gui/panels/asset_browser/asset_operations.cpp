#include "asset_operations.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>

#include "engine/world/engine.hpp"
#include "engine/assets/project/project.hpp"
#include "engine/world/world.hpp"
#include "engine/assets/resources_manager.hpp"
#include "engine/assets/asset_database_serializer.hpp"
#include "engine/assets/asset_importer.hpp"
#include "engine/assets/asset_references.hpp"
#include "engine/core/diagnostics/logger.hpp"

namespace Shard::Editor::GUI::AssetOperations
{
    namespace fs = std::filesystem;
    using Engine::Filesystem::Path;
    using Engine::Filesystem::AssetInfos;
    using Engine::Filesystem::AssetID;

    namespace {

        // Engine resources have IDs below kFirstProjectAssetID : never touched here.
        using Engine::Filesystem::kFirstProjectAssetID;

        fs::path ProjectRoot()
        {
            return fs::path(Engine::Core::GetEngine().GetCurrentProject()->GetProjectResourcesPath().full);
        }

        /// `p` as a path in the project ("models/cube.fbx", "." for the resources root). False if outside it.
        bool NameInProject(const fs::path& p, std::string& out)
        {
            std::error_code ec;
            fs::path canonical = fs::weakly_canonical(p, ec);
            if(ec) return false;
            fs::path root = fs::weakly_canonical(ProjectRoot(), ec);
            if(ec) return false;

            fs::path rel = fs::relative(canonical, root, ec);
            if(ec || rel.empty()) return false;

            std::string s = rel.generic_string();
            if(s == ".." || s.rfind("../", 0) == 0) return false;

            out = s;
            return true;
        }

        bool IsUnder(const std::string& name, const std::string& dirName)
        {
            return name.size() > dirName.size() && name.compare(0, dirName.size(), dirName) == 0 && name[dirName.size()] == '/';
        }

        using AssetList = std::vector<std::pair<AssetID, std::shared_ptr<AssetInfos>>>;

        /// Project assets that are `name` itself, or (for a folder) anything inside it
        AssetList AssetsAt(const std::string& name, bool isDirectory)
        {
            AssetList list;
            for(auto& [id, info] : Engine::Core::GetEngine().GetAssetIDManager()->AssetIDMap){
                if(id.GetAsInt() < kFirstProjectAssetID)
                    continue;

                const std::string& assetName = info->baseInfos.nameInProject;
                if(assetName == name || (isDirectory && IsUnder(assetName, name)))
                    list.emplace_back(id, info);
            }
            return list;
        }

        void PersistDatabase()
        {
            Engine::Serialization::SerializeAssetDataBase(Engine::Core::GetEngine().GetCurrentProject()->GetAssetDatabasePath(),
                                                          *Engine::Core::GetEngine().GetAssetIDManager());
        }

        std::string FirstInUse(const AssetList& assets)
        {
            auto* resources = Engine::Core::GetEngine().GetResourcesManager();
            for(auto& [id, info] : assets){
                if(resources->IsInUse(info->baseInfos.nameInProject))
                    return info->baseInfos.nameInProject;
            }
            return "";
        }

        // Path::WriteFile() opens in text mode, which would turn the \r\n of a file read in binary into \r\r\n
        bool WriteFileBinary(const fs::path& p, const std::string& content)
        {
            std::ofstream out(p, std::ios::binary | std::ios::trunc);
            if(!out) return false;
            out << content;
            return (bool)out;
        }

        int RewriteReferences(const std::string& oldName, const std::string& newName, bool isDirectory)
        {
            return Engine::Assets::RewriteAssetReferences(*Engine::Core::GetEngine().GetAssetIDManager(), oldName, newName, isDirectory);
        }

        void RemapBuildSettings(const std::string& oldName, const std::string& newName, bool isDirectory)
        {
            // The project file follows the files on disk at once (Project::RemapWorlds)
            auto& engine = Engine::Core::GetEngine();
            engine.GetCurrentProject()->RemapWorlds(oldName, newName, isDirectory,
                engine.GetSettings().readOnly ? std::string() : engine.GetSettings().project);
        }

        Result Fail(const std::string& message)
        {
            Result r;
            r.ok = false;
            r.message = message;
            return r;
        }

        void RegisterTree(const fs::path& root)
        {
            auto* fileManager = Engine::Core::GetEngine().GetFileManager();

            if(fs::is_regular_file(root)){
                fileManager->RegisterAsset(Path(root.string(), true));
                return;
            }

            for(auto& entry : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied)){
                if(entry.is_regular_file())
                    fileManager->RegisterAsset(Path(entry.path().string(), true));
            }
        }
    }

    bool IsEditable(const Path& path)
    {
        std::string name;
        return NameInProject(fs::path(path.full), name) && name != ".";
    }

    Path UniquePath(const Path& dir, const std::string& stem, const std::string& extension, bool copySuffix)
    {
        fs::path base(dir.full);

        std::string first = copySuffix ? stem + "_copy" : stem;
        fs::path candidate = base / (first + extension);

        for(int n = copySuffix ? 2 : 1; fs::exists(candidate); n++)
            candidate = base / ((copySuffix ? first + std::to_string(n) : stem + "_" + std::to_string(n)) + extension);

        return Path(candidate.string());
    }

    Result Move(const Path& from, const Path& to)
    {
        std::string oldName, newName;
        if(!NameInProject(fs::path(from.full), oldName) || !NameInProject(fs::path(to.full), newName) || oldName == "." || newName == ".")
            return Fail("Only assets inside the project's resources can be moved or renamed.");

        if(!fs::exists(from.full))
            return Fail("\"" + oldName + "\" does not exist anymore.");

        if(fs::exists(to.full))
            return Fail("\"" + newName + "\" already exists.");

        const bool isDirectory = fs::is_directory(from.full);

        if(isDirectory && IsUnder(newName, oldName))
            return Fail("A folder cannot be moved into itself.");

        AssetList affected = AssetsAt(oldName, isDirectory);

        std::string busy = FirstInUse(affected);
        if(!busy.empty())
            return Fail("\"" + busy + "\" is used by the loaded world (or one of its assets). Load another world first.");

        auto* resources = Engine::Core::GetEngine().GetResourcesManager();
        for(auto& [id, info] : affected)
            resources->TryUnloadAsset(info->baseInfos.nameInProject);

        std::error_code ec;
        fs::create_directories(fs::path(to.full).parent_path(), ec);
        fs::rename(from.full, to.full, ec);
        if(ec)
            return Fail("Couldn't move \"" + oldName + "\" : " + ec.message());

        // The asset keeps its ID : dependency lists and everything else that stored it stay valid.
        auto* fileManager = Engine::Core::GetEngine().GetFileManager();
        for(auto& [id, info] : affected){
            std::string assetNewName = newName + info->baseInfos.nameInProject.substr(oldName.size());

            info->baseInfos = fileManager->GetFileInfos(Path((ProjectRoot() / assetNewName).string(), true));
            info->baseInfos.ID = id;
        }

        RewriteReferences(oldName, newName, isDirectory);
        RemapBuildSettings(oldName, newName, isDirectory);
        PersistDatabase();

        return {};
    }

    Result Copy(const Path& from, const Path& to)
    {
        std::string oldName, newName;
        if(!NameInProject(fs::path(from.full), oldName) || !NameInProject(fs::path(to.full), newName) || oldName == "." || newName == ".")
            return Fail("Only assets inside the project's resources can be copied.");

        if(!fs::exists(from.full))
            return Fail("\"" + oldName + "\" does not exist anymore.");

        if(fs::exists(to.full))
            return Fail("\"" + newName + "\" already exists.");

        const bool isDirectory = fs::is_directory(from.full);

        if(isDirectory && IsUnder(newName, oldName))
            return Fail("A folder cannot be copied into itself.");

        std::error_code ec;
        fs::create_directories(fs::path(to.full).parent_path(), ec);
        fs::copy(from.full, to.full, fs::copy_options::recursive, ec);
        if(ec)
            return Fail("Couldn't copy \"" + oldName + "\" : " + ec.message());

        // The copies are new assets : new IDs. What they reference is unchanged (same paths in the project).
        RegisterTree(fs::path(to.full));
        PersistDatabase();

        return {};
    }

    Result Delete(const Path& path)
    {
        std::string name;
        if(!NameInProject(fs::path(path.full), name) || name == ".")
            return Fail("Only assets inside the project's resources can be deleted.");

        if(!fs::exists(path.full))
            return Fail("\"" + name + "\" does not exist anymore.");

        const bool isDirectory = fs::is_directory(path.full);
        AssetList affected = AssetsAt(name, isDirectory);

        std::string busy = FirstInUse(affected);
        if(!busy.empty())
            return Fail("\"" + busy + "\" is used by the loaded world (or one of its assets). Load another world first.");

        auto* resources = Engine::Core::GetEngine().GetResourcesManager();
        for(auto& [id, info] : affected)
            resources->TryUnloadAsset(info->baseInfos.nameInProject);

        std::error_code ec;
        fs::remove_all(path.full, ec);
        if(ec)
            return Fail("Couldn't delete \"" + name + "\" : " + ec.message());

        auto* assetManager = Engine::Core::GetEngine().GetAssetIDManager();
        for(auto& [id, info] : affected)
            assetManager->DestroyID(id);

        RemapBuildSettings(name, "", isDirectory);
        PersistDatabase();

        return {};
    }

    std::vector<std::string> ReferencedBy(const Path& path)
    {
        std::vector<std::string> referencedBy;

        std::string name;
        if(!NameInProject(fs::path(path.full), name))
            return referencedBy;

        AssetList targets = AssetsAt(name, fs::is_directory(path.full));

        std::set<int> targetIDs;
        for(auto& [id, info] : targets)
            targetIDs.insert(id.GetAsInt());

        for(auto& [id, info] : Engine::Core::GetEngine().GetAssetIDManager()->AssetIDMap){
            if(id.GetAsInt() < kFirstProjectAssetID || targetIDs.count(id.GetAsInt()) > 0)
                continue;

            for(auto& dep : info->dependencies){
                if(targetIDs.count(dep.GetAsInt()) > 0){
                    referencedBy.push_back(info->baseInfos.nameInProject);
                    break;
                }
            }
        }

        return referencedBy;
    }

    Result CreateFolder(const Path& dir, Path& created)
    {
        created = UniquePath(dir, "NewFolder", "");

        std::error_code ec;
        if(!fs::create_directory(created.full, ec)){
            created = Path("");
            return Fail("Couldn't create the folder" + (ec ? " : " + ec.message() : std::string()));
        }
        return {};
    }

    Result CreateMaterial(const Path& dir, Path& created)
    {
        created = UniquePath(dir, "NewMaterial", ".mat");

        // Same defaults as a fresh lit material : white base, flat normal, mid roughness
        const std::string content =
            "{\n"
            "    \"recievesShadows\": true,\n"
            "    \"renderMode\": \"opaque\",\n"
            "    \"shader\": \"shaders/mesh/lit\",\n"
            "    \"uniforms\": [\n"
            "        {\"albedo\": \"textures/white.png\"},\n"
            "        {\"metallicMap\": \"textures/white.png\"},\n"
            "        {\"roughnessMap\": \"textures/grey.png\"},\n"
            "        {\"normalMap\": \"textures/default_normal.png\"},\n"
            "        {\"metallic\": 0.0},\n"
            "        {\"roughness\": 0.5},\n"
            "        {\"useEnvReflections\": false}\n"
            "    ]\n"
            "}\n";

        if(!WriteFileBinary(fs::path(created.full), content)){
            created = Path("");
            return Fail("Couldn't create the material.");
        }

        Engine::Core::GetEngine().GetFileManager()->RegisterAsset(Path(created.full, true));
        PersistDatabase();
        return {};
    }

    Result CreateFromBytes(const Path& dir, const std::string& defaultName, const std::string& bytes, Path& created)
    {
        const fs::path name(defaultName);
        created = UniquePath(dir, name.stem().string(), name.extension().string());
        if(!WriteFileBinary(fs::path(created.full), bytes)){
            created = Path("");
            return Fail("Couldn't create the file.");
        }

        Engine::Core::GetEngine().GetFileManager()->RegisterAsset(Path(created.full, true));
        PersistDatabase();
        return {};
    }

    Result CreateWorld(const Path& dir, Path& created)
    {
        created = UniquePath(dir, "NewWorld", ".world");

        auto world = std::make_shared<Engine::Worlds::World>(fs::path(created.full).stem().string(), created);
        world->Serialize(created);

        if(!fs::exists(created.full)){
            created = Path("");
            return Fail("Couldn't create the world.");
        }

        auto* engine = &Engine::Core::GetEngine();
        engine->GetFileManager()->RegisterAsset(Path(created.full, true));

        // Only worlds listed in the build settings can be loaded (they get their build index from it)
        std::string name;
        if(NameInProject(fs::path(created.full), name))
            engine->GetBuildSettings()->AddToBuildSettings(Path(name));

        PersistDatabase();
        return {};
    }

    ImportSummary Import(const Path& dir, const std::vector<std::string>& sources)
    {
        ImportSummary summary;

        std::string dirName;
        if(!NameInProject(fs::path(dir.full), dirName)){
            summary.failures.push_back("Assets can only be imported inside the project's resources.");
            return summary;
        }

        auto* fileManager = Engine::Core::GetEngine().GetFileManager();

        for(const std::string& source : sources){
            // The paths come from the OS dialog, in UTF-8
            const fs::path from = fs::u8path(source);
            const std::string label = from.filename().string();

            std::error_code ec;
            if(!fs::is_regular_file(from, ec)){
                summary.failures.push_back("\"" + label + "\" is not a file.");
                continue;
            }

            // Already in the project : nothing to copy, it only needs to be known
            std::string insideName;
            if(NameInProject(from, insideName)){
                fileManager->RegisterAsset(Path(from.string(), true));
                summary.imported++;
                continue;
            }

            // A format the engine does not read : the plugin that brought its importer converts it
            if(Engine::Filesystem::AssetImporters::Get().CanImport(source)){
                const Engine::Filesystem::ImportResult result = Engine::Filesystem::AssetImporters::Get().Import(source, dir.full);
                if(!result.ok){
                    summary.failures.push_back("Couldn't import \"" + label + "\" : " + (result.message.empty() ? std::string("the importer failed.") : result.message));
                    continue;
                }
                for(const std::string& output : result.outputs)
                    fileManager->RegisterAsset(Path(output, true));
                summary.imported++;
                continue;
            }

            const Path target = UniquePath(dir, from.stem().string(), from.extension().string());
            fs::copy_file(from, target.full, ec);
            if(ec){
                summary.failures.push_back("Couldn't copy \"" + label + "\" : " + ec.message());
                continue;
            }

            fileManager->RegisterAsset(Path(target.full, true));
            summary.imported++;
        }

        if(summary.imported > 0)
            PersistDatabase();

        return summary;
    }

    std::string Sync(const Path& dir)
    {
        auto* engine = &Engine::Core::GetEngine();
        auto* assetManager = engine->GetAssetIDManager();

        std::string dirName;
        if(!NameInProject(fs::path(dir.full), dirName))
            return "";

        // New files first (skipping hidden ones : .DS_Store, .gitkeep...)
        int added = 0;
        std::error_code ec;
        for(auto& entry : fs::recursive_directory_iterator(dir.full, fs::directory_options::skip_permission_denied, ec)){
            if(!entry.is_regular_file() || entry.path().filename().string().front() == '.')
                continue;

            size_t count = assetManager->AssetIDMap.size();
            engine->GetFileManager()->RegisterAsset(Path(entry.path().string(), true));
            if(assetManager->AssetIDMap.size() > count)
                added++;
        }

        // Then entries whose file is gone
        std::vector<AssetID> stale;
        for(auto& [id, info] : assetManager->AssetIDMap){
            if(id.GetAsInt() < kFirstProjectAssetID)
                continue;

            const std::string& name = info->baseInfos.nameInProject;
            bool inScope = dirName == "." || name == dirName || IsUnder(name, dirName);
            if(inScope && !fs::exists(info->baseInfos.path.full) && !engine->GetResourcesManager()->IsInUse(name))
                stale.push_back(id);
        }
        for(auto& id : stale)
            assetManager->DestroyID(id);

        if(added == 0 && stale.empty())
            return "";

        PersistDatabase();

        std::string summary;
        if(added > 0) summary += std::to_string(added) + " new asset" + (added > 1 ? "s" : "");
        if(!stale.empty()) summary += std::string(summary.empty() ? "" : ", ") + std::to_string(stale.size()) + " missing asset" + (stale.size() > 1 ? "s" : "") + " removed";
        return summary;
    }
}
