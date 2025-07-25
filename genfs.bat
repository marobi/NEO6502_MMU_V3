@echo off
set PROJECT_DIR=%~dp0
set DATA_DIR=%PROJECT_DIR%\data
set OUTPUT_IMG=%PROJECT_DIR%\littlefs.bin
set FS_OFFSET=0x10400000
set FS_SIZE=262144
set MKLITTLEFS=mklittlefs.exe
set PICO_TOOL=C:\Users\Rien\AppData\Local\Arduino15\packages\rp2040\tools\pqt-picotool\4.1.0-1aec55e\picotool.exe

REM === Build Filesystem Image ===
echo Creating LittleFS image...
%MKLITTLEFS% -c "%DATA_DIR%" -b 4096 -p 256 -s %FS_SIZE% "%OUTPUT_IMG%"

IF NOT EXIST "%OUTPUT_IMG%" (
    echo ERROR: Failed to create littlefs.bin
    pause
    exit /b 1
)

echo Put your RP2350B into BOOTSEL mode and press any key...
pause

echo Uploading filesystem to flash offset %FS_OFFSET%
%PICO_TOOL% load -f "%OUTPUT_IMG%" -o %FS_OFFSET%

echo Done!
pause
