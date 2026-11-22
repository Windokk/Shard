#include "engine/plugins/installer.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>

#include "engine/plugins/archive.hpp"

namespace Shard::Engine::Plugins {

    namespace fs = std::filesystem;

    namespace {
        constexpr size_t kMaxArchiveBytes = 1024u * 1024 * 1024;

        std::string Lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            return s;
        }

        bool HasArchiveExtension(const std::string& path)
        {
            const std::string ext = kArchiveExtension;
            return path.size() > ext.size() && Lower(path.substr(path.size() - ext.size())) == ext;
        }

        Core::Result<std::vector<uint8_t>> ReadFileBytes(const std::string& path, size_t maxBytes)
        {
            std::ifstream in(path, std::ios::binary | std::ios::ate);
            if (!in) return Core::Err("cannot read '" + path + "'");
            const std::streamsize size = in.tellg();
            if (size < 0 || size_t(size) > maxBytes) return Core::Err("'" + path + "' is larger than allowed");
            in.seekg(0);
            std::vector<uint8_t> bytes;
            bytes.resize(size_t(size));
            in.read(reinterpret_cast<char*>(bytes.data()), size);
            return Core::Ok(std::move(bytes));
        }

        Core::Result<PluginManifest> ManifestOfArchive(const ZipArchive& zip)
        {
            auto text = zip.Read("shard-plugin.json");
            if (!text) return Core::Err("it has no shard-plugin.json at its root");
            auto manifest = ParseManifestText(std::string(text.Value().begin(), text.Value().end()));
            if (!manifest) return Core::Err("its shard-plugin.json is not valid : " + manifest.GetError().message);
            return manifest;
        }

        Core::Result<bool> CheckCompatible(const PluginManifest& manifest, const InstallOptions& options)
        {
            if (manifest.apiMajor != kPluginApiMajor)
                return Core::Err(manifest.id + " " + manifest.version.ToString() + " is built for plugin API " + std::to_string(manifest.apiMajor) + ", this engine has " + std::to_string(kPluginApiMajor));
            if (!manifest.engineVersions.Satisfies(options.engineVersion))
                return Core::Err(manifest.id + " " + manifest.version.ToString() + " needs engine " + manifest.engineVersions.Text() + ", this is " + options.engineVersion.ToString());
            return Core::Ok(true);
        }

        Core::Result<bool> CheckBinaryForPlatform(const PluginManifest& manifest)
        {
            const std::string key = manifest.abi == "wasm1" ? std::string("wasm") : CurrentPlatformKey();
            if (manifest.binaries.empty() || manifest.binaries.count(key)) return Core::Ok(true);
            return Core::Err(manifest.id + " " + manifest.version.ToString() + " has no binary for " + CurrentPlatformKey());
        }

        Core::Result<InstalledPlugin> InstallBytes(const std::vector<uint8_t>& data, const std::string& sourceLabel, const InstallOptions& options)
        {
            const std::string hash = Crypto::ToHex(Crypto::Sha256Of(data.data(), data.size()));
            if (!options.expectedSha256.empty() && Lower(options.expectedSha256) != hash)
                return Core::Err(sourceLabel + " : its SHA-256 is " + hash + ", expected " + Lower(options.expectedSha256) + " : not installed");

            auto zip = ZipArchive::OpenMemory(data);
            if (!zip) return Core::Err(sourceLabel + " : " + zip.GetError().message);

            auto manifest = ManifestOfArchive(zip.Value());
            if (!manifest) return Core::Err(sourceLabel + " : " + manifest.GetError().message);
            const PluginManifest& m = manifest.Value();
            if (!IsValidPluginId(m.id)) return Core::Err(sourceLabel + " : '" + m.id + "' is not a plugin id");
            auto compatible = CheckCompatible(m, options);
            if (!compatible) return Core::Err(compatible.GetError().message);
            auto binary = CheckBinaryForPlatform(m);
            if (!binary) return Core::Err(binary.GetError().message);

            std::error_code ec;
            const fs::path installRoot = fs::absolute(options.installDirectory, ec);
            fs::create_directories(installRoot, ec);
            const fs::path staging = installRoot / (".staging-" + m.id + "-" + hash.substr(0, 12));
            fs::remove_all(staging, ec);
            auto extracted = zip.Value().ExtractAll(staging.string());
            if (!extracted) { fs::remove_all(staging, ec); return Core::Err(m.id + " : " + extracted.GetError().message); }

            const TrustStore empty;
            const SignatureInfo sig = VerifyPackage(staging.string(), options.trust ? *options.trust : empty);
            if (sig.status == SignatureInfo::Status::Invalid || sig.status == SignatureInfo::Status::Revoked) {
                fs::remove_all(staging, ec);
                return Core::Err(m.id + " : " + sig.message + " : not installed");
            }
            if (sig.status != SignatureInfo::Status::Valid && !options.allowUnsigned) {
                fs::remove_all(staging, ec);
                return Core::Err(m.id + " " + m.version.ToString() + " is " + SignatureStatusName(sig.status) + (sig.message.empty() ? "" : " (" + sig.message + ")") +
                                 " : not installed. Trust the publisher (a trust file with its key) or allow unsigned installs");
            }

            InstallMarker marker;
            marker.id = m.id;
            marker.version = m.version.ToString();
            marker.source = sourceLabel;
            marker.archiveSha256 = hash;
            marker.publisher = sig.publisher;
            auto wrote = WriteInstallMarker(staging.string(), marker);
            if (!wrote) { fs::remove_all(staging, ec); return Core::Err(wrote.GetError().message); }

            const fs::path target = installRoot / m.id;
            const fs::path backup = installRoot / (".replaced-" + m.id);
            fs::remove_all(backup, ec);
            if (fs::exists(target, ec)) {
                fs::rename(target, backup, ec);
                if (ec) { fs::remove_all(staging, ec); return Core::Err("cannot replace the installed " + m.id); }
            }
            fs::rename(staging, target, ec);
            if (ec) {
                if (fs::exists(backup)) fs::rename(backup, target, ec);
                fs::remove_all(staging, ec);
                return Core::Err("cannot move " + m.id + " into place");
            }
            fs::remove_all(backup, ec);

            InstalledPlugin out;
            out.id = m.id;
            out.version = marker.version;
            out.folder = target.generic_string();
            out.source = marker.source;
            out.publisher = marker.publisher;
            return Core::Ok(std::move(out));
        }

        Core::Result<std::vector<uint8_t>> BundleEntryBytes(const BundleEntry& entry)
        {
            if (entry.archiveEntry.empty()) return ReadFileBytes(entry.location, kMaxArchiveBytes);
            auto zip = ZipArchive::Open(entry.location);
            if (!zip) return Core::Err(entry.location + " : " + zip.GetError().message);
            return zip.Value().Read(entry.archiveEntry);
        }

        std::string EntryLabel(const BundleEntry& entry)
        {
            return entry.archiveEntry.empty() ? entry.location : entry.location + "!" + entry.archiveEntry;
        }

        Core::Result<bool> AddEntry(std::map<std::string, BundleEntry>& entries, const std::vector<uint8_t>& bytes, const std::string& location, const std::string& archiveEntry, const InstallOptions& options)
        {
            auto zip = ZipArchive::OpenMemory(bytes);
            if (!zip) return Core::Err(location + " : " + zip.GetError().message);
            auto manifest = ManifestOfArchive(zip.Value());
            if (!manifest) return Core::Err(location + " : " + manifest.GetError().message);
            const PluginManifest& m = manifest.Value();
            if (!IsValidPluginId(m.id)) return Core::Err(location + " : '" + m.id + "' is not a plugin id");
            if (!CheckCompatible(m, options) || !CheckBinaryForPlatform(m)) return Core::Ok(false);
            auto it = entries.find(m.id);
            if (it != entries.end() && !(m.version > it->second.version)) return Core::Ok(true);
            BundleEntry entry;
            entry.id = m.id;
            entry.name = m.name;
            entry.version = m.version;
            entry.location = location;
            entry.archiveEntry = archiveEntry;
            entry.dependencies = m.dependencies;
            entries[m.id] = std::move(entry);
            return Core::Ok(true);
        }
    }

    Core::Result<InstalledPlugin> PluginInstaller::InstallArchive(const std::string& archivePath, const InstallOptions& options)
    {
        if (options.installDirectory.empty()) return Core::Err("there is no plugins folder to install into");
        auto bytes = ReadFileBytes(archivePath, kMaxArchiveBytes);
        if (!bytes) return Core::Err(bytes.GetError().message);
        return InstallBytes(bytes.Value(), archivePath, options);
    }

    Core::Result<std::vector<BundleEntry>> PluginInstaller::ReadBundle(const std::string& bundlePath, const InstallOptions& options)
    {
        std::map<std::string, BundleEntry> entries;
        std::error_code ec;
        if (fs::is_directory(bundlePath, ec)) {
            for (fs::recursive_directory_iterator it(bundlePath, ec), end; it != end; it.increment(ec)) {
                if (ec) break;
                if (!it->is_regular_file(ec) || !HasArchiveExtension(it->path().string())) continue;
                auto bytes = ReadFileBytes(it->path().string(), kMaxArchiveBytes);
                if (!bytes) return Core::Err(bytes.GetError().message);
                auto added = AddEntry(entries, bytes.Value(), it->path().generic_string(), {}, options);
                if (!added) return Core::Err(added.GetError().message);
            }
        } else if (fs::is_regular_file(bundlePath, ec)) {
            auto zip = ZipArchive::Open(bundlePath);
            if (!zip) return Core::Err(bundlePath + " : " + zip.GetError().message);
            for (const ZipArchive::Entry& entry : zip.Value().Entries()) {
                if (entry.isDirectory || !HasArchiveExtension(entry.path)) continue;
                auto bytes = zip.Value().Read(entry);
                if (!bytes) return Core::Err(bundlePath + " : " + bytes.GetError().message);
                auto added = AddEntry(entries, bytes.Value(), bundlePath, entry.path, options);
                if (!added) return Core::Err(added.GetError().message);
            }
        } else {
            return Core::Err("'" + bundlePath + "' is neither a folder nor an archive");
        }
        if (entries.empty()) return Core::Err("'" + bundlePath + "' holds no " + kArchiveExtension + " for this engine and platform");
        std::vector<BundleEntry> out;
        for (auto& [id, entry] : entries) out.push_back(std::move(entry));
        return Core::Ok(std::move(out));
    }

    Core::Result<std::vector<BundleEntry>> PluginInstaller::PlanBundle(const std::string& bundlePath, const std::vector<std::string>& only, const InstallOptions& options)
    {
        auto bundle = ReadBundle(bundlePath, options);
        if (!bundle) return bundle;
        std::map<std::string, const BundleEntry*> byId;
        for (const BundleEntry& e : bundle.Value()) byId[e.id] = &e;

        std::vector<BundleEntry> plan;
        std::set<std::string> done, visiting;
        std::function<Core::Result<bool>(const std::string&, const VersionRange&, const std::string&)> visit =
            [&](const std::string& id, const VersionRange& range, const std::string& requiredBy) -> Core::Result<bool> {
            if (std::find(options.alreadyAvailable.begin(), options.alreadyAvailable.end(), id) != options.alreadyAvailable.end()) return Core::Ok(true);
            auto it = byId.find(id);
            if (it == byId.end()) {
                for (const InstalledPlugin& installed : ListInstalled(options.installDirectory)) {
                    auto version = SemVer::Parse(installed.version);
                    if (installed.id == id && version && range.Satisfies(*version)) return Core::Ok(true);
                }
                return Core::Err("'" + id + "' " + range.Text() + (requiredBy.empty() ? " is not in the bundle" : " is needed by " + requiredBy + " and is neither in the bundle nor installed"));
            }
            const BundleEntry& entry = *it->second;
            if (!range.Satisfies(entry.version)) return Core::Err("'" + id + "' is needed in " + range.Text() + " by " + requiredBy + ", the bundle has " + entry.version.ToString());
            if (done.count(id)) return Core::Ok(true);
            if (!visiting.insert(id).second) return Core::Err("dependency cycle through '" + id + "'");
            for (const DependencySpec& d : entry.dependencies) {
                if (d.optional || d.id.compare(0, 8, "service:") == 0) continue;
                auto sub = visit(d.id, d.range, id);
                if (!sub) { visiting.erase(id); return sub; }
            }
            visiting.erase(id);
            done.insert(id);
            plan.push_back(entry);
            return Core::Ok(true);
        };

        if (only.empty()) {
            for (const BundleEntry& e : bundle.Value()) {
                auto visited = visit(e.id, VersionRange::Any(), {});
                if (!visited) return Core::Err(visited.GetError().message);
            }
        } else {
            for (const std::string& id : only) {
                auto visited = visit(id, VersionRange::Any(), {});
                if (!visited) return Core::Err(visited.GetError().message);
            }
        }
        return Core::Ok(std::move(plan));
    }

    Core::Result<std::vector<InstalledPlugin>> PluginInstaller::InstallBundle(const std::string& bundlePath, const std::vector<std::string>& only, const InstallOptions& options)
    {
        if (options.installDirectory.empty()) return Core::Err("there is no plugins folder to install into");
        auto plan = PlanBundle(bundlePath, only, options);
        if (!plan) return Core::Err(plan.GetError().message);
        std::vector<InstalledPlugin> installed;
        InstallOptions each = options;
        each.expectedSha256.clear();
        for (const BundleEntry& entry : plan.Value()) {
            auto bytes = BundleEntryBytes(entry);
            if (!bytes) return Core::Err(bytes.GetError().message);
            const std::string hash = Crypto::ToHex(Crypto::Sha256Of(bytes.Value().data(), bytes.Value().size()));
            const std::string folder = (fs::path(options.installDirectory) / entry.id).generic_string();
            InstallMarker marker;
            if (ReadInstallMarker(folder, marker) && marker.archiveSha256 == hash) {
                installed.push_back({ entry.id, marker.version, folder, marker.source, marker.publisher });
                continue;
            }
            auto one = InstallBytes(bytes.Value(), EntryLabel(entry), each);
            if (!one) return Core::Err(one.GetError().message);
            installed.push_back(std::move(one.Value()));
        }
        return Core::Ok(std::move(installed));
    }

    std::vector<InstalledPlugin> PluginInstaller::ListInstalled(const std::string& installDirectory)
    {
        std::vector<InstalledPlugin> out;
        std::error_code ec;
        if (!fs::is_directory(installDirectory, ec)) return out;
        for (fs::directory_iterator it(installDirectory, ec), end; it != end; it.increment(ec)) {
            if (ec) break;
            if (!it->is_directory(ec)) continue;
            InstallMarker m;
            if (!ReadInstallMarker(it->path().string(), m)) continue;
            out.push_back({ m.id, m.version, it->path().generic_string(), m.source, m.publisher });
        }
        std::sort(out.begin(), out.end(), [](const InstalledPlugin& a, const InstalledPlugin& b) { return a.id < b.id; });
        return out;
    }

    Core::Result<bool> PluginInstaller::Uninstall(const std::string& installDirectory, const std::string& id)
    {
        if (!IsValidPluginId(id)) return Core::Err("'" + id + "' is not a plugin id");
        const fs::path folder = fs::path(installDirectory) / id;
        InstallMarker marker;
        if (!ReadInstallMarker(folder.string(), marker)) return Core::Err("'" + id + "' was not installed by Shard (nothing is removed that Shard did not put there)");
        std::error_code ec;
        fs::remove_all(folder, ec);
        if (ec) return Core::Err("cannot remove '" + folder.string() + "'");
        return Core::Ok(true);
    }
}
