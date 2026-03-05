@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=amd64 >nul 2>&1
cmake --build --preset default
if %ERRORLEVEL% neq 0 (
    echo Build failed.
    exit /b 1
)

if exist dist rmdir /s /q dist
mkdir dist
copy build_msvc\game.exe dist\
xcopy assets dist\assets\ /e /i /q

echo.
echo Done — dist\ is ready.
