#include "engine/assets/asset_importer.hpp"

#include <algorithm>
#include <filesystem>

namespace Shard::Engine::Filesystem {

    namespace {
        std::string Lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            return s;
        }
    }

    AssetImporters& AssetImporters::Get()
    {
        static AssetImporters importers;
        return importers;
    }

    void AssetImporters::Register(std::shared_ptr<AssetImporterExtension> importer)
    {
        if(!importer)
            return;
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Importers.push_back(std::move(importer));
    }

    void AssetImporters::Unregister(const std::shared_ptr<AssetImporterExtension>& importer)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Importers.erase(std::remove(m_Importers.begin(), m_Importers.end(), importer), m_Importers.end());
    }

    std::shared_ptr<AssetImporterExtension> AssetImporters::Find(const std::string& path) const
    {
        const std::string ext = Lower(std::filesystem::path(path).extension().string());
        if(ext.empty())
            return nullptr;
        std::lock_guard<std::mutex> lock(m_Mutex);
        for(const auto& importer : m_Importers){
            for(const std::string& e : importer->extensions){
                std::string want = Lower(e);
                if(!want.empty() && want[0] != '.')
                    want = "." + want;
                if(want == ext)
                    return importer;
            }
        }
        return nullptr;
    }

    ImportResult AssetImporters::Import(const std::string& sourcePath, const std::string& outputDirectory) const
    {
        ImportResult result;
        std::error_code ec;
        if(!std::filesystem::is_regular_file(sourcePath, ec)){
            result.message = "'" + sourcePath + "' does not exist";
            return result;
        }
        auto importer = Find(sourcePath);
        if(!importer || !importer->import){
            result.message = "no importer takes '" + std::filesystem::path(sourcePath).extension().string() + "' files";
            return result;
        }
        std::filesystem::create_directories(outputDirectory, ec);
        try{
            result = importer->import(sourcePath, outputDirectory);
        } catch(const std::exception& e){
            result = ImportResult{};
            result.message = std::string("the importer '") + importer->name + "' threw : " + e.what();
        } catch(...){
            result = ImportResult{};
            result.message = std::string("the importer '") + importer->name + "' threw";
        }
        return result;
    }

    std::vector<std::string> AssetImporters::Extensions() const
    {
        std::vector<std::string> out;
        std::lock_guard<std::mutex> lock(m_Mutex);
        for(const auto& importer : m_Importers)
            for(const std::string& e : importer->extensions)
                out.push_back(Lower(e));
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }
}
