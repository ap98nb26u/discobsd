#Requires -RunAsAdministrator
<#
.SYNOPSIS
  Write a raw image file to a byte offset on a physical disk, for
  installing DiscoBSD onto a partition of an already-partitioned SD
  card (e.g. an EverDrive GBA X5 card whose partition 1 is the
  EverDrive's own FAT32 boot/menu volume).

  This writes directly to \\.\PhysicalDrive<N> -- it does NOT touch
  the partition table, and does not require the target partition to
  be formatted or mounted. Only the byte range
  [Offset, Offset + file size) is overwritten.

.PARAMETER DiskNumber
  Physical disk number, as shown by "Get-Disk" (the "Number" column).

.PARAMETER SourceFile
  Path to the raw image to write (e.g. a DiscoBSD root filesystem
  image with its own leading MBR/header already stripped).

.PARAMETER Offset
  Byte offset on the physical disk to start writing at. This must be
  the *start offset of the target partition*, as shown by
  "Get-Partition -DiskNumber <N>" (the "Offset" column) or by
  diskpart's "list partition" (Offset column, converted from KB to
  bytes).

.EXAMPLE
  .\write_sd_offset.ps1 -DiskNumber 4 -SourceFile \\wsl.localhost\Debian\home\keiki\gba_develop\discobsd\discobsd\distrib\gba\sdcard_root.img -Offset 15489564672
#>
param(
    [Parameter(Mandatory=$true)][int]$DiskNumber,
    [Parameter(Mandatory=$true)][string]$SourceFile,
    [Parameter(Mandatory=$true)][long]$Offset
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $SourceFile)) {
    Write-Error "Source file not found: $SourceFile"
    exit 1
}
$srcSize = (Get-Item $SourceFile).Length

$disk = Get-Disk -Number $DiskNumber -ErrorAction Stop
if (-not $disk) {
    Write-Error "No disk found with Number $DiskNumber. Re-check with Get-Disk."
    exit 1
}
$endOffset = $Offset + $srcSize

Write-Host "Target disk:   $($disk.Number) - $($disk.FriendlyName) ($($disk.BusType), $($disk.Size) bytes)"
Write-Host "Source file:   $SourceFile"
Write-Host "Write range:   [$Offset, $endOffset) ($srcSize bytes)"

if ($endOffset -gt $disk.Size) {
    Write-Error "Write would run past the end of the disk (disk size $($disk.Size) bytes). Aborting."
    exit 1
}

# Show which partition(s) this range overlaps, purely for confirmation.
Write-Host ""
Write-Host "Partitions on this disk, for reference:"
Get-Partition -DiskNumber $DiskNumber | Format-Table PartitionNumber, Offset, Size, Type -AutoSize | Out-String | Write-Host

$answer = Read-Host "Type YES to write $srcSize bytes to \\.\PhysicalDrive$DiskNumber at offset $Offset"
if ($answer -ne "YES") {
    Write-Host "Aborted, nothing written."
    exit 1
}

$path = "\\.\PhysicalDrive$DiskNumber"
$dst = [System.IO.File]::Open($path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Write, [System.IO.FileShare]::ReadWrite)
try {
    $dst.Seek($Offset, [System.IO.SeekOrigin]::Begin) | Out-Null
    $src = [System.IO.File]::OpenRead($SourceFile)
    try {
        $buf = New-Object byte[] 1048576
        $total = 0
        while (($n = $src.Read($buf, 0, $buf.Length)) -gt 0) {
            $dst.Write($buf, 0, $n)
            $total += $n
        }
        $dst.Flush()
        Write-Host "Wrote $total bytes to offset $Offset on PhysicalDrive$DiskNumber."
    } finally {
        $src.Close()
    }
} finally {
    $dst.Close()
}
