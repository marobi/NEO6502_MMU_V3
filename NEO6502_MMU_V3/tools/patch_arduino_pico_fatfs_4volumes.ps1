# Patch Arduino-Pico 5.6.1 FatFS configuration for NEO6502_MMU multi-stick support.
# Required because FatFS ff.cpp includes ffconf.h from the Arduino-Pico package directory.
# Run once after installing/updating Arduino-Pico 5.6.1, then do a full clean/rebuild.

$ErrorActionPreference = "Stop"

$ffconf = Join-Path $env:LOCALAPPDATA "Arduino15\packages\rp2040\hardware\rp2040\5.6.1\libraries\FatFS\src\ffconf.h"

if (!(Test-Path $ffconf)) {
    Write-Error "Arduino-Pico FatFS ffconf.h not found: $ffconf"
}

$backup = "$ffconf.neo6502_backup"
if (!(Test-Path $backup)) {
    Copy-Item $ffconf $backup
    Write-Host "Backup created: $backup"
} else {
    Write-Host "Backup already exists: $backup"
}

$text = Get-Content $ffconf -Raw
$text = $text -replace '#define\s+FF_VOLUMES\s+1', '#define FF_VOLUMES 4'
$text = $text -replace '#define\s+FF_VOLUME_STRS\s+"FLASH"', '#define FF_VOLUME_STRS "USB0","USB1","USB2","USB3"'
Set-Content -Path $ffconf -Value $text -NoNewline

Write-Host "Patched: $ffconf"
Write-Host "Verify these lines now read:"
Select-String -Path $ffconf -Pattern 'FF_VOLUMES|FF_VOLUME_STRS'
Write-Host "Now perform a full Visual Micro clean/rebuild."
