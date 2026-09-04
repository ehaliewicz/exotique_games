@echo off
SETLOCAL EnableDelayedExpansion

REM ============================================================
REM  Paths -- adjust if your layout differs
REM ============================================================
SET GCC=C:\msys64\ucrt64\bin\gcc.exe

SET ARCH=64

REM Strip .c extension if the caller included it (mirrors ${1%%.c})
SET GAME=%~n1

REM ============================================================
REM  Compile miniaudio.c
REM ============================================================
REM "%GCC%" -c miniaudio.c -Os -static -flto=8 -o miniaudio.o
REM IF ERRORLEVEL 1 (
REM    echo [FAILED] exotique.c
REM    exit /b 1

REM ============================================================
REM  Compile game file
REM ============================================================
REM 
REM -static -flto=8
"%GCC%" -fno-common -Os -s -static -flto=8 -std=c23 -DRGFW -march=westmere ^
    -I ".." ^
    -D ARCH=%ARCH% ^
    rgfw_platform.c miniaudio.c "%GAME%.c" -lwsock32 -lgdi32 -o "%GAME%_rgfw.exe"
IF ERRORLEVEL 1 (
    echo [FAILED] %GAME%.c
    exit /b 1
)

echo.
echo Build OK: %GAME%_rgfw.exe