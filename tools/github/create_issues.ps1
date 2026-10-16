# Opens every drafted issue in docs/issues/*.md on the current GitHub repository, using the GitHub CLI (gh).
#
# Usage (after the repository exists on GitHub and `gh auth login` has been done):
#   powershell -File tools\github\create_issues.ps1            # create all
#   powershell -File tools\github\create_issues.ps1 -DryRun    # just print what would be created
#
# Each draft has a front matter block:  ---  title: ...  labels: a, b, c  ---  then the issue body.
# Missing labels are created first. Issues whose title already exists (open or closed) are skipped, so re-running is safe.
param([switch]$DryRun)

$ErrorActionPreference = 'Stop'
$dir = Join-Path $PSScriptRoot '..\..\docs\issues'
$files = Get-ChildItem $dir -Filter '*.md' | Where-Object { $_.Name -match '^\d\d-' } | Sort-Object Name

$colors = @{
    'bug' = 'd73a4a'; 'feature' = 'a2eeef'; 'research' = '5319e7'; 'ui' = 'fbca04'; 'ai' = 'bfd4f2'; 'arena' = 'c5def5'
    'bosses' = 'b60205'; 'assets' = '0e8a16'; 'stability' = 'e99695'; 'roguelike' = 'f9d0c4'; 'design' = 'd4c5f9'
    'data' = '1d76db'; 'tooling' = 'ededed'; 'needs-repro' = 'fef2c0'; 'owner-decision' = '000000'
    'good first issue' = '7057ff'; 'help wanted' = '008672'
}

if (-not $DryRun) {
    $existing = (gh issue list --state all --limit 500 --json title | ConvertFrom-Json).title
    $have = (gh label list --limit 200 --json name | ConvertFrom-Json).name
}

foreach ($f in $files) {
    $text = Get-Content $f.FullName -Raw
    if ($text -notmatch '(?s)^---\s*\r?\ntitle:\s*(.+?)\r?\nlabels:\s*(.*?)\r?\n---\s*\r?\n(.*)$') { Write-Warning "Skipping $($f.Name): no front matter"; continue }
    $title = $Matches[1].Trim()
    $labels = @($Matches[2].Split(',') | ForEach-Object { $_.Trim() } | Where-Object { $_ })
    $body = $Matches[3].Trim()
    if ($DryRun) { Write-Output "WOULD CREATE: $title  [$($labels -join ', ')]"; continue }
    if ($existing -contains $title) { Write-Output "exists, skipped: $title"; continue }
    foreach ($l in $labels) {
        if ($have -notcontains $l) {
            $c = if ($colors.ContainsKey($l)) { $colors[$l] } else { 'ededed' }
            gh label create $l --color $c | Out-Null
            $have += $l
        }
    }
    $tmp = New-TemporaryFile
    Set-Content -Path $tmp -Value $body -Encoding UTF8
    $ghArgs = @('issue', 'create', '--title', $title, '--body-file', $tmp)
    foreach ($l in $labels) { $ghArgs += @('--label', $l) }
    gh @ghArgs
    Remove-Item $tmp
}
