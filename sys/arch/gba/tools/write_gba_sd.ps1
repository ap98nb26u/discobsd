#Requires -RunAsAdministrator
<#
.SYNOPSIS
  Write a raw DiscoBSD root filesystem image to the EverDrive GBA X5
  SD card's DiscoBSD partition (partition 2, "sd0b"), identifying the
  card by its stable serial number instead of a Get-Disk "Number" that
  can shift between reboots/reconnects.

  This is a thin wrapper around write_sd_offset.ps1: it looks up the
  card by -CardSerial, sanity-checks the partition layout matches what
  we expect (EverDrive FAT32 as partition 1, DiscoBSD root as
  partition 2 at $ExpectedRootOffset), then delegates the actual write.

.PARAMETER SourceFile
  Path to the raw root filesystem image, with its own leading
  sdcard.img MBR header already stripped (i.e. distrib/gba/sdcard_root.img,
  produced with `tail -c +1025 sdcard.img > sdcard_root.img` -- fsutil's
  partition 1 starts at sector 2 = byte 1024).

.PARAMETER CardSerial
  Serial number of the SD card reader, as shown by "Get-Disk" (the
  "Serial Number" column). Defaults to this project's known reader.

.PARAMETER ExpectedRootOffset
  Expected byte offset of the DiscoBSD root partition (sd0b), as a
  sanity check against Get-Partition before writing. Defaults to the
  value already confirmed for this card's current partition layout.
  If the card was ever repartitioned, update this default.

.EXAMPLE
  .\write_gba_sd.ps1 -SourceFile \\wsl.localhost\Debian\home\keiki\gba_develop\discobsd\discobsd\distrib\gba\sdcard_root.img
#>
param(
    [Parameter(Mandatory=$true)][string]$SourceFile,
    [string]$CardSerial = "2012062914345300",
    [long]$ExpectedRootOffset = 15489564672
)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$writeScript = Join-Path $scriptDir "write_sd_offset.ps1"

if (-not (Test-Path $writeScript)) {
    Write-Error "write_sd_offset.ps1 not found next to this script: $writeScript"
    exit 1
}

$disk = Get-Disk | Where-Object SerialNumber -eq $CardSerial
if (-not $disk) {
    Write-Error "No disk found with serial '$CardSerial'. Is the SD card/reader connected? Run Get-Disk to check."
    exit 1
}
if ($disk.Count -gt 1) {
    Write-Error "Multiple disks matched serial '$CardSerial' - refusing to guess. Check Get-Disk manually."
    exit 1
}
if ($disk.OperationalStatus -ne "Online" -or $disk.HealthStatus -ne "Healthy") {
    Write-Error "Disk with serial '$CardSerial' is not Online/Healthy (status: $($disk.OperationalStatus)/$($disk.HealthStatus)). Re-seat the card and retry."
    exit 1
}

Write-Host "Matched card: Disk $($disk.Number) - $($disk.FriendlyName) (serial $CardSerial)"

$parts = Get-Partition -DiskNumber $disk.Number
$rootPart = $parts | Where-Object Offset -eq $ExpectedRootOffset
if (-not $rootPart) {
    Write-Host ""
    Write-Host "Partitions actually found on this disk:"
    $parts | Format-Table PartitionNumber, DriveLetter, Offset, Size, Type -AutoSize | Out-String | Write-Host
    Write-Error "No partition at the expected DiscoBSD root offset ($ExpectedRootOffset) on disk $($disk.Number). The card may have been repartitioned - update -ExpectedRootOffset (or pass it explicitly) after confirming the new layout with Get-Partition."
    exit 1
}

Write-Host "Confirmed sd0b (root) partition at offset $ExpectedRootOffset, size $($rootPart.Size) bytes."
Write-Host ""

& $writeScript -DiskNumber $disk.Number -SourceFile $SourceFile -Offset $ExpectedRootOffset
