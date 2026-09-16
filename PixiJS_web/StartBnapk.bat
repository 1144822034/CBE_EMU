@echo off
cd /d "%~dp0"
call npm run build:android
if errorlevel 1 goto :fail
call npm run build:android:nofx
if errorlevel 1 goto :fail
dir android\app\release\jh_ol*.apk
findstr /C:"versionCode" /C:"versionName" android\app\build.gradle
type client_code_version.txt
echo.
echo Build OK.
pause
exit /b 0

:fail
echo.
echo Build FAILED. Exit code: %ERRORLEVEL%
pause
exit /b %ERRORLEVEL%
