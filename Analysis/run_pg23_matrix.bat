@echo off
setlocal EnableExtensions DisableDelayedExpansion
REM ============================================================================
REM run_pg23_matrix.bat
REM
REM Course:      CS 4463 / CS 5173 - Team 21
REM Project:     Echo Hiding Audio
REM Authors:     John N. Weaver and Alex W. Bryant
REM
REM Purpose:
REM   Run the controlled representative PG-23 payload-fraction and lower-limit
REM   matrix. Additional pg23_run_matrix.py arguments may be supplied.
REM ============================================================================

set "SCRIPT=%~dp0pg23_run_matrix.py"

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
