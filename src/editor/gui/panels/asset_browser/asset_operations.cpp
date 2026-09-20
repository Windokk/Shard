#include "asset_operations.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>

#include "engine/core/engine.hpp"
#include "engine/projects/project.hpp"
#include "engine/levels/level.hpp"
#include "engine/core/resources/resources_manager.hpp"
#include "engine/serialization/assets/asset_database_serializer.hpp"
#include "engine/debugging/logger.hpp"

namespace Shard::Editor::GUI::AssetOperations
{
    namespace fs = std::filesystem;
    using Engine::Filesystem::Path;
    using Engine::Filesystem::AssetInfos;
    using Engine::Filesystem::AssetID;

    namespace {

        // Engine resources have IDs up to 500 (see SerializeAssetDataBase) : never touched here.
        constexpr int kFirstProjectAssetID = 501;

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
            Engine::Serialization::SerializeAssetDataBase(Engine::Core::GetEngine().GetCurrentProject()->GetAssetDatabasePath());
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

        bool ReadFileBinary(const fs::path& p, std::string& out)
        {
            std::ifstream in(p, std::ios::binary);
            if(!in) return false;
            std::ostringstream ss;
            ss << in.rdbuf();
            out = ss.str();
            return true;
        }

        // Path::WriteFile() opens in text mode, which would turn the \r\n of a file read in binary into \r\r\n
        bool WriteFileBinary(const fs::path& p, const std::string& content)
        {
            std::ofstream out(p, std::ios::binary | std::ios::trunc);
            if(!out) return false;
            out << content;
            return (bool)out;
        }

        bool ReplaceAll(std::string& text, const std::string& from, const std::string& to)
        {
            bool changed = false;
            size_t pos = 0;
            while((pos = text.find(from, pos)) != std::string::npos){
                text.replace(pos, from.size(), to);
                pos += to.size();
                changed = true;
            }
            return changed;
        }

        /// Levels and materials name the assets they use by their path in the project : rewrite those
        /// strings after a rename/move. A quoted-string match keeps this to real references.
        int RewriteReferences(const std::string& oldName, const std::string& newName, bool isDirectory)
        {
            std::string from, to;
            if(isDirectory){
                from = "\"" + oldName + "/";
                to = "\"" + newName + "/";
            }
            else{
                from = "\"" + oldName + "\"";
                to = "\"" + newName + "\"";
            }

            int rewritten = 0;
            for(auto& [id, info] : Engine::Core::GetEngine().GetAssetIDManager()->AssetIDMap){
                if(id.GetAsInt() < kFirstProjectAssetID)
                    continue;
                if(info->baseInfos.type != Engine::Filesystem::Type::T_LEVEL && info->baseInfos.type != Engine::Filesystem::Type::T_MATERIAL)
                    continue;

                fs::path file(info->baseInfos.path.full);
                std::string text;
                if(!ReadFileBinary(file, text))
                    continue;

                if(ReplaceAll(text, from, to) && WriteFileBinary(file, text))
                    rewritten++;
            }
            return rewritten;
        }

        /// Follows a rename/move (or, with an empty `newName`, a deletion) in the project's list of levels.
        void RemapBuildSettings(const std::string& oldName, const std::string& newName, bool isDirectory)
        {
            auto& list = Engine::Core::GetEngine().GetBuildSettings()->buildIndex;

            for(size_t i = 0; i < list.size();){
                std::string entry = list[i].full;
                bool absolute = fs::path(entry).is_absolute();

                std::string rel;
                if(absolute){
                    if(!NameInProject(fs::path(entry), rel)){ ++i; continue; }
                }
                else{
                    rel = entry;
                    std::replace(rel.begin(), rel.end(), '\\', '/');
                }

                if(rel != oldName && !(isDirectory && IsUnder(rel, oldName))){ ++i; continue; }

                if(newName.empty()){
                    list.erase(list.begin() + i);
                    continue;
                }

                std::string newRel = newName + rel.substr(oldName.size());
                list[i] = absolute ? Path((ProjectRoot() / newRel).string(), true) : Path(newRel);
                ++i;
            }
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
            return Fail("\"" + busy + "\" is used by the loaded level (or one of its assets). Load another level first.");

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
            return Fail("\"" + busy + "\" is used by the loaded level (or one of its assets). Load another level first.");

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

    Result CreateLevel(const Path& dir, Path& created)
    {
        created = UniquePath(dir, "NewLevel", ".lvl");

        auto level = std::make_shared<Engine::Levels::Level>(fs::path(created.full).stem().string(), created);
        level->Serialize(created);

        if(!fs::exists(created.full)){
            created = Path("");
            return Fail("Couldn't create the level.");
        }

        auto* engine = &Engine::Core::GetEngine();
        engine->GetFileManager()->RegisterAsset(Path(created.full, true));

        // Only levels listed in the build settings can be loaded (they get their build index from it)
        std::string name;
        if(NameInProject(fs::path(created.full), name))
            engine->GetBuildSettings()->AddToBuildSettings(Path(name));

        PersistDatabase();
        return {};
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
