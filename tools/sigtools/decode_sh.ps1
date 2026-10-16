param([string]$Disasm, [string]$Dll)
# Replays Script Hook's pattern-building code: xmm constants -> stack -> memcpy -> resolver call.
$b = [IO.File]::ReadAllBytes($Dll)
function Ints($va) {
    $off = [int64]$va - 0x180049000 + 0x47E00
    0..3 | ForEach-Object { [BitConverter]::ToInt32($b, $off + 4 * $_) }
}
function Norm($base, $sign, $hex) {
    $v = [Convert]::ToInt64($hex, 16); if ($sign -eq '-') { $v = -$v }
    if ($base -eq 'r11') { return "rsp:" + ($script:r11off + $v) }
    return "${base}:$v"
}
$xmm = @{}; $stack = @{}; $len = $null; $src = $null; $edx = $null; $pending = $null
$script:r11off = 0; $sawR11 = $false
$results = New-Object System.Collections.ArrayList
foreach ($line in [IO.File]::ReadLines($Disasm)) {
    if ($line -notmatch '^\s+([0-9A-F]{16}):\s+(\S+)\s*(.*)$') { continue }
    $addr = $Matches[1]; $op = $Matches[2]; $args = $Matches[3].Trim()
    if ($op -eq 'mov' -and $args -eq 'r11,rsp') { $sawR11 = $true; continue }
    if ($sawR11 -and $op -eq 'sub' -and $args -match '^rsp,([0-9A-F]+)h?$') { $script:r11off = [Convert]::ToInt64($Matches[1], 16); $sawR11 = $false; continue }
    if ($op -match '^mov(dqa|dqu|aps|ups)$' -and $args -match '^xmm(\d+),xmmword ptr \[([0-9A-F]{16})h\]$') { $xmm[$Matches[1]] = [Convert]::ToInt64($Matches[2], 16); continue }
    if ($op -eq 'xorps' -and $args -match '^xmm(\d+),xmm(\d+)$' -and $Matches[1] -eq $Matches[2]) { $xmm[$Matches[1]] = 0; continue }
    if ($op -match '^mov(dqa|dqu|aps|ups)$' -and $args -match '^xmmword ptr \[(rbp|rsp|r11)([+-])([0-9A-F]+)h?\],xmm(\d+)$') {
        $k = Norm $Matches[1] $Matches[2] $Matches[3]; $parts = $k.Split(':'); $va = $xmm[$Matches[4]]
        $vals = if ($va) { Ints $va } else { 0, 0, 0, 0 }
        for ($j = 0; $j -lt 4; $j++) { $stack["$($parts[0]):$([int64]$parts[1] + 4*$j)"] = $vals[$j] }
        continue
    }
    if ($op -eq 'mov' -and $args -match '^dword ptr \[(rbp|rsp|r11)([+-])([0-9A-F]+)h?\],([0-9A-F]+)h?$') {
        $k = Norm $Matches[1] $Matches[2] $Matches[3]; $stack[$k] = [BitConverter]::ToInt32([BitConverter]::GetBytes([Convert]::ToInt64($Matches[4], 16)), 0); continue
    }
    if ($args -match '^r8d,(?:\[\w+\+)?([0-9A-F]+)h?\]?$' -and $op -in 'mov', 'lea') { $len = [Convert]::ToInt64($Matches[1], 16); continue }
    if ($op -eq 'lea' -and $args -match '^rdx,\[(rbp|rsp|r11)([+-])([0-9A-F]+)h?\]$') { $src = Norm $Matches[1] $Matches[2] $Matches[3]; continue }
    if ($args -match '^edx,(?:\[\w+\+)?([0-9A-F]+)h?\]?$' -and $op -in 'mov', 'lea') { $edx = [Convert]::ToInt64($Matches[1], 16); continue }
    if ($op -eq 'call' -and $args -eq '0000000180047403') {
        # memcpy(vec, stack+src, len): snapshot the pattern now
        $parts = $src.Split(':'); $pat = @()
        for ($j = 0; $j -lt $len / 4; $j++) { $pat += $stack["$($parts[0]):$([int64]$parts[1] + 4*$j)"] }
        $pending = [ordered]@{ at = $addr; pattern = (($pat | ForEach-Object { if ($_ -eq -1 -or $null -eq $_) { '??' } else { $_.ToString('X2') } }) -join ' '); resolver = $null; off = $null; global = $null; adjust = '' }
        $edx = $null; continue
    }
    if ($pending -and -not $pending.resolver -and $op -eq 'call') { $pending.resolver = $args; $pending.off = $edx; $script:q = @(); continue }
    if ($pending -and $pending.resolver -eq '0000000180039600') {
        # raw scan; several rel32 targets decoded inline: mov ecx,[rax+N] ... mov [G],rdx
        if ($op -eq 'mov' -and $args -match '^ecx,dword ptr \[rax\+([0-9A-F]+)h?\]$') { $script:q += , [Convert]::ToInt64($Matches[1], 16); continue }
        if ($op -eq 'mov' -and $args -match '^qword ptr \[([0-9A-F]{16})h\],rdx$' -and $script:q.Count) {
            $rec = [ordered]@{} + $pending; $rec.off = $script:q[0]; $rec.global = $Matches[1]; $rec.adjust = 'inline'
            $script:q = @($script:q | Select-Object -Skip 1); [void]$results.Add([pscustomobject]$rec); continue
        }
        if ($op -eq 'call' -and $args -in '00000001800432C0', '0000000180047403') { $pending = $null }
        continue
    }
    if ($pending -and $pending.resolver) {
        if ($op -match '^(sub|add)$' -and $args -match '^rax,') { $pending.adjust += "$op $args; " ; continue }
        if ($op -eq 'mov' -and $args -match '^qword ptr \[([0-9A-F]{16})h\],rax$') {
            $pending.global = $Matches[1]; [void]$results.Add([pscustomobject]$pending); $pending = $null
        }
    }
}
$results | Format-Table -AutoSize -Wrap global, resolver, off, adjust, pattern
