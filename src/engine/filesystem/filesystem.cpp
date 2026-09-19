#include "filesystem.hpp"

#include "engine/debugging/logger.hpp"
#include "engine/core/engine.hpp"

namespace Shard::Engine::Filesystem{

    void FileManager::Init(Path projectResPath, Path engineResPath, Path projectRoot)
    {
        this->projectRoot = projectRoot;
        this->projectResPath = projectResPath;
        this->engineResPath = engineResPath;
    }


    std::vector<FileInfos> FileManager::ListDirectory(const Path &path, std::vector<Type> acceptedExtensions, bool includeDirs, bool recursive)
    {
        std::vector<FileInfos> files;

        if (!std::filesystem::exists(path.full) || !std::filesystem::is_directory(path.full))
            DEBUG_ERROR("Cannot list files inside non-existing directory");

        auto processEntry = [&](const auto& entry) {
            Path filePath(entry.path().string());
            
            if (entry.is_directory() && includeDirs)
            {
                FileInfos info = GetFileInfos(filePath);
                files.push_back(info);
            }
            else if (entry.is_regular_file())
            {
                auto extType = filePath.GetExtensionType();
                if (acceptedExtensions.empty() || std::find(acceptedExtensions.begin(), acceptedExtensions.end(), extType) != acceptedExtensions.end()) {
                    files.push_back(GetFileInfos(filePath));
                }
            }
        };

        if (recursive)
        {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(path.full,
                        std::filesystem::directory_options::skip_permission_denied))
            {
                processEntry(entry);
            }
        }
        else
        {
            for (const auto& entry : std::filesystem::directory_iterator(path.full,
                        std::filesystem::directory_options::skip_permission_denied))
            {
                processEntry(entry);
            }
        }

        return files;
    }

    Path FileManager::GetCurrentExecutablePath()
    {
        return Path(std::filesystem::current_path().string(), true);
    }

    FileInfos FileManager::GetFileInfos(const Path &path)
    {
        FileInfos infos;
    
        infos.path = path.full;
        infos.name = path.GetFilename(false);
        infos.extension = path.GetExtensionString();

        if(IsPathInside(engineResPath, path)){
            // This file is part of the engine ressources
            infos.nameInProject = path.RelativeTo(engineResPath).full;
            infos.ID = Core::GetEngine().GetAssetIDManager()->GetIDFromNameInProject(infos.nameInProject);
        }
        else if(IsPathInside(projectResPath, path)){
            // This file is part of the project ressources
            infos.nameInProject = path.RelativeTo(projectResPath).full;
            infos.ID = Core::GetEngine().GetAssetIDManager()->GetIDFromNameInProject(infos.nameInProject);
        }
        else{
            infos.nameInProject = "";
        }
        infos.isDirectory = path.IsDirectory();
        infos.size = path.GetFileSize();
        infos.type = path.GetExtensionType();

        return infos;
    }

    AssetID FileManager::RegisterAsset(const Path &path)
    {
        AssetIDManager* assetManager = Core::GetEngine().GetAssetIDManager();

        FileInfos infos = GetFileInfos(path);
        if(infos.nameInProject.empty()){
            DEBUG_ERROR("Cannot register asset outside of the resource roots : " + path.full);
            return AssetID{};
        }

        // GetFileInfos() resolves the ID by name, and an unknown name comes back as the default
        // AssetID - which could coincide with a real asset's ID, hence the name comparison.
        std::shared_ptr<AssetInfos> existing = assetManager->GetAssetFromID(infos.ID);
        if(existing && existing->baseInfos.nameInProject == infos.nameInProject)
            return infos.ID;

        AssetID id = assetManager->GenerateNewID();
        if(id.GetAsInt() < 0)
            return AssetID{};

        infos.ID = id;

        std::shared_ptr<AssetInfos> assetInfos = std::make_shared<AssetInfos>();
        assetInfos->baseInfos = infos;
        assetManager->AssignID(id, assetInfos);

        return id;
    }

    std::string Path::ReadFile() const
    {
        // Replace "/" with native dir separator

        if(IsDirectory()){
            DEBUG_WARNING("Tried reading a folder : ",full);
            return "";
        }

        std::string filepath = full;

        #if defined(__WIN32__)

        std::replace(filepath.begin(), filepath.end(), '/', '\\');

        #endif

        std::ifstream file(filepath, std::ios::binary);
        if (!file) DEBUG_ERROR("Can't read file at path : " + filepath);
        std::ostringstream sstream;
        sstream << file.rdbuf();
        return sstream.str();
    }

    std::string Path::WithoutExtension() const
    {
        std::filesystem::path p(full);
        return (p.parent_path() / p.stem()).string();
    }

    bool Path::WriteFile(const std::string &content) const
    {
        std::ofstream out(full, std::ios::trunc); // truncate = overwrite if file exists
        if (!out.is_open()) return false;
    
        try {
            out << content;
        } catch (...) {
            return false;
        }
    
        return true;
    }

    bool Path::AppendToFile(const std::string &content) const
    {
        std::ofstream out(full, std::ios::app);
        if (!out.is_open()) return false;
        try {
            out << content;
        } catch (...) {
            return false;
        }
        return true;
    }

    Type Path::GetExtensionType() const
    {
        std::string ext = GetExtensionString();

        static const std::unordered_map<std::string, Type> extensionMap = {
            // Image formats
            {".png", Type::T_IMAGE}, {".jpg", Type::T_IMAGE}, {".jpeg", Type::T_IMAGE}, {".hdr", Type::T_IMAGE},
            {".bmp", Type::T_IMAGE}, {".gif", Type::T_IMAGE}, {".tga", Type::T_IMAGE},
            // Sound formats
            {".wav", Type::T_SOUND}, {".mp3", Type::T_SOUND}, {".ogg", Type::T_SOUND},
            // Font formats
            {".ttf", Type::T_FONT}, {".otf", Type::T_FONT}, {".fnt", Type::T_FONT},
            // Shader formats
            {".geom", Type::T_SHADER}, {".frag", Type::T_SHADER}, {".vert", Type::T_SHADER},
            {".comp", Type::T_COMPUTE_SHADER},
            // Text formats
            {".txt", Type::T_TEXT}, {".md", Type::T_TEXT},
            // Script formats
            {".cpp", Type::T_SCRIPT}, {".hpp", Type::T_SCRIPT},
            // Model formats
            {".fbx", Type::T_MODEL},
            // Material formats
            {".mat", Type::T_MATERIAL}, {".material", Type::T_MATERIAL},
            // Level formats
            {".level", Type::T_LEVEL}, {".lvl", Type::T_LEVEL},
            // Config formats
            {".json", Type::T_CONFIG}, {".ini", Type::T_CONFIG}, {".cfg", Type::T_CONFIG},
            // Directory (fallback, not based on extension)
            {"<directory>", Type::T_DIRECTORY}  // special case if needed
        };

        // Ensure extension starts with dot
        if (ext.empty() || ext[0] != '.') {
            ext = "." + ext;
        }

        auto it = extensionMap.find(ext);
        if (it != extensionMap.end()) {
            return it->second;
        }

        return Type::T_TEXT;
    }

    bool FileManager::IsPathInside(const Path &parent, const Path &child)
    {
        std::filesystem::path parentPath = std::filesystem::weakly_canonical(parent.GetAbsolutePath());
        std::filesystem::path childPath = std::filesystem::weakly_canonical(child.GetAbsolutePath());

        // Check if childPath starts with parentPath
        auto parentIt = parentPath.begin();
        auto childIt = childPath.begin();

        for (; parentIt != parentPath.end(); ++parentIt, ++childIt) {
            if (childIt == childPath.end() || *parentIt != *childIt)
                return false;
        }
        return true;
    }

    bool FileManager::HasExtension(const Path &path, const std::vector<std::string> &validExtensions)
    {
        std::string ext = path.GetExtensionString();
    
        // Normalize the extension: lowercase and strip leading dot if present
        if (!ext.empty() && ext[0] == '.')
            ext = ext.substr(1);
        
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        for (const auto& valid : validExtensions) {
            std::string validExt = valid;
            if (!validExt.empty() && validExt[0] == '.')
                validExt = validExt.substr(1);
            std::transform(validExt.begin(), validExt.end(), validExt.begin(), ::tolower);

            if (ext == validExt)
                return true;
        }
        return false;
    }

    void FileManager::RenameFile(const Path &oldPath, const Path &newPath)
    {
        std::filesystem::rename(oldPath.full, newPath.full);
    }

    void FileManager::SetEngineResRoot(const Path &path)
    {
        engineResPath = path;
    }

    void FileManager::SetProjectResRoot(const Path &path)
    {
        projectResPath = path;
    }

    void FileManager::SetProjectRoot(const Path &path)
    {
        projectRoot = path;
    }

    Path::Path(const std::string &raw, bool normalize)
    {
        full = normalize ? Normalize(raw) : raw;
    }

    Path operator/(const Path& lhs, const Path& rhs) {
        std::string combined = lhs.full;

        if (!combined.empty() && combined.back() != '/' && combined.back() != '\\') {
            combined += '/';
        }

        combined += rhs.full;

        return Path(combined, true);  // Normalize the result
    }
    
    Path operator/(const Path& lhs, const std::string& rhs) {
        return lhs / Path(rhs);
    }

    Path operator/(const std::string& lhs, const Path& rhs) {
        return Path(lhs) / rhs;
    }

    std::string Path::Normalize(const std::string &raw)
    {
        return std::filesystem::weakly_canonical(raw).string();
    }

    Path Path::RelativeTo(const Path &other) const
    {
        std::filesystem::path from = full;
        std::filesystem::path base = other.full;

        try {

            if (!from.is_absolute() || !base.is_absolute()) {
                DEBUG_ERROR("Couldn't compute " + from.string() + " relative to " + base.string() + " : one of them isn't absolute.");
                return *this;
            }

            if (from.root_path() != base.root_path()) {
                DEBUG_ERROR("Couldn't compute " + from.string() + " relative to " + base.string() + " : they don't have the same root.");
                return *this;
            }

            std::filesystem::path rel = std::filesystem::relative(from, base);

            std::string ret = rel.string();

            std::replace(ret.begin(), ret.end(), '\\', '/');

            return Path(ret, false);
        } catch (const std::filesystem::filesystem_error& e) {
            DEBUG_ERROR("Couldn't compute " + from.string() + " relative to " + base.string() + " : unknown error : "+ e.what());
            return *this;
        }
    }

    std::string Path::GetNativePath() const
    {
        
        std::string filepath = full;

        #if defined(__WIN32__)

        std::replace(filepath.begin(), filepath.end(), '/', '\\');

        #endif

        return filepath;

    }

    std::string Path::GetExtensionString() const
    {
        auto ext = std::filesystem::path(full).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return ext;
    }

    std::string Path::GetAbsolutePath() const
    {
        std::filesystem::path abs = std::filesystem::weakly_canonical(std::filesystem::absolute(full));
        return abs.string();
    }

    std::string Path::GetFilename(bool withExtension) const
    {
        std::filesystem::path p(full);

        if (IsDirectory())
        {
            return p.filename().string();
        }

        return withExtension ? p.filename().string() : p.stem().string();
    }

    std::string Path::GetParent() const
    {
        return std::filesystem::path(full).parent_path().string();
    }

    int Path::GetFileSize() const
    {
        if (IsDirectory()) {
            int totalSize = 0;

            for (const auto& entry : std::filesystem::recursive_directory_iterator(full,
                        std::filesystem::directory_options::skip_permission_denied))
            {
                if (entry.is_regular_file()) {
                    totalSize += std::filesystem::file_size(entry.path());
                }
            }

            return static_cast<int>(totalSize);
        } 
        else if (std::filesystem::exists(full) && std::filesystem::is_regular_file(full)) {
            return static_cast<int>(std::filesystem::file_size(full));
        } 
        else {
            DEBUG_ERROR("File or directory does not exist: " + full);
            return -1;
        }
    }

    Path Path::GetParentPath() const
    {
        return Path(GetParent());
    }

    std::string Path::GetPathInsideArchive() const
    {
        const std::string &fullPath = this->full;
        size_t pos = fullPath.find(".caf");
        if (pos == std::string::npos) {
            DEBUG_ERROR("File is not inside an archive");
        }

        size_t start = pos + 5;
        if (start >= fullPath.size()) return "";
    
        return fullPath.substr(start);
    }

    bool Path::Exists() const
    {
        return std::filesystem::exists(full);
    }

    bool Path::IsDirectory() const
    {
        return std::filesystem::is_directory(full);
    }

    bool Path::IsSubPathOf(const Path &child, const Path &parent)
    {
        auto childStr = std::filesystem::weakly_canonical(child.full).string();
        auto parentStr = std::filesystem::weakly_canonical(parent.full).string();
    
        // Ensure parent path ends with a slash to avoid partial match
        if (parentStr.back() != '/' && parentStr.back() != '\\')
            parentStr += '/';
    
        return childStr.compare(0, parentStr.size(), parentStr) == 0;
    }
}