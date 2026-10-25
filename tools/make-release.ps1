# Package the tested build (build\src\Release) as a portable Windows zip for a GitHub release.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\make-release.ps1 [-RepoPath <clone>] [-OutDir <folder>] [-Version 1.0.1] [-NoSmokeTest]
#
# The zip MentorSlicer-<version>-win64.zip holds one folder with:
#   MentorSlicer.exe, its DLLs, mentorslicer-mcp.exe, mesa\, resources\ (the real tree, not the
#   junction of the build folder), LICENSE.txt, README.md, and the Visual C++ runtime DLLs the
#   binaries import (app-local deployment of Microsoft.VC143.CRT, so no redistributable install).
#   The Universal C Runtime (api-ms-win-crt-*) is part of Windows 10 and 11.
# Next to it: <zip>.sha256. Then a smoke test unpacks the zip into a temporary folder and starts
# MentorSlicer.exe with a temporary --datadir (no printer, no user data), checks that it is still
# running after 25 s and closes it.
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

# Visual C++ runtime of the newest MSVC redist folder of Visual Studio 2022.
$redist = Get-ChildItem 'C:\Program Files*\Microsoft Visual Studio\2022\*\VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT' -Directory -ErrorAction SilentlyContinue |
    Sort-Object { [version]($_.Parent.Parent.Name -replace '[^0-9.]', '') } -Descending | Select-Object -First 1
if (-not $redist) { throw 'Cartella Microsoft.VC143.CRT di Visual Studio 2022 non trovata.' }
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
        $exe = Join-Path $test "$name\MentorSlicer.exe"
        $data = Join-Path $test 'data'
        $proc = Start-Process -FilePath $exe -ArgumentList "--datadir `"$data`"" -WorkingDirectory (Split-Path $exe) -PassThru
        Start-Sleep -Seconds 25
        if ($proc.HasExited) { throw "Prova non riuscita: MentorSlicer si e' chiuso subito (codice $($proc.ExitCode))." }
        Write-Host 'Prova riuscita: MentorSlicer avviato dal pacchetto.'
        $null = $proc.CloseMainWindow()
        if (-not $proc.WaitForExit(15000)) { Stop-Process -Id $proc.Id -Force }
    } finally {
        Start-Sleep -Seconds 2
        Remove-Item -LiteralPath $test -Recurse -Force -ErrorAction SilentlyContinue
    }
}
