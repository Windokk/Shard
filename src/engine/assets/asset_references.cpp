#include "asset_references.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "engine/assets/vfs/filesystem.hpp"

namespace Shard::Engine::Assets
{
    namespace fs = std::filesystem;
    using Filesystem::Path;

    namespace {

        // Engine resources have IDs up to 500 (see SerializeAssetDataBase) : never touched here.
        constexpr int kFirstProjectAssetID = 501;

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

        /// `p` as a path in the project resources ("worlds/a.world"). False if outside them.
        bool NameInProject(const fs::path& p, const fs::path& root, std::string& out)
        {
            std::error_code ec;
            fs::path canonical = fs::weakly_canonical(p, ec);
            if(ec) return false;
            fs::path canonicalRoot = fs::weakly_canonical(root, ec);
            if(ec) return false;

            fs::path rel = fs::relative(canonical, canonicalRoot, ec);
            if(ec || rel.empty()) return false;

            std::string s = rel.generic_string();
            if(s == ".." || s.rfind("../", 0) == 0) return false;

            out = s;
            return true;
        }
    }

    bool IsUnderFolder(const std::string& name, const std::string& dirName)
    {
        return name.size() > dirName.size() && name.compare(0, dirName.size(), dirName) == 0 && name[dirName.size()] == '/';
    }

    int RewriteAssetReferences(Filesystem::AssetIDManager& ids, const std::string& oldName, const std::string& newName, bool isDirectory)
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
        for(auto& [id, info] : ids.AssetIDMap){
            if(id.GetAsInt() < kFirstProjectAssetID)
                continue;
            if(info->baseInfos.type != Filesystem::Type::T_WORLD && info->baseInfos.type != Filesystem::Type::T_MATERIAL)
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

    void RemapBuildSettings(Projects::BuildSettings& settings, const Path& projectResources,
                            const std::string& oldName, const std::string& newName, bool isDirectory)
    {
        auto& list = settings.buildIndex;
        const fs::path root(projectResources.full);

        for(size_t i = 0; i < list.size();){
            std::string entry = list[i].full;
            bool absolute = fs::path(entry).is_absolute();

            std::string rel;
            if(absolute){
                if(!NameInProject(fs::path(entry), root, rel)){ ++i; continue; }
            }
            else{
                rel = entry;
                std::replace(rel.begin(), rel.end(), '\\', '/');
            }

            if(rel != oldName && !(isDirectory && IsUnderFolder(rel, oldName))){ ++i; continue; }

            if(newName.empty()){
                list.erase(list.begin() + i);
                continue;
            }

            std::string newRel = newName + rel.substr(oldName.size());
            list[i] = absolute ? Path((root / newRel).string(), true) : Path(newRel);
            ++i;
        }
    }
}
