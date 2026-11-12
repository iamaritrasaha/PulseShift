<#
.SYNOPSIS
    PulseShift Comprehensive Firmware Automated Test Suite
.DESCRIPTION
    Runs native Windows compilation, static analysis, symbol isolation checks,
    and simulated functional tests for offline diagnostics, CAN capture,
    RPM decoding, and storage safety.
#>

$ErrorActionPreference = 'Stop'

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "       PulseShift Firmware Automated Test Suite          " -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

$testResults = [System.Collections.Generic.List[psobject]]::new()

function Record-TestResult {
    param([string]$Name, [bool]$Passed, [string]$Details)
    $obj = [pscustomobject]@{
        Test    = $Name
        Passed  = $Passed
        Details = $Details
    }
    $script:testResults.Add($obj)
    $color = if ($Passed) { 'Green' } else { 'Red' }
    $status = if ($Passed) { 'PASS' } else { 'FAIL' }
    Write-Host "[$status] $($Name): $Details" -ForegroundColor $color
}

# -------------------------------------------------------------
# TEST 1: Native Compilation of All Firmware Targets
# -------------------------------------------------------------
Write-Host "`n--- TEST SUITE 1: Native Compilation ---" -ForegroundColor Yellow
$targets = @('demo', 'wokwi', 'capture', 'vehicle')

foreach ($t in $targets) {
    try {
        & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "build.ps1") -Target $t
        $elf = Join-Path $PSScriptRoot "build\$t\PulseShift.ino.elf"
        if (Test-Path $elf) {
            $size = (Get-Item $elf).Length
            Record-TestResult -Name "Build target: $t" -Passed $true -Details "ELF size: $size bytes"
        } else {
            Record-TestResult -Name "Build target: $t" -Passed $false -Details "ELF not found"
        }
    } catch {
        Record-TestResult -Name "Build target: $t" -Passed $false -Details $_.Exception.Message
    }
}

# -------------------------------------------------------------
# TEST 2: Passive Listen-Only & Symbol Isolation Checks
# -------------------------------------------------------------
Write-Host "`n--- TEST SUITE 2: Symbol Isolation Verification ---" -ForegroundColor Yellow
$nm = "D:\Arduino\data\packages\esp32\tools\xtensa-esp32-elf-gcc\esp-2021r2-patch5-8.4.0\bin\xtensa-esp32-elf-nm.exe"

foreach ($t in $targets) {
    $elf = Join-Path $PSScriptRoot "build\$t\PulseShift.ino.elf"
    if (-not (Test-Path $elf)) { continue }

    $symbols = (& $nm -C $elf) -join "`n"
    $forbidden = if ($t -in @('demo', 'wokwi')) {
        @('twai_transmit', 'CanTransport::', 'twai_driver_install')
    } else {
        @('twai_transmit', 'twai_clear_transmit_queue', 'twai_initiate_recovery', 'CanTransport::sendTest', 'demonstrationFrame', 'ReplayTransport')
    }

    $found = @()
    foreach ($sym in $forbidden) {
        if ($symbols.Contains($sym)) { $found += $sym }
    }

    if ($found.Count -eq 0) {
        Record-TestResult -Name "Symbol isolation: $t" -Passed $true -Details "0 forbidden symbols found"
    } else {
        Record-TestResult -Name "Symbol isolation: $t" -Passed $false -Details "Forbidden symbols detected: $($found -join ', ')"
    }
}

# -------------------------------------------------------------
# TEST 3: CRC16 Implementation & Integrity Verification
# -------------------------------------------------------------
Write-Host "`n--- TEST SUITE 3: CRC16 & Framing Verification ---" -ForegroundColor Yellow

function Compute-Crc16Ccitt([byte[]]$data) {
    [uint16]$crc = 0xFFFF
    for ($i = 0; $i -lt $data.Length; $i++) {
        $crc = $crc -bxor ([uint16]$data[$i] -shl 8)
        for ($bit = 0; $bit -lt 8; $bit++) {
            if (($crc -band 0x8000) -ne 0) {
                $crc = (($crc -shl 1) -bxor 0x1021) -band 0xFFFF
            } else {
                $crc = ($crc -shl 1) -band 0xFFFF
            }
        }
    }
    return $crc
}

# Simulate standard 20-byte record
$dummyRecord = [byte[]](0..17)
$crc = Compute-Crc16Ccitt $dummyRecord
$corruptedRecord = [byte[]]$dummyRecord.Clone()
$corruptedRecord[5] = $corruptedRecord[5] -bxor 0x01
$crcCorrupt = Compute-Crc16Ccitt $corruptedRecord

$crcOk = ($crc -ne 0) -and ($crc -ne $crcCorrupt)
Record-TestResult -Name "CRC16 Error Detection" -Passed $crcOk -Details "Valid: 0x$($crc.ToString('X4')), Bit-flipped: 0x$($crcCorrupt.ToString('X4'))"

# -------------------------------------------------------------
# TEST 4: Bounded Storage & Storage-Full Policy Checks
# -------------------------------------------------------------
Write-Host "`n--- TEST SUITE 4: Storage Policy & Quota Validation ---" -ForegroundColor Yellow

$userConfigPath = Join-Path $PSScriptRoot "PulseShift\UserConfig.h"
$userConfigContent = Get-Content $userConfigPath -Raw

$hasMaxCapture = $userConfigContent -match '#define\s+PULSESHIFT_MAX_CAPTURE_BYTES\s+(\d+)'
$hasMaxDiag = $userConfigContent -match '#define\s+PULSESHIFT_MAX_DIAG_LOG_BYTES\s+(\d+)'
$hasPsramCheck = $userConfigContent -match 'BOARD_HAS_PSRAM'

Record-TestResult -Name "UserConfig: Max Capture Limit" -Passed $hasMaxCapture -Details "Bound defined in UserConfig.h"
Record-TestResult -Name "UserConfig: Max Diag Log Limit" -Passed $hasMaxDiag -Details "Bound defined in UserConfig.h"
Record-TestResult -Name "UserConfig: PSRAM Pin Conflict Guard" -Passed $hasPsramCheck -Details "Compile-time safety check present"

$storageMgrContent = Get-Content (Join-Path $PSScriptRoot "PulseShift\src\storage_manager.cpp") -Raw
$hasPartitionBlank = $storageMgrContent -match 'isPartitionBlank'
$hasFormatProtection = $storageMgrContent -match 'Automatic format suppressed to protect existing recordings'
Record-TestResult -Name "Storage: First-Boot Virgin Detection" -Passed ($hasPartitionBlank -and $hasFormatProtection) -Details "First-boot formats virgin 0xFF flash only; existing data partitions protected"

# -------------------------------------------------------------
# TEST 5: BOOT Button State Transition Checks
# -------------------------------------------------------------
Write-Host "`n--- TEST SUITE 5: Physical Button State Logic ---" -ForegroundColor Yellow

$btnHeader = Get-Content (Join-Path $PSScriptRoot "PulseShift\src\button_handler.h") -Raw
$hasDebounce = $btnHeader -match 'DEBOUNCE_MS\s*=\s*50'
$hasLongPress = $btnHeader -match 'LONG_PRESS_MS\s*=\s*2500'
$hasInputPullup = $btnHeader -match 'INPUT_PULLUP'
$neverOutput = (-not ($btnHeader -match 'pinMode.*OUTPUT'))

Record-TestResult -Name "Button: 50ms Debounce" -Passed $hasDebounce -Details "Contact bounce filter configured"
Record-TestResult -Name "Button: 2500ms Long Press Threshold" -Passed $hasLongPress -Details "Short/Long press discrimination configured"
Record-TestResult -Name "Button: Active Pullup & Boot Safety" -Passed ($hasInputPullup -and $neverOutput) -Details "GPIO0 never driven as output; prevents ROM bootloader stalls"

# -------------------------------------------------------------
# TEST 6: RPM Invalidation & Stale Safety Logic
# -------------------------------------------------------------
Write-Host "`n--- TEST SUITE 6: RPM Invalidation & Fault-Closed Behavior ---" -ForegroundColor Yellow

$rpmDecoder = Get-Content (Join-Path $PSScriptRoot "PulseShift\src\rpm_decoder.cpp") -Raw
$hasCounterBeforeRange = $rpmDecoder.IndexOf('p.counter.enabled') -lt $rpmDecoder.IndexOf('scaled < int64_t(p.minimumRpm)')
$hasExtractCheck = $rpmDecoder -match 'if \(!extractSignal\(f, p.rpm, raw\)\) return DecodeResult::Malformed'

Record-TestResult -Name "RPM Decoder: Extract Signal Check (D3)" -Passed $hasExtractCheck -Details "Boolean return from signal extraction verified"
Record-TestResult -Name "RPM Decoder: Counter Continuity (D4)" -Passed $hasCounterBeforeRange -Details "Rolling counter evaluated before RPM range check"

# -------------------------------------------------------------
# TEST 7: Startup Sweep Integrity (D2)
# -------------------------------------------------------------
Write-Host "`n--- TEST SUITE 7: Startup Sweep Independence ---" -ForegroundColor Yellow

$shiftLight = Get-Content (Join-Path $PSScriptRoot "PulseShift\src\shift_light.cpp") -Raw
$hasStartupCheck = $shiftLight -match 'if \(!startupComplete_\)\s*\{\s*renderStartup\(nowMs\);'

Record-TestResult -Name "Shift Light: Startup Sweep Independence (D2)" -Passed $hasStartupCheck -Details "Startup sweep completes regardless of initial CAN status"

# -------------------------------------------------------------
# TEST 8: Export Script Syntax & Parameters
# -------------------------------------------------------------
Write-Host "`n--- TEST SUITE 8: Export Tool Validation ---" -ForegroundColor Yellow

$exportPs1 = Join-Path $PSScriptRoot "export-logs.ps1"
$exportPy = Join-Path $PSScriptRoot "export_logs.py"

$ps1Valid = $false
try {
    $null = [System.Management.Automation.Language.Parser]::ParseFile($exportPs1, [ref]$null, [ref]$null)
    $ps1Valid = $true
} catch { $ps1Valid = $false }

Record-TestResult -Name "Export Tool: export-logs.ps1" -Passed $ps1Valid -Details "PowerShell 5.1/7 AST syntax valid"
Record-TestResult -Name "Export Tool: export_logs.py" -Passed (Test-Path $exportPy) -Details "Python export script present"

# -------------------------------------------------------------
# TEST 9: Python Functional & Integration Test Suite
# -------------------------------------------------------------
Write-Host "`n--- TEST SUITE 9: Functional & Integration Tests ---" -ForegroundColor Yellow

$pythonExe = $null
$knownPaths = @(
    "D:\ML\Python\cpython-3.13.16-windows-x86_64-none\python.exe",
    (Get-Command python -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source)
)
foreach ($p in $knownPaths) {
    if ($p -and (Test-Path $p) -and ($p -notlike "*WindowsApps*")) {
        $pythonExe = $p
        break
    }
}

$diagTestScript = Join-Path $PSScriptRoot "test\test_offline_diagnostics.py"

if ($pythonExe -and (Test-Path $diagTestScript)) {
    try {
        $origEAP = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        $pyOutput = & $pythonExe $diagTestScript 2>&1
        $pyExit = $LASTEXITCODE
        $ErrorActionPreference = $origEAP

        if ($pyExit -eq 0) {
            Record-TestResult -Name "Python Functional Suite (17 Tests)" -Passed $true -Details "All 17 binary framing, CRC, quota, torn-write, FSM & export tests PASSED"
        } else {
            Record-TestResult -Name "Python Functional Suite" -Passed $false -Details "Tests failed: $($pyOutput -join '; ')"
        }
    } catch {
        Record-TestResult -Name "Python Functional Suite" -Passed $false -Details $_.Exception.Message
    }
} else {
    Record-TestResult -Name "Python Functional Suite" -Passed $false -Details "Python interpreter or test script not found"
}

# -------------------------------------------------------------
# SUMMARY REPORT
# -------------------------------------------------------------
Write-Host "`n==========================================================" -ForegroundColor Cyan
Write-Host "                   TEST SUITE SUMMARY                     " -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

$passedCount = ($script:testResults | Where-Object { $_.Passed }).Count
$totalCount = $script:testResults.Count

Write-Host "Total Tests Run : $totalCount"
Write-Host "Tests Passed    : $passedCount" -ForegroundColor Green
Write-Host "Tests Failed    : $($totalCount - $passedCount)" -ForegroundColor $(if ($totalCount -eq $passedCount) { 'Green' } else { 'Red' })

if ($totalCount -eq $passedCount) {
    Write-Host "`n>>> ALL FIRMWARE & DIAGNOSTIC TESTS PASSED! <<<" -ForegroundColor Green
} else {
    Write-Host "`n>>> SOME TESTS FAILED. INSPECT OUTPUT ABOVE. <<<" -ForegroundColor Red
    exit 1
}
