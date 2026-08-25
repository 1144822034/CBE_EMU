@echo off
setlocal EnableExtensions
for %%I in ("%~dp0..") do set "BIN_DIR=%%~fI"
set "SOURCE_SCE="
for %%F in ("%BIN_DIR%\JHOnlineData\c04*_01.sce") do if exist "%%~fF" set "SOURCE_SCE=%%~fF"
set "MIRROR_SCE=%BIN_DIR%\JHOnlineData\04linan_battle_01.sce"
if "%SOURCE_SCE%"=="" (
    echo Missing deployed c04 scene matching c04*_01.sce.
    pause
    exit /b 1
)
python "%BIN_DIR%\..\tools\prepare_linan_battle_mirror.py" --source "%SOURCE_SCE%" --output "%MIRROR_SCE%" --resource-root "%BIN_DIR%\JHOnlineData"
if errorlevel 1 (
    echo Could not prepare mirror scene with its battle background.
    pause
    exit /b 1
)
set "CBE_SCENE_KEY=04linan_battle_01.sce"
call "%~dp0start-server.bat"
