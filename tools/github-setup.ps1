# Configure the GitHub repository of MentorSlicer through the GitHub CLI (gh), instead of
# setting each option by hand in the web settings. Safe to run again: every step sets a
# value, nothing is toggled.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\github-setup.ps1 -RepoPath <local clone> -DryRun
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\github-setup.ps1 -RepoPath <local clone>
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\github-setup.ps1 -RepoPath <local clone> -MakePublic
#
# -RepoPath is the local clone (default: the repository this script belongs to); the GitHub
# repository is taken from its "origin" remote, or given with -Repo owner/name.
#
# Needs gh logged in as the owner: "scoop install gh" (or winget install GitHub.cli), then
# "gh auth login". -DryRun only prints the requests.
#
# What it sets:
#   1. description, topics; issues on, wiki/projects/discussions off;
#   2. merges: squash and rebase only, branches deleted after merge;
#   3. with -MakePublic: visibility public. Refused while the history still holds the commits
#      with Creality's notarization password and Firebase configuration (initial import),
#      unless -AllowOldHistory;
#   4. on a public repository: Dependabot alerts and security updates, secret scanning with
#      push protection, private vulnerability reporting (the free tier offers them only on
#      public repositories);
#   5. ruleset "Protect main": main cannot be deleted or force-pushed. Run it after any history
#      rewrite, since it blocks the force push that a rewrite needs.

[CmdletBinding()]
param(
    [switch]$DryRun,
    [switch]$MakePublic,
    [switch]$AllowOldHistory,
    [string]$RepoPath = '',
    [string]$Repo = ''
)

$ErrorActionPreference = 'Stop'
# Default: the repository this script belongs to ($PSScriptRoot is not set yet in the param block of Windows PowerShell).
if (-not $RepoPath) { $RepoPath = Join-Path $PSScriptRoot '..' }
if (-not (Test-Path -LiteralPath $RepoPath -PathType Container)) { throw "Cartella del repository non trovata: $RepoPath" }
$root = (Resolve-Path -LiteralPath $RepoPath).Path
# Windows PowerShell turns the stderr of a native command into an error under 'Stop'.
function Invoke-GitQuiet {
    param([string[]]$GitArgs)
    $ErrorActionPreference = 'Continue'
    $out = & git -C $root @GitArgs 2>$null
    return [pscustomobject]@{ Ok = ($LASTEXITCODE -eq 0); Output = ($out | Out-String).Trim() }
}
if (-not (Invoke-GitQuiet @('rev-parse', '--git-dir')).Ok) { throw "Non e' un repository Git: $root" }
Write-Host "Clone locale: $root"

if (-not $DryRun -and -not (Get-Command gh -ErrorAction SilentlyContinue)) {
    throw "GitHub CLI (gh) non trovata. Installala con 'scoop install gh' (oppure 'winget install GitHub.cli'), poi esegui 'gh auth login'."
}
if (-not $DryRun) {
    $ErrorActionPreference = 'Continue'
    & gh auth status *> $null
    $ErrorActionPreference = 'Stop'
    if ($LASTEXITCODE -ne 0) { throw "gh non e' autenticato: esegui 'gh auth login'." }
}

# owner/name from the origin remote unless given.
if (-not $Repo) {
    $url = (Invoke-GitQuiet @('remote', 'get-url', 'origin')).Output
    if ($url -notmatch 'github\.com[:/](?<owner>[^/]+)/(?<name>[^/]+?)(\.git)?$') { throw "Remote origin non GitHub: $url" }
    $Repo = "$($Matches.owner)/$($Matches.name)"
}
Write-Host "Repository: $Repo$(if ($DryRun) { ' (prova, nessuna modifica)' })"

# Sends a JSON body with gh api; in dry-run prints it instead.
function Invoke-GhApi {
    param([string]$Method, [string]$Path, $Body = $null, [switch]$AllowFailure)
    # gh may write to stderr: handle its exit code instead of a PowerShell error.
    $ErrorActionPreference = 'Continue'
    $text = if ($null -ne $Body) { $Body | ConvertTo-Json -Depth 10 -Compress } else { '' }
    if ($DryRun) {
        Write-Host "  $Method $Path $text"
        return $null
    }
    $ghArgs = @('api', '-X', $Method, '-H', 'Accept: application/vnd.github+json', $Path)
    if ($text) {
        $tmp = New-TemporaryFile
        try {
            # gh reads the body from a file: no quoting problems with PowerShell.
            [IO.File]::WriteAllText($tmp.FullName, $text, (New-Object Text.UTF8Encoding $false))
            $out = & gh @ghArgs --input $tmp.FullName 2>&1
        } finally { Remove-Item $tmp -Force }
    } else {
        $out = & gh @ghArgs 2>&1
    }
    if ($LASTEXITCODE -ne 0) {
        if ($AllowFailure) { Write-Warning "$Method $Path non riuscito: $out"; return $null }
        throw "$Method $Path non riuscito: $out"
    }
    if ($out) { return ($out | Out-String | ConvertFrom-Json) }
    return $null
}

# 1-2. Description, topics, features and merge options.
Write-Host '1. Descrizione, argomenti e funzioni'
Invoke-GhApi PATCH "repos/$Repo" @{
    description            = 'Creality Print fork with an AI print advisor (MCP for Claude Code / Codex), chamber preheat and multi-layer brim. Works offline over the LAN.'
    has_issues             = $true
    has_wiki               = $false
    has_projects           = $false
    has_discussions        = $false
    allow_squash_merge     = $true
    allow_rebase_merge     = $true
    allow_merge_commit     = $false
    delete_branch_on_merge = $true
} | Out-Null
Invoke-GhApi PUT "repos/$Repo/topics" @{
    names = @('3d-printing', 'slicer', 'fdm', 'gcode', 'creality', 'creality-k1c', 'crealityprint',
              'mcp', 'model-context-protocol', 'claude-code', 'print-advisor')
} | Out-Null

# 3. Visibility.
$isPublic = $false
if (-not $DryRun) {
    $info = Invoke-GhApi GET "repos/$Repo"
    $isPublic = ($info.visibility -eq 'public')
}
if ($MakePublic -and -not $isPublic) {
    # The initial import still carries Creality's notarization password and Firebase keys in
    # files removed later: they are already public upstream, but clean the history first.
    if ((Invoke-GitQuiet @('merge-base', '--is-ancestor', '4c67b6d', 'HEAD')).Ok -and -not $AllowOldHistory) {
        throw "La storia contiene ancora il commit iniziale 4c67b6d (password di notarizzazione e configurazione Firebase di Creality). Ripulisci la storia prima, oppure usa -AllowOldHistory."
    }
    $global:LASTEXITCODE = 0
    Write-Host '3. Visibilita'': pubblico'
    Invoke-GhApi PATCH "repos/$Repo" @{ visibility = 'public' } | Out-Null
    $isPublic = $true
} elseif ($isPublic) {
    Write-Host '3. Visibilita'': gia'' pubblico'
} else {
    Write-Host '3. Visibilita'': resta privato (usa -MakePublic per pubblicarlo)'
}

# 4. Security features (free on public repositories).
if ($isPublic -or $DryRun) {
    Write-Host '4. Sicurezza'
    Invoke-GhApi PUT "repos/$Repo/vulnerability-alerts" -AllowFailure | Out-Null
    Invoke-GhApi PUT "repos/$Repo/automated-security-fixes" -AllowFailure | Out-Null
    Invoke-GhApi PUT "repos/$Repo/private-vulnerability-reporting" -AllowFailure | Out-Null
    Invoke-GhApi PATCH "repos/$Repo" @{
        security_and_analysis = @{
            secret_scanning                 = @{ status = 'enabled' }
            secret_scanning_push_protection = @{ status = 'enabled' }
        }
    } -AllowFailure | Out-Null
} else {
    Write-Host '4. Sicurezza: saltata (secret scanning e push protection gratuiti solo sui repository pubblici)'
}

# 5. Ruleset protecting main (no deletion, no force push). Direct pushes stay allowed.
Write-Host '5. Regole per main'
$ruleset = @{
    name        = 'Protect main'
    target      = 'branch'
    enforcement = 'active'
    conditions  = @{ ref_name = @{ include = @('~DEFAULT_BRANCH'); exclude = @() } }
    rules       = @(@{ type = 'deletion' }, @{ type = 'non_fast_forward' })
}
$existing = $null
if (-not $DryRun) {
    $existing = @(Invoke-GhApi GET "repos/$Repo/rulesets" -AllowFailure) | Where-Object { $_.name -eq 'Protect main' } | Select-Object -First 1
}
if ($existing) {
    Invoke-GhApi PUT "repos/$Repo/rulesets/$($existing.id)" $ruleset -AllowFailure | Out-Null
} else {
    Invoke-GhApi POST "repos/$Repo/rulesets" $ruleset -AllowFailure | Out-Null
}

Write-Host "Fatto. Controlla su https://github.com/$Repo/settings"
