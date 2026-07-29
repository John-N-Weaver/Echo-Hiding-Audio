@echo off
setlocal EnableExtensions DisableDelayedExpansion
REM ============================================================================
REM run_pg14_analysis.bat
REM
REM Course:      CS 4463 / CS 5173 - Team 21
REM Project:     Echo Hiding Audio
REM Authors:     John N. Weaver and Alex W. Bryant
REM
REM Purpose:
REM   Launch pg14_analysis.py with the user's arguments. Prefer the Windows
REM   Python launcher when available, then fall back to python.exe.
REM ============================================================================

set "SCRIPT=%~dp0pg14_analysis.py"

where py >nul 2>&1
if not errorlevel 1 (
    py -3 "%SCRIPT%" %*
    exit /b %errorlevel%
)

where python >nul 2>&1
if not errorlevel 1 (
    python "%SCRIPT%" %*
    exit /b %errorlevel%
)

echo Error: Python 3 was not found. Install Python 3 or add it to PATH. 1>&2
exit /b 9009
