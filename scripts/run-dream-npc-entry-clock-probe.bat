@echo off
setlocal EnableExtensions

rem Stable player-1 dream-instance observation entry point.
rem A missing password is requested through PowerShell's masked local prompt.
rem Do not put credentials in this file or pass them as command-line text.
if not defined CBE_AUTOMATION_MYSQL_PASSWORD (
    echo Enter the isolated MySQL test password in the masked prompt.
    echo It is kept only in the child process that runs this one test.
    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$secure = Read-Host 'Isolated MySQL test password' -AsSecureString; $bstr = [IntPtr]::Zero; try { $bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure); $env:CBE_AUTOMATION_MYSQL_PASSWORD = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr); & '%~dp0run-shop-return-hangup-automation.ps1' -Scenario 'dream-npc-entry-clock-probe-v1'; exit $LASTEXITCODE } finally { Remove-Item Env:CBE_AUTOMATION_MYSQL_PASSWORD -ErrorAction SilentlyContinue; if ($bstr -ne [IntPtr]::Zero) { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr) } }"
    set "CBE_DREAM_PROBE_EXIT=%ERRORLEVEL%"
    goto :finished
)

cd /d "%~dp0.."
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "scripts\run-shop-return-hangup-automation.ps1" -Scenario dream-npc-entry-clock-probe-v1
set "CBE_DREAM_PROBE_EXIT=%ERRORLEVEL%"

:finished
if not "%CBE_DREAM_PROBE_EXIT%"=="0" (
    echo.
    echo [ERROR] Dream NPC entry probe stopped with exit code %CBE_DREAM_PROBE_EXIT%.
    echo Review the error above; its isolated evidence directory is printed by the runner.
    pause
)
exit /b %CBE_DREAM_PROBE_EXIT%
