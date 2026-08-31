@echo off
chcp 65001 >nul
CD /D %~dp0

echo [gateway] checking port 19092...
netstat -ano | findstr ":19092" | findstr "LISTENING" >nul
if %errorlevel%==0 (
  echo.
  echo [gateway] 19092 已在监听 — gateway 可能已在运行，无需重复启动。
  echo         客户端可继续连 ws://127.0.0.1:19092 或 ws://192.1.1.1:19092
  echo         若要重启：先结束占用 19092 的 node 进程，再运行本脚本。
  echo.
  pause
  exit /b 0
)

echo [gateway] starting npm run gateway ...
echo [gateway] defaults: keepalive 30s + orphan 2min (no env required)
call npm run gateway
if errorlevel 1 (
  echo.
  echo [gateway] 启动失败（常见原因：19092 被占用或 jh-online-server :19090 未启动）
  pause
  exit /b 1
)
