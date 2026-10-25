@echo off
@REM Builds mentorslicer-mcp.exe (the Print Advisor MCP sidecar packaged with Node.js, see
@REM tools\print-advisor-mcp\scripts\build-exe.mjs) and copies it into the folder given as the
@REM first argument, next to MentorSlicer.exe. Users of the build then need no Node.js.
@REM Rebuilt only when the sidecar sources change. Node.js is needed only here, at build time;
@REM without it the build goes on and the Print Advisor dialog shows the Node.js setup instead.
@REM Used by build_exe_only.bat and build_release_vs2022.bat.
setlocal
set "MCP_DIR=%~dp0print-advisor-mcp"
set "MCP_TARGET=%~1"
if "%MCP_TARGET%"=="" (
    echo build-mcp-exe: missing target folder.
    exit /b 2
)
where node >nul 2>nul
if errorlevel 1 (
    echo WARNING: node not found in PATH, mentorslicer-mcp.exe not built.
    exit /b 0
)
if exist "%MCP_DIR%\node_modules\esbuild\package.json" if exist "%MCP_DIR%\node_modules\postject\package.json" goto :build
echo Installing the MCP sidecar packages ^(npm ci^)...
pushd "%MCP_DIR%"
call npm ci --no-audit --no-fund
set "NPM_ERR=%errorlevel%"
popd
if not "%NPM_ERR%"=="0" (
    echo WARNING: npm ci failed, mentorslicer-mcp.exe not built.
    exit /b 0
)
:build
node "%MCP_DIR%\scripts\build-exe.mjs" --copy-to "%MCP_TARGET%"
if errorlevel 1 echo WARNING: mentorslicer-mcp.exe not built ^(see above^).
exit /b 0
