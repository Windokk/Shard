#include "engine/plugins/package.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Shard::Engine::Plugins {

    namespace fs = std::filesystem;
    using nlohmann::json;

    const char* SignatureStatusName(SignatureInfo::Status s)
    {
        switch (s) {
            case SignatureInfo::Status::Unsigned: return "unsigned";
            case SignatureInfo::Status::Valid: return "valid";
            case SignatureInfo::Status::Untrusted: return "untrusted publisher";
            case SignatureInfo::Status::Invalid: return "invalid";
            case SignatureInfo::Status::Revoked: return "revoked key";
        }
        return "?";
    }

    namespace {
        bool ReadAll(const std::string& path, std::string& out)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in) return false;
            std::stringstream ss;
            ss << in.rdbuf();
            out = ss.str();
            return true;
        }

        bool PublicKeyFromHex(const std::string& hex, Crypto::PublicKey& key)
        {
            std::vector<uint8_t> v;
            if (!Crypto::FromHex(hex, v, 32)) return false;
            std::copy(v.begin(), v.end(), key.begin());
            return true;
        }
    }

    // ---- TrustStore ----

    Core::Result<bool> TrustStore::Merge(const json& j)
    {
        if (!j.is_object()) return Core::Err("a trust file is a JSON object");
        if (j.contains("publishers")) {
            if (!j["publishers"].is_object()) return Core::Err("\"publishers\" maps a name to a key");
            for (auto it = j["publishers"].begin(); it != j["publishers"].end(); ++it) {
                Crypto::PublicKey key;
                if (!it.value().is_string() || !PublicKeyFromHex(it.value().get<std::string>(), key))
                    return Core::Err("the key of publisher '" + it.key() + "' is not 64 hexadecimal digits");
                m_Publishers[it.key()] = Crypto::ToHex(key);
            }
        }
        if (j.contains("revoked") && j["revoked"].is_array()) {
            for (const json& k : j["revoked"]) {
                Crypto::PublicKey key;
                if (k.is_string() && PublicKeyFromHex(k.get<std::string>(), key)) m_Revoked.push_back(Crypto::ToHex(key));
            }
        }
        return Core::Ok(true);
    }

    Core::Result<bool> TrustStore::LoadFile(const std::string& path)
    {
        std::string text;
        if (!ReadAll(path, text)) return Core::Err("cannot read '" + path + "'");
        json j = json::parse(text, nullptr, false);
        if (j.is_discarded()) return Core::Err("'" + path + "' is not valid JSON");
        return Merge(j);
    }

    json TrustStore::ToJson() const
    {
        json j;
        j["publishers"] = json::object();
        for (const auto& [name, key] : m_Publishers) j["publishers"][name] = key;
        j["revoked"] = m_Revoked;
        return j;
    }

    Core::Result<bool> TrustStore::SaveFile(const std::string& path) const
    {
        std::error_code ec;
        fs::create_directories(fs::path(path).parent_path(), ec);
        std::ofstream out(path, std::ios::trunc);
        if (!out) return Core::Err("cannot write '" + path + "'");
        out << ToJson().dump(2) << "\n";
        return Core::Ok(bool(out));
    }

    void TrustStore::Trust(const std::string& publisher, const Crypto::PublicKey& key) { m_Publishers[publisher] = Crypto::ToHex(key); }

    void TrustStore::Revoke(const Crypto::PublicKey& key)
    {
        const std::string hex = Crypto::ToHex(key);
        if (std::find(m_Revoked.begin(), m_Revoked.end(), hex) == m_Revoked.end()) m_Revoked.push_back(hex);
    }

    bool TrustStore::IsRevoked(const Crypto::PublicKey& key) const
    {
        return std::find(m_Revoked.begin(), m_Revoked.end(), Crypto::ToHex(key)) != m_Revoked.end();
    }

    bool TrustStore::IsTrusted(const std::string& publisher, const Crypto::PublicKey& key) const
    {
        if (IsRevoked(key)) return false;
        auto it = m_Publishers.find(publisher);
        return it != m_Publishers.end() && it->second == Crypto::ToHex(key);
    }

    // ---- Digest and signature ----

    Core::Result<std::string> ComputePackageDigest(const std::string& folder)
    {
        std::error_code ec;
        if (!fs::is_directory(folder, ec)) return Core::Err("'" + folder + "' is not a folder");
        std::vector<std::string> files;
        for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec)) continue;
            std::string rel = fs::relative(it->path(), folder, ec).generic_string();
            if (rel == kSignatureFile || rel == kInstallMarkerFile || rel == kExtractedMarkerFile) continue;
            files.push_back(std::move(rel));
        }
        std::sort(files.begin(), files.end());
        Crypto::Sha512 digest;
        for (const std::string& rel : files) {
            const std::string hash = Crypto::Sha256FileHex((fs::path(folder) / rel).string());
            if (hash.empty()) return Core::Err("cannot read '" + rel + "'");
            const std::string line = rel + '\0' + hash + '\n';
            digest.Update(line.data(), line.size());
        }
        return Core::Ok(Crypto::ToHex(digest.Finish()));
    }

    std::string SignedMessage(const std::string& id, const std::string& version, const std::string& digestHex)
    {
        return "shard-plugin-v1\n" + id + "\n" + version + "\n" + digestHex;
    }

    namespace {
        bool ReadIdAndVersion(const std::string& folder, std::string& id, std::string& version)
        {
            std::string text;
            if (!ReadAll(folder + "/shard-plugin.json", text)) return false;
            json j = json::parse(text, nullptr, false);
            if (j.is_discarded() || !j.is_object() || !j.contains("id") || !j["id"].is_string() || !j.contains("version") || !j["version"].is_string()) return false;
            id = j["id"].get<std::string>();
            version = j["version"].get<std::string>();
            return true;
        }
    }

    Core::Result<bool> SignPackage(const std::string& folder, const std::string& publisher, const Crypto::SecretKey& secretKey)
    {
        std::string id, version;
        if (!ReadIdAndVersion(folder, id, version)) return Core::Err("'" + folder + "' has no readable shard-plugin.json with an id and a version");
        if (publisher.empty()) return Core::Err("a signature needs a publisher name");
        auto digest = ComputePackageDigest(folder);
        if (!digest) return Core::Err(digest.GetError().message);
        const std::string message = SignedMessage(id, version, digest.Value());
        const Crypto::Signature sig = Crypto::Sign(message.data(), message.size(), secretKey);
        Crypto::PublicKey pk;
        std::copy(secretKey.begin() + 32, secretKey.end(), pk.begin());
        json j;
        j["schema"] = 1;
        j["publisher"] = publisher;
        j["publicKey"] = Crypto::ToHex(pk);
        j["digest"] = digest.Value();
        j["signature"] = Crypto::ToHex(sig);
        std::ofstream out(folder + "/" + kSignatureFile, std::ios::trunc);
        if (!out) return Core::Err("cannot write the signature in '" + folder + "'");
        out << j.dump(2) << "\n";
        return Core::Ok(bool(out));
    }

    SignatureInfo VerifyPackage(const std::string& folder, const TrustStore& trust)
    {
        SignatureInfo info;
        std::string text;
        if (!ReadAll(folder + "/" + kSignatureFile, text)) {
            info.message = "no " + std::string(kSignatureFile);
            return info;
        }
        auto invalid = [&](std::string why) { info.status = SignatureInfo::Status::Invalid; info.message = std::move(why); return info; };

        json j = json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object()) return invalid("shard-signature.json is not valid JSON");
        info.publisher = j.value("publisher", std::string());
        info.publicKey = j.value("publicKey", std::string());
        const std::string claimed = j.value("digest", std::string());
        const std::string sigHex = j.value("signature", std::string());

        Crypto::PublicKey pk;
        std::vector<uint8_t> sigBytes;
        if (info.publisher.empty() || !PublicKeyFromHex(info.publicKey, pk) || !Crypto::FromHex(sigHex, sigBytes, 64))
            return invalid("shard-signature.json is malformed");
        Crypto::Signature sig;
        std::copy(sigBytes.begin(), sigBytes.end(), sig.begin());

        std::string id, version;
        if (!ReadIdAndVersion(folder, id, version)) return invalid("shard-plugin.json is missing or unreadable");
        auto digest = ComputePackageDigest(folder);
        if (!digest) return invalid(digest.GetError().message);
        info.digest = digest.Value();
        if (digest.Value() != claimed) return invalid("the files are not the ones that were signed");

        const std::string message = SignedMessage(id, version, digest.Value());
        if (!Crypto::Verify(message.data(), message.size(), sig, pk)) return invalid("the signature does not match");

        if (trust.IsRevoked(pk)) {
            info.status = SignatureInfo::Status::Revoked;
            info.message = "the key of '" + info.publisher + "' was revoked";
        } else if (!trust.IsTrusted(info.publisher, pk)) {
            info.status = SignatureInfo::Status::Untrusted;
            info.message = "signed by '" + info.publisher + "' (key " + info.publicKey.substr(0, 16) + "...), which is not in the trust store";
        } else {
            info.status = SignatureInfo::Status::Valid;
            info.message = "signed by '" + info.publisher + "'";
        }
        return info;
    }

    // ---- Keys and markers ----

    Core::Result<bool> WriteKeyPair(const std::string& base, const Crypto::SecretKey& sk, const Crypto::PublicKey& pk)
    {
        std::error_code ec;
        fs::create_directories(fs::path(base).parent_path(), ec);
        {
            std::ofstream out(base + ".key", std::ios::binary | std::ios::trunc);   // binary : the same bytes (LF) on every platform
            if (!out) return Core::Err("cannot write '" + base + ".key'");
            out << Crypto::ToHex(sk) << "\n";
        }
        fs::permissions(base + ".key", fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
        std::ofstream pub(base + ".pub", std::ios::binary | std::ios::trunc);
        if (!pub) return Core::Err("cannot write '" + base + ".pub'");
        pub << Crypto::ToHex(pk) << "\n";
        return Core::Ok(true);
    }

    Core::Result<Crypto::SecretKey> ReadSecretKey(const std::string& path)
    {
        std::string text;
        if (!ReadAll(path, text)) return Core::Err("cannot read '" + path + "'");
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
        std::vector<uint8_t> v;
        if (!Crypto::FromHex(text, v, 64)) return Core::Err("'" + path + "' is not a secret key (128 hexadecimal digits)");
        Crypto::SecretKey sk;
        std::copy(v.begin(), v.end(), sk.begin());
        // The public half has to be the one the seed gives
        Crypto::PublicKey pk;
        Crypto::SecretKey rebuilt;
        std::array<uint8_t, 32> seed;
        std::copy(sk.begin(), sk.begin() + 32, seed.begin());
        Crypto::KeyPairFromSeed(seed, pk, rebuilt);
        if (!std::equal(pk.begin(), pk.end(), sk.begin() + 32)) return Core::Err("'" + path + "' is damaged (its public half is not the seed's)");
        return Core::Ok(sk);
    }

    Core::Result<bool> WriteInstallMarker(const std::string& folder, const InstallMarker& m)
    {
        json j;
        j["id"] = m.id;
        j["version"] = m.version;
        j["source"] = m.source;
        j["sha256"] = m.archiveSha256;
        j["publisher"] = m.publisher;
        std::ofstream out(folder + "/" + kInstallMarkerFile, std::ios::trunc);
        if (!out) return Core::Err("cannot write the install marker in '" + folder + "'");
        out << j.dump(2) << "\n";
        return Core::Ok(bool(out));
    }

    bool ReadInstallMarker(const std::string& folder, InstallMarker& m)
    {
        std::string text;
        if (!ReadAll(folder + "/" + kInstallMarkerFile, text)) return false;
        json j = json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object()) return false;
        m.id = j.value("id", std::string());
        m.version = j.value("version", std::string());
        m.source = j.value("source", std::string());
        m.archiveSha256 = j.value("sha256", std::string());
        m.publisher = j.value("publisher", std::string());
        return true;
    }
}
