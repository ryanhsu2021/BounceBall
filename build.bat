@echo off
REM ============================================================
REM  BounceBall one-click build script
REM  Run from "x64 Native Tools Command Prompt for VS 2022"
REM ============================================================

setlocal

where cmake >nul 2>&1
if errorlevel 1 (
    echo [ERROR] cmake not found in PATH.
    echo         Install CMake 3.22+ and make sure it is on PATH.
    exit /b 1
)

where cl >nul 2>&1
if errorlevel 1 (
    echo [ERROR] MSVC environment not loaded.
    echo Please run this from "x64 Native Tools Command Prompt for VS 2022"
    exit /b 1
)

REM ---- Configure if no valid solution exists ----
if not exist "build\BounceBall.sln" (
    echo [INFO] Configuring project...
    if exist build (
        echo [INFO] Removing stale build directory...
        rmdir /s /q build
    )
    cmake -B build -G "Visual Studio 17 2022" -A x64
    if errorlevel 1 (
        echo [ERROR] CMake configure failed
        exit /b 1
    )
) else (
    echo [INFO] Existing build found, skipping configure.
)

echo [INFO] Building Release...
cmake --build build --config Release --parallel
if errorlevel 1 (
    echo [ERROR] Build failed
    exit /b 1
)

echo.
echo ============================================================
echo  BUILD SUCCESS
echo  VST3 copied to:
echo  C:\Program Files\Common Files\VST3\BounceBall.vst3
echo ============================================================
echo.
echo  Next: open Ableton Live, rescan VST3 plugins.

endlocal
