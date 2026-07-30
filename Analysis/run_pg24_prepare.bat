@echo off
setlocal EnableExtensions DisableDelayedExpansion
REM Course:      CS 4463 / CS 5173 - Team 21
REM Project:     Echo Hiding Audio
REM Authors:     John N. Weaver and Alex W. Bryant

set "SCRIPT=%~dp0pg24_listening_study.py"

where py >nul 2>&1
if not errorlevel 1 (
    py -3 "%SCRIPT%" prepare %*
    exit /b %errorlevel%
)

where python >nul 2>&1
if not errorlevel 1 (
    python "%SCRIPT%" prepare %*
    exit /b %errorlevel%
)

echo Error: Python 3 was not found. 1>&2
exit /b 9009
