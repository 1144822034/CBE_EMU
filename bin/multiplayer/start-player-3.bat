@echo off
setlocal EnableExtensions
set "PLAYER=player-3"
rem Keep the test-map combat path observable during normal player-3 repros.
if "%CBE_TRACE_SCENE_BATTLE_COLLISION%"=="" set "CBE_TRACE_SCENE_BATTLE_COLLISION=1"
call "%~dp0start-player-common.bat" "%PLAYER%"
