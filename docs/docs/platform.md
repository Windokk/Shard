# Platform Abstraction Layer (PAL)

`src/engine/platform/` is the only place that talks to the OS and to the window system. It depends on `ShardCore` and SDL3 only; every layer above asks it through its own API, and `scripts/check_deps.py` fails the CI if an OS or SDL header shows up elsewhere in `src/engine/`.

Each module has a public header without any OS type, and one implementation file per OS (`*_win32.cpp` / `*_posix.cpp`, selected by `#if`).

| Folder                        | Features                                                                                                               |
|-------------------------------|------------------------------------------------------------------------------------------------------------------------|
| `windowing/`                  | `IWindow`, `IPlatform`; SDL3 backend (`SDLWindow`, `SDLPlatform<T>`): GL context, monitors, DPI, HDR, fullscreen       |
| `events/`                     | `OSEvent`: typed window system events, several listeners per window                                                    |
| `devices/`                    | `IInput` (keyboard, mouse, gamepads), `Key`, `GamepadButton`, `GamepadAxis`; SDL3 backend                              |
| `clipboard/`                  | Clipboard text, file / folder / message dialogs                                                                        |
| `filesystem/`                 | `Paths` (user directories), `FileWatcher` (ReadDirectoryChangesW / inotify), `AsyncIO` (IOCP / io_uring / thread pool) |
| `lib_loader/`                 | `DynLib`, `ModuleLoader`                                                                                               |
| `thread/`, `atomic/`, `time/` | Named / prioritised / pinned threads, `HardwareConcurrency()`, `SpinLock`, `NowNanoseconds()`, `PreciseSleep()`        |
| `process/`                    | Environment, executable path, `Process::Launch`, reveal in file manager                                                |
| `crash/`                      | Crash report + minidump, crash hooks, Ctrl+C / SIGTERM handler                                                         |
| `network/`                    | `Socket` (UDP / TCP), `NamedPipe`, `SharedMemory`                                                                      |
| `hardware/`                   | CPU features, RAM, OS, per-process memory (CPU and GPU)                                                                |

## Building on Linux

SDL3 is built from source and needs the development packages of the window system (X11 / Wayland, ALSA / PulseAudio, libudev...). `liburing-dev` is optional: with it the async IO uses io_uring (when the kernel allows it), without it a small thread pool does the same job. `AsyncIO::BackendName()` tells which one is in use.

## Gamepads

Connected pads are numbered in the order they were plugged in:

```cpp
auto* input = GetEngine().GetInputManager();
if (input->GetGamepadCount() > 0) {
    float x = input->GetGamepadAxis(0, Input::GamepadAxis::LeftX);   // dead zone applied
    if (input->WasGamepadButtonPressed(0, Input::GamepadButton::South))
        input->SetGamepadRumble(0, 0.5f, 0.5f, 200);
}
```
