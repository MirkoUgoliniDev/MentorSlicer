@REM OcarSlicer build script for Windows
@echo off
setlocal
set WP=%CD%

@REM Version shown in the splash screen, the About box and the update check (version.inc
@REM falls back to 7.0.0.0). Keep in sync with build_exe_only.bat. PROJECT_VERSION_EXTRA is
@REM deliberately NOT set: it stays "Alpha" (only an explicit --datadir uses it in the folder name).
set "CP_VERSION=7.3.0.0"

@REM Prefer the CMake bundled with VS 2022. Strawberry Perl also installs a
@REM MinGW CMake and libraries under C:\Strawberry\c; letting those precede
@REM the MSVC toolchain contaminates CMake's package and include discovery.
@REM Pack deps
if "%1"=="pack" (
    setlocal ENABLEDELAYEDEXPANSION 
    cd %WP%/deps/build
    for /f "tokens=2-4 delims=/ " %%a in ('date /t') do set build_date=%%c%%b%%a
    echo packing deps: OrcaSlicer_dep_win64_!build_date!_vs2022.zip

    %WP%/tools/7z.exe a OrcaSlicer_dep_win64_!build_date!_vs2022.zip OrcaSlicer_dep
    exit /b 0
)

set "VS_CMAKE_BIN=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
if not exist "%VS_CMAKE_BIN%\cmake.exe" (
    set "VS_CMAKE_BIN=%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
)
if not exist "%VS_CMAKE_BIN%\cmake.exe" (
    set "VS_CMAKE_BIN=%ProgramFiles%\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
)
if not exist "%VS_CMAKE_BIN%\cmake.exe" (
    set "VS_CMAKE_BIN=%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
)
if not exist "%VS_CMAKE_BIN%\cmake.exe" (
    echo ERROR: Visual Studio 2022 CMake not found.
    echo Looked under "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\..."
    echo          and "%ProgramFiles%\Microsoft Visual Studio\2022\..."
    exit /b 1
)
set "PATH=%PATH:C:\Strawberry\c\bin;=%"
set "PATH=%PATH:;C:\Strawberry\c\bin=%"
if defined INCLUDE set "INCLUDE=%INCLUDE:C:\Strawberry\c\include;=%"
if defined INCLUDE set "INCLUDE=%INCLUDE:;C:\Strawberry\c\include=%"
if defined LIB set "LIB=%LIB:C:\Strawberry\c\lib;=%"
if defined LIB set "LIB=%LIB:;C:\Strawberry\c\lib=%"
set "CMAKE_PREFIX_PATH="
set "CMAKE_INCLUDE_PATH="
set "CMAKE_LIBRARY_PATH="
set "PATH=%VS_CMAKE_BIN%;%PATH%"
echo Using CMake: %VS_CMAKE_BIN%\cmake.exe

set debug=OFF
set debuginfo=OFF
if "%1"=="debug" set debug=ON
if "%2"=="debug" set debug=ON
if "%1"=="debuginfo" set debuginfo=ON
if "%2"=="debuginfo" set debuginfo=ON
if "%debug%"=="ON" (
    set build_type=Debug
    set build_dir=build-dbg
) else (
    if "%debuginfo%"=="ON" (
        set build_type=RelWithDebInfo
        set build_dir=build-dbginfo
    ) else (
        set build_type=Release
        set build_dir=build
    )
)
echo build type set to %build_type%

setlocal DISABLEDELAYEDEXPANSION 
cd deps
mkdir %build_dir%
cd %build_dir%
set DEPS=%CD%/OrcaSlicer_dep

if "%1"=="slicer" (
    GOTO :slicer
)
echo "building deps.."

set "CMAKE_FRESH_ARG="
if exist CMakeCache.txt (
    findstr /I /R /C:"CMAKE_C_COMPILER:.*Strawberry" /C:"CMAKE_CXX_COMPILER:.*Strawberry" /C:"CMAKE_MAKE_PROGRAM:.*Strawberry" CMakeCache.txt >nul
    if not errorlevel 1 set "CMAKE_FRESH_ARG=--fresh"
)
echo cmake ../ -G "Visual Studio 17 2022" -A x64 -DDESTDIR="%CD%/OrcaSlicer_dep" -DCMAKE_BUILD_TYPE=%build_type% -DDEP_DEBUG=%debug% -DORCA_INCLUDE_DEBUG_INFO=%debuginfo%
cmake %CMAKE_FRESH_ARG% ../ -G "Visual Studio 17 2022" -A x64 -DDESTDIR="%CD%/OrcaSlicer_dep" -DCMAKE_BUILD_TYPE=%build_type% -DDEP_DEBUG=%debug% -DORCA_INCLUDE_DEBUG_INFO=%debuginfo%
if errorlevel 1 exit /b 1
cmake --build . --config %build_type% --target deps -- -m
if errorlevel 1 exit /b 1

if "%1"=="deps" exit /b 0

:slicer
echo "building Orca Slicer..."
cd %WP%
mkdir %build_dir%
cd %build_dir%

echo cmake .. -G "Visual Studio 17 2022" -A x64 -DCREALITYPRINT_VERSION=%CP_VERSION% -DCMAKE_PREFIX_PATH="%DEPS%/usr/local" -DCMAKE_INSTALL_PREFIX="./OrcaSlicer" -DCMAKE_BUILD_TYPE=%build_type%
set "CMAKE_FRESH_ARG="
if exist CMakeCache.txt (
    findstr /I /R /C:"CMAKE_C_COMPILER:.*Strawberry" /C:"CMAKE_CXX_COMPILER:.*Strawberry" /C:"CMAKE_MAKE_PROGRAM:.*Strawberry" CMakeCache.txt >nul
    if not errorlevel 1 set "CMAKE_FRESH_ARG=--fresh"
)
cmake %CMAKE_FRESH_ARG% .. -G "Visual Studio 17 2022" -A x64 -DCREALITYPRINT_VERSION=%CP_VERSION% -DCMAKE_PREFIX_PATH="%DEPS%/usr/local" -DCMAKE_INSTALL_PREFIX="./OrcaSlicer" -DCMAKE_BUILD_TYPE=%build_type%
if errorlevel 1 exit /b 1
cmake --build . --config %build_type% --target ALL_BUILD -- -m
if errorlevel 1 exit /b 1
@REM Print Advisor MCP sidecar next to MentorSlicer.exe; the install target copies it.
call "%WP%\tools\build-mcp-exe.bat" "%WP%\%build_dir%\src\%build_type%"
cd ..
call run_gettext.bat
if errorlevel 1 exit /b 1
cd %build_dir%
cmake --build . --target install --config %build_type%
if errorlevel 1 exit /b 1
