@echo off
setlocal

if /I "%1"=="clean" (
    del /Q test.exe *.o 2>nul
    echo Cleaned.
    exit /b 0
)

echo Compiling...
gcc -Wall -Wextra -O2 -o test.exe test.c coro.c coro_switch.S
if %ERRORLEVEL% neq 0 (
    echo Build failed.
    exit /b %ERRORLEVEL%
)

echo Running...
echo.
test.exe
set EXIT=%ERRORLEVEL%
echo.
if %EXIT% equ 0 (
    echo All tests passed.
) else (
    echo Tests failed (exit code %EXIT%).
)
exit /b %EXIT%
