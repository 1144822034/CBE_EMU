@echo off
setlocal EnableExtensions

if "%~1"=="" (
    echo Usage: %~nx0 ^<player-name^>
    exit /b 2
)

for %%I in ("%~dp0..") do set "BIN_DIR=%%~fI"
set "PROFILE_DIR=%BIN_DIR%\multiplayer-data\%~1"
set "ENDPOINT=%CBE_MULTIPLAYER_ENDPOINT%"
if "%ENDPOINT%"=="" set "ENDPOINT=127.0.0.1:19090"
if "%CBE_HANGUP_AUTO_CONFIRM%"=="" set "CBE_HANGUP_AUTO_CONFIRM=1"

call "%~dp0prepare-profile.bat" "%PROFILE_DIR%" || (
    echo Profile setup failed for %~1.
    pause
    exit /b 1
)

title CBE Emulator - %~1
cd /d "%PROFILE_DIR%"
set "CBE_MOCK_SERVICE=%ENDPOINT%"
echo [%~1] local data: %PROFILE_DIR%\nvram
echo [%~1] mock service: %ENDPOINT%
echo [%~1] hangup reward auto-confirm: %CBE_HANGUP_AUTO_CONFIRM%
"%BIN_DIR%\main.exe" "--mock-service=%ENDPOINT%"
pause
