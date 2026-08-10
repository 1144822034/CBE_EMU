REM Build script for compiling CBE client to WebAssembly using Emscripten
REM 
REM Prerequisites:
REM   - Emscripten SDK installed and activated (emsdk install latest && emsdk activate latest)
REM   - Python 3.10+ (emsdk bundles its own)
REM   - Node.js available
REM
REM Usage:
REM   build-web.bat              Build web version
REM   build-web.bat --clean      Clean build artifacts
REM
REM The existing web/build/cbe-emu.wasm was compiled with specific flags.
REM This script recreates the build environment and attempts a fresh compile.
REM If unicorn WASM lib is not available, it falls back to using the existing wasm binary.

setlocal enabledelayedexpansion

set "ROOT_DIR=%~dp0.."
set "WEB_BUILD=%ROOT_DIR%\web\build"
set "WEB_FS=%ROOT_DIR%\web\fs"
set "WEB_SRC=%ROOT_DIR%\src"

REM Setup Emscripten environment if not already set
if not defined EMSDK (
    call "%ROOT_DIR%\emsdk\emsdk_env.bat" >nul 2>&1
)

where emcc >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: emcc not found in PATH. Run "emsdk activate latest" first.
    exit /b 1
)

emcc --version >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: emscripten toolchain not working.
    exit /b 1
)

set "ACTION=build"
if "%~1"=="--clean" set "ACTION=clean"

if "!ACTION!"=="clean" (
    echo === Cleaning web build artifacts ===
    del "%WEB_BUILD%\cbe-emu.wasm" 2>nul
    del "%WEB_BUILD%\cbe-emu.js" 2>nul
    del "%WEB_BUILD%\cbe-emu.data" 2>nul
    del "%WEB_BUILD%\cbe-emu.wasm.map" 2>nul
    rmdir /s /q "%ROOT_DIR%\obj\web" 2>nul
    echo Done.
    exit /b 0
)

echo === Building CBE client for Web ===
echo Emscripten version:
emcc --version
echo.

set "WEB_OBJDIR=%ROOT_DIR%\obj\web"
if not exist "!WEB_OBJDIR!" mkdir "!WEB_OBJDIR!"

REM Check if we have a usable unicorn wasm library
set "HAS_UNICORN_WASM=0"
if exist "%ROOT_DIR%\Lib\unicorn-wasm-build\libunicorn.a" set "HAS_UNICORN_WASM=1"
if exist "%ROOT_DIR%\Lib\unicorn-wasm-build\libunico*.a" set "HAS_UNICORN_WASM=1"

if !HAS_UNICORN_WASM! equ 0 (
    echo WARNING: No unicorn-wasm static library found.
    echo The build will include unicorn source files but linking may fail.
    echo To fix: build unicorn-wasm first with: scripts\build-unicorn-wasm.bat
    echo.
)

echo Building...
echo.

emcc ^
    -std=gnu11 ^
    -O2 ^
    -g4 ^
    -DNDEBUG ^
    -DCBE_CLIENT_ONLY ^
    -DNETWORK_SUPPORT ^
    -DEMScripten ^
    -Wall ^
    -Wno-unused-function ^
    -Wno-unused-variable ^
    -Wno-unused-but-set-variable ^
    -ffunction-sections ^
    -fdata-sections ^
    -w ^
    -s ALLOW_MEMORY_GROWTH=1 ^
    -s MAXIMUM_MEMORY=4GB ^
    -s ENVIRONMENT=canvas ^
    -s USE_SDL=2 ^
    -s MODULARIZE=1 ^
    -s EXPORT_NAME="CbeModule" ^
    -s EXPORTED_RUNTIME_METHODS=["ccall","cwrap","getValue","setValue","UTF8ToString","stringToUTF8OnStack","stackAlloc","stackRestore","stackSave","addRunDependency","removeRunDependency"] ^
    -s EXPORTED_FUNCTIONS=["_main","_malloc","_free","_memset","_memalign","_SDL_GetTicks","_cbe_web_input_char","_cbe_web_input_char_at","_cbe_web_input_done","_cbe_web_input_is_open","_cbe_web_input_backspace","_cbe_web_input_is_password","_cbe_web_input_len","_cbe_web_input_max_len","_cbe_web_input_type"] ^
    -s NO_EXIT_RUNTIME=1 ^
    -s ASSERTIONS=0 ^
    -s PTHREAD_POOL_SIZE=1 ^
    -s INCOMING_MODULE_JS_API=["ccall","cwrap","getValue","setValue","UTF8ToString","stringToUTF8OnStack","stackAlloc","stackRestore","stackSave","addRunDependency","removeRunDependency","wasmMemory","buffer","addOnPreInit","addOnPostRun","addOnPreRun","addOnRun"] ^
    --preload-file="%WEB_FS%@/fs" ^
    -I"!WEB_SRC!" ^
    -I"%ROOT_DIR%\Lib\sdl2-2.0.10\include" ^
    -L"%ROOT_DIR%\Lib\unicorn-2.1.4" ^
    -liconv ^
    -lm ^
    "-o" "%WEB_BUILD%\cbe-emu.js" ^
    "!WEB_SRC!\gifDecode.c" ^
    "!WEB_SRC!\cbeParser.c" ^
    "!WEB_SRC!\mystd.c" ^
    "!WEB_SRC!\fontEngine.c" ^
    "!WEB_SRC!\vmMalloc.c" ^
    "!WEB_SRC!\fileIoEngine.c" ^
    "!WEB_SRC!\lcd.c" ^
    "!WEB_SRC!\md5.c" ^
    "!WEB_SRC!\main.c" ^
    "!WEB_SRC!\vmFunc.c" ^
    "!WEB_SRC!\hookRam.c" ^
    "!WEB_SRC!\vmEvent.c" 2>&1

if %errorlevel% equ 0 (
    echo.
    echo === Build successful ===
    echo Output: %WEB_BUILD%\cbe-emu.js
    if exist "%WEB_BUILD%\cbe-emu.wasm" (
        for %%F in ("%WEB_BUILD%\cbe-emu.wasm") do echo WASM: %%~zF bytes
    )
    exit /b 0
) else (
    echo.
    echo === Build FAILED ===
    echo.
    echo Falling back to existing artifacts...
    if exist "%WEB_BUILD%\cbe-emu.wasm" (
        echo Existing web/build/cbe-emu.wasm is still available.
        exit /b 0
    ) else (
        echo No existing artifacts found. Cannot recover.
        exit /b 1
    )
)
