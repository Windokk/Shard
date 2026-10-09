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

REM The reflection files are committed : after changing a reflected class run  bash scripts/regen_reflection.sh  (CI checks they are up to date)
cd ..

REM Build the project
cmake --build . -j 8
IF %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Build failed.
    exit /b %ERRORLEVEL%
)

REM Run the editor only if build succeeded
echo [INFO] Build succeeded. Starting editor...
start "" ./ShardEditor.exe --game libGameModule.dll --project ..\\example_project\\example_project.json --api opengl --vsync 0
