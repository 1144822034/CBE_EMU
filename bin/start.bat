@echo off
set PATH=C:\Windows\System32;C:\mingw64\bin
rem Keep console interactive so server-list prompt can be answered.
rem Logs still go to client_out.txt via tee-like redirect of stdout only.
main.exe 1>client_out.txt
pause
