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
set "CLIENT_EXE=%CBE_MULTIPLAYER_CLIENT_EXE%"
if "%CLIENT_EXE%"=="" set "CLIENT_EXE=%BIN_DIR%\main.exe"
if "%CBE_HANGUP_AUTO_CONFIRM%"=="" set "CBE_HANGUP_AUTO_CONFIRM=0"
if "%CBE_TRACE_ACTOR_SCENE_CAPACITY%"=="" set "CBE_TRACE_ACTOR_SCENE_CAPACITY=0"
if "%CBE_TRACE_ACTOR_SCENE_CAPACITY_MAX%"=="" set "CBE_TRACE_ACTOR_SCENE_CAPACITY_MAX=128"
if "%CBE_TRACE_SCE_ENTITY_CALLBACK%"=="" set "CBE_TRACE_SCE_ENTITY_CALLBACK=0"
if "%CBE_TRACE_SCE_NODE_ACTOR_ID%"=="" set "CBE_TRACE_SCE_NODE_ACTOR_ID=1001"
if "%CBE_TRACE_SCENE_BATTLE_COLLISION%"=="" set "CBE_TRACE_SCENE_BATTLE_COLLISION=0"
if "%CBE_TRACE_SCREEN_LIFECYCLE_ORDER%"=="" set "CBE_TRACE_SCREEN_LIFECYCLE_ORDER=1"
if "%CBE_CAPTURE_DREAM_INSTANCE%"=="" set "CBE_CAPTURE_DREAM_INSTANCE=0"
if "%CBE_TRACE_SCENE_NUMBERS%"=="" set "CBE_TRACE_SCENE_NUMBERS=0"

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
echo [%~1] client: %CLIENT_EXE%
echo [%~1] hangup reward auto-confirm: %CBE_HANGUP_AUTO_CONFIRM%
echo [%~1] actor scene capacity trace: %CBE_TRACE_ACTOR_SCENE_CAPACITY% ^(max %CBE_TRACE_ACTOR_SCENE_CAPACITY_MAX% records^)
echo [%~1] SCE entity trace: %CBE_TRACE_SCE_ENTITY_CALLBACK% ^(actor %CBE_TRACE_SCE_NODE_ACTOR_ID%^)
echo [%~1] scene battle collision trace: %CBE_TRACE_SCENE_BATTLE_COLLISION%
echo [%~1] screen lifecycle ordering trace: %CBE_TRACE_SCREEN_LIFECYCLE_ORDER%
echo [%~1] dream-instance packet capture: %CBE_CAPTURE_DREAM_INSTANCE%
echo [%~1] scene-number draw trace: %CBE_TRACE_SCENE_NUMBERS%
"%CLIENT_EXE%" "--mock-service=%ENDPOINT%"
pause
