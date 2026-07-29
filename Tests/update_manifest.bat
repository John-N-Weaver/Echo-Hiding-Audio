@echo off
REM ============================================================================
REM   update_manifest.bat
REM  Project: CS 4463 Team 21   Authors: John N. Weaver, Alex W. Bryant
REM
REM   Course:   CS 4463 / CS 5173 - Team 21
REM   Project      Echo Hiding Audio
REM   Authors   John N. Weaver and Alex W. Bryant
REM   GitHub:       https://github.com/John-N-Weaver/Echo-Hiding-Audio
REM   Created:      July 21, 2026
REM   Last updated: July 28, 2026
REM ============================================================================

setlocal EnableExtensions

REM This file must remain in the Tests folder beside update_manifest.ps1.
set "TESTS_DIR=%~dp0"
for %%I in ("%TESTS_DIR%..") do set "PROJECT_ROOT=%%~fI"

set "SCRIPT=%TESTS_DIR%update_manifest.ps1"
set "MANIFEST=%PROJECT_ROOT%\TestData\manifest.csv"

if not exist "%SCRIPT%" (
    echo ERROR: Could not find:
    echo   "%SCRIPT%"
    echo.
    pause
    exit /b 1
)

if not exist "%PROJECT_ROOT%\TestData" (
    echo ERROR: Could not find TestData folder:
    echo   "%PROJECT_ROOT%\TestData"
    echo.
    pause
    exit /b 1
)

echo Updating WAV manifest...
echo Project root:
echo   "%PROJECT_ROOT%"
echo.

powershell.exe -NoProfile -ExecutionPolicy Bypass ^
  -File "%SCRIPT%" ^
  -ProjectRoot "%PROJECT_ROOT%" ^
  -OutputPath "%MANIFEST%"

set "RC=%ERRORLEVEL%"

echo.
if not "%RC%"=="0" (
    echo Manifest update FAILED with exit code %RC%.
    pause
    exit /b %RC%
)

echo Manifest update completed successfully.
echo Output:
echo   "%MANIFEST%"
echo.
pause
exit /b 0
