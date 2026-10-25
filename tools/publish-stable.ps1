# Publish the tested build (build\src\Release) as a "stable copy" in a fixed
# folder, so that development builds never disturb the copy used for printing.
#
# Usage (from the repository root, or via the "Pubblica stabile" VS Code task):
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\publish-stable.ps1 [-Dest C:\MentorSlicer] [-NoShortcut]
#
# What it does:
#   0. checks every source (exe, mentorslicer-mcp.exe, slicer/FFmpeg/WebView2 DLLs, mesa\, resources\)
#      BEFORE closing anything;
#   1. closes a running MentorSlicer.exe started from the destination folder (files in use
#      cannot be replaced) through tools\close-build-instance.ps1: graceful close first, and
#      it refuses while that instance runs a Print Advisor preheat or slow cooldown, or does
#      not answer (a forced kill would skip the automatic bed switch-off);
#   2. marks stable-version.txt as incomplete, then mirrors the exe, the DLLs and mesa\
#      (no .pdb/.lib/.exp) with robocopy, and removes the DLLs the build no longer has;
#      a mentorslicer-mcp.exe kept open by an MCP client is renamed out of the way first;
#   3. mirrors the REAL resources\ folder of the repository (the build folder
#      only holds a junction to it);
#   4. writes the final stable-version.txt with commit and date, only after every copy succeeded;
#   5. creates a desktop shortcut "MentorSlicer" if missing and removes the shortcut of the
#      old Creality Print copy (F8 transition; that copy stays in its folder as a fallback).
#
# The development build and this copy share the same user data dir (%APPDATA%\MentorSlicer),
# so profiles and settings are common; run only one instance at a time with the Print
# Advisor MCP enabled. The old Creality Print copy keeps its own data folder: after the F8
# import the two diverge.

[CmdletBinding()]
param(
    [string]$Dest = 'C:\MentorSlicer',
    [switch]$NoShortcut
)

$ErrorActionPreference = 'Stop'
$root  = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$build = Join-Path $root 'build\src\Release'
$exe   = Join-Path $build 'MentorSlicer.exe'

# Full path without a trailing backslash: "C:\MentorSlicer" must not match
# "C:\MentorSlicer-old" when looking for running instances.
$Dest = [IO.Path]::GetFullPath($Dest).TrimEnd('\')
if ($Dest.Length -le 3) { throw "Cartella di destinazione non valida: $Dest" }
if (($Dest + '\').StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "La destinazione non puo' stare nel repository: $Dest" }

# 0. Validate every source before closing anything.
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "Build non trovata: $exe. Esegui prima 'Build EXE'." }
$slicerDll = Join-Path $build 'MentorSlicer_Slicer.dll'
if (-not (Test-Path -LiteralPath $slicerDll -PathType Leaf)) { throw "DLL mancante: $slicerDll. Esegui prima 'Build EXE'." }
$mesaSrc = Join-Path $build 'mesa'
if (-not (Test-Path -LiteralPath $mesaSrc -PathType Container)) { throw "Cartella mesa mancante: $mesaSrc" }
# Runtime DLLs loaded by the application (FFmpeg for the RTSP video, Mesa for the software OpenGL fallback).
foreach ($required in @('avformat-62.dll', 'avcodec-62.dll', 'avutil-60.dll', 'swresample-6.dll', 'WebView2Loader.dll',
                        'mesa\opengl32.dll', 'mesa\libgallium_wgl.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $build $required) -PathType Leaf)) { throw "DLL mancante: $(Join-Path $build $required)" }
}
# Print Advisor MCP sidecar packaged by tools\build-mcp-exe.bat (needs Node.js at build time).
$mcpExeName = 'mentorslicer-mcp.exe'
if (-not (Test-Path -LiteralPath (Join-Path $build $mcpExeName) -PathType Leaf)) {
    throw "$mcpExeName mancante in $build. Installa Node.js ed esegui di nuovo 'Build EXE'."
}
$resLink = Join-Path $build 'resources'
if (-not (Test-Path -LiteralPath $resLink -PathType Container)) { throw "Cartella resources mancante: $resLink" }
$resSrc = Get-Item -LiteralPath $resLink
if ($resSrc.LinkType) { $resSrc = Get-Item -LiteralPath (@($resSrc.Target)[0]) }
foreach ($required in @('profiles', 'i18n', 'web')) {
    if (-not (Test-Path -LiteralPath (Join-Path $resSrc.FullName $required) -PathType Container)) {
        throw "resources incompleta: manca $required in $($resSrc.FullName)"
    }
}

# 1. Close the stable instance, if running (graceful; refuses during a preheat/cooldown).
& (Join-Path $PSScriptRoot 'close-build-instance.ps1') -PathPrefix ($Dest + '\')
if ($LASTEXITCODE -ne 0) { throw "Copia stabile non chiusa: pubblicazione annullata, nessun file copiato." }

New-Item -ItemType Directory -Force $Dest | Out-Null

# Marked incomplete until every copy below has succeeded.
$stampFile = Join-Path $Dest 'stable-version.txt'
Set-Content -Path $stampFile -Encoding UTF8 -Value @(
    "MentorSlicer - copia stabile",
    "Stato: INCOMPLETA - pubblicazione iniziata $(Get-Date -Format 'yyyy-MM-dd HH:mm') e non terminata"
)

function Invoke-Robocopy {
    param([string[]]$RoboArgs)
    # /R:2 /W:2: a locked file fails after a few seconds instead of the default 1,000,000 retries.
    & robocopy @RoboArgs /R:2 /W:2 /NFL /NDL /NJH /NP | Out-Host
    # robocopy exit codes < 8 mean success (0 = nothing copied).
    if ($LASTEXITCODE -ge 8) { throw "robocopy fallito (codice $LASTEXITCODE): $($RoboArgs -join ' ')" }
    $global:LASTEXITCODE = 0
}

# 2. Executable, DLLs and mesa (software OpenGL fallback). The build folder may still hold the
# pre-F8 CrealityPrint.exe and its DLL: they are not copied.
Write-Host "Copio exe e DLL in $Dest"
# An MCP client (Claude Code, Codex) keeps mentorslicer-mcp.exe open while its session runs: a running
# executable cannot be overwritten but can be renamed. The renamed copy is deleted by a later run.
Get-ChildItem -LiteralPath $Dest -Filter "$mcpExeName.old-*" -File -ErrorAction SilentlyContinue | ForEach-Object {
    try { Remove-Item -LiteralPath $_.FullName -Force -ErrorAction Stop } catch { }
}
$mcpDest = Join-Path $Dest $mcpExeName
if (Test-Path -LiteralPath $mcpDest -PathType Leaf) {
    try {
        Remove-Item -LiteralPath $mcpDest -Force -ErrorAction Stop
    } catch {
        $old = "$mcpExeName.old-$((Get-Date).ToString('yyyyMMddHHmmss'))"
        Write-Host "$mcpExeName in uso da un client MCP: rinominato in $old (il client usera' il nuovo al prossimo avvio)"
        Rename-Item -LiteralPath $mcpDest -NewName $old
    }
}
Invoke-Robocopy @($build, $Dest, '*.exe', '*.dll', '/XF', '*.pdb', '*.lib', '*.exp', 'CrealityPrint.exe', 'CrealityPrint_Slicer.dll')
Invoke-Robocopy @($mesaSrc, (Join-Path $Dest 'mesa'), '/MIR')
# DLLs left by an older build (e.g. the 2021 FFmpeg package) would stay next to the exe and could be
# picked up by LoadLibrary: drop every top-level DLL of the copy that the build no longer has.
Get-ChildItem -LiteralPath $Dest -Filter '*.dll' -File | Where-Object {
    -not (Test-Path -LiteralPath (Join-Path $build $_.Name) -PathType Leaf)
} | ForEach-Object {
    Write-Host "Tolgo la DLL non piu' usata: $($_.Name)"
    Remove-Item -LiteralPath $_.FullName -Force
}

# 3. Resources: the build folder has a junction, copy the real tree (resolved above).
Write-Host "Copio resources da $($resSrc.FullName)"
Invoke-Robocopy @($resSrc.FullName, (Join-Path $Dest 'resources'), '/MIR', '/XD', '.git')

# 4. Version stamp.
# git is optional here: a missing git or a stderr line must not abort the script after a complete
# copy (that would leave stable-version.txt marked INCOMPLETA).
$commit = '?'; $branch = '?'; $dirty = ''
Push-Location $root
try {
    $ErrorActionPreference = 'Continue'
    try {
        $value = (git rev-parse --short HEAD 2>$null); if ($value) { $commit = $value }
        $value = (git rev-parse --abbrev-ref HEAD 2>$null); if ($value) { $branch = $value }
        if (git status --porcelain --untracked-files=no 2>$null) { $dirty = ' (modifiche non committate)' }
    } catch {
        Write-Host "git non disponibile: commit non registrato ($($_.Exception.Message))"
    }
    $global:LASTEXITCODE = 0
} finally {
    $ErrorActionPreference = 'Stop'
    Pop-Location
}
$stamp = @(
    "MentorSlicer - copia stabile",
    "Stato:      completa",
    "Pubblicata: $(Get-Date -Format 'yyyy-MM-dd HH:mm')",
    "Commit:     $commit su $branch$dirty",
    "Build exe:  $((Get-Item $exe).LastWriteTime.ToString('yyyy-MM-dd HH:mm'))"
)
Set-Content -Path $stampFile -Value $stamp -Encoding UTF8
$stamp | ForEach-Object { Write-Host "  $_" }

# 5. Desktop shortcut.
if (-not $NoShortcut) {
    $desktop = [Environment]::GetFolderPath('Desktop')
    $lnk = Join-Path $desktop 'MentorSlicer.lnk'
    if (-not (Test-Path -LiteralPath $lnk)) {
        $sh = New-Object -ComObject WScript.Shell
        $sc = $sh.CreateShortcut($lnk)
        $sc.TargetPath       = Join-Path $Dest 'MentorSlicer.exe'
        $sc.WorkingDirectory = $Dest
        $sc.IconLocation     = (Join-Path $Dest 'MentorSlicer.exe') + ',0'
        $sc.Description      = 'MentorSlicer con Print Advisor (copia stabile)'
        $sc.Save()
        Write-Host "Creato collegamento sul desktop: $lnk"
    }
    # F8: the shortcut of the old copy (its folder stays as a fallback, started by hand).
    $legacyLnk = Join-Path $desktop 'Creality Print Advisor.lnk'
    if (Test-Path -LiteralPath $legacyLnk) {
        Remove-Item -LiteralPath $legacyLnk -Force
        Write-Host "Tolto il vecchio collegamento: $legacyLnk"
    }
}

Write-Host "Copia stabile aggiornata in $Dest"
