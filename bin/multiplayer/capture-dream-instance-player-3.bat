@echo off
setlocal EnableExtensions

rem Delegates remote-target selection to start-player-3.bat, then enables the
rem read-only Dream Messenger packet capture and map-number draw trace for
rem this client process.
set "CBE_CAPTURE_DREAM_INSTANCE=1"
set "CBE_TRACE_SCENE_NUMBERS=1"
call "%~dp0start-player-3.bat"
exit /b %ERRORLEVEL%
