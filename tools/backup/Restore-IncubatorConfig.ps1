[CmdletBinding()]
param([Parameter(Mandatory=$true)][ValidatePattern('^COM[0-9]+$')][string]$Port,[Parameter(Mandatory=$true)][string]$BackupFolder,[string]$ToolPath)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'FlashBackup.Common.ps1')
$espExecutable=Get-EspFlashTool $ToolPath
$backupDirectory=(Resolve-Path -LiteralPath $BackupFolder).Path
$manifest=Get-Content -LiteralPath (Join-Path $backupDirectory 'manifest.json') -Raw | ConvertFrom-Json
$file1=Join-Path $backupDirectory 'nvs-read-1.bin';$file2=Join-Path $backupDirectory 'nvs-read-2.bin'
if ($manifest.version -ne 1 -or $manifest.chip -ne 'esp32c3') { throw 'Unsupported backup manifest.' }
foreach ($file in @($file1,$file2)) {
    if ((Get-Item -LiteralPath $file).Length -ne $manifest.nvs_length -or (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $manifest.nvs_sha256) { throw 'Backup hash/length mismatch. Nothing written.' }
}
$targetTable=Join-Path ([IO.Path]::GetTempPath()) ('incubator-target-'+[guid]::NewGuid().ToString()+'.bin')
Invoke-EspReadWrite $espExecutable $Port @('read-flash','0x8000','0x1000',$targetTable)
$targetNvs=Get-NvsPartition $targetTable
if ($targetNvs.offset -ne $manifest.nvs_offset -or $targetNvs.length -ne $manifest.nvs_length) { throw 'Target partition differs. Nothing written.' }
Invoke-EspReadWrite $espExecutable $Port @('write-flash',('0x{0:X}' -f $targetNvs.offset),$file1)
$verify=Join-Path ([IO.Path]::GetTempPath()) ('incubator-restored-'+[guid]::NewGuid().ToString()+'.bin')
Invoke-EspReadWrite $espExecutable $Port @('read-flash',('0x{0:X}' -f $targetNvs.offset),('0x{0:X}' -f $targetNvs.length),$verify)
if ((Get-FileHash -LiteralPath $verify -Algorithm SHA256).Hash -ne $manifest.nvs_sha256) { throw 'Restore verification failed. Keep outputs disconnected.' }
Write-Output 'RESTORED AND VERIFIED. Only NVS written. Keep 24V disconnected for first boot and disarm both channels.'
