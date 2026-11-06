#pragma once

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/core/containers/result.hpp"
#include "engine/plugins/crypto.hpp"

namespace Shard::Engine::Plugins {

    inline constexpr const char* kSignatureFile = "shard-signature.json";
    inline constexpr const char* kInstallMarkerFile = ".shard-install.json";
    inline constexpr const char* kExtractedMarkerFile = ".shard-extracted";
inline constexpr const char* kArchiveExtension = ".shardplugin";

    class TrustStore {
    public:
        /// {"publishers": {"acme": "<64 hex digits>"}, "revoked": ["<key hex>"]}
        Core::Result<bool> LoadFile(const std::string& path);
        Core::Result<bool> Merge(const nlohmann::json& json);
        nlohmann::json ToJson() const;
        Core::Result<bool> SaveFile(const std::string& path) const;

        void Trust(const std::string& publisher, const Crypto::PublicKey& key);
        void Revoke(const Crypto::PublicKey& key);
        bool IsTrusted(const std::string& publisher, const Crypto::PublicKey& key) const;
        bool IsRevoked(const Crypto::PublicKey& key) const;
        const std::map<std::string, std::string>& Publishers() const { return m_Publishers; }

    private:
        std::map<std::string, std::string> m_Publishers;
        std::vector<std::string> m_Revoked;
    };

    struct SignatureInfo {
        enum class Status : uint8_t {
            Unsigned,
            Valid,
            Untrusted,
            Invalid,
            Revoked,
        };
        Status status = Status::Unsigned;
        std::string publisher;
        std::string publicKey;
        std::string message;
        std::string digest;
    };
    const char* SignatureStatusName(SignatureInfo::Status status);

    // SHA-512 over the listing of (relative path, SHA-256 of bytes), excluding shard-signature.json and the install marker. Same files give the same digest everywhere.
    Core::Result<std::string> ComputePackageDigest(const std::string& folder);

    std::string SignedMessage(const std::string& id, const std::string& version, const std::string& digestHex);

    Core::Result<bool> SignPackage(const std::string& folder, const std::string& publisher, const Crypto::SecretKey& secretKey);

    SignatureInfo VerifyPackage(const std::string& folder, const TrustStore& trust);

    Core::Result<bool> WriteKeyPair(const std::string& pathWithoutExtension, const Crypto::SecretKey& secretKey, const Crypto::PublicKey& publicKey);
    Core::Result<Crypto::SecretKey> ReadSecretKey(const std::string& path);

    struct InstallMarker {
        std::string id;
        std::string version;
        std::string source;
        std::string archiveSha256;
        std::string publisher;
    };
    Core::Result<bool> WriteInstallMarker(const std::string& folder, const InstallMarker& marker);
    bool ReadInstallMarker(const std::string& folder, InstallMarker& marker);
}
