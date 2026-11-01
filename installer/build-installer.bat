@echo off
chcp 65001 >nul
setlocal

echo ========================================
echo 7-Zip Context Menu - Build Installer
echo ========================================
echo.

:: Check if Inno Setup is installed
set "ISCC="
if exist "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" (
    set "ISCC=C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
) else if exist "C:\Program Files\Inno Setup 6\ISCC.exe" (
    set "ISCC=C:\Program Files\Inno Setup 6\ISCC.exe"
)

if "%ISCC%"=="" (
    echo [ERROR] Inno Setup 6 not found!
    echo.
    echo Please install Inno Setup 6 from:
    echo   https://jrsoftware.org/isdl.php
    echo.
    pause
    exit /b 1
)

:: CMake install deploys the application and its matching Qt runtime.
if not exist "..\build\install\7ZipContextApp.exe" (
    echo [ERROR] Installed desktop application not found!
    echo.
    echo Please build the project first:
    echo   cmake --build build --config RelWithDebInfo
    echo   cmake --install build --config RelWithDebInfo
    echo.
    pause
    exit /b 1
)

echo Using Inno Setup: %ISCC%
echo.

:: Create output directory
if not exist "..\build\installer" mkdir "..\build\installer"

:: Compile installer
echo Building installer...
"%ISCC%" /Q 7ZipContext.iss

if errorlevel 1 (
    echo.
    echo [ERROR] Failed to build installer!
    pause
    exit /b 1
)

echo.
echo ========================================
echo Installer created successfully!
echo ========================================
echo.
echo Output: build\installer\7ZipContext-Setup-1.0.0.exe
echo.
pause
