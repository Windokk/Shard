# ShardReflect

A reflection tool for the Shard Game Engine

Works for Linux and Windows (MinGW)

## Dependencies

- LLVM
- Clang
- Zlib

## CLI options

-f : Specifies a file for which to generate reflection

--dir : Specifies a directory in which to look for files (for reflection generation)

--recursive : Should we look recursively through the directories ?

--clang : Path to the clang lib root (usually /usr/lib/clang/<version> on Linux) (mandatory)

--cpp : Path to the stdlibc++ root (usually /usr/include/c++/<version> on Linux) (mandatory)

-I : Path to the include dir of Shard (usually <Path/To/Shard>/src)

## Regenerating the engine's files

`bash scripts/regen_reflection.sh` (from the root of the repository, after building ShardReflect) runs the tool on every header that has `CLASS()` / `STRUCT()` annotations and writes the `*.reflection.hpp` files next to them. They are committed, and the CI (`.github/workflows/reflection.yml`) regenerates them and fails when the result differs from what is committed : never edit them by hand, change the annotations instead.

## What a header can say

- `CLASS()` / `STRUCT()` on the type, `FIELD(...)` on its fields.
- `FIELD(Editable)` / `FIELD(ReadOnly)`, and `FIELD(Editable, range=<min>|<max>)` for the bounds of the editor's widget (`range=0.0f|1.0f`, `range=1|0` when only the minimum matters).
- A class gets a descriptor only if it declares one (`DECLARE_DESCRIPTOR()`) : an abstract annotated base (such as `Volume`) has none, and its fields are part of the descriptor of each class derived from it.
- Each header only produces its own file : the annotated classes it includes are left to their own header.
