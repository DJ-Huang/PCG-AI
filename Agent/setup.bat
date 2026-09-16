@echo off
setlocal EnableDelayedExpansion

rem PCG-AI Agent skills setup — junction picg-extension/skills to user profile
rem   setup.bat
rem   setup.bat unlink

set "AGENT_DIR=%~dp0"
if "%AGENT_DIR:~-1%"=="\" set "AGENT_DIR=%AGENT_DIR:~0,-1%"
for %%I in ("%AGENT_DIR%\..") do set "REPO_ROOT=%%~fI"
set "MODE=sync"
set "EXIT_CODE=0"

if /i "%~1"=="unlink" (
    set "MODE=unlink"
    goto :args_done
)

:args_done
cd /d "%REPO_ROOT%" || (
    echo [ERROR] Cannot cd to: %REPO_ROOT%
    pause
    exit /b 1
)

echo.
echo ============================================================
echo   PCG-AI Agent Skills Setup  [!MODE!]
echo   Agent: %AGENT_DIR%
echo   Repo:  %REPO_ROOT%
echo ============================================================
echo.

where node >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Node.js required: https://nodejs.org/
    pause
    exit /b 1
)

if /i "!MODE!"=="unlink" (
    node "%REPO_ROOT%\scripts\setup-agent-skills.mjs" unlink --once
) else (
    node "%REPO_ROOT%\scripts\setup-agent-skills.mjs" --once
)
set "EXIT_CODE=!errorlevel!"

echo.
pause
exit /b %EXIT_CODE%
