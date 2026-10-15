[CmdletBinding()]
param([Parameter(Mandatory=$true)][ValidatePattern('^COM[0-9]+$')][string]$Port,[string]$Destination,[string]$ToolPath,[switch]$FullFlash)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'FlashBackup.Common.ps1')
$espExecutable=Get-EspFlashTool $ToolPath
if (-not $Destination) { $Destination=Join-Path ([Environment]::GetFolderPath('Desktop')) ('ESP32-Config-'+(Get-Date -Format 'yyyyMMdd-HHmmss')) }
if (Test-Path -LiteralPath $Destination) { throw 'Use a new destination folder; no existing backups will be overwritten.' }
New-Item -ItemType Directory -Path $Destination | Out-Null
$backupDirectory=(Resolve-Path -LiteralPath $Destination).Path
$part1=Join-Path $backupDirectory 'partition-table-1.bin';$part2=Join-Path $backupDirectory 'partition-table-2.bin'
Invoke-EspReadWrite $espExecutable $Port @('read-flash','0x8000','0x1000',$part1)
Invoke-EspReadWrite $espExecutable $Port @('read-flash','0x8000','0x1000',$part2)
if ((Get-FileHash -LiteralPath $part1 -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $part2 -Algorithm SHA256).Hash) { throw 'Partition reads differ. Backup is not verified.' }
$nvs=Get-NvsPartition $part1
$nvs1=Join-Path $backupDirectory 'nvs-read-1.bin';$nvs2=Join-Path $backupDirectory 'nvs-read-2.bin'
Invoke-EspReadWrite $espExecutable $Port @('read-flash',('0x{0:X}' -f $nvs.offset),('0x{0:X}' -f $nvs.length),$nvs1)
Invoke-EspReadWrite $espExecutable $Port @('read-flash',('0x{0:X}' -f $nvs.offset),('0x{0:X}' -f $nvs.length),$nvs2)
$nonEmpty=$false;foreach($byte in [IO.File]::ReadAllBytes($nvs1)){if($byte -ne 255){$nonEmpty=$true;break}}
if (-not $nonEmpty) { throw 'NVS is erased (all FF). This is not a usable configuration backup.' }
$hash1=(Get-FileHash -LiteralPath $nvs1 -Algorithm SHA256).Hash;$hash2=(Get-FileHash -LiteralPath $nvs2 -Algorithm SHA256).Hash
if ((Get-Item -LiteralPath $nvs1).Length -ne $nvs.length -or (Get-Item -LiteralPath $nvs2).Length -ne $nvs.length -or $hash1 -ne $hash2) { throw 'NVS reads differ or size is invalid. Backup is not verified.' }
$manifest=@{version=1;chip='esp32c3';flash_bytes=4194304;created=(Get-Date -Format o);nvs_offset=$nvs.offset;nvs_length=$nvs.length;nvs_sha256=$hash1;partition_sha256=(Get-FileHash -LiteralPath $part1 -Algorithm SHA256).Hash;contains_secrets=$true;includes_firmware=$false;note='NVS configuration, identities, bonds and saved progress. Keep private.'}
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backupDirectory 'manifest.json') -Encoding UTF8
if ($FullFlash) {
    $full=Join-Path $backupDirectory 'full-flash.bin'
    Invoke-EspReadWrite $espExecutable $Port @('read-flash','0x0','0x400000',$full)
    if ((Get-Item -LiteralPath $full).Length -ne 4194304) { throw 'Full Flash read incomplete.' }
    $manifest.includes_firmware=$true;$manifest.full_flash_sha256=(Get-FileHash -LiteralPath $full -Algorithm SHA256).Hash
    $manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backupDirectory 'manifest.json') -Encoding UTF8
    Get-FileHash -LiteralPath $full -Algorithm SHA256 | Format-List
}
Write-Output ('VERIFIED: '+$backupDirectory)
Write-Output ('NVS bytes: '+$nvs.length+'; SHA256: '+$hash1)
