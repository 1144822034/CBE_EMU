@echo off
setlocal EnableExtensions
rem Bump client_code_version + Android versionCode / versionName.
rem
rem Usage:
rem   bump-versions.bat <client_code_version> [versionCode] [versionName]
rem
rem Examples:
rem   bump-versions.bat 35
rem       client=35, versionCode=current+1, versionName=1.0.<newCode>
rem       (server_code_version synced to same client value)
rem   bump-versions.bat 35 52
rem       client=35, versionCode=52, versionName=1.0.52
rem   bump-versions.bat 35 52 1.0.52
rem       explicit all three

cd /d "%~dp0\.."
where node >nul 2>nul
if errorlevel 1 (
  echo [bump-versions] node not found in PATH
  exit /b 1
)

node "%~dp0bump-versions.mjs" %*
exit /b %ERRORLEVEL%
