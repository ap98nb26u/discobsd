# com_bridge.ps1 - copy bytes between a Windows serial port and a TCP port.
#
#   powershell -ExecutionPolicy Bypass -File com_bridge.ps1 [-Port COM3]
#       [-Baud 115200] [-TcpHost localhost] [-TcpPort 5555] [-NoFlow]
#
# Opens the serial port wired to the GBA's link port (UART mode, 8N1),
# connects to slip_tun.py in WSL2 (WSL2 forwards Windows' localhost to it),
# and copies bytes both ways until either side closes.  It does not look
# at the data: SLIP framing is done by slip_tun.py and the GBA.  Uses only
# Windows PowerShell 5.1 and .NET, so nothing needs installing.
# Hardware (RTS/CTS) handshaking is on by default: the GBA drops its RTS
# while its 4-byte receive FIFO is full, and /dev/sio relies on the peer
# pausing then (a GBA syscall runs with interrupts off).  -NoFlow turns it
# off, for a cable without the handshake lines.
# The port is held while this runs; stop it (Ctrl-C) to use Tera Term again.
param(
    [string]$Port = "COM3",
    [int]$Baud = 115200,            # slipd's default and the console's
    [string]$TcpHost = "localhost",
    [int]$TcpPort = 5555,
    [switch]$NoFlow
)
$ErrorActionPreference = "Stop"       # e.g. COM3 busy: stop, don't carry on

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, "None", 8, "One"
if ($NoFlow) {
    $sp.Handshake = "None"
    $sp.RtsEnable = $true
} else {
    $sp.Handshake = "RequestToSend"
}
$sp.DtrEnable = $true
$sp.ReadBufferSize = 65536
$sp.WriteBufferSize = 65536
$sp.Open()

$tc = New-Object System.Net.Sockets.TcpClient
$tc.NoDelay = $true
$tc.Connect($TcpHost, $TcpPort)
$ns = $tc.GetStream()
Write-Host "com_bridge: $Port at $Baud (handshake $($sp.Handshake)) <-> ${TcpHost}:$TcpPort"

$buf = New-Object byte[] 4096
try {
    while ($true) {
        $idle = $true
        $n = $sp.BytesToRead
        if ($n -gt 0) {
            $k = $sp.Read($buf, 0, [Math]::Min($n, $buf.Length))
            $ns.Write($buf, 0, $k)
            $idle = $false
        }
        $a = $tc.Available
        if ($a -gt 0) {
            $k = $ns.Read($buf, 0, [Math]::Min($a, $buf.Length))
            $sp.Write($buf, 0, $k)
            $idle = $false
        } elseif ($tc.Client.Poll(0, [System.Net.Sockets.SelectMode]::SelectRead) -and
                  $tc.Available -eq 0) {
            break                       # readable with nothing to read: closed
        }                               # (data that came in between: next pass)
        if ($idle) { Start-Sleep -Milliseconds 1 }
    }
} finally {
    $tc.Close()
    $sp.Close()
    Write-Host "com_bridge: closed"
}
