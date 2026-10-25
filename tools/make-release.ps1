# Package the tested build (build\src\Release) as a portable Windows zip for a GitHub release.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\make-release.ps1 [-RepoPath <clone>] [-OutDir <folder>] [-Version 1.0.1] [-NoSmokeTest]
#
# The zip MentorSlicer-<version>-win64.zip holds one folder with:
#   MentorSlicer.exe, its DLLs, mentorslicer-mcp.exe, mesa\, resources\ (the real tree, not the
#   junction of the build folder), LICENSE.txt, README.md, and the Visual C++ runtime DLLs the
#   binaries import (app-local deployment of Microsoft.VC14x.CRT, so no redistributable install).
#   The Universal C Runtime (api-ms-win-crt-*) is part of Windows 10 and 11.
# Next to it: <zip>.sha256. Then a smoke test unpacks the zip into a temporary folder, starts
# MentorSlicer.exe with a temporary --datadir (no printer, no user data), waits for its main
# window (at most 60 s) and closes it, then runs the offline MCP smoke test on the packaged
# mentorslicer-mcp.exe (needs node and tools\print-advisor-mcp\node_modules).
# Version: by default MENTORSLICER_VERSION of src\slic3r\GUI\MentorSlicerVersion.hpp.

[CmdletBinding()]
param(
    [string]$RepoPath = '',
    [string]$OutDir = '',
    [string]$Version = '',
    [switch]$NoSmokeTest
)

$ErrorActionPreference = 'Stop'
if (-not $RepoPath) { $RepoPath = Join-Path $PSScriptRoot '..' }
$root  = (Resolve-Path -LiteralPath $RepoPath).Path
$build = Join-Path $root 'build\src\Release'
if (-not $OutDir) { $OutDir = Join-Path $root 'build\release' }

if (-not $Version) {
    $header = Get-Content -Raw (Join-Path $root 'src\slic3r\GUI\MentorSlicerVersion.hpp')
    if ($header -notmatch 'MENTORSLICER_VERSION\s+"([^"]+)"') { throw 'MENTORSLICER_VERSION non trovato.' }
    $Version = $Matches[1]
}
$name = "MentorSlicer-$Version-win64"
Write-Host "Pacchetto $name da $build"

# Sources, checked as in publish-stable.ps1.
foreach ($required in @('MentorSlicer.exe', 'MentorSlicer_Slicer.dll', 'mentorslicer-mcp.exe', 'WebView2Loader.dll',
                        'avformat-62.dll', 'avcodec-62.dll', 'avutil-60.dll', 'swresample-6.dll',
                        'mesa\opengl32.dll', 'mesa\libgallium_wgl.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $build $required) -PathType Leaf)) { throw "Manca $required in $build. Esegui prima 'Build EXE'." }
}
$resSrc = Get-Item -LiteralPath (Join-Path $build 'resources')
if ($resSrc.LinkType) { $resSrc = Get-Item -LiteralPath (@($resSrc.Target)[0]) }

# Visual C++ runtime: the redist folder of the toolset in use (VCToolsRedistDir, set by VsDevCmd),
# otherwise the newest one of the Visual Studio found by vswhere.
$redistRoots = @()
if ($env:VCToolsRedistDir) { $redistRoots += $env:VCToolsRedistDir }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path -LiteralPath $vswhere) {
    foreach ($vs in @(& $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)) {
        $msvc = Join-Path $vs 'VC\Redist\MSVC'
        if (Test-Path -LiteralPath $msvc) {
            $redistRoots += Get-ChildItem -LiteralPath $msvc -Directory | Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } |
                Sort-Object { [version]$_.Name } -Descending | ForEach-Object { $_.FullName }
        }
    }
}
$redist = $redistRoots | ForEach-Object { Get-ChildItem -LiteralPath (Join-Path $_ 'x64') -Directory -Filter 'Microsoft.VC14*.CRT' -ErrorAction SilentlyContinue } |
    Select-Object -First 1
if (-not $redist) { throw 'Runtime Visual C++ (x64\Microsoft.VC14*.CRT) non trovato: esegui lo script da un prompt di Visual Studio o installa i Build Tools.' }
$crt = @('vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll', 'msvcp140_codecvt_ids.dll', 'concrt140.dll')

# Staging folder.
New-Item -ItemType Directory -Force $OutDir | Out-Null
$stage = Join-Path $OutDir $name
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory $stage | Out-Null

function Invoke-Robocopy {
    param([string[]]$RoboArgs)
    & robocopy @RoboArgs /R:2 /W:2 /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy fallito (codice $LASTEXITCODE): $($RoboArgs -join ' ')" }
    $global:LASTEXITCODE = 0
}
Invoke-Robocopy @($build, $stage, '*.exe', '*.dll', '/XF', '*.pdb', '*.lib', '*.exp', 'CrealityPrint.exe', 'CrealityPrint_Slicer.dll')
Invoke-Robocopy @((Join-Path $build 'mesa'), (Join-Path $stage 'mesa'), '/E')
Invoke-Robocopy @($resSrc.FullName, (Join-Path $stage 'resources'), '/E', '/XD', '.git')
foreach ($dll in $crt) { Copy-Item -LiteralPath (Join-Path $redist.FullName $dll) -Destination $stage }
Copy-Item -LiteralPath (Join-Path $root 'LICENSE.txt') -Destination $stage
Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination $stage
Write-Host "Runtime Visual C++ da $($redist.FullName)"

# Zip (Windows tar writes zip archives; much faster than Compress-Archive).
$zip = Join-Path $OutDir "$name.zip"
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
& "$env:SystemRoot\System32\tar.exe" -a -c -f $zip -C $OutDir $name
if ($LASTEXITCODE -ne 0) { throw 'Creazione dello zip non riuscita.' }
$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $zip).Hash.ToLower()
Set-Content -LiteralPath "$zip.sha256" -Value "$hash  $name.zip" -Encoding ASCII
Write-Host ("Creato {0} ({1:N0} MB), SHA-256 {2}" -f $zip, ((Get-Item $zip).Length / 1MB), $hash)

# Smoke test: unpack and start with a temporary data folder.
if (-not $NoSmokeTest) {
    $test = Join-Path ([IO.Path]::GetTempPath()) "mentorslicer-release-test-$PID"
    New-Item -ItemType Directory -Force $test | Out-Null
    try {
        & "$env:SystemRoot\System32\tar.exe" -x -f $zip -C $test
        if ($LASTEXITCODE -ne 0) { throw 'Estrazione dello zip non riuscita.' }
        $exe = Join-Path $test "$name\MentorSlicer.exe"
        $data = Join-Path $test 'data'
        $proc = Start-Process -FilePath $exe -ArgumentList "--datadir `"$data`"" -WorkingDirectory (Split-Path $exe) -PassThru
        # The main window must appear: an error box at startup would keep the process alive too.
        $deadline = (Get-Date).AddSeconds(60)
        do {
            Start-Sleep -Seconds 2
            $proc.Refresh()
        } until ($proc.HasExited -or $proc.MainWindowTitle -like '*MentorSlicer*' -or (Get-Date) -gt $deadline)
        if ($proc.HasExited) { throw "Prova non riuscita: MentorSlicer si e' chiuso subito (codice $($proc.ExitCode))." }
        if ($proc.MainWindowTitle -notlike '*MentorSlicer*') {
            Stop-Process -Id $proc.Id -Force
            throw "Prova non riuscita: nessuna finestra principale di MentorSlicer (titolo: '$($proc.MainWindowTitle)')."
        }
        Write-Host "Prova riuscita: MentorSlicer avviato dal pacchetto ($($proc.MainWindowTitle))."
        $null = $proc.CloseMainWindow()
        if (-not $proc.WaitForExit(15000)) { Stop-Process -Id $proc.Id -Force }

        # The packaged MCP server: offline smoke test (tool list and "MentorSlicer is not open").
        $smoke = Join-Path $root 'tools\print-advisor-mcp\scripts\smoke.mjs'
        if ((Get-Command node -ErrorAction SilentlyContinue) -and (Test-Path -LiteralPath (Join-Path $root 'tools\print-advisor-mcp\node_modules'))) {
            & node $smoke --offline --exe (Join-Path $test "$name\mentorslicer-mcp.exe")
            if ($LASTEXITCODE -ne 0) { throw 'Prova non riuscita: mentorslicer-mcp.exe del pacchetto non risponde come previsto.' }
        } else {
            Write-Warning 'node o tools\print-advisor-mcp\node_modules mancanti: prova di mentorslicer-mcp.exe saltata.'
        }
    } finally {
        Start-Sleep -Seconds 2
        Remove-Item -LiteralPath $test -Recurse -Force -ErrorAction SilentlyContinue
    }
}
