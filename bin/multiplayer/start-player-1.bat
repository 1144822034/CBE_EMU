@echo off
setlocal EnableExtensions
set "PLAYER=player-1"
rem Temporary read-only SCE entity parser probe for the Forge Valley monkey challenge.
rem It only writes player-1\logs\sce-entity-callback.log; no guest state is changed.
if "%CBE_TRACE_SCE_ENTITY_CALLBACK%"=="" set "CBE_TRACE_SCE_ENTITY_CALLBACK=1"
call "%~dp0start-player-common.bat" "%PLAYER%"
