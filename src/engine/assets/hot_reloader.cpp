#include "hot_reloader.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>

#include "engine/assets/asset_references.hpp"
#include "engine/assets/resources_manager.hpp"
#include "engine/core/diagnostics/logger.hpp"

namespace Shard::Engine::Assets {

    namespace fs = std::filesystem;
    using Core::Platform::FileChange;
    using Core::Platform::FileChangeKind;
    using Core::Resources::AssetKind;
    using Core::Resources::ResourceKey;
    using Filesystem::AssetID;
    using Filesystem::AssetInfos;
    using Filesystem::Path;

    namespace {

        // Engine resources have IDs up to 500 (see SerializeAssetDataBase) : only project assets are added, renamed, removed.
        constexpr int kFirstProjectAssetID = 501;

        std::string LowerExtension(const std::string& path)
        {
            const size_t dot = path.rfind('.');
            const size_t slash = path.find_last_of('/');
            if(dot == std::string::npos || (slash != std::string::npos && dot < slash))
                return "";
            std::string ext = path.substr(dot);
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return (char)std::tolower(c); });
            return ext;
        }

        std::string WithoutExtension(const std::string& path)
        {
            const size_t dot = path.rfind('.');
            const size_t slash = path.find_last_of('/');
            if(dot == std::string::npos || (slash != std::string::npos && dot < slash))
                return path;
            return path.substr(0, dot);
        }

        std::string FileName(const std::string& path)
        {
            const size_t slash = path.find_last_of('/');
            return slash == std::string::npos ? path : path.substr(slash + 1);
        }

        /// Files an editor or the OS leaves around while it works : never assets.
        bool IsTransient(const std::string& path)
        {
            const std::string name = FileName(path);
            if(name.empty() || name.front() == '.' || name.front() == '~' || name.back() == '~')
                return true;
            const std::string ext = LowerExtension(name);
            return ext == ".tmp" || ext == ".swp" || ext == ".swx" || ext == ".part" || ext == ".crdownload" || ext == ".bak";
        }

        bool IsImageExtension(const std::string& ext)
        {
            return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".hdr" || ext == ".bmp" || ext == ".gif" || ext == ".tga";
        }

        bool IsDirectory(const std::string& path)
        {
            std::error_code ec;
            return fs::is_directory(fs::u8path(path), ec);
        }

        /// The file can be opened and has content : whoever writes it is done (or at least not holding it exclusively).
        bool IsReadable(const std::string& path, bool& empty)
        {
            std::ifstream in(fs::u8path(path), std::ios::binary | std::ios::ate);
            if(!in)
                return false;
            empty = in.tellg() <= 0;
            return true;
        }
    }

    HotReloader::HotReloader() = default;

    HotReloader::~HotReloader()
    {
        Stop();
    }

    bool HotReloader::Start(const Config& config)
    {
        Stop();
        m_Config = config;
        if(!m_Config.files || !m_Config.ids || !m_Config.resources)
            return false;

        const std::pair<Path, bool> candidates[] = {
            { m_Config.files->GetProjectResRoot(), false },
            { m_Config.files->GetEngineResRoot(), true },
        };
        for(const auto& [root, isEngine] : candidates){
            if(root.full.empty() || !root.Exists() || !root.IsDirectory())
                continue;

            std::error_code ec;
            const std::string absolute = fs::absolute(fs::u8path(root.full), ec).generic_u8string();

            Root entry;
            entry.path = absolute;
            entry.isEngine = isEngine;
            entry.watcher = std::make_unique<Core::Platform::FileWatcher>();
            if(!entry.watcher->Start(absolute, true)){
                DEBUG_WARNING("[HotReload] Can't watch ", absolute);
                continue;
            }
            m_Roots.push_back(std::move(entry));
        }

        m_Running = !m_Roots.empty();
        return m_Running;
    }

    void HotReloader::Stop()
    {
        for(Root& root : m_Roots)
            root.watcher->Stop();
        m_Roots.clear();
        m_Entries.clear();
        m_Running = false;
    }

    // ---------------------------------------------------------------------------------------------
    // Gathering
    // ---------------------------------------------------------------------------------------------

    void HotReloader::Ingest(const FileChange& change, bool engine)
    {
        const auto now = std::chrono::steady_clock::now();

        auto find = [&](const std::string& path) -> Entry* {
            for(Entry& e : m_Entries)
                if(e.path == path)
                    return &e;
            return nullptr;
        };
        auto erase = [&](Entry* entry){
            m_Entries.erase(m_Entries.begin() + (entry - m_Entries.data()));
        };

        switch(change.kind){
            case FileChangeKind::Created:
            {
                if(Entry* e = find(change.path); e && e->kind == FileChangeKind::Deleted){
                    // Deleted then written again : a safe-save, the asset was only modified
                    e->kind = FileChangeKind::Modified;
                    e->last = now;
                    return;
                }
                m_Entries.push_back({ FileChangeKind::Created, change.path, "", engine, now, 0 });
                return;
            }
            case FileChangeKind::Modified:
            {
                if(Entry* e = find(change.path)){
                    e->last = now;
                    return;
                }
                m_Entries.push_back({ FileChangeKind::Modified, change.path, "", engine, now, 0 });
                return;
            }
            case FileChangeKind::Deleted:
            {
                if(Entry* e = find(change.path)){
                    if(e->kind == FileChangeKind::Created){      // never applied : it came and went
                        erase(e);
                        return;
                    }
                    if(e->kind == FileChangeKind::Renamed){      // renamed then deleted : the original is gone
                        e->path = e->oldPath;
                        e->oldPath.clear();
                    }
                    e->kind = FileChangeKind::Deleted;
                    e->last = now;
                    return;
                }
                m_Entries.push_back({ FileChangeKind::Deleted, change.path, "", engine, now, 0 });
                return;
            }
            case FileChangeKind::Renamed:
            {
                // A file written under a temporary name, then renamed onto the real one
                if(Entry* e = find(change.oldPath); e && (e->kind == FileChangeKind::Created || e->kind == FileChangeKind::Modified))
                    erase(e);
                m_Entries.push_back({ FileChangeKind::Renamed, change.path, change.oldPath, engine, now, 0 });
                return;
            }
        }
    }

    void HotReloader::Update()
    {
        if(!m_Running)
            return;

        for(Root& root : m_Roots){
            for(const FileChange& change : root.watcher->Poll())
                Ingest(change, root.isEngine);
        }

        if(m_Entries.empty())
            return;

        const auto now = std::chrono::steady_clock::now();
        const auto settle = std::chrono::duration<double>(m_SettleSeconds);

        std::vector<Entry> pending;
        std::vector<Entry> ready;
        for(Entry& entry : m_Entries){
            if(now - entry.last >= settle)
                ready.push_back(std::move(entry));
            else
                pending.push_back(std::move(entry));
        }
        m_Entries = std::move(pending);

        for(Entry& entry : ready){
            if(!Apply(entry)){
                entry.last = std::chrono::steady_clock::now();
                m_Entries.push_back(std::move(entry));      // not ready yet (still being written) : again later
            }
            else if(!entry.engine && entry.kind != FileChangeKind::Modified)
                m_Revision++;                               // a listing of the folders (the asset browser) is out of date
        }

        if(m_DatabaseDirty){
            m_DatabaseDirty = false;
            if(m_Config.persistDatabase)
                m_Config.persistDatabase();
        }
    }

    bool HotReloader::Apply(Entry& entry)
    {
        if(IsTransient(entry.path) && entry.kind != FileChangeKind::Renamed && entry.kind != FileChangeKind::Deleted)
            return true;

        switch(entry.kind){
            case FileChangeKind::Created:
            case FileChangeKind::Modified:
            {
                if(IsDirectory(entry.path)){
                    if(entry.kind == FileChangeKind::Created && !entry.engine)
                        ApplyCreated(entry.path);
                    return true;
                }

                std::error_code ec;
                if(!fs::exists(fs::u8path(entry.path), ec))
                    return true;                        // gone again before we got to it

                bool empty = false;
                if(!IsReadable(entry.path, empty) || (empty && entry.attempts < 5)){
                    if(++entry.attempts > 40){
                        DEBUG_WARNING("[HotReload] Gave up on ", entry.path, " (cannot be read)");
                        return true;
                    }
                    return false;
                }

                if(entry.engine)
                    ApplyModified(entry.path, true);
                else if(entry.kind == FileChangeKind::Created)
                    ApplyCreated(entry.path);
                else
                    ApplyModified(entry.path, false);
                return true;
            }
            case FileChangeKind::Deleted:
                if(!entry.engine)
                    ApplyDeleted(entry.path);
                return true;
            case FileChangeKind::Renamed:
                if(!entry.engine)
                    ApplyRenamed(entry.oldPath, entry.path);
                else
                    ApplyModified(entry.path, true);
                return true;
        }
        return true;
    }

    // ---------------------------------------------------------------------------------------------
    // Applying
    // ---------------------------------------------------------------------------------------------

    std::string HotReloader::NameOf(const std::string& path) const
    {
        // The path under the watched root it came from. Not GetFileInfos : it looks at the file, which a deleted one no longer is.
        for(const Root& root : m_Roots){
            if(path.size() > root.path.size() && path.compare(0, root.path.size(), root.path) == 0 && path[root.path.size()] == '/')
                return path.substr(root.path.size() + 1);
        }
        return "";
    }

    int HotReloader::RegisterPath(const std::string& path)
    {
        auto& assetMap = m_Config.ids->AssetIDMap;
        int added = 0;

        auto registerFile = [&](const std::string& file){
            if(IsTransient(file))
                return;
            const size_t count = assetMap.size();
            m_Config.files->RegisterAsset(Path(file, true));
            if(assetMap.size() > count){
                added++;
                const std::string name = NameOf(file);
                DEBUG_INFO("[HotReload] New asset : ", name);
                if(LowerExtension(file) == ".mat" || LowerExtension(file) == ".material")
                    m_Config.resources->RefreshDependencies(name);
            }
        };

        std::error_code ec;
        if(IsDirectory(path)){
            for(auto it = fs::recursive_directory_iterator(fs::u8path(path), fs::directory_options::skip_permission_denied, ec);
                !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
            {
                if(it->is_regular_file(ec))
                    registerFile(it->path().generic_u8string());
            }
        }
        else
            registerFile(path);

        if(added > 0){
            m_DatabaseDirty = true;
            m_Revision++;
        }
        return added;
    }

    void HotReloader::ApplyCreated(const std::string& path)
    {
        if(IsDirectory(path)){
            RegisterPath(path);
            return;
        }

        const std::string name = NameOf(path);
        if(name.empty())
            return;

        // Already known (written again after a delete, or created by the editor itself) : its content may have changed
        const AssetID id = m_Config.ids->GetIDFromNameInProject(name);
        auto info = m_Config.ids->GetAssetFromID(id);
        if(info && info->baseInfos.nameInProject == name){
            ApplyModified(path, false);
            return;
        }

        RegisterPath(path);
    }

    void HotReloader::ApplyModified(const std::string& path, bool engine)
    {
        const std::string name = NameOf(path);
        if(name.empty())
            return;

        if(!engine){
            // Written by something that never told us it exists
            const AssetID id = m_Config.ids->GetIDFromNameInProject(name);
            auto info = m_Config.ids->GetAssetFromID(id);
            if(!info || info->baseInfos.nameInProject != name){
                RegisterPath(path);
                return;
            }
        }

        ReloadByFile(name);
    }

    void HotReloader::ReloadByFile(const std::string& name)
    {
        auto* resources = m_Config.resources;
        const std::string ext = LowerExtension(name);

        std::vector<ResourceKey> reloaded;
        auto reload = [&](AssetKind kind, const std::string& key, bool warnOnFailure){
            if(!resources->Has(kind, key))
                return;
            if(resources->Reload(kind, key)){
                reloaded.push_back({ kind, key });
                DEBUG_INFO("[HotReload] Reloaded ", key);
            }
            else if(warnOnFailure)
                DEBUG_WARNING("[HotReload] Couldn't reload ", name, " : keeping the previous version");
        };

        if(IsImageExtension(ext)){
            reload(AssetKind::Texture, name, false);
            reload(AssetKind::ColorTexture, name, false);
        }
        else if(ext == ".vert" || ext == ".frag" || ext == ".geom")
            reload(AssetKind::Shader, WithoutExtension(name), true);
        else if(ext == ".comp")
            reload(AssetKind::ComputeShader, WithoutExtension(name), true);
        else if(ext == ".glsl"){
            // Included by an unknown number of shaders
            for(const std::string& key : resources->ResidentPaths(AssetKind::Shader))
                reload(AssetKind::Shader, key, true);
            for(const std::string& key : resources->ResidentPaths(AssetKind::ComputeShader))
                reload(AssetKind::ComputeShader, key, true);
        }
        else if(ext == ".mat" || ext == ".material"){
            resources->RefreshDependencies(name);
            reload(AssetKind::Material, name, true);
            return;
        }
        else
            return;

        // A material stores the GL handle of its textures and was built against its shader's uniforms : read it again
        std::set<std::string> materials;
        for(const ResourceKey& key : reloaded){
            if(key.kind != AssetKind::Texture && key.kind != AssetKind::ColorTexture && key.kind != AssetKind::Shader)
                continue;
            for(const ResourceKey& owner : resources->ResidentOwnersOf(key)){
                if(owner.kind == AssetKind::Material)
                    materials.insert(owner.path);
            }
        }
        for(const std::string& material : materials)
            reload(AssetKind::Material, material, true);
    }

    void HotReloader::ApplyDeleted(const std::string& path)
    {
        const std::string name = NameOf(path);
        if(name.empty())
            return;

        std::vector<std::pair<AssetID, std::string>> affected;
        for(auto& [id, info] : m_Config.ids->AssetIDMap){
            if(id.GetAsInt() < kFirstProjectAssetID)
                continue;
            const std::string& assetName = info->baseInfos.nameInProject;
            if(assetName == name || IsUnderFolder(assetName, name))
                affected.emplace_back(id, assetName);
        }
        if(affected.empty())
            return;     // nothing registered : the editor already dealt with it

        int removed = 0;
        for(auto& [id, assetName] : affected){
            if(m_Config.resources->IsInUse(assetName)){
                DEBUG_WARNING("[HotReload] ", assetName, " was deleted but is still used by the loaded world : it stays in memory");
                continue;
            }
            m_Config.resources->TryUnloadAsset(assetName);
            m_Config.ids->DestroyID(id);
            DEBUG_INFO("[HotReload] Removed asset : ", assetName);
            removed++;
        }

        if(removed > 0){
            if(m_Config.buildSettings)
                RemapBuildSettings(*m_Config.buildSettings, m_Config.files->GetProjectResRoot(), name, "", true);
            m_DatabaseDirty = true;
            m_Revision++;
        }
    }

    void HotReloader::ApplyRenamed(const std::string& oldPath, const std::string& newPath)
    {
        const std::string oldName = NameOf(oldPath);
        const std::string newName = NameOf(newPath);

        if(newName.empty()){
            // Moved out of the resources
            if(!oldName.empty())
                ApplyDeleted(oldPath);
            return;
        }
        if(IsTransient(newPath))
            return;

        const bool isDirectory = IsDirectory(newPath);

        std::vector<std::pair<AssetID, std::shared_ptr<AssetInfos>>> affected;
        if(!oldName.empty()){
            for(auto& [id, info] : m_Config.ids->AssetIDMap){
                if(id.GetAsInt() < kFirstProjectAssetID)
                    continue;
                const std::string& assetName = info->baseInfos.nameInProject;
                if(assetName == oldName || (isDirectory && IsUnderFolder(assetName, oldName)))
                    affected.emplace_back(id, info);
            }
        }

        if(affected.empty()){
            // Not an asset we knew : a file that appeared under its final name (written to a temporary one first), or moved in
            if(isDirectory){
                RegisterPath(newPath);
                return;
            }

            // Already registered under the new name and renamed without changing its type : the editor moved it itself
            const AssetID known = m_Config.ids->GetIDFromNameInProject(newName);
            auto knownInfo = m_Config.ids->GetAssetFromID(known);
            const bool registered = knownInfo && knownInfo->baseInfos.nameInProject == newName;
            if(!registered || oldPath.empty() || LowerExtension(oldPath) != LowerExtension(newPath))
                ApplyCreated(newPath);
            return;
        }

        const std::string projectRoot = m_Config.files->GetProjectResRoot().full;
        bool inUse = false;
        for(auto& [id, info] : affected){
            const std::string assetOldName = info->baseInfos.nameInProject;
            if(m_Config.resources->IsInUse(assetOldName))
                inUse = true;

            // The asset keeps its ID : dependency lists and everything else that stored it stay valid
            const std::string assetNewName = newName + assetOldName.substr(oldName.size());
            info->baseInfos = m_Config.files->GetFileInfos(Path(projectRoot + "/" + assetNewName, true));
            info->baseInfos.ID = id;
        }

        RewriteAssetReferences(*m_Config.ids, oldName, newName, isDirectory);
        if(m_Config.buildSettings)
            RemapBuildSettings(*m_Config.buildSettings, m_Config.files->GetProjectResRoot(), oldName, newName, isDirectory);
        m_Config.resources->RenameResident(oldName, newName, isDirectory);

        DEBUG_INFO("[HotReload] Renamed ", oldName, " -> ", newName);
        if(inUse)
            DEBUG_WARNING("[HotReload] ", oldName, " is used by the loaded world, which still names it that way : reload the world");

        m_DatabaseDirty = true;
        m_Revision++;
    }
}
