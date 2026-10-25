# Network watch for MentorSlicer without installing tools and without admin rights
# (doc/specs/F0-baseline-verifica-spec.md, section 5.4). Windows PowerShell 5.1.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\net-watch.ps1 [-Minutes 5] [-IntervalSeconds 1]
#              [-PathPrefix <repo>\build\src\Release\] [-OutCsv <file>] [-OutTxt <file>]
#
# 1. Finds the MentorSlicer.exe processes (only those whose path starts with -PathPrefix, when given).
# 2. Every poll rebuilds the descendant process set (msedgewebview2.exe starts late and the web pages
#    live there), lists the TCP connections owned by that set and accumulates the DNS cache
#    (IP -> names) because its entries expire.
# 3. At the end every remote IP is resolved (DNS cache map, then PTR) and classified:
#    LAN (private/link-local), LOCAL (loopback), INFO (Microsoft/WebView2), FLAG (Creality, Google
#    Analytics/Firebase, Baidu, Aliyun, Bambu), WARN (any other public address).
# 4. DNS-only pass: cache entries matching the flag patterns that are new since the start snapshot
#    are reported as "FLAG (dns only)". This is system-wide, so other programs can cause them.
#
# Exit codes: 0 = no FLAG; 1 = at least one FLAG; 2 = app not found or error. WARN does not change
# the exit code. At F0 (pre-lean build) exit 1 is expected: that CSV is the "before" picture.
#
# Limitations: a connection shorter than -IntervalSeconds is caught only by the DNS-only pass;
# TIME_WAIT connections lose their owning PID (0) and are not attributed; if WebView2 resolves names
# with DNS-over-HTTPS they never reach the Windows DNS cache (only the TCP list with PTR names
# remains). Resource Monitor (resmon, Rete tab) on the same session is the cross-check.

[CmdletBinding()]
param(
    [double]$Minutes = 5,
    [int]$IntervalSeconds = 1,
    [string]$PathPrefix,
    [string]$OutCsv,
    [string]$OutTxt
)

$ErrorActionPreference = 'Stop'

$flagPattern = '(^|\.)(crealitycloud\.(com|cn)|creality\.(com|cn)|cxswyjy\.com|googletagmanager\.com|google-analytics\.com|analytics\.google\.com|firebase(installations)?\.googleapis\.com|firebaseio\.com|app-measurement\.com|doubleclick\.net|baidu\.com|aliyuncs\.com|bambulab\.(com|cn))\.?$'
$infoPattern = '(^|\.)(microsoft\.com|msedge\.net|windows\.com|azureedge\.net|msftconnecttest\.com|windowsupdate\.com|live\.com|office\.net|bing\.com)\.?$'
$skipAddresses = @('0.0.0.0', '::', '127.0.0.1', '::1')

$repoRoot = Split-Path -Parent $PSScriptRoot
$stamp = Get-Date -Format 'yyyyMMdd-HHmm'
if (-not $OutCsv) { $OutCsv = Join-Path $repoRoot ("tests\regression\out\net-watch-" + $stamp + ".csv") }
if (-not $OutTxt) { $OutTxt = [System.IO.Path]::ChangeExtension($OutCsv, '.txt') }

$summary = New-Object System.Collections.Generic.List[string]
function Say([string]$text) { Write-Host $text; [void]$summary.Add($text) }

function Get-RootProcesses {
    $procs = @(Get-CimInstance Win32_Process -Filter "Name='MentorSlicer.exe'")
    if ($PathPrefix) {
        $procs = @($procs | Where-Object { $_.ExecutablePath -and $_.ExecutablePath.StartsWith($PathPrefix, [System.StringComparison]::OrdinalIgnoreCase) })
    }
    return $procs
}

# Root PIDs plus every descendant, walking ParentProcessId until nothing is added.
function Get-Tree([int[]]$roots) {
    $all = Get-CimInstance Win32_Process | Select-Object ProcessId, ParentProcessId, Name
    $set = New-Object 'System.Collections.Generic.HashSet[int]'
    $names = @{}
    foreach ($r in $roots) { [void]$set.Add($r) }
    foreach ($p in $all) { $names[[int]$p.ProcessId] = $p.Name }
    do {
        $added = 0
        foreach ($p in $all) {
            if ($set.Contains([int]$p.ParentProcessId) -and $set.Add([int]$p.ProcessId)) { $added++ }
        }
    } while ($added -gt 0)
    return @{ Set = $set; Names = $names }
}

function Test-PrivateAddress([string]$ip) {
    if ($ip -match '^10\.') { return $true }
    if ($ip -match '^172\.(1[6-9]|2[0-9]|3[01])\.') { return $true }
    if ($ip -match '^192\.168\.') { return $true }
    if ($ip -match '^169\.254\.') { return $true }
    if ($ip -match '^(?i)fe[89ab][0-9a-f]:') { return $true }
    if ($ip -match '^(?i)f[cd][0-9a-f]{2}:') { return $true }
    return $false
}

function Get-Class([string]$ip, [string[]]$names) {
    if ($skipAddresses -contains $ip -or $ip -match '^127\.') { return 'LOCAL' }
    if (Test-PrivateAddress $ip) { return 'LAN' }
    foreach ($n in $names) { if ($n -match $flagPattern) { return 'FLAG' } }
    foreach ($n in $names) { if ($n -match $infoPattern) { return 'INFO' } }
    return 'WARN'
}

try {
    $roots = Get-RootProcesses
    if (-not $roots -or $roots.Count -eq 0) {
        Write-Host 'MentorSlicer non trovato' -ForegroundColor Red
        if ($PathPrefix) { Write-Host "  (nessun MentorSlicer.exe con percorso che inizia per $PathPrefix)" }
        exit 2
    }
    $rootPids = @($roots | ForEach-Object { [int]$_.ProcessId })
    Say ("net-watch: {0} min, poll {1} s, start {2}" -f $Minutes, $IntervalSeconds, (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
    foreach ($r in $roots) { Say ("  root pid {0}: {1}" -f $r.ProcessId, $r.ExecutablePath) }

    # DNS snapshot at start: keys Entry|Data, used by the DNS-only pass.
    $dns0 = @{}
    foreach ($d in @(Get-DnsClientCache -ErrorAction SilentlyContinue)) { $dns0[[string]$d.Entry + '|' + [string]$d.Data] = $true }

    $ipNames = @{}      # IP -> hashtable of names
    $seen = @{}         # "ip:port" -> record
    $treeNames = @{}    # pid -> process name (last seen)
    $maxTree = 0
    $polls = 0
    $end = (Get-Date).AddMinutes($Minutes)
    while ((Get-Date) -lt $end) {
        $polls++
        $tree = Get-Tree $rootPids
        $set = $tree.Set
        if ($set.Count -gt $maxTree) { $maxTree = $set.Count }
        foreach ($pidKey in $set) { $treeNames[$pidKey] = $tree.Names[$pidKey] }

        foreach ($d in @(Get-DnsClientCache -ErrorAction SilentlyContinue)) {
            if ($d.Type -ne 1 -and $d.Type -ne 28) { continue }
            $ip = [string]$d.Data
            if (-not $ipNames.ContainsKey($ip)) { $ipNames[$ip] = @{} }
            $ipNames[$ip][[string]$d.Entry] = $true
        }

        foreach ($c in @(Get-NetTCPConnection -ErrorAction SilentlyContinue)) {
            if (-not $set.Contains([int]$c.OwningProcess)) { continue }
            if ($skipAddresses -contains $c.RemoteAddress) { continue }
            $k = [string]$c.RemoteAddress + ':' + [string]$c.RemotePort
            $now = Get-Date
            if (-not $seen.ContainsKey($k)) {
                $seen[$k] = [pscustomobject]@{
                    Ip = [string]$c.RemoteAddress; Port = [int]$c.RemotePort; ProcessId = [int]$c.OwningProcess
                    Process = $tree.Names[[int]$c.OwningProcess]; First = $now; Last = $now; States = @{}
                }
            } else { $seen[$k].Last = $now }
            $seen[$k].States[[string]$c.State] = $true
        }
        Start-Sleep -Seconds $IntervalSeconds
    }
    Say ("polls: {0}, process tree up to {1} processes: {2}" -f $polls, $maxTree, (($treeNames.GetEnumerator() | ForEach-Object { "$($_.Value)($($_.Key))" }) -join ', '))

    # Resolve and classify the TCP endpoints.
    $rows = New-Object System.Collections.Generic.List[object]
    foreach ($rec in $seen.Values) {
        $names = @()
        if ($ipNames.ContainsKey($rec.Ip)) { $names = @($ipNames[$rec.Ip].Keys) }
        $source = 'dns-cache'
        if ($names.Count -eq 0 -and -not (Test-PrivateAddress $rec.Ip)) {
            try {
                $ptr = @(Resolve-DnsName -Name $rec.Ip -Type PTR -QuickTimeout -DnsOnly -ErrorAction Stop | Where-Object { $_.NameHost })
                if ($ptr.Count -gt 0) { $names = @($ptr | ForEach-Object { $_.NameHost }); $source = 'ptr' }
            } catch { }
        }
        if ($names.Count -eq 0) { $source = 'none' }
        $class = Get-Class $rec.Ip $names
        $rows.Add([pscustomobject]@{
            Class = $class; Ip = $rec.Ip; Port = $rec.Port; Names = ($names -join ' '); NameSource = $source
            Process = $rec.Process; ProcessId = $rec.ProcessId; First = $rec.First.ToString('HH:mm:ss'); Last = $rec.Last.ToString('HH:mm:ss')
            States = (($rec.States.Keys | Sort-Object) -join '+'); Kind = 'tcp'
        })
    }

    # DNS-only pass: new flagged cache entries since the start snapshot.
    foreach ($d in @(Get-DnsClientCache -ErrorAction SilentlyContinue)) {
        $k = [string]$d.Entry + '|' + [string]$d.Data
        if ($dns0.ContainsKey($k)) { continue }
        if (([string]$d.Entry) -notmatch $flagPattern) { continue }
        $rows.Add([pscustomobject]@{
            Class = 'FLAG (dns only)'; Ip = [string]$d.Data; Port = ''; Names = [string]$d.Entry; NameSource = 'dns-cache'
            Process = '(system-wide)'; ProcessId = ''; First = ''; Last = ''; States = ''; Kind = 'dns'
        })
    }

    $order = @{ 'FLAG' = 0; 'FLAG (dns only)' = 1; 'WARN' = 2; 'INFO' = 3; 'LAN' = 4; 'LOCAL' = 5 }
    $sorted = @($rows | Sort-Object @{ Expression = { $order[$_.Class] } }, Names, Ip, Port)

    $csvDir = Split-Path -Parent $OutCsv
    if ($csvDir -and -not (Test-Path $csvDir)) { New-Item -ItemType Directory -Path $csvDir -Force | Out-Null }
    $sorted | Export-Csv -Path $OutCsv -NoTypeInformation -Encoding UTF8

    Say ''
    if ($sorted.Count -eq 0) { Say 'nessuna connessione remota vista' }
    else { Say (($sorted | Format-Table Class, Ip, Port, Names, Process, ProcessId, First, Last, States -AutoSize | Out-String).TrimEnd()) }

    $nFlag = @($sorted | Where-Object { $_.Class -eq 'FLAG' }).Count
    $nDns = @($sorted | Where-Object { $_.Class -eq 'FLAG (dns only)' }).Count
    $nWarn = @($sorted | Where-Object { $_.Class -eq 'WARN' }).Count
    $nInfo = @($sorted | Where-Object { $_.Class -eq 'INFO' }).Count
    $nLan = @($sorted | Where-Object { $_.Class -eq 'LAN' }).Count
    Say ''
    Say ("RIEPILOGO: {0} FLAG, {1} FLAG (dns only), {2} WARN (da esaminare), {3} INFO, {4} LAN" -f $nFlag, $nDns, $nWarn, $nInfo, $nLan)
    Say 'Limiti: connessioni piu brevi del poll solo nel passaggio DNS; TIME_WAIT senza PID; DNS-over-HTTPS non passa dalla cache DNS;'
    Say '        le voci "dns only" sono di tutto il sistema (altri programmi possono causarle).'
    Say ("csv: {0}" -f $OutCsv)
    $summary | Set-Content -Path $OutTxt -Encoding UTF8
    Say ("txt: {0}" -f $OutTxt)

    if (($nFlag + $nDns) -gt 0) { exit 1 }
    exit 0
} catch {
    Write-Host ("net-watch: errore: " + $_.Exception.Message) -ForegroundColor Red
    exit 2
}
