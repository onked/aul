@echo off
echo Building Aul...
make

:: 'neq 0' means there was a syntax error in your C code
if %errorlevel% neq 0 (
    echo.
    echo [ERROR] Build failed. Check the GCC output above.
    pause
    exit /b %errorlevel%
)

echo.
echo Running Aul...
echo ---------------------------------------
aul.exe tests/hello.aul
echo ---------------------------------------

pause