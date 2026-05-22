@echo off
REM =============================================================================
REM  DesktopPet 构建脚本 (Batch)
REM  使用 RedPanda C++ 自带的 MinGW64 编译器
REM =============================================================================
setlocal enabledelayedexpansion

set "MINGW_DIR=E:\Desktop\一一\软件\RedPanda-CPP\mingw64"
set "GXX=%MINGW_DIR%\bin\g++.exe"

set "PROJECT_DIR=e:\Yiyi Desktop animal"
set "OUTPUT_DIR=%PROJECT_DIR%\dist"
set "EXE_PATH=%OUTPUT_DIR%\DesktopPet.exe"

REM 检查编译器
if not exist "%GXX%" (
    echo [ERROR] g++.exe not found: %GXX%
    echo Please check RedPanda C++ installation path.
    exit /b 1
)

echo ======================================
echo   Desktop Pet - Build Script
echo ======================================
echo.

REM 清理旧输出
if exist "%OUTPUT_DIR%" (
    echo [CLEAN] Removing old build directory...
    rmdir /s /q "%OUTPUT_DIR%"
)

REM 创建输出目录
mkdir "%OUTPUT_DIR%" 2>nul
echo [OK] Output directory: %OUTPUT_DIR%

REM 进入项目目录
cd /d "%PROJECT_DIR%"

REM 编译
echo [BUILD] Compiling main.cpp ...
"%GXX%" -o "%EXE_PATH%" main.cpp ^
    -mwindows ^
    -static-libgcc ^
    -static-libstdc++ ^
    -O2 ^
    -Wall ^
    -Wextra ^
    -lgdiplus ^
    -lcomctl32 ^
    -municode

if %ERRORLEVEL% neq 0 (
    echo [FAIL] Compilation failed (exit code: %ERRORLEVEL%)
    exit /b %ERRORLEVEL%
)

echo [OK] Compilation successful!

REM 复制素材目录
if exist "%PROJECT_DIR%\animal" (
    echo [COPY] Copying animal assets...
    xcopy /e /i /y "%PROJECT_DIR%\animal" "%OUTPUT_DIR%\animal" >nul
    echo [OK] Assets copied to %OUTPUT_DIR%\animal
) else (
    echo [WARN] Asset directory not found: %PROJECT_DIR%\animal
)

REM 输出结果
for %%F in ("%EXE_PATH%") do set "SIZE=%%~zF"
set /a "SIZE_KB=%SIZE% / 1024"

echo.
echo ======================================
echo   Build Complete!
echo ======================================
echo   Executable: %EXE_PATH%
echo   Size:       %SIZE_KB% KB
echo   Run:        dist\DesktopPet.exe
echo ======================================

endlocal
