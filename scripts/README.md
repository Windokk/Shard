# scripts/

Development, testing and code-generation tools for Shard. The `.sh` scripts are run with `bash scripts/<name>.sh` from the repository root (Git Bash / MSYS2 on Windows), the `.py` ones with `python scripts/<name>.py`.

## Setup

### `install_linux_deps.sh`
Installs the development libraries Shard needs on Debian / Ubuntu through `apt` (ninja, pkg-config, X11, Wayland, ALSA / PulseAudio, udev, DRM / GBM, Mesa...). SDL3 is built from source (`submodules/SDL`) and only enables its backends (X11, Wayland, audio, input) when the matching packages are found: without them it still builds, but cannot open a window. A compiler, CMake (3.28.2+) and git must already be installed.

### `patch_imgui.sh`
Replaces the `<vulkan/vulkan.h>` include of `submodules/imgui/backends/imgui_impl_vulkan.h` with the engine's glad loader from the RHI. Run it after `git submodule update --init --recursive`. Fails if the patch could not be applied (the include is no longer in the file).

## Player tests

These scripts use `build/ShardGame` and the `plugins/game/bin/*/libGameModule` module: build the project first.

### `smoke_test.sh`
Boot test: starts the player (`ShardGame`) on `example_project`, loads a world, renders a few frames and shuts down. Fails if the player crashes, hangs, does not reach the end, or logs an `[ERROR]` / `[FATAL]` line (unreadable file, shader that does not compile...). It does not judge the picture. On Linux without a display it starts a virtual one (`xvfb-run`, Mesa's llvmpipe software OpenGL).
Variables: `SHARD_SMOKE_WORLD` (default `cornell.world`), `SHARD_SMOKE_FRAMES` (30), `SHARD_SMOKE_TIMEOUT` (300 s), `SHARD_SMOKE_API` (`opengl`, the default, or `vulkan`), `SHARD_SMOKE_VIEW` (`"px py pz tx ty tz [fov]"`, a camera for a world that has none).
With `SHARD_SMOKE_API=vulkan` the validation layers are on (`SHARD_SMOKE_VALIDATION=` empty turns them off) and their messages, which the engine logs as `[ERROR]`, fail the test. `cornell.world` has no camera, so the engine skips every render pass on it : the CI's Vulkan smoke test uses `physics.world`.

### `parity_test.sh`
Fails when two renderings of one world differ. It runs `compare_frames.py apis` with limits (and its blank picture guard) :
- `bash scripts/parity_test.sh pipelines` : forward+ and deferred on Vulkan, `physics.world`, one camera. This is what the CI runs on Mesa's software Vulkan (lavapipe). The CI's OpenGL (llvmpipe) has no `GL_ARB_bindless_texture`, so it cannot be compared there.
- `bash scripts/parity_test.sh apis` : OpenGL against Vulkan, `sponza.world`, the 4 views of `reference_views.json`. Needs a GPU : run it before a change to a backend or to a shader.
Variables : `SHARD_PARITY_WORLD`, `SHARD_PARITY_WARMUP`, `SHARD_PARITY_TIMEOUT`, `SHARD_PARITY_MAX_MEAN`, `SHARD_PARITY_MAX_P99`, `SHARD_PARITY_MAX_OVER`, `SHARD_PARITY_ARGS` (more options for `compare_frames.py apis`, for example `"--raytrace 64 4 --max-rt-bias 0.5"`). The pictures and the difference maps go to `build/api_compare/<world>_<mode>/`. The limits come from measures on an RTX 5060 and are written at the top of the script.

### `stress_test.sh`
Robustness test: runs the player through the console-command script `stress_script.txt`, once with the render thread and once without. Fails on a crash, a hang, an `[ERROR]` / `[FATAL]` line, or a graphics call made from the wrong thread.
Variables: `SHARD_STRESS_FRAMES` (1200), `SHARD_STRESS_TIMEOUT` (300 s).

### `stress_script.txt`
The scenario of the stress test, one command per line as `<frame> <command>`: framebuffer resizes, play mode in and out, world loads, MSAA toggles, thumbnails, render-thread and memory stats dumps.

### `bench.sh`
Benchmark: runs the player on a world (Sponza by default) and prints the `[BENCH]` lines: frame time, CPU time breakdown, GPU time of every pass, render thread, and memory (RAM, VRAM, GPU allocations by kind). Extra arguments are passed to the player (e.g. `+render.thread=0 --view-mode unlit`). Note: `build/` is a Debug build by default, so only the GPU figures are comparable.
Variables: `SHARD_BENCH_WORLD`, `SHARD_BENCH_FRAMES` (900, the first 120 are ignored), `SHARD_BENCH_VSYNC` (1 keeps it on), and the regression guards `SHARD_BENCH_MIN_FPS` and `SHARD_BENCH_MAX_VRAM_MB`.

### `compare_frames.py`
Compares pictures. Three commands (`--help` after one lists its options); `diff` and `apis` exit with 1 when the pictures break `--max-mean`, `--max-p99` or `--max-over`, so they can gate a CI step. Standard library only.
- `python scripts/compare_frames.py diff a.png b.png`: how two pictures (`.png`, or linear `.hdr`) of the same size differ: mean error (in levels of 255, per channel), bias, RMSE, PSNR, percentiles of the pixel error, the share of pixels off by more than 1 to 32 levels, the areas that differ most. Writes `diff.<a>.<b>.png` (A | B | difference map, black = same, white = `--gain` levels off) and `.json`.
- `python scripts/compare_frames.py apis --world sponza.world`: renders the world with `--api opengl`, then `--api vulkan`, from the same views (same console script, same seed, `timescale 0`), one PNG per view, and runs `diff` on each pair. Writes `opengl/`, `vulkan/` (pictures, `commands.txt`, `player.log`), `diff.<view>.png` and `report.json` to `build/api_compare/<world>/`. Views: `--view` (repeatable, as for `reference`), else the world's in `reference_views.json`. `--warmup 60` frames by default (use about 900 to let the live GI probes settle), `--pipeline`, `--command`, `--width`/`--height`, `--reuse` (compare the pictures already in `--out`).
  - The two sides are `--a` and `--b` (default `opengl` and `vulkan`), each `api` or `api:pipeline`, so `--a vulkan:forward_plus --b vulkan:deferred` compares two pipelines on one API. The folders and the reports are named after the sides (`vulkan.deferred/`).
  - With `--max-mean`, `--max-p99` or `--max-over` a picture that is blank (its levels span less than 8) fails : two pictures where nothing was drawn (a world without a camera, a shader that did not compile) are identical, and a test must not pass on that. `--allow-blank` accepts it.
  - `--raytrace SPP BOUNCES` also path traces each view (`raytrace.render`, `--rt-width` x `--rt-height`, 160x90 by default) on both sides and compares the two `.rt.hdr`. The noise is not the same on the two APIs, so only the bias is judged (`--max-rt-bias`, levels of 255 per channel).
- `python scripts/compare_frames.py reference --world sponza.world`: the real-time renderer against the path tracer, one lighting term at a time (direct, indirect diffuse, indirect specular, emission: see the renderer docs). It runs the player on a world and, for each view (`render.camera`), renders the reference with `raytrace.render ... terms` and captures one linear frame per term with `render.lighting_terms` + `render.screenshot`. Then, over all views and for each view, it reports each term's share of the light, the overall bias in stops, the error per area (too bright / too dark) and the error relative to the whole picture. Writes `report.html`, `report.json`, `commands.txt` and `player.log` to `build/reference_compare/<world>/`, and one folder per view with its pictures and one `compare.<term>.png` (reference | real time | error map). Standard library only.
- `reference` defaults: 640x360, 256 spp, 900 warm-up frames (live GI probes need about 800 in a Debug build)
- `--view "name=px py pz tx ty tz [fov]"` (repeatable, `camera` is the world's own camera); by default, the world's views in `reference_views.json`
- `--keep-reference`: render only the real-time side of the views whose reference is already there (to iterate on the real-time renderer)
- `--pipeline forward_plus`, `--api vulkan`, `--command "<console command>"`, `--width`/`--height`, `--spp`, `--bounces`, `--warmup`, `--block` (area size in pixels), `--out`
- `--reuse`: recompute the report from the pictures already in `--out`

### `reference_views.json`
The views `compare_frames.py` takes of each world (position, point looked at, vertical field of view). Keep them fixed, so the measurements stay comparable from one change to the next.

## Architecture checks

### `check_deps.py`
Checks the engine's layering: an `#include` from module A to module B is only allowed if B is in a strictly lower layer than A (peers and upward includes are violations). The current violations are frozen in `deps_baseline.json`; the check fails when something gets worse and reports when something improves.
- `python scripts/check_deps.py`: check against the baseline
- `--update`: lower the baseline to the current state
- `--matrix`: print every cross-module dependency
- `--force`: allow the baseline to be raised

### `deps_baseline.json`
The baseline of `check_deps.py`: the tolerated dependency edges and the forbidden patterns (`gl-call-outside-backend`, `os-header-in-engine`, `sdl-in-engine`). It is currently empty: no violations. Do not edit it by hand.

## Code generation

Generated files are committed: when the sources that feed them change, re-run the generator and commit the result. Tests and CI check that they are up to date.

### `regen_reflection.sh`
Regenerates the `*.reflection.hpp` files with `ShardReflect`, next to every header that has `CLASS()` / `STRUCT()` annotations. Optional argument: path to `ShardReflect` (default `build/tools/ShardReflect[.exe]`, to be built first, it needs LLVM + Clang). Needs `clang` and `g++` on the PATH, or `SHARD_CLANG_RESOURCE_DIR` and `SHARD_CPP_DIR`. CI (`.github/workflows/reflection.yml`) fails if the output differs from what is committed.

### `gen_api_reference.py`
Writes `docs/docs/api_reference.md` from the comments of the SDK headers (`sdk/include/shard/*.h` for the C ABI, `sdk/include/shard/cpp/*.hpp` for the C++ SDK). It also checks that the revision each header announces matches the one the engine offers in `query_interface` (`src/engine/plugins/abi/c_plugin.cpp`). Never edit the page by hand.
- `python scripts/gen_api_reference.py`: write the page
- `--check`: fail (exit 1) if the page is out of date (test `Docs.ApiReference`)

### `gen_wasm_imports.py`
Generates what a sandboxed (tier 2, WebAssembly) plugin and the host agree on, from the SDK's C headers:
- `sdk/include/shard/shard_wasm_imports.h`: the module's side (one import per function of every interface, the API tables, `query_interface`, `ShardHostApi`);
- `src/engine/plugins/wasm/wasm_layout_gen.hpp`: the wasm32 layout of every struct (4-byte pointers), the permissions and the interfaces the sandbox offers;
- `src/engine/plugins/wasm/wasm_imports_gen.cpp`: the host's side, for functions whose arguments are numbers, strings, buffers and plain structs. Functions that take callbacks or hand out pointers into host memory are written by hand (the `MANUAL` list, whose completeness the generator checks).

`python scripts/gen_wasm_imports.py` writes the three files; `--check` fails if one is out of date (test `Wasm.GeneratedImports`). See `docs/design-notes/wasm-sandbox.fr.md`.
