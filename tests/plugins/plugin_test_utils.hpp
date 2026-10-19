#pragma once

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

namespace PluginTest {

    /// A folder under the system's temp directory, removed with the object
    class TempDir {
    public:
        TempDir()
        {
            static std::atomic<int> counter{ 0 };
            m_Path = std::filesystem::temp_directory_path() /
                     ("shard_plugin_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" + std::to_string(counter++));
            std::filesystem::create_directories(m_Path);
        }
        ~TempDir()
        {
            std::error_code ec;
            std::filesystem::remove_all(m_Path, ec);
        }
        TempDir(const TempDir&) = delete;
        TempDir& operator=(const TempDir&) = delete;

        std::string Path() const { return m_Path.generic_string(); }
        std::string Sub(const std::string& relative) const { return (m_Path / relative).generic_string(); }

        void Write(const std::string& relative, const std::string& content) const
        {
            const std::filesystem::path file = m_Path / relative;
            std::filesystem::create_directories(file.parent_path());
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out << content;
        }

        std::string Read(const std::string& relative) const
        {
            std::ifstream in(m_Path / relative, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        }

    private:
        std::filesystem::path m_Path;
    };

    /// A minimal manifest, with the fields a test wants to change
    inline nlohmann::json Manifest(const std::string& id, const std::string& version = "1.0.0")
    {
        nlohmann::json m;
        m["schema"] = 1;
        m["id"] = id;
        m["version"] = version;
        m["kind"] = "plugin";
        m["linkage"] = "static";
        m["hosts"] = nlohmann::json::array({ "engine", "tool" });
        return m;
    }

    inline std::string DatapackManifest(const std::string& id, const std::string& version = "1.0.0", nlohmann::json extra = nlohmann::json::object())
    {
        nlohmann::json m = Manifest(id, version);
        m["kind"] = "module";
        m["moduleKind"] = "datapack";
        m["linkage"] = "dynamic";
        m["tier"] = 3;
        for (auto it = extra.begin(); it != extra.end(); ++it) m[it.key()] = it.value();
        return m.dump(2);
    }
}
