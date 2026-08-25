@echo off
setlocal EnableExtensions

rem Remote scene-hangup repro: preserves player-3's ordinary profile and logs.
rem Set CBE_MULTIPLAYER_ENDPOINT before launch to test another remote endpoint.
if "%CBE_MULTIPLAYER_ENDPOINT%"=="" set "CBE_MULTIPLAYER_ENDPOINT=121.40.139.236:19090"
set "CBE_MULTIPLAYER_CLIENT_EXE=%~dp0..\main-remote-hangup.exe"
set "CBE_HANGUP_AUTO_CONFIRM=0"

call "%~dp0start-player-3.bat"
