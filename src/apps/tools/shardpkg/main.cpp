#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/plugins/archive.hpp"
#include "engine/plugins/crypto.hpp"
#include "engine/plugins/fingerprint.hpp"
#include "engine/plugins/manifest.hpp"
#include "engine/plugins/package.hpp"

namespace fs = std::filesystem;
using namespace Shard::Engine::Plugins;
using nlohmann::json;

namespace {

    void Usage()
    {
        std::cout <<
            "shardpkg keygen <name> [--out <folder>]\n"
            "shardpkg pack <folder> [--out <file>]\n"
            "shardpkg sign <folder> --publisher <name> --key <file>\n"
            "shardpkg verify <folder|archive> [--trust <file>]\n"
            "shardpkg sums <folder> [--out <file>]\n"
            "shardpkg fingerprint\n";
    }

    const char* Option(const std::vector<std::string>& args, const char* name)
    {
        for (size_t i = 0; i + 1 < args.size(); ++i)
            if (args[i] == name) return args[i + 1].c_str();
        return nullptr;
    }

    bool ReadFile(const std::string& path, std::string& out)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        std::stringstream ss;
        ss << in.rdbuf();
        out = ss.str();
        return true;
    }

    int Fail(const std::string& message)
    {
        std::cerr << "[ERROR] " << message << std::endl;
        return 1;
    }

    int KeyGen(const std::vector<std::string>& args)
    {
        if (args.empty()) return Fail("keygen needs a name");
        const char* out = Option(args, "--out");
        const std::string base = (out ? std::string(out) : std::string(".")) + "/" + args[0];
        std::array<uint8_t, 32> seed;
        if (!Crypto::RandomSeed(seed)) return Fail("the system gave no random numbers");
        Crypto::PublicKey pk;
        Crypto::SecretKey sk;
        Crypto::KeyPairFromSeed(seed, pk, sk);
        auto wrote = WriteKeyPair(base, sk, pk);
        if (!wrote) return Fail(wrote.GetError().message);
        std::cout << "wrote " << base << ".key (secret : keep it private) and " << base << ".pub\n"
                  << "public key : " << Crypto::ToHex(pk) << "\n"
                  << "people who trust you add it to their plugin-trust.json : { \"publishers\": { \"" << args[0] << "\": \"" << Crypto::ToHex(pk) << "\" } }\n";
        return 0;
    }

    int Pack(const std::vector<std::string>& args)
    {
        if (args.empty()) return Fail("pack needs a plugin folder");
        std::string text;
        if (!ReadFile(args[0] + "/shard-plugin.json", text)) return Fail("no shard-plugin.json in " + args[0]);
        auto manifest = ParseManifestText(text);
        if (!manifest) return Fail("shard-plugin.json :\n" + manifest.GetError().message);
        const char* outOpt = Option(args, "--out");
        const std::string out = outOpt ? outOpt : manifest.Value().id + "-" + manifest.Value().version.ToString() + kArchiveExtension;
        ZipWriter zip;
        auto added = zip.AddDirectory(args[0], "", { kExtractedMarkerFile, kInstallMarkerFile });
        if (!added) return Fail(added.GetError().message);
        auto saved = zip.Save(out);
        if (!saved) return Fail(saved.GetError().message);
        const SignatureInfo sig = VerifyPackage(args[0], TrustStore());
        std::cout << "packed " << added.Value() << " files into " << out << " (" << SignatureStatusName(sig.status) << ")\n";
        if (sig.status == SignatureInfo::Status::Unsigned) std::cout << "note : unsigned. Sign it first (shardpkg sign) if it is meant for a registry\n";
        return 0;
    }

    int Sign(const std::vector<std::string>& args)
    {
        if (args.empty()) return Fail("sign needs a plugin folder");
        const char* publisher = Option(args, "--publisher");
        const char* keyFile = Option(args, "--key");
        if (!publisher || !keyFile) return Fail("sign needs --publisher <name> and --key <file>");
        auto key = ReadSecretKey(keyFile);
        if (!key) return Fail(key.GetError().message);
        auto signedOk = SignPackage(args[0], publisher, key.Value());
        if (!signedOk) return Fail(signedOk.GetError().message);
        std::cout << "signed " << args[0] << " as '" << publisher << "'\n";
        return 0;
    }

    int Verify(const std::vector<std::string>& args)
    {
        if (args.empty()) return Fail("verify needs a plugin folder or archive");
        TrustStore trust;
        if (const char* trustFile = Option(args, "--trust")) {
            auto loaded = trust.LoadFile(trustFile);
            if (!loaded) return Fail(loaded.GetError().message);
        }
        std::string folder = args[0];
        fs::path temp;
        if (fs::path(args[0]).extension() == kArchiveExtension) {
            auto zip = ZipArchive::Open(args[0]);
            if (!zip) return Fail(zip.GetError().message);
            temp = fs::temp_directory_path() / ("shardpkg_verify_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            auto extracted = zip.Value().ExtractAll(temp.string());
            if (!extracted) return Fail(extracted.GetError().message);
            folder = temp.string();
        }
        const SignatureInfo sig = VerifyPackage(folder, trust);
        std::error_code ec;
        if (!temp.empty()) fs::remove_all(temp, ec);
        std::cout << SignatureStatusName(sig.status);
        if (!sig.message.empty()) std::cout << " : " << sig.message;
        std::cout << "\n";
        return (sig.status == SignatureInfo::Status::Valid) ? 0 : (sig.status == SignatureInfo::Status::Untrusted ? 0 : 2);
    }

    int Sums(const std::vector<std::string>& args)
    {
        if (args.empty()) return Fail("usage : shardpkg sums <folder> [--out <file>]");
        std::vector<fs::path> archives;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(args[0], ec), end; it != end; it.increment(ec)) {
            if (ec) break;
            if (it->is_regular_file(ec) && it->path().extension() == kArchiveExtension) archives.push_back(it->path());
        }
        if (archives.empty()) return Fail("no " + std::string(kArchiveExtension) + " in " + args[0]);
        std::sort(archives.begin(), archives.end());
        std::ostringstream text;
        for (const fs::path& archive : archives) {
            std::string bytes;
            if (!ReadFile(archive.string(), bytes)) return Fail("cannot read " + archive.string());
            const std::string hash = Crypto::ToHex(Crypto::Sha256Of(bytes.data(), bytes.size()));
            text << hash << "  " << fs::relative(archive, args[0], ec).generic_string() << "\n";
        }
        const std::string outPath = Option(args, "--out") ? Option(args, "--out") : (fs::path(args[0]) / "SHA256SUMS").string();
        std::ofstream out(outPath, std::ios::trunc | std::ios::binary);
        if (!out) return Fail("cannot write " + outPath);
        out << text.str();
        std::cout << "wrote " << outPath << " (" << archives.size() << " archives)\n";
        return 0;
    }
}

int main(int argc, char** argv)
{
    if (argc < 2 || std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0) {
        Usage();
        return argc < 2 ? 1 : 0;
    }
    const std::string command = argv[1];
    std::vector<std::string> args(argv + 2, argv + argc);
    if (command == "keygen") return KeyGen(args);
    if (command == "pack") return Pack(args);
    if (command == "sign") return Sign(args);
    if (command == "verify") return Verify(args);
    if (command == "sums") return Sums(args);
    if (command == "fingerprint") { std::cout << kBuildFingerprint << "\n"; return 0; }
    std::cerr << "[ERROR] Unknown command : " << command << "\n";
    Usage();
    return 1;
}
