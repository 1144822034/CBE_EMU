@echo off
REM Build Unicorn engine as WASM static library for Emscripten
REM Usage: build-unicorn-wasm.bat

setlocal enabledelayedexpansion

set "ROOT_DIR=%~dp0.."
set "SRC_DIR=%ROOT_DIR%\Lib\unicorn-wasm-src"
set "BUILD_DIR=%ROOT_DIR%\Lib\unicorn-wasm-build"
set "UNICORN_H=%ROOT_DIR%\Lib\unicorn-2.1.4"

REM Setup environment
if not defined EMSDK (
    call "%ROOT_DIR%\emsdk\emsdk_env.bat" >nul 2>&1
)

set "EMCC=emcc"
set "EMAR=emar"

where !EMCC! >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: emscripten not found in PATH.
    exit /b 1
)

echo === Building Unicorn engine for WebAssembly ===
echo Source: !SRC_DIR!
echo Output: !BUILD_DIR!

if exist "!BUILD_DIR!" rmdir /s /q "!BUILD_DIR!"
mkdir "!BUILD_DIR!"
cd "!BUILD_DIR!"

echo Configuring with emcmake...
call emcmake cmake "!SRC_DIR!" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DUNICORN_INSTALL_PREFIX="!BUILD_DIR!/install" ^
    -DUNICORN_ARCH=wasm ^
    -DUNICORN_OS=emscripten ^
    -DBUILD_SHARED_LIBS=OFF ^
    -DCMAKE_C_FLAGS_RELEASE="-Os -DNDEBUG" ^
    2>&1

if %errorlevel% neq 0 (
    echo CMake configure failed, trying manual build...
    goto :manual_build
)

echo Building...
call cmake --build . --config Release -j2 2>&1

if %errorlevel% equ 0 (
    echo Copying to Lib/unicorn-2.1.4...
    if exist "*.a" (
        copy /y "*.a" "!ROOT_DIR!\Lib\unicorn-2.1.4\" >nul
    )
    if exist "install/include/" (
        xcopy /s /y "install\include\*" "!UNICORN_H!\" >nul
    )
    echo === Unicorn WASM build successful ===
    dir "*.a" 2>&1
    exit /b 0
)

:manual_build
echo === Fallback: Manual emcc build ===

set "WASM_CFLAGS=-Os -DNDEBUG -std=c11 -fno-rtti -fno-exceptions -ffunction-sections -fdata-sections"
set "WASM_LDFLAGS=-s ALLOW_MEMORY_GROWTH=1 -s EXPORTED_FUNCTIONS='['_uc_open','_uc_close','_uc_emu_start','_uc_emu_stop','_uc_reg_write','_uc_reg_read','_uc_mem_map','_uc_mem_map_ptr','_uc_mem_write','_uc_mem_read','_uc_hook_add','_uc_hook_del','_uc_errno','_uc_strerror']' -s EXTRA_EXPORTED_RUNTIME_METHODS='[\"ccall\",\"cwrap\"]' -s MODULARIZE=1 -s ENVIRONMENT='js' -s SUPPORT_EXCEPTIONS=0 -s INVOKE_JS=0 -s SINGLE_FILE=0 -s NO_EXIT_RUNTIME=1 -s STACK_SIZE=5MB -s INITIAL_MEMORY=32MB --closure 0 -s ASSERTIONS=0"

REM Collect unicorn source files
set "UC_SOURCES="

for %%F in (!SRC_DIR!\uc.c) do set "UC_SOURCES=!UC_SOURCES! %%~fF"
for /r "!SRC_DIR!\qemu" %%F in (*.c) do set "UC_SOURCES=!UC_SOURCES! %%~fF"

echo Sources count: ~!UC_SOURCES!~

echo Running emcc...
call emcc !WASM_CFLAGS! !UC_SOURCES! ^
    -I"!SRC_DIR!\include" ^
    -I"!SRC_DIR!" ^
    -I"!SRC_DIR!\qemu" ^
    -I"!SRC_DIR!\qemu\include" ^
    -I"!SRC_DIR!\qemu\accel\tcg" ^
    -I"!SRC_DIR!\qemu\accel\tcg\aarch64" ^
    -I"!SRC_DIR!\qemu\accel\tcg\arm" ^
    -I"!SRC_DIR!\qemu\accel\tcg\ppc" ^
    -I"!SRC_DIR!\qemu\accel\tcg\riscv" ^
    -I"!SRC_DIR!\qemu\accel\tcg\s390x" ^
    -I"!SRC_DIR!\qemu\accel\tcg\sparc" ^
    -I"!SRC_DIR!\qemu\accel\tcg\mips" ^
    -I"!SRC_DIR!\qemu\accel\tcg\x86" ^
    -I"!SRC_DIR!\qemu\tcg" ^
    -I"!SRC_DIR!\qemu\tcg\arc" ^
    -I"!SRC_DIR!\qemu\tcg\arm" ^
    -I"!SRC_DIR!\qemu\tcg\hppa" ^
    -I"!SRC_DIR!\qemu\tcg\m68k" ^
    -I"!SRC_DIR!\qemu\tcg\microblaze" ^
    -I"!SRC_DIR!\qemu\tcg\mips" ^
    -I"!SRC_DIR!\qemu\tcg\nios2" ^
    -I"!SRC_DIR!\qemu\tcg\ppc" ^
    -I"!SRC_DIR!\qemu\tcg\riscv" ^
    -I"!SRC_DIR!\qemu\tcg\s390x" ^
    -I"!SRC_DIR!\qemu\tcg\sparc" ^
    -I"!SRC_DIR!\qemu\tcg\tricore" ^
    -I"!SRC_DIR!\qemu\tcg\x86" ^
    -I"!SRC_DIR!\qemu\tcg\xtensa" ^
    !WASM_LDFLAGS! ^
    -o "!ROOT_DIR!\web\build\unicorn-wasm.js" 2>&1

if %errorlevel% equ 0 (
    echo === Unicorn manual build successful ===
    exit /b 0
) else (
    echo === Unicorn build FAILED ===
    exit /b 1
)
