#include "engine/plugins/registry.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <fstream>
#include <set>
#include <sstream>

#include "engine/platform/network/sockets/socket.hpp"
#include "engine/plugins/archive.hpp"

namespace Shard::Engine::Plugins {

    namespace fs = std::filesystem;
    using nlohmann::json;
    using Core::Platform::Socket;
    using Core::Platform::SocketAddress;
    using Core::Platform::SocketStatus;
    using Core::Platform::SocketType;

    std::optional<ParsedUrl> ParseUrl(const std::string& url)
    {
        ParsedUrl u;
        const size_t scheme = url.find("://");
        if (scheme == std::string::npos) return std::nullopt;
        u.scheme = url.substr(0, scheme);
        std::transform(u.scheme.begin(), u.scheme.end(), u.scheme.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        std::string rest = url.substr(scheme + 3);
        const size_t slash = rest.find('/');
        std::string authority = slash == std::string::npos ? rest : rest.substr(0, slash);
        u.path = slash == std::string::npos ? "/" : rest.substr(slash);
        const size_t colon = authority.rfind(':');
        if (colon != std::string::npos) {
            u.host = authority.substr(0, colon);
            try {
                const int port = std::stoi(authority.substr(colon + 1));
                if (port <= 0 || port > 65535) return std::nullopt;
                u.port = uint16_t(port);
            } catch (...) { return std::nullopt; }
        } else {
            u.host = authority;
            u.port = u.scheme == "https" ? 443 : 80;
        }
        if (u.host.empty()) return std::nullopt;
        return u;
    }

    namespace {
        std::string Lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            return s;
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

        Core::Result<std::vector<uint8_t>> HttpGet(const ParsedUrl& url, size_t maxBytes, int timeoutMs, int redirects)
        {
            auto socket = Socket::Create(SocketType::TCP);
            if (!socket) return Core::Err("no socket available");
            if (!socket->Connect(SocketAddress{ url.host, url.port }, timeoutMs)) return Core::Err("cannot connect to " + url.host + ":" + std::to_string(url.port));

            const std::string request = "GET " + url.path + " HTTP/1.0\r\nHost: " + url.host + "\r\nUser-Agent: Shard-Plugins/1\r\nAccept: */*\r\nConnection: close\r\n\r\n";
            size_t sentTotal = 0;
            while (sentTotal < request.size()) {
                size_t sent = 0;
                const SocketStatus st = socket->Send(request.data() + sentTotal, request.size() - sentTotal, &sent);
                if (st != SocketStatus::Ok) return Core::Err("cannot send the request to " + url.host);
                sentTotal += sent;
            }

            std::string raw;
            std::vector<char> buffer(1 << 16);
            size_t headerEnd = std::string::npos;
            for (;;) {
                if (!socket->WaitReadable(timeoutMs)) return Core::Err("timed out waiting for " + url.host);
                size_t got = 0;
                const SocketStatus st = socket->Receive(buffer.data(), buffer.size(), &got);
                if (st == SocketStatus::Closed) break;
                if (st != SocketStatus::Ok && st != SocketStatus::WouldBlock) return Core::Err("connection to " + url.host + " failed");
                raw.append(buffer.data(), got);
                if (headerEnd == std::string::npos) headerEnd = raw.find("\r\n\r\n");
                if (raw.size() > maxBytes + 65536) return Core::Err("the response is larger than allowed");
            }
            if (headerEnd == std::string::npos) return Core::Err("malformed response from " + url.host);

            const std::string head = raw.substr(0, headerEnd);
            std::istringstream lines(head);
            std::string statusLine;
            std::getline(lines, statusLine);
            int status = 0;
            {
                const size_t sp = statusLine.find(' ');
                if (sp == std::string::npos) return Core::Err("malformed status line from " + url.host);
                try { status = std::stoi(statusLine.substr(sp + 1, 3)); } catch (...) { return Core::Err("malformed status line from " + url.host); }
            }
            std::string location;
            long long contentLength = -1;
            std::string line;
            while (std::getline(lines, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                const size_t colon = line.find(':');
                if (colon == std::string::npos) continue;
                const std::string name = Lower(line.substr(0, colon));
                std::string value = line.substr(colon + 1);
                while (!value.empty() && value.front() == ' ') value.erase(0, 1);
                if (name == "location") location = value;
                else if (name == "content-length") { try { contentLength = std::stoll(value); } catch (...) {} }
                else if (name == "transfer-encoding" && Lower(value).find("chunked") != std::string::npos) return Core::Err("chunked responses are not supported (the server should answer HTTP/1.0)");
            }

            if (status >= 300 && status < 400 && !location.empty()) {
                if (redirects <= 0) return Core::Err("too many redirects");
                std::string target = location;
                if (target.compare(0, 4, "http") != 0) target = url.scheme + "://" + url.host + ":" + std::to_string(url.port) + (target.empty() || target[0] != '/' ? "/" : "") + target;
                auto next = ParseUrl(target);
                if (!next) return Core::Err("bad redirect '" + location + "'");
                if (next->scheme != "http") return Core::Err("redirected to " + next->scheme + ", which is not supported");
                return HttpGet(*next, maxBytes, timeoutMs, redirects - 1);
            }
            if (status != 200) return Core::Err(url.host + " answered " + std::to_string(status));

            std::vector<uint8_t> body(raw.begin() + long(headerEnd + 4), raw.end());
            if (contentLength >= 0 && body.size() != size_t(contentLength)) return Core::Err("the response from " + url.host + " is truncated");
            if (body.size() > maxBytes) return Core::Err("the response is larger than allowed");
            return Core::Ok(std::move(body));
        }
    }

    Core::Result<std::vector<uint8_t>> FetchBytes(const std::string& source, size_t maxBytes, int timeoutMs)
    {
        if (source.compare(0, 7, "file://") == 0) return ReadFileBytes(source.substr(7), maxBytes);
        if (source.find("://") == std::string::npos) return ReadFileBytes(source, maxBytes);
        auto url = ParseUrl(source);
        if (!url) return Core::Err("'" + source + "' is not a valid URL");
        if (url->scheme == "https") return Core::Err("https is not supported : serve the registry over http (the index and the archives are verified by hash and signature)");
        if (url->scheme != "http") return Core::Err("unsupported URL scheme '" + url->scheme + "'");
        return HttpGet(*url, maxBytes, timeoutMs, 3);
    }

    // ---- Registry ----

    Core::Result<bool> Registry::Load(const std::string& source)
    {
        std::string location = source;
        std::error_code ec;
        if (source.find("://") == std::string::npos && fs::is_directory(source, ec)) location = (fs::path(source) / "index.json").generic_string();
        else if (source.compare(0, 4, "http") == 0 && !source.empty() && source.back() == '/') location = source + "index.json";
        auto bytes = FetchBytes(location, 32u * 1024 * 1024);
        if (!bytes) return Core::Err(bytes.GetError().message);
        json index = json::parse(bytes.Value().begin(), bytes.Value().end(), nullptr, false);
        if (index.is_discarded()) return Core::Err("the index at '" + location + "' is not valid JSON");
        return LoadFromJson(index, location);
    }

    Core::Result<bool> Registry::LoadFromJson(const json& index, const std::string& source)
    {
        if (!index.is_object() || index.value("schema", 0) != 1 || !index.contains("plugins") || !index["plugins"].is_array())
            return Core::Err("'" + source + "' is not a plugin registry index (schema 1)");
        m_Source = source;
        m_Name = index.value("name", std::string());
        m_Versions.clear();
        for (const json& plugin : index["plugins"]) {
            if (!plugin.is_object() || !plugin.contains("id") || !plugin["id"].is_string() || !plugin.contains("versions") || !plugin["versions"].is_array()) continue;
            const std::string id = plugin["id"].get<std::string>();
            if (!IsValidPluginId(id)) continue;
            for (const json& v : plugin["versions"]) {
                if (!v.is_object()) continue;
                RegistryVersion rv;
                rv.id = id;
                rv.name = plugin.value("name", id);
                rv.description = plugin.value("description", std::string());
                auto version = SemVer::Parse(v.value("version", std::string()));
                if (!version || !v.contains("file") || !v["file"].is_string() || !v.contains("sha256") || !v["sha256"].is_string()) continue;
                rv.version = *version;
                rv.file = v["file"].get<std::string>();
                rv.sha256 = Lower(v["sha256"].get<std::string>());
                rv.size = v.value("size", uint64_t(0));
                rv.publisher = v.value("publisher", std::string());
                if (v.contains("engine") && v["engine"].is_string()) {
                    if (auto range = VersionRange::Parse(v["engine"].get<std::string>())) rv.engineVersions = *range;
                }
                if (v.contains("dependencies") && v["dependencies"].is_array()) {
                    for (const json& d : v["dependencies"]) {
                        if (!d.is_object() || !d.contains("id") || !d["id"].is_string()) continue;
                        DependencySpec spec;
                        spec.id = d["id"].get<std::string>();
                        if (d.contains("version") && d["version"].is_string()) {
                            if (auto range = VersionRange::Parse(d["version"].get<std::string>())) spec.range = *range;
                        }
                        spec.optional = d.value("optional", false);
                        rv.dependencies.push_back(std::move(spec));
                    }
                }
                rv.registry = source;
                m_Versions.push_back(std::move(rv));
            }
        }
        return Core::Ok(true);
    }

    std::vector<RegistryVersion> Registry::Search(const std::string& text) const
    {
        const std::string needle = Lower(text);
        std::vector<RegistryVersion> best;
        for (const RegistryVersion& v : m_Versions) {
            if (!needle.empty() && Lower(v.id).find(needle) == std::string::npos && Lower(v.name).find(needle) == std::string::npos &&
                Lower(v.description).find(needle) == std::string::npos) continue;
            auto it = std::find_if(best.begin(), best.end(), [&](const RegistryVersion& b) { return b.id == v.id; });
            if (it == best.end()) best.push_back(v);
            else if (v.version > it->version) *it = v;
        }
        std::sort(best.begin(), best.end(), [](const RegistryVersion& a, const RegistryVersion& b) { return a.id < b.id; });
        return best;
    }

    std::optional<RegistryVersion> Registry::Find(const std::string& id, const VersionRange& range, const SemVer& engine) const
    {
        std::optional<RegistryVersion> best;
        for (const RegistryVersion& v : m_Versions) {
            if (v.id != id || !range.Satisfies(v.version) || !v.engineVersions.Satisfies(engine)) continue;
            if (!best || v.version > best->version) best = v;
        }
        return best;
    }

    std::string Registry::ArchiveLocation(const RegistryVersion& v) const
    {
        if (v.file.find("://") != std::string::npos) return v.file;
        if (m_Source.find("://") != std::string::npos && m_Source.compare(0, 7, "file://") != 0) {
            const size_t slash = m_Source.rfind('/');
            return m_Source.substr(0, slash == std::string::npos ? 0 : slash + 1) + v.file;
        }
        std::string base = m_Source.compare(0, 7, "file://") == 0 ? m_Source.substr(7) : m_Source;
        return (fs::path(base).parent_path() / v.file).generic_string();
    }

    // ---- Installer ----

    Core::Result<std::vector<RegistryVersion>> PluginInstaller::Plan(const std::string& id, const VersionRange& range, const InstallOptions& options) const
    {
        std::vector<RegistryVersion> plan;
        std::set<std::string> visiting;
        std::function<Core::Result<bool>(const std::string&, const VersionRange&)> visit = [&](const std::string& wanted, const VersionRange& wantedRange) -> Core::Result<bool> {
            if (std::find(options.alreadyAvailable.begin(), options.alreadyAvailable.end(), wanted) != options.alreadyAvailable.end()) return Core::Ok(true);
            for (const RegistryVersion& done : plan)
                if (done.id == wanted) {
                    if (!wantedRange.Satisfies(done.version)) return Core::Err("'" + wanted + "' is needed in " + wantedRange.Text() + " and " + done.version.ToString() + " was already chosen");
                    return Core::Ok(true);
                }
            if (!visiting.insert(wanted).second) return Core::Err("dependency cycle through '" + wanted + "'");
            std::optional<RegistryVersion> found;
            for (const Registry& r : m_Registries) {
                auto v = r.Find(wanted, wantedRange, options.engineVersion);
                if (v && (!found || v->version > found->version)) found = v;
            }
            if (!found) {
                visiting.erase(wanted);
                return Core::Err("no registry has '" + wanted + "' " + wantedRange.Text() + " for engine " + options.engineVersion.ToString());
            }
            for (const DependencySpec& d : found->dependencies) {
                if (d.optional || d.id.compare(0, 8, "service:") == 0) continue;
                auto sub = visit(d.id, d.range);
                if (!sub) { visiting.erase(wanted); return sub; }
            }
            visiting.erase(wanted);
            plan.push_back(*found);
            return Core::Ok(true);
        };
        auto result = visit(id, range);
        if (!result) return Core::Err(result.GetError().message);
        return Core::Ok(std::move(plan));
    }

    Core::Result<InstalledPlugin> PluginInstaller::InstallOne(const RegistryVersion& version, const InstallOptions& options) const
    {
        const Registry* source = nullptr;
        for (const Registry& r : m_Registries) if (r.Source() == version.registry) source = &r;
        if (!source) return Core::Err("the registry of '" + version.id + "' is gone");

        const std::string location = source->ArchiveLocation(version);
        auto bytes = FetchBytes(location, 1024ull * 1024 * 1024);
        if (!bytes) return Core::Err("downloading " + version.id + " " + version.version.ToString() + " : " + bytes.GetError().message);
        const auto& data = bytes.Value();
        if (version.size != 0 && data.size() != version.size) return Core::Err(version.id + " : the archive is " + std::to_string(data.size()) + " bytes, the index says " + std::to_string(version.size));
        const std::string hash = Crypto::ToHex(Crypto::Sha256Of(data.data(), data.size()));
        if (hash != version.sha256) return Core::Err(version.id + " : the archive's SHA-256 is " + hash + ", the index says " + version.sha256 + " : not installed");

        auto zip = ZipArchive::OpenMemory(data);
        if (!zip) return Core::Err(version.id + " : " + zip.GetError().message);

        std::error_code ec;
        const fs::path installRoot = fs::absolute(options.installDirectory, ec);
        fs::create_directories(installRoot, ec);
        const fs::path staging = installRoot / (".staging-" + version.id + "-" + hash.substr(0, 12));
        fs::remove_all(staging, ec);
        auto extracted = zip.Value().ExtractAll(staging.string());
        if (!extracted) { fs::remove_all(staging, ec); return Core::Err(version.id + " : " + extracted.GetError().message); }

        // The archive has to hold the plugin it is listed as
        std::string manifestText;
        {
            std::ifstream in(staging / "shard-plugin.json");
            std::stringstream ss;
            ss << in.rdbuf();
            manifestText = ss.str();
        }
        auto manifest = ParseManifestText(manifestText);
        if (!manifest) { fs::remove_all(staging, ec); return Core::Err(version.id + " : its shard-plugin.json is not valid : " + manifest.GetError().message); }
        if (manifest.Value().id != version.id || manifest.Value().version != version.version) {
            fs::remove_all(staging, ec);
            return Core::Err("the archive holds " + manifest.Value().id + " " + manifest.Value().version.ToString() + ", not " + version.id + " " + version.version.ToString());
        }

        const TrustStore empty;
        const SignatureInfo sig = VerifyPackage(staging.string(), options.trust ? *options.trust : empty);
        const bool signedAndTrusted = sig.status == SignatureInfo::Status::Valid;
        if (sig.status == SignatureInfo::Status::Invalid || sig.status == SignatureInfo::Status::Revoked) {
            fs::remove_all(staging, ec);
            return Core::Err(version.id + " : " + sig.message + " : not installed");
        }
        if (!signedAndTrusted && !options.allowUnsigned) {
            fs::remove_all(staging, ec);
            return Core::Err(version.id + " " + version.version.ToString() + " is " + SignatureStatusName(sig.status) + (sig.message.empty() ? "" : " (" + sig.message + ")") +
                             " : not installed. Trust the publisher (a trust file with its key) or allow unsigned installs");
        }

        InstallMarker marker;
        marker.id = version.id;
        marker.version = version.version.ToString();
        marker.source = source->Source();
        marker.archiveSha256 = hash;
        marker.publisher = sig.publisher;
        auto wrote = WriteInstallMarker(staging.string(), marker);
        if (!wrote) { fs::remove_all(staging, ec); return Core::Err(wrote.GetError().message); }

        // Put it in place : the old version goes only once the new one is complete
        const fs::path target = installRoot / version.id;
        const fs::path backup = installRoot / (".replaced-" + version.id);
        fs::remove_all(backup, ec);
        if (fs::exists(target, ec)) {
            fs::rename(target, backup, ec);
            if (ec) { fs::remove_all(staging, ec); return Core::Err("cannot replace the installed " + version.id); }
        }
        fs::rename(staging, target, ec);
        if (ec) {
            if (fs::exists(backup)) fs::rename(backup, target, ec);
            fs::remove_all(staging, ec);
            return Core::Err("cannot move " + version.id + " into place");
        }
        fs::remove_all(backup, ec);

        InstalledPlugin out;
        out.id = version.id;
        out.version = marker.version;
        out.folder = target.generic_string();
        out.source = marker.source;
        out.publisher = marker.publisher;
        return Core::Ok(std::move(out));
    }

    Core::Result<std::vector<InstalledPlugin>> PluginInstaller::Install(const std::string& id, const VersionRange& range, const InstallOptions& options) const
    {
        auto plan = Plan(id, range, options);
        if (!plan) return Core::Err(plan.GetError().message);
        std::vector<InstalledPlugin> installed;
        for (const RegistryVersion& v : plan.Value()) {
            // Already there at this version : nothing to download
            InstallMarker marker;
            const std::string folder = (fs::path(options.installDirectory) / v.id).generic_string();
            if (ReadInstallMarker(folder, marker) && marker.version == v.version.ToString() && marker.archiveSha256 == v.sha256) {
                installed.push_back({ v.id, marker.version, folder, marker.source, marker.publisher });
                continue;
            }
            auto one = InstallOne(v, options);
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
        if (!ReadInstallMarker(folder.string(), marker)) return Core::Err("'" + id + "' was not installed from a registry (nothing is removed that Shard did not put there)");
        std::error_code ec;
        fs::remove_all(folder, ec);
        if (ec) return Core::Err("cannot remove '" + folder.string() + "'");
        return Core::Ok(true);
    }

    std::vector<std::pair<InstalledPlugin, RegistryVersion>> PluginInstaller::FindUpdates(const std::string& installDirectory, const SemVer& engineVersion) const
    {
        std::vector<std::pair<InstalledPlugin, RegistryVersion>> updates;
        for (const InstalledPlugin& installed : ListInstalled(installDirectory)) {
            auto current = SemVer::Parse(installed.version);
            if (!current) continue;
            auto range = VersionRange::Parse("^" + current->ToString());
            if (!range) continue;
            std::optional<RegistryVersion> best;
            for (const Registry& r : m_Registries) {
                auto v = r.Find(installed.id, *range, engineVersion);
                if (v && v->version > *current && (!best || v->version > best->version)) best = v;
            }
            if (best) updates.emplace_back(installed, *best);
        }
        return updates;
    }
}
