@echo off
REM Configure the build (usage: build.bat [Debug|Release|RelWithDebInfo] - omit to keep the build folder's current type)
set BUILD_TYPE_ARG=
if not "%~1"=="" set BUILD_TYPE_ARG=-DCMAKE_BUILD_TYPE=%~1
cmake -S . -B build -G "MinGW Makefiles" %BUILD_TYPE_ARG%
IF %ERRORLEVEL% NEQ 0 (
    echo [ERROR] CMake configuration failed.
    exit /b %ERRORLEVEL%
)

cd build

REM Build reflection
cd tools

REM ShardReflect --clang C:/msys64/mingw64/lib/clang/21 --cpp C:/msys64/mingw64/include/c++/15.2.0 --dir ..\..\src\engine\world\components --dir ..\..\src\engine\renderer\components --dir ..\..\src\engine\physics --dir ..\..\src\engine\audio -I "..\..\src;..\..\submodules\;..\..\submodules\json\single_include;..\..\submodules\jolt;..\..\submodules\glm;..\..\submodules\freetype\include"
REM ShardReflect --clang C:/msys64/mingw64/lib/clang/21 --cpp C:/msys64/mingw64/include/c++/15.2.0 -f ..\..\src\apps\game\character.hpp -I "..\..\src;..\..\submodules\;..\..\submodules\json\single_include;..\..\submodules\jolt;..\..\submodules\glm;..\..\submodules\freetype\include"
cd ..

REM Build the project
cmake --build . -j 8
IF %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Build failed.
    exit /b %ERRORLEVEL%
)

REM Run the editor only if build succeeded
echo [INFO] Build succeeded. Starting editor...
start "" ./ShardGame.exe --game libGameModule.dll --project ..\\example_project\\example_project.json --api opengl