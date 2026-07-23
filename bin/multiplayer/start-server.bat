@echo off
setlocal EnableExtensions

for %%I in ("%~dp0..") do set "BIN_DIR=%%~fI"
set "PORT=%CBE_MULTIPLAYER_SERVER_PORT%"
if "%PORT%"=="" set "PORT=19090"

title CBE Mock Service - %PORT%
cd /d "%BIN_DIR%"
echo Mock service listening on 127.0.0.1:%PORT%
echo Clients in this folder use the same service by default.
jh-online-server.exe --mock-service-only --mock-service-bind=127.0.0.1 --mock-service-port=%PORT%
pause
