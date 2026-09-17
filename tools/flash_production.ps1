[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^A\d{2}(?:0[1-9]|1[0-2])(?!0000)[0-9A-Fa-f]{4}$')]
    [string]$DeviceId,

    [string]$Csv = '',
    [string]$Firmware = '',
    [string]$WchIsp = '',
    [string]$Python = 'python',
    [ValidateRange(0, 255)]
    [int]$ProgrammerIndex = 0,
    [string]$Broker = '',
    [ValidateRange(1, 65535)]
    [int]$Port = 1883,
    [string]$Apn = '',
    [switch]$PrepareOnly
)

$ErrorActionPreference = 'Stop'
$DeviceId = $DeviceId.ToUpperInvariant()
if ([string]::IsNullOrWhiteSpace($Csv)) { $Csv = Join-Path $PSScriptRoot '..\production.csv' }
if ([string]::IsNullOrWhiteSpace($Firmware)) { $Firmware = Join-Path $PSScriptRoot '..\production-release\splitac-burn.hex' }
if ([string]::IsNullOrWhiteSpace($WchIsp)) { $WchIsp = Join-Path $env:USERPROFILE '.platformio\packages\tool-wchisp\wchisp.exe' }
$Csv = [System.IO.Path]::GetFullPath($Csv)
$Firmware = [System.IO.Path]::GetFullPath($Firmware)
$WchIsp = [System.IO.Path]::GetFullPath($WchIsp)
$tool = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'splitac_production.py'))
$tempRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
$tempDir = Join-Path $tempRoot ("splitac-flash-" + [guid]::NewGuid().ToString('N'))
$dumpFile = Join-Path $tempDir 'dataflash-readback.bin'
$dataFlash = Join-Path $tempDir ($DeviceId + '-dataflash.bin')

if (-not (Test-Path -LiteralPath $Csv -PathType Leaf)) { throw "Production CSV not found: $Csv" }
if (-not (Test-Path -LiteralPath $Firmware -PathType Leaf)) { throw "Burn firmware not found: $Firmware" }
if (-not (Get-Command $Python -ErrorAction SilentlyContinue)) {
    throw "Python 3 not found: $Python. Install Python 3 or specify python.exe with -Python."
}
if (-not $PrepareOnly -and -not (Test-Path -LiteralPath $WchIsp -PathType Leaf)) {
    throw "wchisp not found: $WchIsp. Install the PlatformIO tool-wchisp package first."
}

New-Item -ItemType Directory -Path $tempDir | Out-Null
try {
    $arguments = @(
        $tool, 'provision', '--input', $Csv, '--output-dir', $tempDir,
        '--device-id', $DeviceId, '--port', $Port
    )
    if (-not [string]::IsNullOrWhiteSpace($Broker)) { $arguments += @('--broker', $Broker) }
    if (-not [string]::IsNullOrWhiteSpace($Apn)) { $arguments += @('--apn', $Apn) }
    & $Python @arguments
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $dataFlash -PathType Leaf)) {
        throw "Failed to build temporary DataFlash for $DeviceId"
    }

    $dataFlashInfo = Get-Item -LiteralPath $dataFlash
    if ($dataFlashInfo.Length -ne 32768) {
        throw "Invalid DataFlash size: $($dataFlashInfo.Length) bytes"
    }
    if ($PrepareOnly) {
        Write-Host "Preparation passed: $DeviceId, DataFlash 32768 bytes" -ForegroundColor Green
        return
    }

    Write-Host "Connecting to ISP device $ProgrammerIndex ..."
    & $WchIsp -d $ProgrammerIndex info
    if ($LASTEXITCODE -ne 0) {
        throw 'No device in Boot ISP mode. Hold BOOT and power-cycle the board.'
    }

    Write-Host "[1/4] Flashing and verifying firmware: $Firmware"
    & $WchIsp -d $ProgrammerIndex flash --no-reset $Firmware
    if ($LASTEXITCODE -ne 0) { throw 'CodeFlash programming or verification failed' }

    Write-Host "[2/4] Programming generated device configuration: $DeviceId"
    & $WchIsp -d $ProgrammerIndex eeprom write $dataFlash
    if ($LASTEXITCODE -ne 0) { throw 'DataFlash programming failed' }

    Write-Host '[3/4] Reading back and verifying DataFlash'
    & $WchIsp -d $ProgrammerIndex eeprom dump $dumpFile
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $dumpFile -PathType Leaf)) {
        throw 'DataFlash readback failed'
    }
    $expectedHash = (Get-FileHash -LiteralPath $dataFlash -Algorithm SHA256).Hash
    $actualHash = (Get-FileHash -LiteralPath $dumpFile -Algorithm SHA256).Hash
    if ($expectedHash -ne $actualHash) { throw 'DataFlash readback does not match' }

    Write-Host '[4/4] Resetting and starting device'
    & $WchIsp -d $ProgrammerIndex reset
    if ($LASTEXITCODE -ne 0) { throw 'Programming passed, but reset failed; power-cycle the board' }

    Write-Host "Programming passed: $DeviceId" -ForegroundColor Green
}
finally {
    $resolvedTemp = [System.IO.Path]::GetFullPath($tempDir)
    if ($resolvedTemp.StartsWith($tempRoot, [System.StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $resolvedTemp)) {
        Remove-Item -LiteralPath $resolvedTemp -Recurse -Force
    }
}
