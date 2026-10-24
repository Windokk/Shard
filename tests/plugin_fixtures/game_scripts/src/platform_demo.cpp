// What the ABI 1.7 interfaces give a plugin, through the SDK's own wrappers : jobs, time, assets, reflection, sockets, debug draw, physics queries,
// the render resources and the edit mode of a script. Native only (the sandboxed build of the game leaves this file out).
#include <shard/game.hpp>

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "stats.hpp"

#if defined(_WIN32)
#   define TEST_EXPORT extern "C" __declspec(dllexport)
#else
#   define TEST_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {
    std::map<std::string, double>& Results()
    {
        static std::map<std::string, double> results;
        return results;
    }
    std::string& Text()
    {
        static std::string text;
        return text;
    }
}

namespace {
    Shard::Collider& Collider()
    {
        static Shard::Collider collider;
        return collider;
    }
}

// ABI 1.4 : an actor component told when it joins an actor, is edited and leaves
struct Beacon {
    int32_t power = 1;
    float range = 5.f;
};
SHARD_COMPONENT(Beacon, (power)(range))

SHARD_ON_LOAD(PlatformDemo, ctx)
{
    ctx.Components().Register<Beacon>("Beacon", Shard::ComponentEvents<Beacon>{
        [](uint64_t actor, uint64_t entity, Beacon& b) {
            ++Results()["beaconAdded"];
            Results()["beaconActorOk"] = actor != 0 && entity != 0 ? 1 : 0;
            Results()["beaconPowerSeen"] = b.power;
        },
        [](uint64_t, uint64_t, Beacon& b) {
            ++Results()["beaconRemoved"];
            Results()["beaconPowerAtRemoval"] = b.power;
        },
        [](uint64_t, uint64_t, Beacon& b, uint32_t field) {
            ++Results()["beaconChanged"];
            Results()["beaconField"] = field;
            Results()["beaconPowerChanged"] = b.power;
        } });
    // ABI 1.7 (editor 1.2) : a double click on a ".beacon" file, and an entry of the Create menu ; both only exist in the editor
    ctx.Editor().AddAssetOpener(".beacon", [](std::string_view path) {
        ++Results()["opened"];
        Text() = std::string(path);
    });
    ctx.Editor().AddAssetTemplate("Beacon", "new_beacon.beacon", "{\"power\":3}");
    ctx.Commands().Add("platform.echo", "platform.echo <text> : prints it", [](const std::vector<std::string_view>& args, std::string& out) {
        if (args.empty()) { out = "nothing"; return false; }
        out = "echo " + std::string(args[0]);
        return true;
    });
}

class PlatformProbe : public Shard::Script {
public:
    static constexpr bool kExecuteInEditMode = true;

    int32_t level = 3;
    float speed = 1.f;

    void OnEditTick(float deltaTime) override
    {
        ++Results()["editTicks"];
        Results()["editDt"] = deltaTime;
    }
    void OnFieldChanged(uint32_t field) override
    {
        ++Results()["fieldChanges"];
        Results()["lastField"] = field;
    }
    void OnPlay() override
    {
        Results()["level"] = level;
        Results()["fieldLevel"] = Shard::FieldIndex<PlatformProbe>("level");
        Results()["fieldSpeed"] = Shard::FieldIndex<PlatformProbe>("speed");
        Results()["fieldNone"] = Shard::FieldIndex<PlatformProbe>("nothing");
        RunJobs();
        RunTime();
        RunReflect();
        RunNet();
        RunAssets();
        RunPhysics();
        RunDebugAndRender();
        RunActors();
        RunCommands();
    }

private:
    static void RunJobs()
    {
        Results()["workers"] = Shard::Jobs::WorkerCount();

        std::atomic<int> ran{ 0 };
        Shard::JobHandle job = Shard::Jobs::Run([&ran] { ran = 41; ++ran; });
        job.Wait();
        Results()["jobRan"] = ran.load();

        std::vector<int> squares(1000, 0);
        Shard::Jobs::ParallelFor(1000, [&](uint32_t i, uint32_t) { squares[i] = int(i * i); });
        long long sum = 0;
        for (int v : squares) sum += v;
        Results()["parallelSum"] = double(sum);

        auto counter = std::make_shared<std::atomic<int>>(0);
        Shard::JobHandle async = Shard::Jobs::ParallelForAsync(500, [counter](uint32_t, uint32_t) { ++*counter; });
        async.Wait();
        Results()["asyncCount"] = counter->load();

        Shard::Jobs::PostMain([] { ++Results()["postedToMain"]; });
    }

    static void RunTime()
    {
        const Shard::TimeInfo t = Shard::Time::Now();
        Results()["fixedDt"] = t.fixedDeltaTime;
        Results()["timeScale"] = t.timeScale;
        Results()["playing"] = t.playing ? 1 : 0;
        const Shard::StateHash a = Shard::Time::Hash(), b = Shard::Time::Hash();
        Results()["hashStable"] = a == b ? 1 : 0;
    }

    static void RunReflect()
    {
        const Shard::Reflect reflect(Shard::detail::PluginHandle());
        reflect.AddFunction("platform.add", "Platform", "adds two numbers",
                            { { "a", Shard::ValueType::F32, false, "" }, { "b", Shard::ValueType::F32, false, "" } },
                            { { "sum", Shard::ValueType::F32, false, "" } },
                            [](const std::vector<Shard::Value>& args, std::vector<Shard::Value>& results) {
                                if (args.size() != 2) return false;
                                results[0] = Shard::Value(args[0].AsFloat() + args[1].AsFloat());
                                return true;
                            });
        auto sum = Shard::Reflect::Call("platform.add", { Shard::Value(1.5f), Shard::Value(2.0f) });
        Results()["reflectSum"] = sum && sum->size() == 1 ? (*sum)[0].AsFloat() : -1.0;
        Results()["reflectMissing"] = Shard::Reflect::Call("platform.nothing", {}) ? 1 : 0;
        bool listed = false;
        for (const Shard::FunctionEntry& f : Shard::Reflect::Functions()) listed = listed || f.name == "platform.add";
        Results()["reflectListed"] = listed ? 1 : 0;
        Results()["reflectComponents"] = double(Shard::Reflect::Types(Shard::TypeKind::Component).size());
        try {
            reflect.AddFunction("platform.add", "Platform", "", {}, {}, [](const std::vector<Shard::Value>&, std::vector<Shard::Value>&) { return true; });
            Results()["reflectDuplicate"] = 0;
        } catch (const Shard::Error&) {
            Results()["reflectDuplicate"] = 1;
        }
    }

    static void RunNet()
    {
        int datagrams = 0;
        std::string received;
        Shard::Net::Socket server = Shard::Net::OpenUdp(0, [&](const Shard::Net::Event& e) {
            if (e.kind == Shard::Net::EventKind::Data) {
                ++datagrams;
                received.assign(static_cast<const char*>(e.data), size_t(e.size));
            }
        }, "127.0.0.1");
        const uint32_t port = server.LocalPort();
        Results()["udpPort"] = port;
        Shard::Net::Socket client = Shard::Net::OpenUdp(0, [](const Shard::Net::Event&) {}, "127.0.0.1");
        client.SendTo({ "127.0.0.1", port }, "ping", 4);
        for (int i = 0; i < 200 && datagrams == 0; ++i) {
            Shard::Net::Poll();
            if (datagrams == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        Results()["udpDatagrams"] = datagrams;
        Text() = received;
        server.Close();
        Results()["udpClosed"] = server ? 0 : 1;

        // A stream over the loopback : the listener accepts, the client connects, a message goes each way, the client closes
        int accepted = 0, connected = 0, clientClosed = 0, serverClosed = 0;
        std::string toServer, toClient;
        Shard::Net::Socket peer;
        Shard::Net::Socket listener = Shard::Net::Listen({ "127.0.0.1", 0 }, [&](const Shard::Net::Event& e) {
            if (e.kind == Shard::Net::EventKind::Accepted) {
                ++accepted;
                peer = Shard::Net::Socket::Adopt(e.socket);
            } else if (e.kind == Shard::Net::EventKind::Data) {
                toServer.append(static_cast<const char*>(e.data), size_t(e.size));
            } else if (e.kind == Shard::Net::EventKind::Closed) {
                ++serverClosed;
            }
        });
        Shard::Net::Socket tcpClient = Shard::Net::Connect({ "127.0.0.1", listener.LocalPort() }, [&](const Shard::Net::Event& e) {
            if (e.kind == Shard::Net::EventKind::Connected) ++connected;
            else if (e.kind == Shard::Net::EventKind::Data) toClient.append(static_cast<const char*>(e.data), size_t(e.size));
            else if (e.kind == Shard::Net::EventKind::Closed) ++clientClosed;
        });
        tcpClient.Send("hello server");
        auto pump = [&](auto done) {
            for (int i = 0; i < 400 && !done(); ++i) {
                Shard::Net::Poll();
                if (!done()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        };
        pump([&] { return toServer.size() >= 12; });
        Results()["tcpAccepted"] = accepted;
        Results()["tcpConnected"] = connected;
        Results()["tcpToServer"] = toServer == "hello server" ? 1 : 0;
        if (peer) peer.Send("hello client");
        pump([&] { return toClient.size() >= 12; });
        Results()["tcpToClient"] = toClient == "hello client" ? 1 : 0;
        tcpClient.Close();
        pump([&] { return serverClosed > 0; });
        Results()["tcpServerSawClose"] = serverClosed > 0 ? 1 : 0;
    }

    static void RunAssets()
    {
        Results()["assetResolved"] = Shard::Assets::Resolve("meshes/none.mesh") ? 1 : 0;
        Results()["assetExists"] = Shard::Assets::Exists("meshes/none.mesh") ? 1 : 0;
        Results()["assetInfo"] = Shard::Assets::Info(123456) ? 1 : 0;
        Results()["assetMesh"] = Shard::Assets::ReadMesh(123456) ? 1 : 0;
        Results()["assetPathOf"] = double(Shard::Assets::PathOf(123456).size());
    }

    static void RunPhysics()
    {
        // Straight down from (0, 5, 0) : the test's ground, a box whose top face is at y = 0
        const auto all = Shard::Physics::RaycastAll({ 0.f, 5.f, 0.f }, { 0.f, -1.f, 0.f }, 20.f);
        Results()["rayAll"] = double(all.size());
        Results()["rayAllDistance"] = all.empty() ? -1.0 : all[0].distance;
        Results()["rayAllActor"] = !all.empty() && all[0].actor && all[0].actor.Name() == "Ground" ? 1 : 0;
        Results()["overlapSphere"] = double(Shard::Physics::OverlapSphere({ 0.f, 0.f, 0.f }, 3.f).size());
        Results()["overlapFar"] = double(Shard::Physics::OverlapSphere({ 0.f, 50.f, 0.f }, 3.f).size());
        const auto sweep = Shard::Physics::SweepSphere({ 0.f, 5.f, 0.f }, 0.5f, { 0.f, -1.f, 0.f }, 20.f);
        Results()["sweep"] = sweep ? 1 : 0;
        Results()["sweepDistance"] = sweep ? sweep->distance : -1.0;
        Shard::QueryFilter noBodies;
        noBodies.collidersOnly = true;
        Results()["rayCollidersOnly"] = double(Shard::Physics::RaycastAll({ 0.f, 5.f, 0.f }, { 0.f, -1.f, 0.f }, 20.f, noBodies).size());

        // A static collider of the plugin, a unit box at (1, 2, 3) : it joins the physics world at the next fixed step
        const float box[6] = { 0.f, 0.f, 0.f, 1.f, 1.f, 1.f };
        try {
            Collider() = Shard::Collider::Boxes(box, 1, { 1.f, 2.f, 3.f }, {}, 77);
            Results()["colliderMade"] = Collider() ? 1 : 0;
            Results()["colliderPose"] = Collider().SetPose({ 1.f, 2.f, 3.f }) ? 1 : 0;
        } catch (const Shard::Error& e) {
            Results()["colliderMade"] = -double(e.Result());
        }
    }

    static void RunDebugAndRender()
    {
        Shard::Debug::Line({ 0.f, 0.f, 0.f }, { 1.f, 1.f, 1.f }, Shard::Rgba(255, 0, 0), 5.f);
        Shard::Debug::Sphere({ 0.f, 1.f, 0.f }, 0.5f, Shard::Rgba(0, 255, 0), 5.f);
        Shard::Debug::Box({ 0.f, 1.f, 0.f }, { 0.5f, 0.5f, 0.5f }, Shard::Rgba(0, 0, 255), 5.f);
        Shard::Debug::Text({ 0.f, 2.f, 0.f }, "probe", Shard::Rgba(255, 255, 255), 5.f);
        Results()["debugEnabled"] = Shard::Debug::Enabled() ? 1 : 0;

        // A mesh built on the CPU : the resource exists with or without a renderer behind it
        Shard::MeshBuilder builder;
        builder.Quad({ -1.f, 0.f, -1.f }, { -1.f, 0.f, 1.f }, { 1.f, 0.f, 1.f }, { 1.f, 0.f, -1.f }, { 0.f, 1.f, 0.f });
        Results()["meshVertices"] = builder.VertexCount();
        Results()["meshIndices"] = builder.IndexCount();
        try {
            Shard::Mesh mesh = Shard::Mesh::Create(builder);
            Results()["meshCreated"] = mesh ? 1 : 0;
            Shard::Texture texture = Shard::Texture::Create(2, 2);
            Results()["textureCreated"] = texture ? 1 : 0;
            const Shard::Vec2 size = texture.Size();
            Results()["textureWidth"] = size.x;
        } catch (const Shard::Error& e) {
            Results()["meshCreated"] = -double(e.Result());
        }
    }

    static void RunCommands()
    {
        const Shard::Commands commands(Shard::detail::PluginHandle());
        const auto ok = commands.Execute("platform.echo hello");
        Results()["commandOk"] = ok.ok ? 1 : 0;
        Results()["commandFound"] = ok.found ? 1 : 0;
        Results()["commandOutput"] = ok.output == "echo hello" ? 1 : 0;
        const auto bad = commands.Execute("platform.echo");
        Results()["commandRefused"] = !bad.ok && bad.found ? 1 : 0;
        const auto missing = commands.Execute("platform.nothing");
        Results()["commandMissing"] = missing.found ? 0 : 1;
        // In the editor an edit made by the plugin can be undone from the editor's history
        Results()["undoPushed"] = Shard::Editor(Shard::detail::PluginHandle()).PushUndo("probe edit", [] { ++Results()["undone"]; }, [] { ++Results()["redone"]; }) ? 1 : 0;
    }

    void RunActors()
    {
        const uint64_t stable = Owner().StableId();
        Results()["stableId"] = stable != 0 ? 1 : 0;
        Shard::Actor again = Shard::Actor::FromStableId(stable);
        Results()["stableRoundTrip"] = again && again == Owner() ? 1 : 0;
        const Shard::Vec3 here = Owner().WorldPosition();
        Results()["inSphere"] = double(GetScene().FindInSphere(here, 1.f).size());
        Results()["inBox"] = double(GetScene().FindInBox(here - Shard::Vec3{ 1.f, 1.f, 1.f }, here + Shard::Vec3{ 1.f, 1.f, 1.f }).size());
    }
};
SHARD_REGISTER_SCRIPT(PlatformProbe, (level)(speed))

TEST_EXPORT double plat_stat(const char* name)
{
    auto it = Results().find(name);
    return it == Results().end() ? -1000.0 : it->second;
}
// A ray down onto the collider : what it reports about it (called after the physics stepped)
TEST_EXPORT int plat_ray_collider()
{
    Shard::QueryFilter only;
    only.collidersOnly = true;
    const auto hits = Shard::Physics::RaycastAll({ 1.f, 10.f, 3.f }, { 0.f, -1.f, 0.f }, 20.f, only);
    Results()["colliderHits"] = double(hits.size());
    if (hits.empty()) return 0;
    Results()["colliderDistance"] = hits[0].distance;
    Results()["colliderUserData"] = double(hits[0].userData);
    Results()["colliderHandle"] = hits[0].collider == Collider().Handle() ? 1 : 0;
    Results()["colliderHasActor"] = hits[0].actor ? 1 : 0;
    return int(hits.size());
}
TEST_EXPORT void plat_release_collider() { Collider().Reset(); }
TEST_EXPORT const char* plat_text() { return Text().c_str(); }
TEST_EXPORT void plat_reset() { Results().clear(); Text().clear(); }
