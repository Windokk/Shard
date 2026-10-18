# Platform Abstraction Layer (PAL)

`src/engine/platform/` is the only code that talks to the OS or the window system. It depends on `ShardCore` and SDL3 and nothing else. The layers above go through its API, and `scripts/check_deps.py` fails CI if an OS or SDL header appears elsewhere in `src/engine/`.

Each module has a public header with no OS types, and one implementation file per OS (`*_win32.cpp` / `*_posix.cpp`, chosen with `#if`).

| Folder                        | Contents                                                                                                               |
|-------------------------------|------------------------------------------------------------------------------------------------------------------------|
| `windowing/`                  | `IWindow`, `IPlatform`; SDL3 backend (`SDLWindow`, `SDLPlatform<T>`): GL context, monitors, DPI, HDR, fullscreen        |
| `events/`                     | `OSEvent`: typed window system events, with several listeners per window                                               |
| `devices/`                    | `IInput` (keyboard, mouse, gamepads), `Key`, `GamepadButton`, `GamepadAxis`; SDL3 backend                              |
| `clipboard/`                  | Clipboard text, file / folder / message dialogs                                                                        |
| `filesystem/`                 | `Paths` (user directories), `FileWatcher` (ReadDirectoryChangesW / inotify), `AsyncIO` (IOCP / io_uring / thread pool) |
| `lib_loader/`                 | `DynLib`, `ModuleLoader`                                                                                               |
| `thread/`, `atomic/`, `time/` | Named, prioritised and pinned threads, `HardwareConcurrency()`, `SpinLock`, `NowNanoseconds()`, `PreciseSleep()`       |
| `process/`                    | Environment, executable path, `Process::Launch`, reveal in file manager                                                |
| `crash/`                      | Crash report and minidump, crash hooks, Ctrl+C / SIGTERM handler                                                       |
| `network/`                    | `Socket` (UDP / TCP), `NamedPipe`, `SharedMemory`                                                                      |
| `hardware/`                   | CPU features, RAM, OS, per-process memory (CPU and GPU)                                                                |

## Building on Linux

SDL3 is built from source, so you need the development packages for your window system (X11 / Wayland, ALSA / PulseAudio, libudev...). `liburing-dev` is optional. With it, async IO uses io_uring when the kernel allows it. Without it, a small thread pool does the job. `AsyncIO::BackendName()` tells you which one is active.

## Gamepads

Pads are numbered in the order they were plugged in.

```cpp
auto* input = GetEngine().GetInputManager();
if (input->GetGamepadCount() > 0) {
    float x = input->GetGamepadAxis(0, Input::GamepadAxis::LeftX);   // dead zone applied
    if (input->WasGamepadButtonPressed(0, Input::GamepadButton::South))
        input->SetGamepadRumble(0, 0.5f, 0.5f, 200);
}
```
