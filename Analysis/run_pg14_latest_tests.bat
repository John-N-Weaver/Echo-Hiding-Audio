@echo off
setlocal EnableExtensions DisableDelayedExpansion
REM ============================================================================
REM run_pg14_latest_tests.bat
REM
REM Course:      CS 4463 / CS 5173 - Team 21
REM Project:     Echo Hiding Audio
REM Authors:     John N. Weaver and Alex W. Bryant
REM
REM Purpose:
REM   Analyze the actual T1-T10 files recorded in Tests\Latest Test Run.log.
REM ============================================================================

set "SCRIPT=%~dp0pg14_analyze_latest_tests.py"

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
