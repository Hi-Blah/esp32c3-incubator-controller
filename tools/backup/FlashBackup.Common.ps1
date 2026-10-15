function Get-EspFlashTool([string]$Explicit) {
    if ($Explicit) { return (Resolve-Path -LiteralPath $Explicit).Path }
    $base = Join-Path $env:LOCALAPPDATA 'Arduino15\packages\esp32\tools\esptool_py'
    $found = Get-ChildItem -LiteralPath $base -Filter esptool.exe -Recurse -File | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $found) { throw 'esptool.exe not found. Supply -ToolPath with its full path.' }
    return $found.FullName
}
function Invoke-EspReadWrite([string]$Executable,[string]$Port,[string[]]$ToolArguments) {
    & $Executable --chip esp32c3 --port $Port --baud 115200 --before no-reset --after no-reset @ToolArguments
    if ($LASTEXITCODE -ne 0) { throw 'esptool failed. Stop and keep the existing backup files.' }
}
function Get-NvsPartition([string]$TablePath) {
    $bytes = [IO.File]::ReadAllBytes($TablePath)
    if ($bytes.Length -ne 4096) { throw 'Partition-table size is invalid.' }
    for ($at=0; $at -le 4064; $at+=32) {
        if ([BitConverter]::ToUInt16($bytes,$at) -ne 0x50AA) { break }
        if ($bytes[$at+2] -eq 1 -and $bytes[$at+3] -eq 2) {
            $offset = [BitConverter]::ToUInt32($bytes,$at+4)
            $length = [BitConverter]::ToUInt32($bytes,$at+8)
            $flags = [BitConverter]::ToUInt32($bytes,$at+28)
            if (($offset -band 0xFFF) -or ($length -band 0xFFF) -or $offset -lt 0x9000 -or $length -lt 4096 -or $length -gt 0x100000 -or ([uint64]$offset+$length) -gt 0x400000 -or ($flags -band 1)) { throw 'Unsupported or encrypted NVS partition.' }
            return @{offset=$offset;length=$length}
        }
    }
    throw 'NVS partition not found. No configuration read/write attempted.'
}
