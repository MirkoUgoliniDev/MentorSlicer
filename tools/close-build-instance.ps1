# Close the MentorSlicer.exe instances started from a folder, gracefully when possible.
# Until the old Creality Print copy is deleted (F8 transition) the old executable name
# CrealityPrint.exe is closed too, and its legacy data folder is probed for a descriptor.
#
# Used by the hidden VS Code task "Chiudi MentorSlicer di build" (default: every
# MentorSlicer.exe under <repo>\build*) and by tools\publish-stable.ps1 (-PathPrefix
# <stable folder>\). Never touches an instance started from anywhere else.
#
# Safety: a forced kill skips MainFrame::shutdown, so the Print Advisor shutdown hook that
# switches off a preheat or a slow cooldown bed never runs. Therefore:
#   1. if one of the target processes is the instance serving the Print Advisor (its pid is in
#      <APPDATA>\MentorSlicer\cache\print-advisor\endpoint.json, or for an old-name build in the
#      legacy Creality Print data folder), its read-only
#      GET /v1/monitor_status is queried (3 s timeout). The script aborts with exit code 1 when:
#        - a preheat is running or waits for its automatic bed switch-off;
#        - a slow cooldown is running, or an interrupted one still owes its final setpoint;
#        - the status cannot be read (the app does not answer) or endpoint.json exists
#          but cannot be parsed (after one retry);
#        - a target process has no readable path (elevated process: cannot tell where it runs).
#      An armed exhaust guard only prints a warning: it has no bed-safety impact.
#   2. otherwise each process gets CloseMainWindow() and up to -GraceSeconds (20 s) to exit;
#      only a process still running after that is killed with Stop-Process -Force.
#
# -Force skips every check of step 1 (never passed by the VS Code tasks): use it only when the
# user has verified the printer by hand. The session token of the descriptor is sent only to the
# loopback endpoint and never printed.
#
# Exit codes: 0 = nothing to close or all closed; 1 = aborted or a process could not be stopped.

[CmdletBinding()]
param(
    # Folder prefixes (no wildcards); a process matches when its exe path starts with one of them.
    [string[]]$PathPrefix,
    [int]$GraceSeconds = 20,
    [string]$DataDir = (Join-Path $env:APPDATA 'MentorSlicer'),
    # F8 transition: data folder of the old Creality Print copy (CrealityPrint.exe). Remove with it.
    [string]$LegacyDataDir = (Join-Path $env:APPDATA 'Creality\Creality Print\7.3 Alpha'),
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

if (-not $PathPrefix -or $PathPrefix.Count -eq 0) {
    # Default: <repo>\build* (build, build_debug, ...), like the original task.
    $repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
    $PathPrefix = @(Join-Path $repo 'build')
}
$PathPrefix = @($PathPrefix | ForEach-Object { [IO.Path]::GetFullPath($_) })

# CrealityPrint: builds made before the F8 rename (and the old copy, filtered out by the path prefix).
$all = @(Get-Process MentorSlicer, CrealityPrint -ErrorAction SilentlyContinue)
$unknown = @($all | Where-Object { -not $_.Path })
if ($unknown.Count -gt 0) {
    Write-Host "ATTENZIONE: $($unknown.ProcessName -join ', ') (PID $($unknown.Id -join ', ')) gira con privilegi diversi e non so da quale cartella e' stato avviato."
    if (-not $Force) {
        Write-Host 'Chiudi MentorSlicer (o la vecchia Creality Print) a mano, oppure riesegui senza privilegi elevati. Nessuna istanza chiusa.'
        exit 1
    }
}
$targets = @($all | Where-Object {
    $exePath = $_.Path
    $exePath -and ($PathPrefix | Where-Object { $exePath.StartsWith($_, [StringComparison]::OrdinalIgnoreCase) })
})
if ($targets.Count -eq 0) {
    Write-Host "Nessun MentorSlicer in esecuzione da $($PathPrefix -join ', ')"
    exit 0
}

# --- 1. Print Advisor background actions --------------------------------------------------
function Get-AdvisorMonitorStatus {
    param([string]$BaseUrl, [string]$Token)
    $uri = [Uri]$BaseUrl
    if ($uri.Scheme -ne 'http' -or $uri.Host -ne '127.0.0.1' -or $uri.IsDefaultPort) {
        throw 'descrittore endpoint.json non valido (base_url non locale)'
    }
    $request = [Net.HttpWebRequest]::Create([Uri]::new($uri, '/v1/monitor_status'))
    $request.Method = 'GET'
    $request.Proxy = $null              # loopback only, never through a proxy
    $request.AllowAutoRedirect = $false
    $request.Timeout = 3000
    $request.ReadWriteTimeout = 3000
    $request.Accept = 'application/json'
    $request.Headers['Authorization'] = "Bearer $Token"
    $response = $request.GetResponse()
    try {
        $reader = New-Object IO.StreamReader($response.GetResponseStream(), [Text.Encoding]::UTF8)
        return ($reader.ReadToEnd() | ConvertFrom-Json)
    } finally { $response.Close() }
}

# Parsed descriptor, or $null when the file is missing. One retry after 500 ms covers a file
# that is being rewritten; a file that still cannot be parsed throws.
function Read-Descriptor {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    for ($attempt = 0; ; $attempt++) {
        try { return (Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json) }
        catch {
            if ($attempt -ge 1) { throw "endpoint.json illeggibile: $Path" }
            Start-Sleep -Milliseconds 500
        }
    }
}

# True when a slow-cooldown final setpoint is still owed. The field is null when none; it may be
# a scalar (older builds), an array of {address, to} or an object keyed by address.
function Test-Pending {
    param($Value)
    if ($null -eq $Value) { return $false }
    if ($Value -is [bool]) { return $Value }
    if ($Value -is [array]) { return $Value.Count -gt 0 }
    if ($Value -is [pscustomobject]) { return @($Value.PSObject.Properties).Count -gt 0 }
    if ($Value -is [string]) { return $Value -ne '' }
    return $true
}

if (-not $Force) {
    # The descriptor of the instance being closed: the MentorSlicer data folder first, then the
    # legacy Creality Print one (a build made before the F8 rename writes there).
    $candidates = @(
        (Join-Path $DataDir 'cache\print-advisor\endpoint.json'),
        (Join-Path $LegacyDataDir 'cache\print-advisor\endpoint.json')
    )
    $descriptor = $null
    $served = $null
    $seen = $false
    foreach ($candidate in $candidates) {
        try { $parsed = Read-Descriptor $candidate }
        catch {
            Write-Host "$($_.Exception.Message): non posso sapere se MentorSlicer ha un preriscaldo attivo."
            Write-Host 'Chiudi MentorSlicer a mano (oppure riesegui con -Force dopo aver controllato la stampante).'
            exit 1
        }
        if (-not $parsed) { continue }
        $seen = $true
        $match = if ($parsed.pid) { $targets | Where-Object { $_.Id -eq [int]$parsed.pid } } else { $null }
        if ($match) { $descriptor = $parsed; $served = $match; break }
    }
    if (-not $seen) {
        Write-Host 'Nessun descrittore Print Advisor trovato: il Print Advisor non e'' attivo in queste istanze.'
    }

    if ($served) {
        $status = $null
        $failure = $null
        try {
            if (-not $descriptor.base_url -or -not $descriptor.token) { throw 'descrittore endpoint.json non valido' }
            $status = Get-AdvisorMonitorStatus -BaseUrl $descriptor.base_url -Token $descriptor.token
        } catch {
            # Message only: the request never carries the token into the exception text.
            $failure = $_.Exception.Message
        }
        if (-not $status -or $status.success -ne $true) {
            Write-Host "MentorSlicer non risponde (PID $($served.Id): $failure)."
            Write-Host 'Non posso verificare preriscaldo o raffreddamento: chiudi MentorSlicer a mano, poi riprova (oppure riesegui con -Force dopo aver controllato la stampante).'
            exit 1
        }
        $active = @()
        $preheat = $status.preheat
        if ($preheat -and ($preheat.active -eq $true -or ($preheat.auto_off -and $preheat.auto_off.pending -eq $true))) { $active += 'preriscaldo' }
        $cooldown = $status.slow_cooldown
        if ($cooldown -and $cooldown.active -eq $true) { $active += 'raffreddamento lento' }
        elseif ($cooldown -and (Test-Pending $cooldown.final_setpoint_pending)) { $active += 'temperatura finale del raffreddamento ancora da inviare' }
        if ($active.Count -gt 0) {
            Write-Host "Preriscaldo/raffreddamento attivo ($($active -join ', ')) in MentorSlicer (PID $($served.Id)): chiudi MentorSlicer a mano o annullalo prima."
            Write-Host 'Una chiusura forzata salterebbe lo spegnimento automatico del piatto.'
            exit 1
        }
        if ($status.exhaust_guard -and $status.exhaust_guard.armed -eq $true) {
            Write-Host "ATTENZIONE: la guardia dell'estrazione e' armata; chiudendo MentorSlicer si ferma."
        }
    } elseif ($seen) {
        Write-Host 'Nessuna delle istanze da chiudere serve il Print Advisor.'
    }
}

# --- 2. Graceful close, then force -------------------------------------------------------
Write-Host "Chiudo MentorSlicer (PID $($targets.Id -join ', '))"
foreach ($p in $targets) {
    try { [void]$p.CloseMainWindow() } catch { }
}
$deadline = (Get-Date).AddSeconds($GraceSeconds)
while ((Get-Date) -lt $deadline -and ($targets | Where-Object { -not $_.HasExited })) {
    Start-Sleep -Milliseconds 500
    $targets | ForEach-Object { $_.Refresh() }
}
$stuck = @($targets | Where-Object { -not $_.HasExited })
if ($stuck.Count -gt 0) {
    Write-Host "MentorSlicer non si e' chiuso entro $GraceSeconds s (finestra aperta?): chiusura forzata di PID $($stuck.Id -join ', ')"
    $stuck | Stop-Process -Force -ErrorAction SilentlyContinue
    $stuck | Wait-Process -Timeout 15 -ErrorAction SilentlyContinue
    if ($stuck | Where-Object { -not $_.HasExited }) {
        Write-Host 'Impossibile terminare MentorSlicer.'
        exit 1
    }
}
exit 0
