# Builds build\EndlessMode_Spawner.stage: replaces the (unused in vanilla MSMR) default ProximitySpawnSystem
# config with ours. Stage layout (from existing mods): <span>/<asset path> + info.json, format_version 2.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$py = "$env:LOCALAPPDATA\Programs\Python\Python312\python.exe"
$alert = Join-Path $root '..\tools\ALERT'

Push-Location $alert
& $py json2config.py (Join-Path $PSScriptRoot 'prox_prison.json') ProximitySpawnerConfig | Out-Null
Pop-Location

$config = Join-Path $PSScriptRoot 'prox_prison.json.config'
$info = Join-Path $root 'build\stage_info.json'
[ordered]@{ game = 'MSMR'; name = 'Endless Mode - Spawner Config'; author = 'quailo'; format_version = 2 } |
    ConvertTo-Json | Set-Content $info -Encoding UTF8

# Zip entries with forward slashes and no directory entries (Compress-Archive on PS 5 writes backslashes).
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$out = Join-Path $root 'build\EndlessMode_Spawner.stage'
if (Test-Path -LiteralPath $out) { Remove-Item -LiteralPath $out }
$archive = [IO.Compression.ZipFile]::Open($out, 'Create')
try {
    [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $config,
        '0/configs/ProxSpawnConfig_Default/ProxSpawnConfig_Default.config')
    [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $info, 'info.json')
} finally {
    $archive.Dispose()
}
Write-Output "Packed $out"
