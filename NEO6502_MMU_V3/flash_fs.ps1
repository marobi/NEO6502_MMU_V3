# ================================================
# RP2350 LittleFS ONLY Flash Script (Linker Truth)
# ================================================

Write-Host ""
Write-Host "=== RP2350 LittleFS ONLY Flash (Linker Based) ==="
Write-Host ""

$ProjectDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectName = Split-Path $ProjectDir -Leaf

$VMBuildRoot = "$env:LOCALAPPDATA\Temp\VMBuilds\$ProjectName"
$ArduinoRoot = "$env:LOCALAPPDATA\arduino15\packages\rp2040"
$OpenOCD     = "$ArduinoRoot\tools\pqt-openocd\5.0.0-9576866\bin\openocd.exe"
$OpenOCDScripts = "$ArduinoRoot\tools\pqt-openocd\5.0.0-9576866\share\openocd\scripts"

# ------------------------------------------------
# Locate latest Visual Micro build folder
# ------------------------------------------------

if (!(Test-Path $VMBuildRoot)) {
    Write-Error "No Visual Micro build folder found. Build once first."
    exit 1
}

$LatestBuild = Get-ChildItem $VMBuildRoot -Directory |
               Sort-Object LastWriteTime -Descending |
               Select-Object -First 1

$ReleaseFolder = Join-Path $LatestBuild.FullName "Release"
$linkerFile    = Join-Path $ReleaseFolder "memmap_default.ld"

if (!(Test-Path $linkerFile)) {
    Write-Error "memmap_default.ld not found."
    exit 1
}

Write-Host "Using build folder:"
Write-Host $ReleaseFolder
Write-Host ""

# ------------------------------------------------
# Extract FS region from linker script
# ------------------------------------------------

$fsStartLine = Select-String -Path $linkerFile -Pattern "_FS_start"
$fsEndLine   = Select-String -Path $linkerFile -Pattern "_FS_end"

if (!$fsStartLine -or !$fsEndLine) {
    Write-Error "Could not find _FS_start/_FS_end in linker script."
    exit 1
}

$fsStartDec = ($fsStartLine.Line -replace '[^0-9]','')
$fsEndDec   = ($fsEndLine.Line   -replace '[^0-9]','')

$fsStart = [int64]$fsStartDec
$fsEnd   = [int64]$fsEndDec
$fsSize  = $fsEnd - $fsStart

if ($fsSize -le 0) {
    Write-Error "Invalid filesystem size computed."
    exit 1
}

$fsStartHex = "0x{0:X8}" -f $fsStart

Write-Host "Filesystem Start:"
Write-Host $fsStartHex
Write-Host ""
Write-Host "Filesystem Size:"
Write-Host $fsSize
Write-Host ""

# ------------------------------------------------
# Locate mklittlefs
# ------------------------------------------------

$mklfs = Get-ChildItem "$ArduinoRoot\tools" -Recurse -Filter "mklittlefs.exe" | Select-Object -First 1

if (!$mklfs) {
    Write-Error "mklittlefs tool not found."
    exit 1
}

# ------------------------------------------------
# Build filesystem image
# ------------------------------------------------

$dataFolder = Join-Path $ProjectDir "data"

if (!(Test-Path $dataFolder)) {
    Write-Error "No 'data' folder found."
    exit 1
}

$fsImage = Join-Path $ReleaseFolder "littlefs.bin"

Write-Host "Building LittleFS image..."
Write-Host ""

& $mklfs.FullName `
    -c $dataFolder `
    -s $fsSize `
    -b 4096 `
    -p 256 `
    $fsImage

if ($LASTEXITCODE -ne 0) {
    Write-Error "LittleFS build failed."
    exit 1
}

Write-Host "Filesystem image created:"
Write-Host $fsImage
Write-Host ""

# ------------------------------------------------
# CRC32 implementation (fast, correct)
# ------------------------------------------------

Add-Type -TypeDefinition @"
using System;
using System.IO;

public class CRC32 {
    private static uint[] table;

    static CRC32() {
        table = new uint[256];
        for (uint i = 0; i < 256; i++) {
            uint crc = i;
            for (int j = 0; j < 8; j++)
                crc = (crc & 1) == 1 ? (0xEDB88320 ^ (crc >> 1)) : (crc >> 1);
            table[i] = crc;
        }
    }

    public static uint Compute(string filename) {
        uint crc = 0xFFFFFFFF;
        byte[] bytes = File.ReadAllBytes(filename);
        foreach (byte b in bytes)
            crc = table[(crc ^ b) & 0xFF] ^ (crc >> 8);
        return crc ^ 0xFFFFFFFF;
    }
}
"@

# ------------------------------------------------
# Dump existing flash region
# ------------------------------------------------

$tempDump = Join-Path $ReleaseFolder "flash_dump.bin"

Write-Host "Reading current flash region..."
Write-Host ""

& $OpenOCD `
    -f "interface/cmsis-dap.cfg" `
    -f "target/rp2350.cfg" `
    -s $OpenOCDScripts `
    -c "init; adapter speed 5000; dump_image {$tempDump} $fsStartHex $fsSize; exit"

if (!(Test-Path $tempDump)) {
    Write-Error "Flash dump failed."
    exit 1
}

# ------------------------------------------------
# CRC compare
# ------------------------------------------------

Write-Host "Computing CRC..."
Write-Host ""

$newCRC = [CRC32]::Compute($fsImage)
$oldCRC = [CRC32]::Compute($tempDump)

Write-Host ("New CRC  : 0x{0:X8}" -f $newCRC)
Write-Host ("Flash CRC: 0x{0:X8}" -f $oldCRC)
Write-Host ""

if ($newCRC -eq $oldCRC) {
    Write-Host "Filesystem unchanged. Skipping flash."
    Remove-Item $tempDump -Force
    exit 0
}

Write-Host "Filesystem differs. Flashing..."
Write-Host ""

# ------------------------------------------------
# Flash filesystem region
# ------------------------------------------------

& $OpenOCD `
    -f "interface/cmsis-dap.cfg" `
    -f "target/rp2350.cfg" `
    -s $OpenOCDScripts `
    -c "init; adapter speed 5000; program {$fsImage} $fsStartHex verify; reset; exit"

if ($LASTEXITCODE -ne 0) {
    Write-Error "OpenOCD flash failed."
    exit 1
}

Remove-Item $tempDump -Force

Write-Host ""
Write-Host "Filesystem flashed successfully."
Write-Host ""
