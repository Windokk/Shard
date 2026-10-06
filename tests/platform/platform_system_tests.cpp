#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>

#include "engine/platform/crash/crash_handler.hpp"
#include "engine/platform/filesystem/async_io.hpp"
#include "engine/platform/filesystem/file_watcher.hpp"
#include "engine/platform/filesystem/paths.hpp"
#include "engine/platform/lib_loader/dynlib.hpp"
#include "engine/platform/lib_loader/module_loader.hpp"
#include "engine/platform/network/pipes/ipc.hpp"
#include "engine/platform/network/sockets/socket.hpp"
#include "engine/platform/process/process.hpp"

using namespace Shard::Engine::Core::Platform;
namespace fs = std::filesystem;

namespace {
    // A fresh empty directory in the temp folder, removed with the object
    struct TempDir {
        std::string path;
        TempDir()
        {
            static int counter = 0;
            const fs::path dir = fs::temp_directory_path() / ("shard_platform_test_" + std::to_string(CurrentProcessId()) + "_" + std::to_string(counter++));
            fs::remove_all(dir);
            fs::create_directories(dir);
            path = Paths::Normalize(dir.string());
        }
        ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
    };
}

// --- paths ---

TEST(PlatformPaths, NormalizeAndToNativeRoundTrip)
{
    EXPECT_EQ(Paths::Normalize("a\\b\\c"), "a/b/c");
    EXPECT_EQ(Paths::Normalize("a/b/"), "a/b");
    EXPECT_EQ(Paths::Normalize("/"), "/");
#if defined(_WIN32)
    EXPECT_EQ(Paths::ToNative("a/b/c"), "a\\b\\c");
#else
    EXPECT_EQ(Paths::ToNative("a/b/c"), "a/b/c");
#endif
    EXPECT_EQ(Paths::Normalize(Paths::ToNative("x/y/z")), "x/y/z");
}

TEST(PlatformPaths, UserDirectoriesExistAndAreNamedAfterTheApp)
{
    for (const std::string& dir : { Paths::UserConfigDirectory("ShardPlatformTest"), Paths::UserDataDirectory("ShardPlatformTest"), Paths::UserCacheDirectory("ShardPlatformTest") })
    {
        EXPECT_FALSE(dir.empty());
        EXPECT_NE(dir.find("ShardPlatformTest"), std::string::npos);
        EXPECT_TRUE(fs::is_directory(dir));
    }
    std::error_code ec;
    fs::remove_all(Paths::UserConfigDirectory("ShardPlatformTest"), ec);
    fs::remove_all(Paths::UserDataDirectory("ShardPlatformTest"), ec);
    fs::remove_all(Paths::UserCacheDirectory("ShardPlatformTest"), ec);
}

TEST(PlatformPaths, ExecutableDirectoryHoldsTheExecutable)
{
    const std::string exe = ExecutablePath();
    ASSERT_FALSE(exe.empty());
    EXPECT_TRUE(fs::exists(exe));
    EXPECT_EQ(Paths::ExecutableDirectory(), ExecutableDirectory());
    EXPECT_EQ(exe.find('\\'), std::string::npos);
}

// --- async io ---

TEST(PlatformAsyncIO, WriteThenReadReturnsTheSameBytes)
{
    TempDir dir;
    const std::string path = dir.path + "/data.bin";

    std::vector<uint8_t> bytes(100000);
    for (size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<uint8_t>(i * 31);

    ASSERT_TRUE(AsyncIO::WriteFile(path, bytes).get().ok);
    const IOResult read = AsyncIO::ReadFile(path).get();
    ASSERT_TRUE(read.ok) << read.error;
    EXPECT_EQ(read.data, bytes);
}

TEST(PlatformAsyncIO, ReadingAMissingFileReportsTheError)
{
    TempDir dir;
    const IOResult result = AsyncIO::ReadFile(dir.path + "/missing.bin").get();
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.empty());
}

TEST(PlatformAsyncIO, ManyRequestsInFlightAllComplete)
{
    TempDir dir;
    std::vector<std::future<IOResult>> writes;
    for (int i = 0; i < 50; ++i)
        writes.push_back(AsyncIO::WriteFile(dir.path + "/f" + std::to_string(i), std::vector<uint8_t>(1000, static_cast<uint8_t>(i))));
    for (auto& w : writes)
        EXPECT_TRUE(w.get().ok);
    for (int i = 0; i < 50; ++i)
        EXPECT_EQ(AsyncIO::ReadFile(dir.path + "/f" + std::to_string(i)).get().data.front(), static_cast<uint8_t>(i));
}

namespace {
    // Every AsyncIO test also runs on the generic backend, so that both behave the same
    struct BackendGuard {
        explicit BackendGuard(bool threadPool) { threadPool ? AsyncIO::UseThreadPoolBackend() : AsyncIO::UseNativeBackend(); }
        ~BackendGuard() { AsyncIO::UseNativeBackend(); }
    };

    std::vector<uint8_t> Pattern(size_t size)
    {
        std::vector<uint8_t> bytes(size);
        for (size_t i = 0; i < size; ++i)
            bytes[i] = static_cast<uint8_t>((i * 131) ^ (i >> 8));
        return bytes;
    }
}

TEST(PlatformAsyncIO, NativeBackendIsUsedWhereThereIsOne)
{
    BackendGuard guard(false);
    const std::string name = AsyncIO::BackendName();
#if defined(_WIN32)
    EXPECT_EQ(name, "IOCP");
#else
    EXPECT_TRUE(name == "io_uring" || name == "thread pool") << name;
#endif
    AsyncIO::UseThreadPoolBackend();
    EXPECT_STREQ(AsyncIO::BackendName(), "thread pool");
}

TEST(PlatformAsyncIO, LargeFilesAreSplitInChunksAndComeBackIntact)
{
    for (const bool threadPool : { false, true })
    {
        BackendGuard guard(threadPool);
        TempDir dir;
        const std::string path = dir.path + "/large.bin";
        const std::vector<uint8_t> bytes = Pattern(5 * 1024 * 1024 + 123);     // a few 1 MB chunks and a partial one

        ASSERT_TRUE(AsyncIO::WriteFile(path, bytes).get().ok) << AsyncIO::BackendName();
        EXPECT_EQ(fs::file_size(path), bytes.size());
        const IOResult read = AsyncIO::ReadFile(path).get();
        ASSERT_TRUE(read.ok) << read.error;
        EXPECT_TRUE(read.data == bytes) << AsyncIO::BackendName();
    }
}

TEST(PlatformAsyncIO, EmptyFilesAndOverwrites)
{
    for (const bool threadPool : { false, true })
    {
        BackendGuard guard(threadPool);
        TempDir dir;
        const std::string path = dir.path + "/f.bin";

        ASSERT_TRUE(AsyncIO::WriteFile(path, {}).get().ok);
        EXPECT_TRUE(fs::exists(path));
        const IOResult empty = AsyncIO::ReadFile(path).get();
        EXPECT_TRUE(empty.ok);
        EXPECT_TRUE(empty.data.empty());

        ASSERT_TRUE(AsyncIO::WriteFile(path, Pattern(5000)).get().ok);
        ASSERT_TRUE(AsyncIO::WriteFile(path, Pattern(100)).get().ok);   // shorter : the old tail must be gone
        EXPECT_EQ(AsyncIO::ReadFile(path).get().data.size(), 100u);
    }
}

TEST(PlatformAsyncIO, WritingInAMissingDirectoryFails)
{
    for (const bool threadPool : { false, true })
    {
        BackendGuard guard(threadPool);
        TempDir dir;
        const IOResult result = AsyncIO::WriteFile(dir.path + "/no/such/dir/f.bin", Pattern(10)).get();
        EXPECT_FALSE(result.ok);
        EXPECT_FALSE(result.error.empty());
    }
}

TEST(PlatformAsyncIO, ConcurrentRequestsFromSeveralThreads)
{
    for (const bool threadPool : { false, true })
    {
        BackendGuard guard(threadPool);
        TempDir dir;
        std::atomic<int> failures{ 0 };
        {
            std::vector<std::thread> threads;
            for (int t = 0; t < 4; ++t)
                threads.emplace_back([&, t] {
                    for (int i = 0; i < 20; ++i)
                    {
                        const std::string path = dir.path + "/t" + std::to_string(t) + "_" + std::to_string(i);
                        const std::vector<uint8_t> bytes = Pattern(static_cast<size_t>(1000 + t * 100000 + i));
                        if (!AsyncIO::WriteFile(path, bytes).get().ok || AsyncIO::ReadFile(path).get().data != bytes)
                            ++failures;
                    }
                });
            for (auto& thread : threads)
                thread.join();
        }
        EXPECT_EQ(failures.load(), 0) << AsyncIO::BackendName();
    }
}

TEST(PlatformAsyncIO, ShutdownWaitsForPendingRequests)
{
    BackendGuard guard(false);
    TempDir dir;
    auto pending = AsyncIO::WriteFile(dir.path + "/pending.bin", Pattern(4 * 1024 * 1024));
    AsyncIO::Shutdown();
    EXPECT_EQ(pending.wait_for(std::chrono::seconds(0)), std::future_status::ready);
    EXPECT_TRUE(pending.get().ok);
    // and the next request restarts the backend
    EXPECT_TRUE(AsyncIO::ReadFile(dir.path + "/pending.bin").get().ok);
}

// --- file watcher ---

namespace {
    // Polls until `predicate` is satisfied by the changes seen so far (the OS notification is asynchronous)
    template <class Predicate>
    bool WaitForChange(FileWatcher& watcher, std::vector<FileChange>& seen, Predicate predicate)
    {
        for (int i = 0; i < 100; ++i)
        {
            for (FileChange& c : watcher.Poll())
                seen.push_back(std::move(c));
            if (predicate(seen))
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return false;
    }
}

TEST(PlatformFileWatcher, SeesCreatedModifiedAndDeletedFiles)
{
    TempDir dir;
    FileWatcher watcher;
    ASSERT_TRUE(watcher.Start(dir.path));
    EXPECT_TRUE(watcher.IsWatching());

    const std::string file = dir.path + "/watched.txt";
    std::vector<FileChange> seen;
    const auto has = [&](FileChangeKind kind) {
        return [&, kind](const std::vector<FileChange>& changes) {
            for (const FileChange& c : changes)
                if (c.kind == kind && c.path == file) return true;
            return false;
        };
    };

    { std::ofstream(file) << "hello"; }
    EXPECT_TRUE(WaitForChange(watcher, seen, has(FileChangeKind::Created)));

    { std::ofstream(file, std::ios::app) << " world"; }
    EXPECT_TRUE(WaitForChange(watcher, seen, has(FileChangeKind::Modified)));

    fs::remove(file);
    EXPECT_TRUE(WaitForChange(watcher, seen, has(FileChangeKind::Deleted)));

    watcher.Stop();
    EXPECT_FALSE(watcher.IsWatching());
}

TEST(PlatformFileWatcher, WatchesSubdirectoriesWhenRecursive)
{
    TempDir dir;
    fs::create_directories(dir.path + "/sub/deeper");

    FileWatcher watcher;
    ASSERT_TRUE(watcher.Start(dir.path, true));

    const std::string file = dir.path + "/sub/deeper/x.txt";
    { std::ofstream(file) << "x"; }

    std::vector<FileChange> seen;
    EXPECT_TRUE(WaitForChange(watcher, seen, [&](const std::vector<FileChange>& changes) {
        for (const FileChange& c : changes)
            if (c.path == file) return true;
        return false;
    }));
}

TEST(PlatformFileWatcher, RefusesAMissingDirectory)
{
    FileWatcher watcher;
    EXPECT_FALSE(watcher.Start("this/directory/does/not/exist"));
    EXPECT_FALSE(watcher.IsWatching());
}

// --- dynlib ---

TEST(PlatformDynLib, FailureToOpenIsExplained)
{
    DynLib lib;
    EXPECT_FALSE(lib.Open("definitely_not_a_library_xyz"));
    EXPECT_FALSE(lib.IsOpen());
    EXPECT_FALSE(lib.LastError().empty());
    EXPECT_EQ(lib.Symbol("anything"), nullptr);
}

TEST(PlatformDynLib, FindsASymbolOfASystemLibrary)
{
#if defined(_WIN32)
    const char* libraryName = "kernel32.dll";
    const char* symbol = "GetTickCount";
#elif defined(__APPLE__)
    const char* libraryName = "/usr/lib/libSystem.B.dylib";
    const char* symbol = "getpid";
#else
    const char* libraryName = "libc.so.6";
    const char* symbol = "getpid";
#endif
    DynLib lib;
    ASSERT_TRUE(lib.Open(libraryName)) << lib.LastError();
    EXPECT_NE(lib.Symbol(symbol), nullptr);
    EXPECT_EQ(lib.Symbol("no_such_symbol_xyz"), nullptr);

    DynLib moved = std::move(lib);
    EXPECT_FALSE(lib.IsOpen());
    EXPECT_TRUE(moved.IsOpen());
}

TEST(PlatformDynLib, FileNameFollowsThePlatformConvention)
{
    const std::string name = DynLib::FileName("Game");
    EXPECT_NE(name.find("Game"), std::string::npos);
    EXPECT_EQ(name.substr(name.size() - std::string(DynLib::Extension()).size()), DynLib::Extension());
}

TEST(PlatformDynLib, ModuleLoaderLoadsFindsAndUnloads)
{
#if defined(_WIN32)
    const char* libraryName = "kernel32.dll";
    const char* symbol = "GetTickCount";
#else
    const char* libraryName = "libc.so.6";
    const char* symbol = "getpid";
#endif
    auto& loader = ModuleLoader::GetInstance();
    ASSERT_TRUE(loader.LoadModule("test_system", libraryName));
    EXPECT_TRUE(loader.IsModuleLoaded("test_system"));
    EXPECT_NE(loader.GetSymbol<void*>("test_system", symbol), nullptr);
    EXPECT_EQ(loader.GetSymbol<void*>("test_system", "no_such_symbol_xyz"), nullptr);
    loader.UnloadModule("test_system");
    EXPECT_FALSE(loader.IsModuleLoaded("test_system"));
    EXPECT_FALSE(loader.LoadModule("test_missing", "definitely_not_a_library_xyz"));
}

// --- process ---

TEST(PlatformProcess, EnvironmentVariables)
{
    EXPECT_FALSE(GetEnv("SHARD_TEST_UNSET_VARIABLE").has_value());
    ASSERT_TRUE(SetEnv("SHARD_TEST_VARIABLE", "valeur é"));
    EXPECT_EQ(GetEnv("SHARD_TEST_VARIABLE").value_or(""), "valeur é");
}

TEST(PlatformProcess, WorkingDirectoryCanBeChangedAndRestored)
{
    TempDir dir;
    const std::string before = WorkingDirectory();
    ASSERT_TRUE(SetWorkingDirectory(dir.path));
    EXPECT_EQ(fs::weakly_canonical(WorkingDirectory()), fs::weakly_canonical(dir.path));
    ASSERT_TRUE(SetWorkingDirectory(before));
}

TEST(PlatformProcess, LaunchedProcessReportsItsExitCode)
{
#if defined(_WIN32)
    auto process = Process::Launch("cmd.exe", { "/c", "exit", "7" });
#else
    auto process = Process::Launch("sh", { "-c", "exit 7" });
#endif
    ASSERT_NE(process, nullptr);
    EXPECT_GT(process->Id(), 0u);
    const auto code = process->Wait(10000);
    ASSERT_TRUE(code.has_value());
    EXPECT_EQ(*code, 7);
    EXPECT_FALSE(process->IsRunning());
}

TEST(PlatformProcess, LaunchOfAMissingExecutableFails)
{
    EXPECT_EQ(Process::Launch("definitely_not_an_executable_xyz"), nullptr);
}

TEST(PlatformProcess, ArgumentsWithSpacesAndQuotesArrivePlain)
{
    TempDir dir;
    const std::string out = dir.path + "/out.txt";
#if defined(_WIN32)
    // cmd re-parses its own command line : check through the file that the argument was one argument
    auto process = Process::Launch("cmd.exe", { "/c", "echo", "a b", ">", out });
#else
    auto process = Process::Launch("sh", { "-c", "printf '%s' \"$0\" > \"$1\"", "a \"b\" c", out });
#endif
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->Wait(10000).has_value());
    std::ifstream file(out);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("a"), std::string::npos);
}

TEST(PlatformProcess, ASlowProcessCanBeKilled)
{
#if defined(_WIN32)
    auto process = Process::Launch("ping", { "-n", "30", "127.0.0.1" });
#else
    auto process = Process::Launch("sleep", { "30" });
#endif
    ASSERT_NE(process, nullptr);
    EXPECT_TRUE(process->IsRunning());
    EXPECT_FALSE(process->Wait(50).has_value());    // times out
    process->Kill();
    EXPECT_TRUE(process->Wait(10000).has_value());
}

// --- sockets ---

TEST(PlatformSockets, UdpLoopback)
{
    auto receiver = Socket::Create(SocketType::UDP);
    auto sender = Socket::Create(SocketType::UDP);
    ASSERT_NE(receiver, nullptr);
    ASSERT_NE(sender, nullptr);
    ASSERT_TRUE(receiver->Bind({ "127.0.0.1", 0 }));
    const uint16_t port = receiver->LocalPort();
    ASSERT_NE(port, 0);

    const char message[] = "ping";
    size_t sent = 0;
    ASSERT_EQ(sender->SendTo(message, sizeof(message), { "127.0.0.1", port }, &sent), SocketStatus::Ok);
    EXPECT_EQ(sent, sizeof(message));

    ASSERT_TRUE(receiver->WaitReadable(2000));
    char buffer[16] = {};
    size_t received = 0;
    SocketAddress from;
    ASSERT_EQ(receiver->ReceiveFrom(buffer, sizeof(buffer), &received, &from), SocketStatus::Ok);
    EXPECT_EQ(std::string(buffer), "ping");
    EXPECT_EQ(from.host, "127.0.0.1");
}

TEST(PlatformSockets, NonBlockingReceiveWithNothingToReadWouldBlock)
{
    auto socket = Socket::Create(SocketType::UDP);
    ASSERT_TRUE(socket->Bind({ "127.0.0.1", 0 }));
    ASSERT_TRUE(socket->SetBlocking(false));
    char buffer[8];
    size_t received = 0;
    EXPECT_EQ(socket->ReceiveFrom(buffer, sizeof(buffer), &received), SocketStatus::WouldBlock);
    EXPECT_FALSE(socket->WaitReadable(10));
}

TEST(PlatformSockets, TcpLoopbackEchoAndClose)
{
    auto server = Socket::Create(SocketType::TCP);
    ASSERT_NE(server, nullptr);
    server->SetReuseAddress(true);
    ASSERT_TRUE(server->Bind({ "127.0.0.1", 0 }));
    ASSERT_TRUE(server->Listen());
    const uint16_t port = server->LocalPort();

    auto client = Socket::Create(SocketType::TCP);
    ASSERT_TRUE(client->Connect({ "127.0.0.1", port }, 2000));
    ASSERT_TRUE(server->WaitReadable(2000));
    SocketAddress peer;
    auto accepted = server->Accept(&peer);
    ASSERT_NE(accepted, nullptr);
    EXPECT_EQ(peer.host, "127.0.0.1");

    const char message[] = "hello tcp";
    ASSERT_EQ(client->Send(message, sizeof(message)), SocketStatus::Ok);
    ASSERT_TRUE(accepted->WaitReadable(2000));
    char buffer[32] = {};
    size_t received = 0;
    ASSERT_EQ(accepted->Receive(buffer, sizeof(buffer), &received), SocketStatus::Ok);
    EXPECT_EQ(std::string(buffer), "hello tcp");

    client->Close();
    ASSERT_TRUE(accepted->WaitReadable(2000));
    EXPECT_EQ(accepted->Receive(buffer, sizeof(buffer), &received), SocketStatus::Closed);
}

TEST(PlatformSockets, ConnectToAClosedPortFailsWithinTheTimeout)
{
    uint16_t port = 0;
    {
        auto probe = Socket::Create(SocketType::TCP);
        ASSERT_TRUE(probe->Bind({ "127.0.0.1", 0 }));
        port = probe->LocalPort();
    }   // closed : nobody listens there any more
    auto client = Socket::Create(SocketType::TCP);
    EXPECT_FALSE(client->Connect({ "127.0.0.1", port }, 2000));
}

// --- pipes / shared memory ---

TEST(PlatformIpc, NamedPipeCarriesBytesBothWays)
{
    const std::string name = "shard_test_pipe_" + std::to_string(CurrentProcessId());
    auto server = NamedPipe::CreateServer(name);
    ASSERT_NE(server, nullptr);

    std::unique_ptr<NamedPipe> client;
    std::thread connector([&] { client = NamedPipe::Connect(name, 2000); });
    ASSERT_TRUE(server->WaitForClient(2000));
    connector.join();
    ASSERT_NE(client, nullptr);
    EXPECT_TRUE(server->IsConnected());

    const char request[] = "request";
    ASSERT_EQ(client->Write(request, sizeof(request)), IpcStatus::Ok);
    char buffer[32] = {};
    size_t read = 0;
    ASSERT_EQ(server->Read(buffer, sizeof(buffer), &read, 2000), IpcStatus::Ok);
    EXPECT_EQ(std::string(buffer), "request");

    const char reply[] = "reply";
    ASSERT_EQ(server->Write(reply, sizeof(reply)), IpcStatus::Ok);
    ASSERT_EQ(client->Read(buffer, sizeof(buffer), &read, 2000), IpcStatus::Ok);
    EXPECT_EQ(std::string(buffer), "reply");

    // nothing to read : timeout, not a hang
    EXPECT_EQ(server->Read(buffer, sizeof(buffer), &read, 20), IpcStatus::TimedOut);

    client.reset();
    EXPECT_EQ(server->Read(buffer, sizeof(buffer), &read, 2000), IpcStatus::Closed);
}

TEST(PlatformIpc, ConnectToAMissingServerFails)
{
    EXPECT_EQ(NamedPipe::Connect("shard_test_no_such_pipe", 0), nullptr);
}

TEST(PlatformIpc, SharedMemoryIsSeenByTheOtherSide)
{
    const std::string name = "shard_test_shm_" + std::to_string(CurrentProcessId());
    auto owner = SharedMemory::Create(name, 4096);
    ASSERT_NE(owner, nullptr);
    EXPECT_EQ(owner->Size(), 4096u);
    std::memcpy(owner->Data(), "shared!", 8);

    auto other = SharedMemory::Open(name);
    ASSERT_NE(other, nullptr);
    EXPECT_EQ(other->Size(), 4096u);
    EXPECT_STREQ(static_cast<const char*>(other->Data()), "shared!");

    static_cast<char*>(other->Data())[0] = 'S';
    EXPECT_STREQ(static_cast<const char*>(owner->Data()), "Shared!");

    EXPECT_EQ(SharedMemory::Open("shard_test_no_such_shm"), nullptr);
}

// --- crash ---

TEST(PlatformCrash, ReportIsWrittenAndHooksRun)
{
    TempDir dir;
    CrashHandlerDesc desc;
    desc.appName = "ShardTest";
    desc.outputDirectory = dir.path + "/crashes";
    CrashHandler::Install(desc);

    bool hookRan = false;
    std::string hookReason;
    CrashHandler::AddHook([&](const CrashInfo& info) { hookRan = true; hookReason = info.reason; });

    const CrashInfo info = CrashHandler::WriteReport("test crash");
    CrashHandler::Uninstall();

    EXPECT_TRUE(hookRan);
    EXPECT_EQ(hookReason, "test crash");
    ASSERT_FALSE(info.reportPath.empty());
    ASSERT_TRUE(fs::exists(info.reportPath));

    std::ifstream file(info.reportPath);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("test crash"), std::string::npos);
    EXPECT_NE(content.find("Stack"), std::string::npos);
}
