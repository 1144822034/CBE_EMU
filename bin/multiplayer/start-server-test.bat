@echo off
setlocal EnableExtensions

for %%I in ("%~dp0..") do set "BIN_DIR=%%~fI"
set "PORT=%CBE_MULTIPLAYER_SERVER_PORT%"
set "CBE_MYSQL_HOST=tql.cbhub.top"
set "CBE_MYSQL_PORT=19096"
set "CBE_MYSQL_DATABASE=jh_online"
rem The standalone WT16/3(result=2) response is a disproven protocol probe.
rem It reaches the parser but does not restore startup movement collision, so
rem it must never be enabled as the normal startup repair.
set "CBE_TEST_STARTUP_SCE_DIRECT_ENTER=0"
rem Retained only for packet forensics.
if "%CBE_TRACE_STARTUP_SCE_FOLLOWUP%"=="" set "CBE_TRACE_STARTUP_SCE_FOLLOWUP=0"
if "%PORT%"=="" set "PORT=19090"

title CBE Mock Service - %PORT%
cd /d "%BIN_DIR%"
echo Mock service listening on 127.0.0.1:%PORT%
echo Clients in this folder use the same service by default.
echo Startup SCE 16/3 probe (must remain 0 in normal runs): %CBE_TEST_STARTUP_SCE_DIRECT_ENTER%
echo Startup SCE follow-up packet trace: %CBE_TRACE_STARTUP_SCE_FOLLOWUP%
jh-online-server.exe --mock-service-only --mock-service-bind=127.0.0.1 --mock-service-port=%PORT% >server_out.txt
pause
