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

    /// @brief The publishers whose signatures are accepted : a name and an Ed25519 public key each.
    class TrustStore {
    public:
        /// {"publishers": {"acme": "<64 hex digits>"}, "revoked": ["<key hex>"]}
        Core::Result<bool> LoadFile(const std::string& path);
        Core::Result<bool> Merge(const nlohmann::json& json);
        nlohmann::json ToJson() const;
        Core::Result<bool> SaveFile(const std::string& path) const;

        void Trust(const std::string& publisher, const Crypto::PublicKey& key);
        void Revoke(const Crypto::PublicKey& key);
        /// True if `publisher` is trusted with exactly this key and the key was not revoked
        bool IsTrusted(const std::string& publisher, const Crypto::PublicKey& key) const;
        bool IsRevoked(const Crypto::PublicKey& key) const;
        const std::map<std::string, std::string>& Publishers() const { return m_Publishers; }

    private:
        std::map<std::string, std::string> m_Publishers;         // name -> key hex
        std::vector<std::string> m_Revoked;                      // key hex
    };

    /// @brief What a plugin folder says about who made it
    struct SignatureInfo {
        enum class Status : uint8_t {
            Unsigned,           ///< no shard-signature.json
            Valid,              ///< signed, the files are what was signed, the publisher is trusted
            Untrusted,          ///< signed and intact, but the publisher or its key is not in the trust store
            Invalid,            ///< the signature does not match : the files changed, or the signature is forged
            Revoked,            ///< intact, signed with a key that was revoked
        };
        Status status = Status::Unsigned;
        std::string publisher;
        std::string publicKey;
        std::string message;
        std::string digest;         ///< of the files, hex
    };
    const char* SignatureStatusName(SignatureInfo::Status status);

    /// @brief SHA-512 of the listing of the folder's files (relative path, SHA-256 of the bytes), shard-signature.json and the install marker left out.
    /// The same files give the same digest on every machine.
    Core::Result<std::string> ComputePackageDigest(const std::string& folder);

    /// What a signature covers : the plugin's id and version (from its manifest) and the digest of its files
    std::string SignedMessage(const std::string& id, const std::string& version, const std::string& digestHex);

    /// Signs the plugin in `folder` and writes shard-signature.json in it
    Core::Result<bool> SignPackage(const std::string& folder, const std::string& publisher, const Crypto::SecretKey& secretKey);

    /// Checks shard-signature.json against the files
    SignatureInfo VerifyPackage(const std::string& folder, const TrustStore& trust);

    /// A key file : the secret key as hex (64 bytes), one line. Written readable by its owner only.
    Core::Result<bool> WriteKeyPair(const std::string& pathWithoutExtension, const Crypto::SecretKey& secretKey, const Crypto::PublicKey& publicKey);
    Core::Result<Crypto::SecretKey> ReadSecretKey(const std::string& path);

    /// What an install leaves next to the plugin
    struct InstallMarker {
        std::string id;
        std::string version;
        std::string source;         ///< the registry
        std::string archiveSha256;
        std::string publisher;
    };
    Core::Result<bool> WriteInstallMarker(const std::string& folder, const InstallMarker& marker);
    bool ReadInstallMarker(const std::string& folder, InstallMarker& marker);
}
