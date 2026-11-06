#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <algorithm>
#include <fstream>

#include <nlohmann/json.hpp>

#include "engine/assets/assetID.hpp"

namespace Shard::Engine::Filesystem{

    enum class Type{
        T_IMAGE,
        T_SOUND,
        T_FONT,
        T_SHADER,
        T_COMPUTE_SHADER,
        T_TEXT,
        T_SCRIPT,
        T_WORLD,
        T_MODEL,
        T_MATERIAL,
        T_PROBES,
        T_LIGHTMAP,
        T_CONFIG,
        T_DIRECTORY,
        T_CUSTOM
    };

    bool RegisterCustomExtension(const std::string& extension);
    bool UnregisterCustomExtension(const std::string& extension);
    bool IsCustomExtension(const std::string& extension);

    class Path {

        public:
        std::string full;

        Path() = default;
        Path(const std::string &raw, bool normalize = false);

        static std::string Normalize(const std::string& raw);
    
        Path RelativeTo(const Path& other) const;

        std::string GetNativePath() const;

        std::string GetExtensionString() const;

        std::string ReadFile() const;

        std::string WithoutExtension() const;

        bool WriteFile(const std::string &content) const;

        bool AppendToFile(const std::string &content) const;

        Type GetExtensionType() const;

        std::string GetAbsolutePath() const;

        std::string GetFilename(bool withExtension = true) const;
    
        std::string GetParent() const;

        int GetFileSize() const;

        Path GetParentPath() const;

        std::string GetPathInsideArchive() const;
    
        bool Exists() const;
    
        bool IsDirectory() const;

        static bool IsSubPathOf(const Path& child, const Path& parent);

        bool operator==(const Path& p) const {
            return full == p.full;
        }

        bool operator!=(const Path& p) const {
            return !(*this == p);
        }

        friend Path operator/(const Path& lhs, const Path& rhs);

        friend Path operator/(const std::string& lhs, const Path& rhs);

        friend Path operator/(const Path& lhs, const std::string& rhs);

    };

    struct FileInfos {
        Path path;
        std::string nameInProject = "";
        std::string extension = "";
        std::string name = "";
        Type type;
        bool isDirectory;
        int size;
        AssetID ID;
    };

    struct AssetInfos {
        FileInfos baseInfos;
        std::vector<AssetID> dependencies;
    };

    class FileManager {
        public:

            explicit FileManager(AssetIDManager& assetIDManager) : assetIDs(&assetIDManager) {}

            Path GetCurrentExecutablePath();

            FileInfos GetFileInfos(const Path& path);

            // Gives a file created in-session (e.g. a baked probe volume) an asset ID. The database is otherwise only read at project load.
            AssetID RegisterAsset(const Path& path);

            void Init(Path projectResPath, Path engineResPath, Path projectRoot);
            std::vector<FileInfos> ListDirectory(const Path &path, std::vector<Type> acceptedExtensions, bool includeDirs = false, bool recursive = false);

            bool IsPathInside(const Path& parent, const Path& child);
            bool HasExtension(const Path& path, const std::vector<std::string>& validExtensions);

            void RenameFile(const Path& oldPath, const Path& newPath);

            void SetEngineResRoot(const Path &path);
            void SetProjectResRoot(const Path &path);
            void SetProjectRoot(const Path& path);
            Path GetProjectRoot() { return projectRoot; };
            Path GetEngineResRoot() { return engineResPath; };
            Path GetProjectResRoot() { return projectResPath; };

        private:
            Path engineResPath = Path("");
            Path projectRoot = Path("");
            Path projectResPath = Path("");
            AssetIDManager* assetIDs = nullptr;
    };
}