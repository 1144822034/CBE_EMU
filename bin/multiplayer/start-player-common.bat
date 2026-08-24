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
if "%CBE_HANGUP_AUTO_CONFIRM%"=="" set "CBE_HANGUP_AUTO_CONFIRM=0"
if "%CBE_TRACE_ACTOR_SCENE_CAPACITY%"=="" set "CBE_TRACE_ACTOR_SCENE_CAPACITY=1"
if "%CBE_TRACE_ACTOR_SCENE_CAPACITY_MAX%"=="" set "CBE_TRACE_ACTOR_SCENE_CAPACITY_MAX=128"
if "%CBE_TRACE_SCENE_ASSET_LIFECYCLE%"=="" set "CBE_TRACE_SCENE_ASSET_LIFECYCLE=0"
if "%CBE_TRACE_SCENE_ASSET_LIFECYCLE_MAX%"=="" set "CBE_TRACE_SCENE_ASSET_LIFECYCLE_MAX=2048"
if "%CBE_TRACE_SCE_ENTITY_CALLBACK%"=="" set "CBE_TRACE_SCE_ENTITY_CALLBACK=0"
if "%CBE_TRACE_SCE_NODE_ACTOR_ID%"=="" set "CBE_TRACE_SCE_NODE_ACTOR_ID=1001"
if "%CBE_TRACE_SCENE_BATTLE_COLLISION%"=="" set "CBE_TRACE_SCENE_BATTLE_COLLISION=0"
if "%CBE_TRACE_SCREEN_LIFECYCLE_ORDER%"=="" set "CBE_TRACE_SCREEN_LIFECYCLE_ORDER=1"

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
echo [%~1] actor scene capacity trace: %CBE_TRACE_ACTOR_SCENE_CAPACITY% ^(max %CBE_TRACE_ACTOR_SCENE_CAPACITY_MAX% records^)
echo [%~1] scene asset lifecycle trace: %CBE_TRACE_SCENE_ASSET_LIFECYCLE% ^(max %CBE_TRACE_SCENE_ASSET_LIFECYCLE_MAX% records^)
echo [%~1] SCE entity trace: %CBE_TRACE_SCE_ENTITY_CALLBACK% ^(actor %CBE_TRACE_SCE_NODE_ACTOR_ID%^)
echo [%~1] scene battle collision trace: %CBE_TRACE_SCENE_BATTLE_COLLISION%
echo [%~1] screen lifecycle ordering trace: %CBE_TRACE_SCREEN_LIFECYCLE_ORDER%
"%BIN_DIR%\main.exe" "--mock-service=%ENDPOINT%"
pause
