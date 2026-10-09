@echo off
title Upload Firmware Arduino Uno
echo =========================================================================
echo FLASHING FIRMWARE MONITORING PLTS ^& IOT KE ARDUINO UNO
echo =========================================================================
echo Pastikan koneksi USB di browser sudah DI-PUTUS (klik Putus USB) sebelum upload!
echo.

set PORT=COM4
if not "%~1"=="" set PORT=%~1

set CLI_PATH=arduino-cli
where arduino-cli >nul 2>nul
if %ERRORLEVEL% EQU 0 goto DO_UPLOAD

if exist "%LOCALAPPDATA%\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" (
    set "CLI_PATH=%LOCALAPPDATA%\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
    goto DO_UPLOAD
)

if exist "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" (
    set "CLI_PATH=C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
    goto DO_UPLOAD
)

echo [INFO] arduino-cli otomatis tidak ditemukan di path default.
echo Anda bisa membuka file sketch berikut langsung menggunakan Arduino IDE:
echo "%~dp0monitoring_plts_firebase\monitoring_plts_firebase.ino"
echo lalu pilih board Arduino Uno dan klik tombol Upload (panah kanan).
echo.
pause
exit /b 0

:DO_UPLOAD
echo Mengunggah ke port %PORT%...
"%CLI_PATH%" upload -p %PORT% --fqbn arduino:avr:uno "%~dp0monitoring_plts_firebase"
if %ERRORLEVEL% EQU 0 (
    echo.
    echo =========================================================================
    echo [BERHASIL] Firmware berhasil di-upload ke Arduino Uno di %PORT%!
    echo Sekarang Anda bisa mengklik kembali 'Hubungkan USB' di web browser.
    echo =========================================================================
) else (
    echo.
    echo [INFO] Upload gagal. Kemungkinan penyebab:
    echo 1. Port %PORT% masih terbuka di browser (klik 'Putus USB' di dashboard).
    echo 2. Port Arduino berbeda (buka Device Manager untuk cek nomor COM).
    echo 3. Atau buka monitoring_plts_firebase.ino di Arduino IDE untuk upload manual.
)
pause
