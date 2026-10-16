# Packs build\EndlessArena.dll into build\EndlessArena.script (zip: info.json + dll), the format Overstrike installs.
$ErrorActionPreference = 'Stop'
$build = Join-Path $PSScriptRoot 'build'
$stage = Join-Path $build 'pack'
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory $stage | Out-Null

$info = [ordered]@{
    name         = 'EndlessArena'
    author       = 'Masacaai'
    version      = '0.1.0'
    game         = 'MSMR'
    type         = 'script'
    dependencies = @()
}
$info | ConvertTo-Json | Set-Content (Join-Path $stage 'info.json') -Encoding UTF8
Copy-Item (Join-Path $build 'EndlessArena.dll') $stage

$zip = Join-Path $build 'EndlessArena.zip'
$script = Join-Path $build 'EndlessArena.script'
Remove-Item $zip, $script -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip
Move-Item $zip $script
Write-Output "Packed $script"
