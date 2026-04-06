@echo off
echo Building Aul...
make

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