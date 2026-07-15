@echo off
SETLOCAL EnableDelayedExpansion

REM ============================================================
REM  Paths -- adjust if your layout differs
REM ============================================================
SET GCC=C:\msys64\ucrt64\bin\gcc.exe

REM SDL2 dev package layout (MinGW flavour):
REM   <SDL2_DIR>\include\SDL2\SDL.h   <- headers
REM   <SDL2_DIR>\lib\libSDL2.a        <- libs
REM If your zip has an x86_64-w64-mingw32\ sub-folder, append it here.
SET SDL2_DIR=C:\Users\Erik\code\SDL2-2.32.10\x86_64-w64-mingw32
SET SDL2_INC=%SDL2_DIR%\include\SDL2
SET SDL2_LIB=%SDL2_DIR%\lib

SET ARCH=64

REM ============================================================
REM  Warning flags  (split across two variables; cmd line-length limit)
REM ============================================================
REM -Waggregate-return
SET EXTRA_CFLAGS=-DSDL_MAIN_HANDLED -Warith-conversion -Wcast-align=strict -Wcast-qual -Wconversion -Wdouble-promotion -Wduplicated-branches -Wduplicated-cond -Wfloat-equal -Wformat=2 -Wlogical-op -Wmissing-include-dirs -Wnull-dereference -Wstrict-aliasing=2 -Wstrict-overflow=2 -Wswitch-default -Wswitch-enum -Wundef -Wuninitialized -Wwrite-strings
SET CFLAGS=-Wall -Wextra -Wpedantic -Wshadow -Werror -Wfatal-errors %EXTRA_CFLAGS%

REM ============================================================
REM  Argument check
REM ============================================================
IF "%~1"=="" (
    echo Usage: %~nx0 ^<game_name^>
    exit /b 1
)

REM Strip .c extension if the caller included it (mirrors ${1%%.c})
SET GAME=%~n1

REM ============================================================
REM  Compile exotique.c
REM ============================================================
"%GCC%" -c %CFLAGS% -std=c2x -Os ^
    -I "%SDL2_INC%" -D_REENTRANT ^
    exotique.c
IF ERRORLEVEL 1 (
    echo [FAILED] exotique.c
    exit /b 1
)

REM ============================================================
REM  Compile game file
REM ============================================================
REM 
"%GCC%" -c %CFLAGS% -std=c99 -O3 -ggdb -ffast-math -march=znver4 -fno-strict-aliasing -nostdlib -nostdinc -nodefaultlibs -nolibc -ffreestanding ^
    -I ".." ^
    ^
    -D ARCH=%ARCH% ^
    "%GAME%.c"
IF ERRORLEVEL 1 (
    echo [FAILED] %GAME%.c
    exit /b 1
)

REM ============================================================
REM  Link
REM  NOTE: -lasan may fail with MinGW -- remove if GCC complains
REM        it cannot find libasan.
REM ============================================================
"%GCC%" -Wl,-Map=output.map exotique.o "%GAME%.o" ^
    -L "%SDL2_LIB%" -lSDL2 ^
    ^
    -o "%GAME%.exe"
IF ERRORLEVEL 1 (
    echo [FAILED] link step
    exit /b 1
)

echo.
echo Build OK: %GAME%.exe