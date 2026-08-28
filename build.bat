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
REM SET SDL2_LIB=C:\Users\Erik\code\SDL\build\build\.libs

SET SDL2_NET_DIR=C:\Users\Erik\code\SDL2_net-devel-2.4.0-mingw\SDL2_net-2.4.0\x86_64-w64-mingw32
SET SDL2_NET_INC=%SDL2_NET_DIR%\include\SDL2
SET SDL2_NET_LIB=%SDL2_NET_DIR%\lib

SET ARCH=64

REM ============================================================
REM  Warning flags  (split across two variables; cmd line-length limit)
REM ============================================================
REM -Waggregate-return
SET EXTRA_CFLAGS=-DSDL_MAIN_HANDLED -Warith-conversion -Wcast-align=strict -Wcast-qual -Wconversion -Wdouble-promotion -Wduplicated-branches -Wduplicated-cond -Wfloat-equal -Wformat=2 -Wlogical-op -Wmissing-include-dirs -Wnull-dereference -Wstrict-aliasing=2 -Wstrict-overflow=2 -Wswitch-default -Wswitch-enum -Wundef -Wuninitialized -Wwrite-strings
SET CFLAGS=-Wall -Wextra -DSDL_MAIN_HANDLED
REM -Wpedantic -Wshadow -Werror -Wfatal-errors %EXTRA_CFLAGS%
SET SIMPLE_CFLAGS=-Wall 

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
"%GCC%" -c %CFLAGS% -std=c2x -Og -ggdb -march=westmere -ffunction-sections  ^
    -I "%SDL2_INC%" -D_REENTRANT ^
    exotique.c
IF ERRORLEVEL 1 (
    echo [FAILED] exotique.c
    exit /b 1
)
REM ============================================================
REM  Compile miniaudio.c
REM ============================================================
REM "%GCC%" -c miniaudio.c -o miniaudio.o
REM IF ERRORLEVEL 1 (
REM    echo [FAILED] exotique.c
REM    exit /b 1

REM ============================================================
REM  Compile game file
REM ============================================================
REM 
"%GCC%" -c %CFLAGS% -std=c23 -Og -ggdb -ffast-math -march=westmere -fno-strict-aliasing -nostdlib -nodefaultlibs -nolibc -ffreestanding -ffunction-sections ^
    -I ".." -I %SDL2_INC% -I %SDL2_NET_INC% ^
    ^
    -D ARCH=%ARCH% ^
    "%GAME%.c"
IF ERRORLEVEL 1 (
    echo [FAILED] %GAME%.c
    exit /b 1
)

REM ============================================================
REM  Link
REM ============================================================
gcc -Wl,--gc-sections -Og -ggdb  ^
    exotique.o miniaudio.o "%GAME%.o"^
     -Wl,-Map=game.map ^
    -L "%SDL2_LIB%" -L "%SDL2_NET_LIB%" ^
    -static -lSDL2 -lSDL2_net -lwsock32 ^
    -lmingw32 -lSDL2main -lSDL2 -lm -ldinput8 -ldxguid -ldxerr8 -luser32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 -lshell32 -lversion -luuid -static-libgcc -lsetupapi  -liphlpapi ^
    ^
    -o "%GAME%.exe"
IF ERRORLEVEL 1 (
    echo [FAILED] link step
    exit /b 1
)

echo.
echo Build OK: %GAME%.exe