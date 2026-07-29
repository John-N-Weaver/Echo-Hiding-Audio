@echo off
setlocal EnableExtensions DisableDelayedExpansion
REM ============================================================================
REM run_tests_then_pg14.bat
REM
REM Course:      CS 4463 / CS 5173 - Team 21
REM Project:     Echo Hiding Audio
REM Authors:     John N. Weaver and Alex W. Bryant
REM
REM Purpose:
REM   Run the complete regression harness and, only after it passes, analyze
REM   the actual T1-T10 cover/stego/payload/extracted pairs with PG-14.
REM ============================================================================

echo ============================================================
echo Step 1 of 2: Running Echo Hiding Audio regression tests
echo ============================================================
call "%~dp0..\Tests\run_tests.bat"
set "TEST_EXIT=%ERRORLEVEL%"

if not "%TEST_EXIT%"=="0" (
    echo.
    echo Error: regression tests failed with exit code %TEST_EXIT%.
    echo PG-14 analysis was not run because the generated pairs may be invalid.
    exit /b %TEST_EXIT%
)

echo.
echo ============================================================
echo Step 2 of 2: Running PG-14 analysis on actual T1-T10 pairs
echo ============================================================
call "%~dp0run_pg14_latest_tests.bat"
exit /b %ERRORLEVEL%
