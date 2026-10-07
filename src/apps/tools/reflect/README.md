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
