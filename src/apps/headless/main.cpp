// ShardHeadless : runs the simulation of a project with no window, no GPU and no sound card.
//
//  * As a replay runner (the CI) : plays a recorded replay back and checks that the state of the simulation hashes to what the
//    replay says, frame after frame. The same replay has to pass on Windows and on Linux, on a machine without a GPU.
//  * As a dedicated server (--server) : the same loop, paced to the fixed step of the simulation instead of as fast as it goes.
//
// It is the same engine and the same game module as the player : only the platform (no window system) and the graphics backend
// (the null backend : the frame is built, nothing is drawn) differ. Every frame is exactly one fixed step, the random streams
// derive from the seed : the same inputs always end in the same state.
//
//   ShardHeadless --game libGameModule.dll --project ../example_project/example_project.json --replay run.replay
//
// Exit code : 0 = ran (and matched the replay / --expect), 1 = could not start, 2 = the run diverged.

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "engine/core/config/cvar.hpp"
#include "engine/core/determinism/determinism.hpp"
#include "engine/core/determinism/state_hash.hpp"
#include "engine/core/diagnostics/logger.hpp"
#include "engine/game_module.hpp"
#include "engine/platform/replay/input_replay.hpp"
#include "engine/platform/windowing/headless/headless_platform.hpp"
#include "engine/renderer/frontend/renderer.hpp"
#include "engine/renderer/rhi/renderer_api.hpp"
#include "engine/replay_session.hpp"
#include "engine/world/engine.hpp"
#include "engine/world/time_manager.hpp"
#include "engine/world/world_manager.hpp"

using namespace Shard;
using namespace Shard::Engine;
using namespace Shard::Engine::Core;

Debugging::Level minDebugLevel = Debugging::Level::Info;

namespace {

    std::atomic<bool> g_Interrupted{false};

    void OnSignal(int) { g_Interrupted = true; }

    struct Options {
        std::string project;
        std::string gameModule;
        std::string world;
        std::string replayPath;
        std::string writeReplayPath;
        std::string expectHash;
        std::string scriptPath;
        int64_t frames = -1;                // -1 : what the replay says, else the default
        std::optional<uint64_t> seed;
        uint64_t checkEvery = 60;
        bool printState = false;
        bool requireChecks = false;
        bool server = false;
        int width = 1280;
        int height = 720;
    };

    void PrintUsage()
    {
        std::cout <<
            "ShardHeadless : the simulation without a window, a GPU or a sound card\n"
            "\n"
            "  ShardHeadless --game <module> --project <project.json> [options]\n"
            "\n"
            "  --world <path>         world to load (default : the replay's, then the project's first)\n"
            "  --frames <n>           frames to run (default : the length of the replay, else 600 ; --server : until stopped)\n"
            "  --seed <n|random>      seed the random streams derive from (default : the replay's, else 1)\n"
            "  --replay <file>        play a replay back : its inputs are fed, its state checks are verified\n"
            "  --write-replay <file>  write the replay of this run (the inputs of --replay, the state checks of this run) :\n"
            "                         creates a replay, or re-baselines one after a change that is meant to alter the simulation\n"
            "  --check-every <n>      a state check every n frames (default 60 ; 0 : only at the end)\n"
            "  --expect <hash>        the state after the last frame has to hash to this (16 hex digits)\n"
            "  --print-state          print the state at every check\n"
            "  --require-checks       with --replay : fail when the replay has no state check to verify (a replay that checks nothing proves nothing)\n"
            "  --script <file>        console commands to run at given frames, one \"<frame> <command>\" per line (try \"60 sim.hash\")\n"
            "  --server               pace the run to the fixed step (real time) : a dedicated server\n"
            "  --width / --height     size of the viewport the (null) renderer lays its targets out for\n"
            "  --debug <level>        log, info, warning, error or fatal (default info)\n"
            "  +name=value            sets a console variable\n"
            "\n"
            "Exit code : 0 = ran, and matched the replay / --expect ; 1 = could not start ; 2 = the run diverged\n";
    }

    bool ParseArgs(int argc, char* argv[], Options& options)
    {
        std::vector<std::string> warnings;
        CVarRegistry::Global().ParseCommandLine(argc, argv, &warnings);
        for (const std::string& w : warnings)
            std::cerr << "[WARNING] " << w << std::endl;

        auto value = [&](int& i, const char* name) -> const char* {
            if (i + 1 >= argc)
            {
                std::cerr << "[ERROR] " << name << " needs a value" << std::endl;
                return nullptr;
            }
            return argv[++i];
        };

        for (int i = 1; i < argc; i++)
        {
            const std::string arg = argv[i];
            if (!arg.empty() && arg[0] == '+')
                continue;       // a console variable, read above

            const char* v = nullptr;
            if (arg == "--help" || arg == "-h")
            {
                PrintUsage();
                std::exit(0);
            }
            else if (arg == "--server")
                options.server = true;
            else if (arg == "--print-state")
                options.printState = true;
            else if (arg == "--require-checks")
                options.requireChecks = true;
            else if (arg == "--project")
            {
                if (!(v = value(i, "--project"))) return false;
                options.project = v;
            }
            else if (arg == "--game")
            {
                if (!(v = value(i, "--game"))) return false;
                options.gameModule = v;
            }
            else if (arg == "--world")
            {
                if (!(v = value(i, "--world"))) return false;
                options.world = v;
            }
            else if (arg == "--replay")
            {
                if (!(v = value(i, "--replay"))) return false;
                options.replayPath = v;
            }
            else if (arg == "--write-replay")
            {
                if (!(v = value(i, "--write-replay"))) return false;
                options.writeReplayPath = v;
            }
            else if (arg == "--script")
            {
                if (!(v = value(i, "--script"))) return false;
                options.scriptPath = v;
            }
            else if (arg == "--expect")
            {
                if (!(v = value(i, "--expect"))) return false;
                options.expectHash = v;
            }
            else if (arg == "--frames")
            {
                if (!(v = value(i, "--frames"))) return false;
                options.frames = std::stoll(v);
            }
            else if (arg == "--check-every")
            {
                if (!(v = value(i, "--check-every"))) return false;
                options.checkEvery = std::stoull(v);
            }
            else if (arg == "--width")
            {
                if (!(v = value(i, "--width"))) return false;
                options.width = std::stoi(v);
            }
            else if (arg == "--height")
            {
                if (!(v = value(i, "--height"))) return false;
                options.height = std::stoi(v);
            }
            else if (arg == "--seed")
            {
                if (!(v = value(i, "--seed"))) return false;
                if (std::string(v) == "random")
                    options.seed = Determinism::Seed();
                else
                    options.seed = std::stoull(v);
            }
            else if (arg == "--debug")
            {
                if (!(v = value(i, "--debug"))) return false;
                const std::string level = v;
                if (level == "log") minDebugLevel = Debugging::Level::Log;
                else if (level == "info") minDebugLevel = Debugging::Level::Info;
                else if (level == "warning") minDebugLevel = Debugging::Level::Warning;
                else if (level == "error") minDebugLevel = Debugging::Level::Error;
                else if (level == "fatal") minDebugLevel = Debugging::Level::Fatal;
                else std::cerr << "[WARNING] Unknown debug level : " << level << std::endl;
            }
            else
            {
                std::cerr << "[ERROR] Unknown argument : " << arg << " (--help lists them)" << std::endl;
                return false;
            }
        }

        if (options.project.empty() || options.gameModule.empty())
        {
            std::cerr << "[ERROR] --project and --game are required (--help lists the options)" << std::endl;
            return false;
        }
        return true;
    }
}

int main(int argc, char* argv[])
{
    Options options;
    try
    {
        if (!ParseArgs(argc, argv, options))
            return 1;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ERROR] Bad argument : " << e.what() << std::endl;
        return 1;
    }

    // What a replay says about the run : its seed, its world, how long it lasts
    Platform::ReplayData replay;
    const bool hasReplay = !options.replayPath.empty();
    if (hasReplay)
    {
        std::string error;
        if (!Platform::ReplayData::Load(options.replayPath, replay, error))
        {
            std::cerr << "[ERROR] " << error << std::endl;
            return 1;
        }
    }

    uint64_t expectedFinal = 0;
    const bool hasExpectation = !options.expectHash.empty();
    if (hasExpectation && !Determinism::HashFromString(options.expectHash, expectedFinal))
    {
        std::cerr << "[ERROR] --expect needs 16 hexadecimal digits, got \"" << options.expectHash << "\"" << std::endl;
        return 1;
    }

    EngineInstance* engine = &EngineInstance::GetInstance();
    Core::SetEngine(engine);
    Debugging::SetLogger(&Debugging::Logger::GetInstance());

    if (!LoadGameModule(options.gameModule))
        return 1;

    EngineCreationSettings settings;
    settings.project = options.project;
    settings.startWorld = !options.world.empty() ? options.world : replay.world;
    settings.scriptFile = options.scriptPath;
    settings.windowWidth = options.width;
    settings.windowHeight = options.height;
    settings.headless = true;
    settings.deterministic = true;
    settings.readOnly = true;           // a run leaves the project as it found it
    settings.api = static_cast<uint32_t>(Rendering::RendererAPI::API::Null);
    settings.seed = options.seed ? options.seed : (hasReplay ? std::optional<uint64_t>(replay.seed) : std::optional<uint64_t>(1));
    if (options.seed && hasReplay && *options.seed != replay.seed)
        std::cerr << "[WARNING] --seed " << *options.seed << " differs from the replay's (" << replay.seed << ") : its state checks will not match" << std::endl;

    Platform::HeadlessPlatform* platform = new Platform::HeadlessPlatform();
    settings.platform = platform;

    engine->Init(settings);

    if (engine->GetWorldManager()->GetLoadedWorldCount() == 0)
    {
        std::cerr << "[ERROR] No world was loaded, aborting..." << std::endl;
        return 1;
    }

    const float fixedStep = engine->GetTimeManager()->GetFixedDeltaTime();
    if (hasReplay && replay.fixedStep > 0.0f && replay.fixedStep != fixedStep)
    {
        std::cerr << "[ERROR] The replay was recorded with a fixed step of " << replay.fixedStep << " s, the project has " << fixedStep
                  << " s : the simulation would not be the same." << std::endl;
        return 1;
    }

    engine->SetPlayMode(true);

    // The replay : its inputs go to the window, its checks are compared (unless the run is about to replace them)
    ReplaySession play;
    ReplaySession record;
    const bool recording = !options.writeReplayPath.empty();
    if (hasReplay)
    {
        play.Play(replay, !recording);
        play.SetCheckInterval((options.printState && !recording) ? options.checkEvery : 0);
        play.onState = [&](const SimulationStateHash& state) { std::cout << "[STATE] " << state.Describe() << std::endl; };
    }
    if (recording)
    {
        record.Record(*platform->GetHeadlessWindow(), *settings.seed, fixedStep, settings.startWorld);
        record.SetCheckInterval(options.checkEvery);
        if (options.printState)
            record.onState = [&](const SimulationStateHash& state) { std::cout << "[STATE] " << state.Describe() << std::endl; };
    }
    else if (!hasReplay && options.printState)
    {
        // No replay : a session that only prints the state
        play.Play(Platform::ReplayData{});
        play.SetCheckInterval(options.checkEvery);
        play.onState = [&](const SimulationStateHash& state) { std::cout << "[STATE] " << state.Describe() << std::endl; };
    }

    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);

    uint64_t length;
    if (options.frames >= 0)
        length = static_cast<uint64_t>(options.frames);
    else if (options.server)
        length = UINT64_MAX;
    else
        length = hasReplay && replay.Length() > 0 ? replay.Length() : 600;

    std::cout << "[HEADLESS] " << (options.server ? "server" : "run") << " : world " << engine->GetWorldManager()->GetWorldAt(0)->GetName() << ", seed " << *settings.seed
              << ", fixed step " << fixedStep << " s, "
              << (length == UINT64_MAX ? std::string("until stopped") : std::to_string(length) + " frames")
              << (hasReplay ? ", replay " + options.replayPath : std::string()) << std::endl;

    auto post = [platform](const Platform::OSEvent& event) { platform->GetHeadlessWindow()->PostEvent(event); };

    const auto started = std::chrono::steady_clock::now();
    auto deadline = started;
    const std::chrono::duration<double> step(fixedStep);

    uint64_t frame = 0;
    for (; frame < length && !g_Interrupted && !engine->ShouldEnd(); ++frame)
    {
        play.BeforeFrame(frame, post);
        record.BeforeFrame(frame, post);

        if (!engine->Run())
            break;

        play.AfterFrame(*engine);
        record.AfterFrame(*engine);

        if (play.Diverged())
            break;

        if (options.server)
        {
            // Real time : the next frame starts one fixed step after this one was due (a late frame does not make the following ones shorter)
            deadline += std::chrono::duration_cast<std::chrono::steady_clock::duration>(step);
            std::this_thread::sleep_until(deadline);
        }
    }

    play.Finish(*engine);
    if (recording)
        record.Finish(*engine);
    const SimulationStateHash final = HashSimulationState(*engine);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

    int exitCode = 0;

    if (play.Diverged())
    {
        std::cerr << "[REPLAY FAILED] " << play.Report() << std::endl;
        exitCode = 2;
    }
    else if (hasReplay && recording == false)
    {
        std::cout << "[REPLAY] " << play.ChecksPassed() << " state check(s) matched" << std::endl;
        if (options.requireChecks && play.ChecksPassed() == 0)
        {
            std::cerr << "[REPLAY FAILED] --require-checks : the replay has no state check to verify" << std::endl;
            exitCode = 2;
        }
    }

    if (hasExpectation && final.total != expectedFinal)
    {
        std::cerr << "[EXPECT FAILED] the state after " << final.frame << " frames hashes to " << Determinism::HashToString(final.total)
                  << ", expected " << Determinism::HashToString(expectedFinal) << std::endl;
        exitCode = 2;
    }

    if (recording)
    {
        std::string error;
        if (!record.TakeRecording().Save(options.writeReplayPath, error))
        {
            std::cerr << "[ERROR] " << error << std::endl;
            exitCode = exitCode ? exitCode : 1;
        }
        else
            std::cout << "[HEADLESS] replay written to " << options.writeReplayPath << std::endl;
    }

    // One line a script can read
    std::cout << "[HEADLESS] state " << Determinism::HashToString(final.total) << " frames " << final.frame << " physics-steps "
              << final.physicsSteps << " time " << seconds << " s" << std::endl;

    engine->Destroy();
    UnloadGameModule();

    return exitCode;
}
