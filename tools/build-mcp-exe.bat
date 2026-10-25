@echo off
@REM Builds mentorslicer-mcp.exe (the Print Advisor MCP sidecar packaged with Node.js, see
@REM tools\print-advisor-mcp\scripts\build-exe.mjs) and copies it into the folder given as the
@REM first argument, next to MentorSlicer.exe. Users of the build then need no Node.js.
@REM Rebuilt only when the sidecar sources, its packages or the Node version change; build-exe.mjs
@REM runs "npm ci" when package-lock.json changed. Node.js is needed only here, at build time.
@REM On any failure the build goes on with a warning and no mentorslicer-mcp.exe is left in the
@REM target folder, so publish-stable.ps1 and make-release.ps1 stop instead of shipping an old one;
@REM the Print Advisor dialog then shows the Node.js setup.
@REM Used by build_exe_only.bat and build_release_vs2022.bat.
setlocal
set "MCP_DIR=%~dp0print-advisor-mcp"
set "MCP_TARGET=%~1"
if "%MCP_TARGET%"=="" (
    echo build-mcp-exe: missing target folder.
    exit /b 2
)
where node >nul 2>nul
if not errorlevel 1 goto :build
echo WARNING: node not found in PATH, mentorslicer-mcp.exe not built.
if exist "%MCP_TARGET%\mentorslicer-mcp.exe" del /q "%MCP_TARGET%\mentorslicer-mcp.exe" >nul 2>nul
exit /b 0
:build
node "%MCP_DIR%\scripts\build-exe.mjs" --copy-to "%MCP_TARGET%"
if errorlevel 1 echo WARNING: mentorslicer-mcp.exe not built ^(see above^); it was removed from the target folder.
exit /b 0
