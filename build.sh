#!/bin/bash
# Configure the build
cmake -S . -B build -G "Unix Makefiles"
if [ $? -ne 0 ]; then
    echo "[ERROR] CMake configuration failed."
    exit $?
fi

cd build

# Build reflection
cd tools

# ShardReflect --clang C:/msys64/mingw64/lib/clang/21 --cpp C:/msys64/mingw64/include/c++/15.2.0 --dir ../../src/engine/world/components --dir ../../src/engine/renderer/components --dir ../../src/engine/physics --dir ../../src/engine/audio -I "../../src;../../submodules/;../../submodules/json/single_include;../../submodules/jolt;../../submodules/glm;../../submodules/freetype/include"
# ShardReflect --clang C:/msys64/mingw64/lib/clang/21 --cpp C:/msys64/mingw64/include/c++/15.2.0 -f ../../src/apps/game/character.hpp -I "../../src;../../submodules/;../../submodules/json/single_include;../../submodules/jolt;../../submodules/glm;../../submodules/freetype/include"
cd ..

# Build the project
cmake --build . -j 8
if [ $? -ne 0 ]; then
    echo "[ERROR] Build failed."
    exit $?
fi

# Run the editor only if build succeeded
echo "[INFO] Build succeeded. Starting editor..."
./ShardEditor --game libGameModule.so --project ../example_project/example_project.json --api opengl
