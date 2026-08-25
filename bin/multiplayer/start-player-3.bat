@echo off
setlocal EnableExtensions
set "PLAYER=player-3"
rem Keep the Linan scene-battle path observable during direct player-3 repros.
if "%CBE_TRACE_SCENE_BATTLE_COLLISION%"=="" set "CBE_TRACE_SCENE_BATTLE_COLLISION=1"
if "%CBE_TRACE_SCE_ENTITY_CALLBACK%"=="" set "CBE_TRACE_SCE_ENTITY_CALLBACK=1"
if "%CBE_TRACE_SCE_NODE_ACTOR_ID%"=="" set "CBE_TRACE_SCE_NODE_ACTOR_ID=1"
if "%CBE_TRACE_SCENE_BATTLE_CONTROL_STATE%"=="" set "CBE_TRACE_SCENE_BATTLE_CONTROL_STATE=1"
call "%~dp0start-player-common.bat" "%PLAYER%"
