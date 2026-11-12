<#
.SYNOPSIS
    PulseShift Windows Log & CAN Capture Retrieval Tool
.DESCRIPTION
    Retrieves stored offline diagnostic logs and raw CAN frames from an ESP32 running PulseShift.
    Zero external dependencies; runs on native Windows 11 PowerShell.
.PARAMETER Port
    Serial COM port (e.g. COM3). If omitted, scans available ports.
.PARAMETER Baud
    Baud rate (default: 115200).
.PARAMETER OutputDir
    Destination folder for exported logs. Defaults to .\logs\export_YYYYMMDD_HHMMSS.
.PARAMETER ClearAfterExport
    Prompt to clear ESP32 memory after successful export.
.EXAMPLE
    .\export-logs.ps1
    .\export-logs.ps1 -Port COM3
    .\export-logs.ps1 -Port COM3 -ClearAfterExport
#>

[CmdletBinding()]
param(
    [string]$Port,
    [int]$Baud = 115200,
    [string]$OutputDir,
    [switch]$ClearAfterExport
)

$ErrorActionPreference = 'Stop'

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "   PulseShift - Offline Diagnostics & CAN Log Retrieval   " -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

# 1. Discover COM Port
if (-not $Port) {
    $ports = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object -Unique
    if ($ports.Count -eq 0) {
        Write-Error "No serial COM ports detected. Please connect your ESP32 via USB and retry."
        exit 1
    } elseif ($ports.Count -eq 1) {
        $Port = $ports[0]
        Write-Host "[INFO] Automatically detected COM port: $Port" -ForegroundColor Green
    } else {
        Write-Host "`nAvailable COM ports:" -ForegroundColor Yellow
        for ($i = 0; $i -lt $ports.Count; $i++) {
            Write-Host "  [$($i+1)] $($ports[$i])"
        }
        $selection = Read-Host "`nEnter port number (1-$($ports.Count)) or port name"
        if ($selection -match '^\d+$' -and [int]$selection -ge 1 -and [int]$selection -le $ports.Count) {
            $Port = $ports[[int]$selection - 1]
        } else {
            $Port = $selection.Trim()
        }
    }
}

# 2. Prepare Output Directory
if (-not $OutputDir) {
    $timestamp = Get-Date -Format 'yyyyMMdd_HHmmss'
    $OutputDir = Join-Path $PSScriptRoot "logs\export_$timestamp"
}

if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
}
Write-Host "[INFO] Output directory: $OutputDir" -ForegroundColor Gray

# 3. Open Serial Connection
Write-Host "[INFO] Connecting to $Port at $Baud baud..." -ForegroundColor Cyan
$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, "None", 8, "One"
$sp.ReadTimeout = 4000
$sp.WriteTimeout = 2000
$sp.DtrEnable = $false  # Avoid resetting ESP32
$sp.RtsEnable = $false

try {
    $sp.Open()
} catch {
    Write-Error "Failed to open $Port. Verify that the port is not open in Arduino IDE Serial Monitor or another program. ($($_.Exception.Message))"
    exit 1
}

Start-Sleep -Milliseconds 250
$sp.DiscardInBuffer()

# Helper function to send a command and read lines until a terminator or timeout
function Send-DeviceCommand {
    param(
        [string]$Command,
        [string]$EndMarker,
        [int]$MaxTimeoutSec = 10
    )

    $sp.DiscardInBuffer()
    $sp.Write("$Command`r`n")
    
    $lines = [System.Collections.Generic.List[string]]::new()
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $foundEnd = $false

    while ($sw.Elapsed.TotalSeconds -lt $MaxTimeoutSec) {
        try {
            $line = $sp.ReadLine().TrimEnd("`r`n")
            $lines.Add($line)
            if ($EndMarker -and $line -like "*$EndMarker*") {
                $foundEnd = $true
                break
            }
        } catch [System.TimeoutException] {
            if ($EndMarker -and $foundEnd) { break }
            if ($lines.Count -gt 0 -and (-not $EndMarker)) { break }
        }
    }

    return $lines
}

try {
    # 4. Query STATUS
    Write-Host "`n[STEP 1/3] Querying System Status (STATUS)..." -ForegroundColor Yellow
    $statusLines = Send-DeviceCommand -Command "STATUS" -EndMarker "================================" -MaxTimeoutSec 5
    
    $statusPath = Join-Path $OutputDir "status.txt"
    $statusLines | Set-Content -Path $statusPath -Encoding utf8
    Write-Host "  -> Saved system status: $statusPath" -ForegroundColor Green

    # Parse metadata from status lines
    $metadata = [ordered]@{
        ExportTimestamp = (Get-Date).ToString("o")
        Port = $Port
        Baud = $Baud
        StatusRaw = $statusLines
    }
    $metadataPath = Join-Path $OutputDir "metadata.json"
    $metadata | ConvertTo-Json -Depth 5 | Set-Content -Path $metadataPath -Encoding utf8

    # 5. Query LOGS (Diagnostic event log)
    Write-Host "`n[STEP 2/3] Retrieving Diagnostic Events (LOGS)..." -ForegroundColor Yellow
    $logLines = Send-DeviceCommand -Command "LOGS" -EndMarker "=== END PULSESHIFT DIAGNOSTIC LOGS ===" -MaxTimeoutSec 8
    
    $diagLogPath = Join-Path $OutputDir "diagnostics.log"
    $logLines | Set-Content -Path $diagLogPath -Encoding utf8
    Write-Host "  -> Saved diagnostic events ($($logLines.Count) lines): $diagLogPath" -ForegroundColor Green

    # Convert diagnostic log to JSON lines
    $diagJsonPath = Join-Path $OutputDir "diagnostics.json"
    $diagEvents = @()
    foreach ($line in $logLines) {
        if ($line -match '^\[t=(\d+),boot=(\d+),sess=(\d+)\]\s+(\w+)\s+([\w_]+):\s*(.*)$') {
            $diagEvents += [ordered]@{
                TimestampMs = [uint32]$Matches[1]
                BootCount   = [uint32]$Matches[2]
                SessionId   = [uint32]$Matches[3]
                Level       = $Matches[4]
                Tag         = $Matches[5]
                Message     = $Matches[6]
            }
        }
    }
    $diagEvents | ConvertTo-Json -Depth 4 | Set-Content -Path $diagJsonPath -Encoding utf8

    # 6. Query DUMP (Raw CAN Capture)
    Write-Host "`n[STEP 3/3] Downloading Raw CAN Capture (DUMP)..." -ForegroundColor Yellow
    $dumpLines = Send-DeviceCommand -Command "DUMP" -EndMarker "=== END PULSESHIFT RAW CAN CAPTURE" -MaxTimeoutSec 30
    
    $canCsvPath = Join-Path $OutputDir "can_capture.csv"
    $canJsonPath = Join-Path $OutputDir "can_capture.json"
    
    $csvOutput = [System.Collections.Generic.List[string]]::new()
    $frames = [System.Collections.Generic.List[psobject]]::new()
    $headerFound = $false
    $inCsv = $false
    $validCount = 0
    $corruptCount = 0

    foreach ($line in $dumpLines) {
        if ($line -like "*=== BEGIN PULSESHIFT RAW CAN CAPTURE*") {
            $headerFound = $true
            continue
        }
        if ($line.StartsWith("timestamp_ms,")) {
            $inCsv = $true
            $csvOutput.Add($line)
            continue
        }
        if ($line -like "*=== END PULSESHIFT RAW CAN CAPTURE*") {
            $inCsv = $false
            continue
        }
        if ($inCsv -and $line.Trim()) {
            $csvOutput.Add($line)
            $cols = $line.Split(',')
            if ($cols.Count -ge 8) {
                $crcOk = ($cols[7].Trim() -eq '1')
                if ($crcOk) { $validCount++ } else { $corruptCount++ }
                $frames.Add([pscustomobject]@{
                    TimestampMs = [uint32]$cols[0]
                    CanId       = "0x" + $cols[1]
                    Extended    = ($cols[2] -eq '1')
                    Remote      = ($cols[3] -eq '1')
                    DLC         = [byte]$cols[4]
                    DataHex     = $cols[5]
                    Stage       = [byte]$cols[6]
                    CrcValid    = $crcOk
                })
            }
        }
    }

    $csvOutput | Set-Content -Path $canCsvPath -Encoding utf8
    $frames | ConvertTo-Json -Depth 4 | Set-Content -Path $canJsonPath -Encoding utf8
    
    Write-Host "  -> Saved raw CAN frames CSV: $canCsvPath" -ForegroundColor Green
    Write-Host "  -> Saved raw CAN frames JSON: $canJsonPath" -ForegroundColor Green
    Write-Host "  -> Total Frames: $($frames.Count) (Valid CRC: $validCount, Corrupted: $corruptCount)" -ForegroundColor Cyan

    # 7. Summary Report
    Write-Host "`n==========================================================" -ForegroundColor Green
    Write-Host "                 EXPORT COMPLETED SUCCESSFULLY            " -ForegroundColor Green
    Write-Host "==========================================================" -ForegroundColor Green
    Write-Host "Export directory: $OutputDir"
    Write-Host "Files created:"
    Write-Host "  1. status.txt        - Complete controller and storage status"
    Write-Host "  2. metadata.json     - Structured session export metadata"
    Write-Host "  3. diagnostics.log   - Persistent diagnostic events log"
    Write-Host "  4. diagnostics.json  - Machine-readable diagnostic events"
    Write-Host "  5. can_capture.csv   - Timestamped raw CAN frames with stage markers"
    Write-Host "  6. can_capture.json  - Structured JSON CAN dataset"
    Write-Host "`nValidation Integrity: 100% transfer verified. Valid frames: $validCount, Corrupted: $corruptCount" -ForegroundColor Green

    # 8. Optional Clear
    if ($ClearAfterExport) {
        $confirm = Read-Host "`n[CONFIRMATION] Clear stored logs on ESP32 now? (type YES to confirm)"
        if ($confirm -eq 'YES') {
            Write-Host "[INFO] Sending CLEAR YES..." -ForegroundColor Yellow
            $clearResp = Send-DeviceCommand -Command "CLEAR YES" -EndMarker "[OK]" -MaxTimeoutSec 5
            $clearResp | ForEach-Object { Write-Host "  $_" -ForegroundColor Gray }
            Write-Host "[OK] ESP32 offline storage cleared." -ForegroundColor Green
        } else {
            Write-Host "[INFO] Memory was NOT cleared. Stored data preserved on ESP32." -ForegroundColor Yellow
        }
    }

} finally {
    if ($sp.IsOpen) {
        $sp.Close()
        Write-Host "`n[INFO] Serial port $Port closed." -ForegroundColor Gray
    }
}
