@REM MentorSlicer - build the executable only, skipping the install/package step.
@REM
@REM Unlike build_release_vs2022.bat this script does NOT run the "install"
@REM target, so it does not copy
@REM the ~10000 resource files into build\OrcaSlicer\ on every iteration.
@REM
@REM The build tree (build\src\<config>\) is already runnable: CMake stages all
@REM runtime DLLs there as a post-build step and symlinks the resources dir.
@REM
@REM Usage:
@REM   build_exe_only.bat                 release build, incremental
@REM   build_exe_only.bat debug           debug build      (build-dbg)
@REM   build_exe_only.bat debuginfo       RelWithDebInfo   (build-dbginfo)
@REM   build_exe_only.bat run             build, then launch the executable
@REM   build_exe_only.bat debug run       combine options in any order
@REM
@REM This script assumes the dependencies are already built. If they are not,
@REM run "build_release_vs2022.bat deps" first.
@echo off
setlocal
set WP=%CD%

@REM Version shown in the splash screen, the About box and the update check.
@REM version.inc falls back to 7.0.0.0 when CMake is not told otherwise (Creality's
@REM CI passes it on the command line). Keep in sync with build_release_vs2022.bat.
@REM PROJECT_VERSION_EXTRA is deliberately NOT set: it stays "Alpha" (BuildInfoUtil.cmake).
@REM Since F8 the default data folder is %APPDATA%\MentorSlicer; only an explicit --datadir
@REM still gets the "<data version> Alpha" subfolder.
set "CP_VERSION=7.3.0.0"

if not exist "%WP%\CMakeLists.txt" (
    echo ERROR: run this script from the repository root.
    exit /b 1
)

@REM --- Locate the CMake bundled with VS 2022 -------------------------------
@REM Strawberry Perl ships a MinGW CMake plus libraries under C:\Strawberry\c;
@REM letting those precede the MSVC toolchain contaminates CMake's package and
@REM include discovery, so they are stripped from the environment below.
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

@REM --- Parse arguments -----------------------------------------------------
set debug=OFF
set debuginfo=OFF
set launch=OFF
for %%a in (%*) do (
    if /I "%%a"=="debug"     set debug=ON
    if /I "%%a"=="debuginfo" set debuginfo=ON
    if /I "%%a"=="run"       set launch=ON
)

if "%debug%"=="ON" (
    set build_type=Debug
    set build_dir=build-dbg
    set dep_dir=build-dbg
) else (
    if "%debuginfo%"=="ON" (
        set build_type=RelWithDebInfo
        set build_dir=build-dbginfo
        set dep_dir=build-dbginfo
    ) else (
        set build_type=Release
        set build_dir=build
        set dep_dir=build
    )
)
echo Build type: %build_type%  ^(dir: %build_dir%^)

@REM --- Locate the prebuilt dependencies ------------------------------------
set "DEPS=%WP%/deps/%dep_dir%/OrcaSlicer_dep"
if not exist "%WP%\deps\%dep_dir%\OrcaSlicer_dep\usr\local" (
    echo ERROR: dependencies not found at "%WP%\deps\%dep_dir%\OrcaSlicer_dep\usr\local"
    echo Build them first with: build_release_vs2022.bat deps
    exit /b 1
)

@REM --- Configure -----------------------------------------------------------
if not exist "%WP%\%build_dir%" mkdir "%WP%\%build_dir%"
cd /d "%WP%\%build_dir%"

@REM Only a Strawberry *toolchain* in the cache is a problem. A stray Strawberry
@REM library path (e.g. libtiff.a picked up by wxWidgets) is harmless, so match
@REM the compiler/make entries specifically instead of the whole prefix -
@REM otherwise every run would needlessly reconfigure from scratch.
set "CMAKE_FRESH_ARG="
if exist CMakeCache.txt (
    findstr /I /R /C:"CMAKE_C_COMPILER:.*Strawberry" /C:"CMAKE_CXX_COMPILER:.*Strawberry" /C:"CMAKE_MAKE_PROGRAM:.*Strawberry" CMakeCache.txt >nul
    if not errorlevel 1 (
        echo Strawberry toolchain detected in CMake cache: reconfiguring from scratch.
        set "CMAKE_FRESH_ARG=--fresh"
    )
)

if not exist CMakeCache.txt goto :configure
if defined CMAKE_FRESH_ARG goto :configure
@REM F8: a cache from before the MentorSlicer rename knows only the old solution and the old
@REM app_gui target name; "cmake --build --target" would not find the new ones.
findstr /R /C:"^CMAKE_PROJECT_NAME:STATIC=MentorSlicer$" CMakeCache.txt >nul
if errorlevel 1 (
    echo CMake cache predates the MentorSlicer rename: reconfiguring.
    goto :configure
)
@REM The automatic regeneration triggered by "cmake --build" (ZERO_CHECK) reuses the
@REM cached variables, so a changed CP_VERSION only reaches the build if we run the
@REM configure step ourselves with the new -D value.
findstr /R /C:"^CREALITYPRINT_VERSION:.*=%CP_VERSION%$" CMakeCache.txt >nul
if errorlevel 1 (
    echo CREALITYPRINT_VERSION in the CMake cache differs from %CP_VERSION%: reconfiguring.
    goto :configure
)
echo Reusing existing CMake cache ^(skipping configure^).
goto :build

:configure
echo Configuring...
cmake %CMAKE_FRESH_ARG% .. -G "Visual Studio 17 2022" -A x64 -DCREALITYPRINT_VERSION=%CP_VERSION% -DCMAKE_PREFIX_PATH="%DEPS%/usr/local" -DCMAKE_INSTALL_PREFIX="./OrcaSlicer" -DCMAKE_BUILD_TYPE=%build_type%
if errorlevel 1 (
    echo.
    echo BUILD FAILED: CMake configure step.
    exit /b 1
)

:build
@REM --- Build the GUI executable only --------------------------------------
@REM MentorSlicer_app_gui produces MentorSlicer.exe and pulls in the slicer
@REM DLL plus every runtime dependency it needs. Building this target instead
@REM of ALL_BUILD skips the installer-only helpers.
@REM A failed automatic regeneration (ZERO_CHECK) does not fail MSBuild: the build would go on
@REM with the old project files. Run it first and stop if CMake reported an error.
set "ZC_LOG=%TEMP%\mentorslicer_zero_check.log"
cmake --build . --config %build_type% --target ZERO_CHECK > "%ZC_LOG%" 2>&1
set "ZC_ERR=%errorlevel%"
type "%ZC_LOG%"
findstr /C:"CMake Configure step failed" /C:"Configuring incomplete" "%ZC_LOG%" >nul
if not errorlevel 1 set "ZC_ERR=1"
del "%ZC_LOG%" >nul 2>&1
if not "%ZC_ERR%"=="0" (
    echo.
    echo BUILD FAILED: CMake regeneration step.
    exit /b 1
)
echo Building target MentorSlicer_app_gui...
cmake --build . --config %build_type% --target MentorSlicer_app_gui -- -m
if errorlevel 1 (
    echo.
    echo BUILD FAILED.
    exit /b 1
)

set "EXE=%WP%\%build_dir%\src\%build_type%\MentorSlicer.exe"
if not exist "%EXE%" (
    echo.
    echo BUILD FAILED: expected executable not found at
    echo   %EXE%
    exit /b 1
)

@REM --- Italian translation ---------------------------------------------------
@REM The install target normally runs run_gettext.bat; here only the Italian
@REM catalogue is recompiled, and only when a .po is newer than the .mo.
@REM CrealityPrint_it.po is Creality's file, kept as upstream ships it; the
@REM Print Advisor strings live in PrintAdvisor_it.po. The two are merged
@REM (our entries win) with tools\msgcat.exe if present, otherwise with
@REM tools\web-patches\merge-po.mjs, and the result goes to msgfmt.
set "SRC_ROOT=%~dp0"
set "PO_IT=%SRC_ROOT%localization\i18n\it\CrealityPrint_it.po"
set "PO_PA=%SRC_ROOT%localization\i18n\it\PrintAdvisor_it.po"
set "PO_MERGED=%WP%\%build_dir%\CrealityPrint_it.merged.po"
set "MO_IT=%SRC_ROOT%resources\i18n\it\CrealityPrint.mo"
if not exist "%PO_IT%" goto :mo_done
if not exist "%SRC_ROOT%tools\msgfmt.exe" goto :mo_done
if not exist "%MO_IT%" goto :mo_build
powershell.exe -NoProfile -Command "$m = (Get-Item -LiteralPath $env:MO_IT).LastWriteTimeUtc; foreach ($p in @($env:PO_IT, $env:PO_PA)) { if ((Test-Path -LiteralPath $p) -and (Get-Item -LiteralPath $p).LastWriteTimeUtc -gt $m) { exit 1 } }; exit 0"
if not errorlevel 1 goto :mo_done
:mo_build
echo Compiling the Italian translation...
if not exist "%SRC_ROOT%resources\i18n\it" mkdir "%SRC_ROOT%resources\i18n\it"
set "PO_SRC=%PO_IT%"
if not exist "%PO_PA%" goto :mo_fmt
if exist "%SRC_ROOT%tools\msgcat.exe" goto :mo_msgcat
where node >nul 2>nul
if errorlevel 1 goto :mo_merge_failed
node "%SRC_ROOT%tools\web-patches\merge-po.mjs" "%PO_MERGED%" "%PO_IT%" "%PO_PA%"
if errorlevel 1 goto :mo_merge_failed
set "PO_SRC=%PO_MERGED%"
goto :mo_fmt
:mo_msgcat
"%SRC_ROOT%tools\msgcat.exe" --use-first -o "%PO_MERGED%" "%PO_PA%" "%PO_IT%"
if errorlevel 1 goto :mo_merge_failed
set "PO_SRC=%PO_MERGED%"
goto :mo_fmt
:mo_merge_failed
echo WARNING: cannot merge PrintAdvisor_it.po ^(node or msgcat missing/failed^):
echo          the Print Advisor strings will not be translated.
echo The Italian .mo was NOT rebuilt; it will be retried on the next build.
goto :mo_done
:mo_fmt
"%SRC_ROOT%tools\msgfmt.exe" --check-format -o "%MO_IT%" "%PO_SRC%"
if errorlevel 1 echo WARNING: msgfmt failed, the Italian translation was not updated.
:mo_done

@REM --- Print Advisor web patches -------------------------------------------
@REM The compiled pages in resources\web are replaced by every Creality update;
@REM report (without failing the build) any Print Advisor patch that is missing.
where node >nul 2>nul
if errorlevel 1 goto :web_nonode
node "%SRC_ROOT%tools\web-patches\apply-web-patches.mjs" --check --root "%SRC_ROOT%."
if not errorlevel 1 goto :web_done
echo.
echo WARNING: some Print Advisor web patches are missing ^(see the table above^).
echo          Apply them with: node tools\web-patches\apply-web-patches.mjs --apply
echo          and port the NOT FOUND ones by hand: doc\MCP\web-bundle-patches.md
goto :web_done
:web_nonode
echo WARNING: node not found in PATH, Print Advisor web patches not checked.
:web_done

@REM --- Print Advisor MCP sidecar --------------------------------------------
@REM mentorslicer-mcp.exe next to MentorSlicer.exe, so users need no Node.js.
call "%SRC_ROOT%tools\build-mcp-exe.bat" "%WP%\%build_dir%\src\%build_type%"

echo.
echo ===============================================================
echo  Build OK  ^(install/package step skipped^)
echo  Executable: %EXE%
echo ===============================================================

if "%launch%"=="ON" (
    echo Launching...
    start "" "%EXE%"
)

cd /d "%WP%"
exit /b 0
