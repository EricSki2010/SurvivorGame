@echo off
cd /d "%~dp0"

:: Verify we're in the right repo
for /f "delims=" %%i in ('git remote get-url origin 2^>nul') do set "REMOTE=%%i"
if "%REMOTE%" neq "https://github.com/EricSki2010/SurvivorGame.git" (
    echo ERROR: Not in the SurvivorGame repo.
    echo Expected: https://github.com/EricSki2010/SurvivorGame.git
    echo Got:      %REMOTE%
    pause
    exit /b 1
)

git add -A
set /p MSG="Commit message: "
if "%MSG%"=="" (
    echo No message entered, aborting.
    pause
    exit /b 1
)
git commit -m "%MSG%"
git push origin main
echo.
echo Done.
pause
