@echo off
REM ============================================================================
REM  run_tests.bat - Echo Hiding Audio automated Windows test harness
REM
REM   Course:   CS 4463 / CS 5173 - Team 21
REM   Project      Echo Hiding Audio
REM   Authors   John N. Weaver and Alex W. Bryant
REM   GitHub:       https://github.com/John-N-Weaver/Echo-Hiding-Audio
REM   Created:      July 21, 2026
REM   Last updated: July 28, 2026
REM  The script may be launched from any working directory. It resolves paths
REM  relative to the folder containing this file.
REM
REM  EXPECTED LAYOUT
REM     ..\TestData\messages\**\*               payload fixtures of any type
REM     ..\TestData\**\*.wav                    cover WAV files
REM     out\                                     generated automatically
REM
REM  REQUIREMENTS
REM     Windows cmd.exe, Windows PowerShell, and a compiled executable.
REM
REM  OUTPUTS
REM     Test Run Summary.txt           concise UTF-8 summary of the latest run
REM     Latest Test Run.log            complete transcript of the latest run
REM     out\logs\run_<timestamp>.log   archived timestamped transcript
REM     out\results.csv                append-only machine-readable results
REM     out\t*_hide.log                individual hide logs
REM     out\t*_extract.log             individual extract logs
REM
REM  The edge suite also validates strict command-line parsing and both
REM  default output-name behaviors implemented by main.cpp.
REM
REM  RESULT CATEGORIES
REM     PASS        complete payload is byte-identical
REM     PASS-PARTIAL  recovered capacity-limited prefix is byte-identical
REM     FAIL        unexpected command result or recovered-byte mismatch
REM     SKIP        documented unsupported format or insufficient cover length
REM
REM  CONFIGURATION
REM     set STEGO=C:\full\path\Echo Hiding Audio.exe
REM     set MAX_TESTS=50
REM     set MIN_WAV_BYTES=500000
REM     set NOLOG=1
REM
REM  MIN_WAV_BYTES is an approximate prefilter. Exact capacity depends on WAV
REM  frame count. Current cost: 2048 frames x 7 copies = 14336 frames per
REM  logical bit; the fixed header consumes 64 logical bits.
REM ============================================================================

setlocal EnableExtensions DisableDelayedExpansion
REM The harness already captures every child process. Prevent the executable
REM from creating a separate CommandLogs transcript for each automated call.
set "STEGO_DISABLE_COMMAND_LOG=1"
if defined STEGO_TEE goto :RUN_TESTS
if "%NOLOG%"=="1" goto :RUN_TESTS
call :TEE_WRAPPER
exit /b %errorlevel%

:TEE_WRAPPER
setlocal EnableExtensions DisableDelayedExpansion
where powershell.exe >nul 2>&1
if errorlevel 1 (
    echo PowerShell is required but was not found.
    endlocal & exit /b 1
)
set "TESTS_DIR=%~dp0"
set "OUTDIR=%TESTS_DIR%out"
set "LOGROOT=%OUTDIR%\logs"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"
if not exist "%LOGROOT%" mkdir "%LOGROOT%"
for /f "delims=" %%T in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"') do set "STAMP=%%T"
set "RUNLOG=%LOGROOT%\run_%STAMP%.log"
set "STEGO_TEE=1"
set "STEGO_CSV=%OUTDIR%\results.csv"
set "STEGO_RESULTS_TXT=%TESTS_DIR%Test Run Summary.txt"
set "STEGO_LATEST_LOG=%TESTS_DIR%Latest Test Run.log"
set "STEGO_RUNLOG=%RUNLOG%"
set "STEGO_STAMP=%STAMP%"
if exist "%OUTDIR%\results.txt" del /q "%OUTDIR%\results.txt" >nul 2>&1
if exist "%STEGO_RESULTS_TXT%" del /q "%STEGO_RESULTS_TXT%" >nul 2>&1
echo Archived transcript: "%RUNLOG%"
REM Run this batch file again with STEGO_TEE set. Using CALL directly avoids
REM cmd.exe /S /C quoting failures when the project path contains spaces.
call "%~f0" >"%RUNLOG%" 2>&1
set "CHILD_RC=%errorlevel%"
copy /y "%RUNLOG%" "%STEGO_LATEST_LOG%" >nul
echo.
if exist "%STEGO_RESULTS_TXT%" (
    powershell.exe -NoProfile -Command "Get-Content -LiteralPath $env:STEGO_RESULTS_TXT"
) else (
    echo ERROR: The test harness did not produce Test Run Summary.txt.
    echo The complete transcript follows:
    echo.
    type "%RUNLOG%"
)
endlocal & exit /b %CHILD_RC%

:RUN_TESTS
setlocal EnableExtensions EnableDelayedExpansion
if "%STEGO%"=="" set "STEGO=%~dp0..\x64\Debug\Echo Hiding Audio.exe"
if "%MAX_TESTS%"=="" set "MAX_TESTS=10"
if "%MIN_WAV_BYTES%"=="" set "MIN_WAV_BYTES=2000000"
set "HERE=%~dp0"
set "DATADIR=%HERE%..\TestData"
set "MSGDIR=%DATADIR%\messages"
set "OUTDIR=%HERE%out"
if not defined STEGO_CSV set "STEGO_CSV=%OUTDIR%\results.csv"
if not defined STEGO_RESULTS_TXT set "STEGO_RESULTS_TXT=%HERE%Test Run Summary.txt"
if not defined STEGO_LATEST_LOG set "STEGO_LATEST_LOG=%HERE%Latest Test Run.log"
if not defined STEGO_RUNLOG set "STEGO_RUNLOG=not recorded"
if not defined STEGO_STAMP set "STEGO_STAMP=manual"
set "TEST_DATA_DIR=%DATADIR%"
set "MESSAGE_DIR=%MSGDIR%"

where powershell.exe >nul 2>&1
if errorlevel 1 (echo PowerShell is required but was not found. & exit /b 1)
echo.
echo Using EXE: "%STEGO%"
if not exist "%STEGO%" (echo Cannot find executable. Build first or set STEGO. & exit /b 1)
if not exist "%DATADIR%" (echo Missing test-data folder: "%DATADIR%" & exit /b 1)
if not exist "%MSGDIR%" (echo Missing payload folder: "%MSGDIR%" & exit /b 1)
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

set "CSV_HEADER=run_stamp,record_type,test,cover,message,message_bytes,embedded_bytes,result,return_codes,detail"
if exist "%STEGO_CSV%" (
    set "CURRENT_HEADER="
    set /p CURRENT_HEADER=<"%STEGO_CSV%"
    if /i not "!CURRENT_HEADER!"=="!CSV_HEADER!" (
        set "LEGACY_CSV=%OUTDIR%\results_legacy_!STEGO_STAMP!.csv"
        echo Existing results.csv uses an older schema; moving it to "!LEGACY_CSV!".
        move /y "%STEGO_CSV%" "!LEGACY_CSV!" >nul
    )
)
if not exist "%STEGO_CSV%" echo !CSV_HEADER!>"%STEGO_CSV%"

REM ---- discover payloads -----------------------------------------------------
REM Search only TestData\messages, but accept every regular file regardless of
REM extension. The stego engine treats payloads as opaque binary bytes, so text,
REM images, archives, documents, audio, and other file types are valid messages.
REM Any documentation placed inside TestData\messages will also be tested.
set "MSGCOUNT=0"
for /f "usebackq delims=" %%F in (`powershell -NoProfile -Command "Get-ChildItem -LiteralPath $env:MESSAGE_DIR -Recurse -File | Sort-Object FullName | ForEach-Object { $_.FullName }"`) do (
    set /a MSGCOUNT+=1
    set "MSG[!MSGCOUNT!]=%%~fF"
)
if !MSGCOUNT! EQU 0 (
    echo No payload files found under "%MSGDIR%".
    exit /b 1
)
echo Payload directory: "%MSGDIR%"
echo Found !MSGCOUNT! payload file(s) of any file type.

REM ---- discover covers, globally sorted by size -----------------------------
set "WAVCOUNT=0"
for /f "usebackq delims=" %%F in (`powershell -NoProfile -Command "Get-ChildItem -LiteralPath $env:TEST_DATA_DIR -Recurse -Filter *.wav -File | Where-Object { $_.Length -ge [int64]$env:MIN_WAV_BYTES } | Sort-Object Length -Descending | ForEach-Object { $_.FullName }"`) do (
    set /a WAVCOUNT+=1
    set "WAV[!WAVCOUNT!]=%%~fF"
)
if !WAVCOUNT! EQU 0 (
    echo No WAV files at or above !MIN_WAV_BYTES! bytes were found.
    echo Lower MIN_WAV_BYTES to inspect shorter covers.
    exit /b 1
)
echo Found !WAVCOUNT! eligible cover WAV(s). MAX_TESTS=!MAX_TESTS!.

REM ---- corpus round trips ----------------------------------------------------
set "RUN=0"
set "MI=0"
set "PASS=0"
set "TRUNC=0"
set "FAILED=0"
set "SKIPPED=0"
for /l %%i in (1,1,!WAVCOUNT!) do (
    if !RUN! LSS !MAX_TESTS! (
        set /a RUN+=1
        set /a MI+=1
        if !MI! GTR !MSGCOUNT! set "MI=1"
        set "COVER=!WAV[%%i]!"
        for %%m in (!MI!) do set "MSGF=!MSG[%%m]!"
        set "SW=%OUTDIR%\t!RUN!_stego.wav"
        set "EX=%OUTDIR%\t!RUN!_extracted.bin"
        set "HLOG=%OUTDIR%\t!RUN!_hide.log"
        set "XLOG=%OUTDIR%\t!RUN!_extract.log"
        call :DELETE "!SW!"
        call :DELETE "!EX!"
        call :DELETE "!HLOG!"
        call :DELETE "!XLOG!"
        set "MSZ=0"
        set "ESZ=0"
        for %%A in ("!MSGF!") do set "MSZ=%%~zA"
        echo.
        echo === T!RUN!: cover="!COVER!"
        echo        payload="!MSGF!"
        "%STEGO%" -hide -m "!MSGF!" -c "!COVER!" -o "!SW!" >"!HLOG!" 2>&1
        set "HR=!errorlevel!"
        type "!HLOG!"
        if not "!HR!"=="0" (
            call :CLASSIFY "!HLOG!"
            if "!KIND!"=="SKIP" (
                set /a SKIPPED+=1
                echo T!RUN! SKIP ^(!WHY!^)
                call :ROW TEST "T!RUN!" "!COVER!" "!MSGF!" !MSZ! 0 SKIP "hide=!HR! extract=-" "!WHY!"
            ) else (
                set /a FAILED+=1
                echo T!RUN! FAIL ^(hide returned !HR!^)
                call :ROW TEST "T!RUN!" "!COVER!" "!MSGF!" !MSZ! 0 FAIL "hide=!HR! extract=-" "hide error"
            )
        ) else if not exist "!SW!" (
            set /a FAILED+=1
            echo T!RUN! FAIL ^(hide returned success but produced no stego file^)
            call :ROW TEST "T!RUN!" "!COVER!" "!MSGF!" !MSZ! 0 FAIL "hide=!HR! extract=-" "missing stego output"
        ) else (
            "%STEGO%" -extract -s "!SW!" -o "!EX!" >"!XLOG!" 2>&1
            set "XR=!errorlevel!"
            type "!XLOG!"
            if not "!XR!"=="0" (
                set /a FAILED+=1
                echo T!RUN! FAIL ^(extract returned !XR!^)
                call :ROW TEST "T!RUN!" "!COVER!" "!MSGF!" !MSZ! 0 FAIL "hide=!HR! extract=!XR!" "extract error"
            ) else if not exist "!EX!" (
                set /a FAILED+=1
                echo T!RUN! FAIL ^(no extracted file produced^)
                call :ROW TEST "T!RUN!" "!COVER!" "!MSGF!" !MSZ! 0 FAIL "hide=!HR! extract=!XR!" "missing extracted output"
            ) else (
                for %%A in ("!EX!") do set "ESZ=%%~zA"
                if !MSZ! GTR 0 if !ESZ! EQU 0 (
                    set /a FAILED+=1
                    echo T!RUN! FAIL ^(zero bytes recovered from nonempty payload^)
                    call :ROW TEST "T!RUN!" "!COVER!" "!MSGF!" !MSZ! !ESZ! FAIL "hide=!HR! extract=!XR!" "empty extracted payload"
                ) else (
                    call :COMPARE_PREFIX "!MSGF!" "!EX!"
                    if "!CMPRES!"=="MATCH" (
                        if !ESZ! LSS !MSZ! (
                            set /a TRUNC+=1
                            echo T!RUN! PASS-PARTIAL ^(recovered prefix is exact^)
                            call :ROW TEST "T!RUN!" "!COVER!" "!MSGF!" !MSZ! !ESZ! PASS-PARTIAL "hide=!HR! extract=!XR!" "embedded prefix exact"
                        ) else if !ESZ! EQU !MSZ! (
                            set /a PASS+=1
                            echo T!RUN! PASS ^(byte-identical round trip^)
                            call :ROW TEST "T!RUN!" "!COVER!" "!MSGF!" !MSZ! !ESZ! PASS "hide=!HR! extract=!XR!" "byte-identical"
                        ) else (
                            set /a FAILED+=1
                            echo T!RUN! FAIL ^(extracted output longer than original^)
                            call :ROW TEST "T!RUN!" "!COVER!" "!MSGF!" !MSZ! !ESZ! FAIL "hide=!HR! extract=!XR!" "extracted output longer than original"
                        )
                    ) else (
                        set /a FAILED+=1
                        echo T!RUN! FAIL ^(!CMPRES!^)
                        call :ROW TEST "T!RUN!" "!COVER!" "!MSGF!" !MSZ! !ESZ! FAIL "hide=!HR! extract=!XR!" "!CMPRES!"
                    )
                )
            )
        )
    )
)
if !WAVCOUNT! GTR !MAX_TESTS! echo NOTE: stopped at MAX_TESTS=!MAX_TESTS!.

REM ---- deterministic edge tests ---------------------------------------------
set "EPASS=0"
set "EFAIL=0"
set "PRIMARY_COVER=!WAV[1]!"
set "PRIMARY_MSG=!MSG[1]!"

REM E1 random payload hide and extract
set "SW=%OUTDIR%\e1_random.wav"
set "EX=%OUTDIR%\e1_random.bin"
set "LOG=%OUTDIR%\e1.log"
call :DELETE "!SW!"
call :DELETE "!EX!"
echo.
echo === E1: random payload hide and extract ===
set "HR=-"
set "XR=-"
"%STEGO%" -hide -m random -c "!PRIMARY_COVER!" -o "!SW!" >"!LOG!" 2>&1
set "HR=!errorlevel!"
if "!HR!"=="0" (
    "%STEGO%" -extract -s "!SW!" -o "!EX!" >>"!LOG!" 2>&1
    set "XR=!errorlevel!"
)
type "!LOG!"
set "ESZ=0"
if exist "!EX!" for %%A in ("!EX!") do set "ESZ=%%~zA"
set "EDGE_OK=0"
if "!HR!"=="0" if "!XR!"=="0" if !ESZ! GTR 0 set "EDGE_OK=1"
if "!EDGE_OK!"=="1" (
    set /a EPASS+=1
    echo E1 PASS ^(!ESZ! bytes recovered^)
    call :ROW EDGE E1 "!PRIMARY_COVER!" random 0 !ESZ! PASS "hide=!HR! extract=!XR!" "random payload extracted"
) else (
    set /a EFAIL+=1
    echo E1 FAIL
    call :ROW EDGE E1 "!PRIMARY_COVER!" random 0 !ESZ! FAIL "hide=!HR! extract=!XR!" "random hide or extract failed"
)

REM E2 missing cover
call :DELETE "%OUTDIR%\missing_cover.wav"
echo.
echo === E2: missing cover ===
"%STEGO%" -hide -m "!PRIMARY_MSG!" -c "%OUTDIR%\missing_cover.wav" -o "%OUTDIR%\e2.wav" >"%OUTDIR%\e2.log" 2>&1
set "ERC=!errorlevel!"
type "%OUTDIR%\e2.log"
if not "!ERC!"=="0" (
    set /a EPASS+=1
    echo E2 PASS
    call :ROW EDGE E2 "-" "!PRIMARY_MSG!" 0 0 PASS "hide=!ERC! extract=-" "clean error on missing cover"
) else (
    set /a EFAIL+=1
    echo E2 FAIL
    call :ROW EDGE E2 "-" "!PRIMARY_MSG!" 0 0 FAIL "hide=!ERC! extract=-" "missing cover accepted"
)

REM E3 non-WAV cover
echo.
echo === E3: non-WAV cover ===
"%STEGO%" -hide -m "!PRIMARY_MSG!" -c "%~f0" -o "%OUTDIR%\e3.wav" >"%OUTDIR%\e3.log" 2>&1
set "ERC=!errorlevel!"
type "%OUTDIR%\e3.log"
if not "!ERC!"=="0" (
    set /a EPASS+=1
    echo E3 PASS
    call :ROW EDGE E3 "%~f0" "!PRIMARY_MSG!" 0 0 PASS "hide=!ERC! extract=-" "clean error on non-WAV input"
) else (
    set /a EFAIL+=1
    echo E3 FAIL
    call :ROW EDGE E3 "%~f0" "!PRIMARY_MSG!" 0 0 FAIL "hide=!ERC! extract=-" "non-WAV input accepted"
)

REM E4 untouched cover extraction
call :DELETE "%OUTDIR%\e4.bin"
echo.
echo === E4: extract from untouched cover ===
"%STEGO%" -extract -s "!PRIMARY_COVER!" -o "%OUTDIR%\e4.bin" >"%OUTDIR%\e4.log" 2>&1
set "ERC=!errorlevel!"
type "%OUTDIR%\e4.log"
if not "!ERC!"=="0" (
    set /a EPASS+=1
    echo E4 PASS
    call :ROW EDGE E4 "!PRIMARY_COVER!" "-" 0 0 PASS "hide=- extract=!ERC!" "magic mismatch reported"
) else (
    set /a EFAIL+=1
    echo E4 FAIL
    call :ROW EDGE E4 "!PRIMARY_COVER!" "-" 0 0 FAIL "hide=- extract=!ERC!" "plain cover accepted as stego"
)

REM E5 empty payload
set "MSG=%OUTDIR%\e5_empty.bin"
set "SW=%OUTDIR%\e5_empty.wav"
set "EX=%OUTDIR%\e5_empty_out.bin"
type nul >"!MSG!"
call :DELETE "!SW!"
call :DELETE "!EX!"
echo.
echo === E5: empty payload ===
set "HR=-"
set "XR=-"
"%STEGO%" -hide -m "!MSG!" -c "!PRIMARY_COVER!" -o "!SW!" >"%OUTDIR%\e5.log" 2>&1
set "HR=!errorlevel!"
if "!HR!"=="0" (
    "%STEGO%" -extract -s "!SW!" -o "!EX!" >>"%OUTDIR%\e5.log" 2>&1
    set "XR=!errorlevel!"
)
type "%OUTDIR%\e5.log"
set "ESZ=-1"
if exist "!EX!" for %%A in ("!EX!") do set "ESZ=%%~zA"
set "EDGE_OK=0"
if "!HR!"=="0" if "!XR!"=="0" if "!ESZ!"=="0" set "EDGE_OK=1"
if "!EDGE_OK!"=="1" (
    set /a EPASS+=1
    echo E5 PASS
    call :ROW EDGE E5 "!PRIMARY_COVER!" "!MSG!" 0 0 PASS "hide=!HR! extract=!XR!" "empty payload round trip"
) else (
    set /a EFAIL+=1
    echo E5 FAIL
    call :ROW EDGE E5 "!PRIMARY_COVER!" "!MSG!" 0 0 FAIL "hide=!HR! extract=!XR!" "empty payload failed"
)

REM E6 one-byte binary payload
set "MSG=%OUTDIR%\e6_one_byte.bin"
set "SW=%OUTDIR%\e6_one_byte.wav"
set "EX=%OUTDIR%\e6_one_byte_out.bin"
powershell -NoProfile -Command "[IO.File]::WriteAllBytes('%OUTDIR%\e6_one_byte.bin',[byte[]](0xA5))"
call :DELETE "!SW!"
call :DELETE "!EX!"
echo.
echo === E6: one-byte binary payload ===
set "HR=-"
set "XR=-"
"%STEGO%" -hide -m "!MSG!" -c "!PRIMARY_COVER!" -o "!SW!" >"%OUTDIR%\e6.log" 2>&1
set "HR=!errorlevel!"
if "!HR!"=="0" (
    "%STEGO%" -extract -s "!SW!" -o "!EX!" >>"%OUTDIR%\e6.log" 2>&1
    set "XR=!errorlevel!"
)
type "%OUTDIR%\e6.log"
set "ESZ=0"
if exist "!EX!" for %%A in ("!EX!") do set "ESZ=%%~zA"
set "CMPRES=NOT_RUN"
if exist "!EX!" call :COMPARE_PREFIX "!MSG!" "!EX!"
set "EDGE_OK=0"
if "!HR!"=="0" if "!XR!"=="0" if "!CMPRES!"=="MATCH" if "!ESZ!"=="1" set "EDGE_OK=1"
if "!EDGE_OK!"=="1" (
    set /a EPASS+=1
    echo E6 PASS
    call :ROW EDGE E6 "!PRIMARY_COVER!" "!MSG!" 1 1 PASS "hide=!HR! extract=!XR!" "one-byte payload exact"
) else (
    set /a EFAIL+=1
    echo E6 FAIL
    call :ROW EDGE E6 "!PRIMARY_COVER!" "!MSG!" 1 !ESZ! FAIL "hide=!HR! extract=!XR!" "one-byte payload failed: !CMPRES!"
)

REM E7 deterministic oversized payload
set "MSG=%OUTDIR%\e7_oversize.bin"
set "SW=%OUTDIR%\e7_oversize.wav"
set "EX=%OUTDIR%\e7_oversize_out.bin"
powershell -NoProfile -Command "$b=New-Object byte[] 4096; for($i=0;$i -lt $b.Length;$i++){$b[$i]=[byte]($i %% 251)}; [IO.File]::WriteAllBytes('%OUTDIR%\e7_oversize.bin',$b)"
call :DELETE "!SW!"
call :DELETE "!EX!"
echo.
echo === E7: oversized payload ===
set "HR=-"
set "XR=-"
"%STEGO%" -hide -m "!MSG!" -c "!PRIMARY_COVER!" -o "!SW!" >"%OUTDIR%\e7.log" 2>&1
set "HR=!errorlevel!"
if "!HR!"=="0" (
    "%STEGO%" -extract -s "!SW!" -o "!EX!" >>"%OUTDIR%\e7.log" 2>&1
    set "XR=!errorlevel!"
)
type "%OUTDIR%\e7.log"
set "ESZ=0"
if exist "!EX!" for %%A in ("!EX!") do set "ESZ=%%~zA"
set "CMPRES=NOT_RUN"
if exist "!EX!" call :COMPARE_PREFIX "!MSG!" "!EX!"
set "SAW_HIDE_EXHAUSTED=0"
set "SAW_EXTRACT_PARTIAL=0"
findstr /i /c:"cover exhausted before the complete message was hidden" "%OUTDIR%\e7.log" >nul && set "SAW_HIDE_EXHAUSTED=1"
findstr /i /c:"declared payload length is" "%OUTDIR%\e7.log" >nul && set "SAW_EXTRACT_PARTIAL=1"
set "EDGE_OK=0"
if "!HR!"=="0" if "!XR!"=="0" if "!CMPRES!"=="MATCH" if !ESZ! GTR 0 if !ESZ! LSS 4096 if "!SAW_HIDE_EXHAUSTED!"=="1" if "!SAW_EXTRACT_PARTIAL!"=="1" set "EDGE_OK=1"
if "!EDGE_OK!"=="1" (
    set /a EPASS+=1
    echo E7 PASS-PARTIAL ^(!ESZ! bytes recovered exactly; exhaustion warnings verified^)
    call :ROW EDGE E7 "!PRIMARY_COVER!" "!MSG!" 4096 !ESZ! PASS-PARTIAL "hide=!HR! extract=!XR!" "full stream attempted; cover exhausted; prefix exact"
) else (
    set /a EFAIL+=1
    echo E7 FAIL
    call :ROW EDGE E7 "!PRIMARY_COVER!" "!MSG!" 4096 !ESZ! FAIL "hide=!HR! extract=!XR!" "oversized/cover-exhaustion behavior incorrect: !CMPRES!"
)

REM E8 destructive path collision, performed only on a disposable copy
set "TMP_COVER=%OUTDIR%\e8_collision_cover.wav"
copy /y "!PRIMARY_COVER!" "!TMP_COVER!" >nul
echo.
echo === E8: same cover and stego-output path ===
"%STEGO%" -hide -m "!PRIMARY_MSG!" -c "!TMP_COVER!" -o "!TMP_COVER!" >"%OUTDIR%\e8.log" 2>&1
set "ERC=!errorlevel!"
type "%OUTDIR%\e8.log"
if not "!ERC!"=="0" (
    set /a EPASS+=1
    echo E8 PASS
    call :ROW EDGE E8 "!TMP_COVER!" "!PRIMARY_MSG!" 0 0 PASS "hide=!ERC! extract=-" "path collision rejected"
) else (
    set /a EFAIL+=1
    echo E8 FAIL
    call :ROW EDGE E8 "!TMP_COVER!" "!PRIMARY_MSG!" 0 0 FAIL "hide=!ERC! extract=-" "path collision allowed"
)

REM E9-E19 command-line parser validation

echo.
echo === E9: -h help option ===
"%STEGO%" -h >"%OUTDIR%\e9.log" 2>&1
set "ERC=!errorlevel!"
type "%OUTDIR%\e9.log"
if "!ERC!"=="0" (
    set /a EPASS+=1
    echo E9 PASS
    call :ROW EDGE E9 "-" "-" 0 0 PASS "cli=!ERC!" "-h help option"
) else (
    set /a EFAIL+=1
    echo E9 FAIL
    call :ROW EDGE E9 "-" "-" 0 0 FAIL "cli=!ERC!" "-h help option failed"
)

echo.
echo === E10: --help help option ===
"%STEGO%" --help >"%OUTDIR%\e10.log" 2>&1
set "ERC=!errorlevel!"
type "%OUTDIR%\e10.log"
if "!ERC!"=="0" (
    set /a EPASS+=1
    echo E10 PASS
    call :ROW EDGE E10 "-" "-" 0 0 PASS "cli=!ERC!" "--help option"
) else (
    set /a EFAIL+=1
    echo E10 FAIL
    call :ROW EDGE E10 "-" "-" 0 0 FAIL "cli=!ERC!" "--help option failed"
)

echo.
echo === E11: unknown option rejected ===
"%STEGO%" -unknown >"%OUTDIR%\e11.log" 2>&1
set "ERC=!errorlevel!"
type "%OUTDIR%\e11.log"
if not "!ERC!"=="0" (
    set /a EPASS+=1
    echo E11 PASS
    call :ROW EDGE E11 "-" "-" 0 0 PASS "cli=!ERC!" "unknown option rejected"
) else (
    set /a EFAIL+=1
    echo E11 FAIL
    call :ROW EDGE E11 "-" "-" 0 0 FAIL "cli=!ERC!" "unknown option accepted"
)

echo.
echo === E12: duplicate -m rejected ===
"%STEGO%" -hide -m "!PRIMARY_MSG!" -m "!PRIMARY_MSG!" -c "!PRIMARY_COVER!" >"%OUTDIR%\e12.log" 2>&1
set "ERC=!errorlevel!"
type "%OUTDIR%\e12.log"
if not "!ERC!"=="0" (
    set /a EPASS+=1
    echo E12 PASS
    call :ROW EDGE E12 "!PRIMARY_COVER!" "!PRIMARY_MSG!" 0 0 PASS "cli=!ERC!" "duplicate -m rejected"
) else (
    set /a EFAIL+=1
    echo E12 FAIL
    call :ROW EDGE E12 "!PRIMARY_COVER!" "!PRIMARY_MSG!" 0 0 FAIL "cli=!ERC!" "duplicate -m accepted"
)

echo.
echo === E13: trailing -o rejected ===
"%STEGO%" -hide -m "!PRIMARY_MSG!" -c "!PRIMARY_COVER!" -o >"%OUTDIR%\e13.log" 2>&1
set "ERC=!errorlevel!"
type "%OUTDIR%\e13.log"
if not "!ERC!"=="0" (
    set /a EPASS+=1
    echo E13 PASS
    call :ROW EDGE E13 "!PRIMARY_COVER!" "!PRIMARY_MSG!" 0 0 PASS "cli=!ERC!" "trailing -o rejected"
) else (
    set /a EFAIL+=1
    echo E13 FAIL
    call :ROW EDGE E13 "!PRIMARY_COVER!" "!PRIMARY_MSG!" 0 0 FAIL "cli=!ERC!" "trailing -o accepted"
)

echo.
echo === E14: option token cannot be consumed as -m value ===
"%STEGO%" -hide -m -c "!PRIMARY_COVER!" >"%OUTDIR%\e14.log" 2>&1
set "ERC=!errorlevel!"
type "%OUTDIR%\e14.log"
if not "!ERC!"=="0" (
    set /a EPASS+=1
    echo E14 PASS
    call :ROW EDGE E14 "!PRIMARY_COVER!" "-" 0 0 PASS "cli=!ERC!" "missing -m value rejected"
) else (
    set /a EFAIL+=1
    echo E14 FAIL
    call :ROW EDGE E14 "!PRIMARY_COVER!" "-" 0 0 FAIL "cli=!ERC!" "-c consumed as -m value"
)

echo.
echo === E15: both modes rejected ===
"%STEGO%" -hide -extract -m "!PRIMARY_MSG!" -c "!PRIMARY_COVER!" -s "!PRIMARY_COVER!" >"%OUTDIR%\e15.log" 2>&1
set "ERC=!errorlevel!"
type "%OUTDIR%\e15.log"
if not "!ERC!"=="0" (
    set /a EPASS+=1
    echo E15 PASS
    call :ROW EDGE E15 "!PRIMARY_COVER!" "!PRIMARY_MSG!" 0 0 PASS "cli=!ERC!" "both modes rejected"
) else (
    set /a EFAIL+=1
    echo E15 FAIL
    call :ROW EDGE E15 "!PRIMARY_COVER!" "!PRIMARY_MSG!" 0 0 FAIL "cli=!ERC!" "both modes accepted"
)

echo.
echo === E16: missing mode rejected ===
"%STEGO%" -m "!PRIMARY_MSG!" -c "!PRIMARY_COVER!" >"%OUTDIR%\e16.log" 2>&1
set "ERC=!errorlevel!"
type "%OUTDIR%\e16.log"
if not "!ERC!"=="0" (
    set /a EPASS+=1
    echo E16 PASS
    call :ROW EDGE E16 "!PRIMARY_COVER!" "!PRIMARY_MSG!" 0 0 PASS "cli=!ERC!" "missing mode rejected"
) else (
    set /a EFAIL+=1
    echo E16 FAIL
    call :ROW EDGE E16 "!PRIMARY_COVER!" "!PRIMARY_MSG!" 0 0 FAIL "cli=!ERC!" "missing mode accepted"
)

echo.
echo === E17: -s rejected in hide mode ===
"%STEGO%" -hide -m "!PRIMARY_MSG!" -c "!PRIMARY_COVER!" -s "!PRIMARY_COVER!" >"%OUTDIR%\e17.log" 2>&1
set "ERC=!errorlevel!"
type "%OUTDIR%\e17.log"
if not "!ERC!"=="0" (
    set /a EPASS+=1
    echo E17 PASS
    call :ROW EDGE E17 "!PRIMARY_COVER!" "!PRIMARY_MSG!" 0 0 PASS "cli=!ERC!" "-s rejected in hide mode"
) else (
    set /a EFAIL+=1
    echo E17 FAIL
    call :ROW EDGE E17 "!PRIMARY_COVER!" "!PRIMARY_MSG!" 0 0 FAIL "cli=!ERC!" "-s accepted in hide mode"
)

echo.
echo === E18: -m rejected in extract mode ===
"%STEGO%" -extract -s "!PRIMARY_COVER!" -m "!PRIMARY_MSG!" >"%OUTDIR%\e18.log" 2>&1
set "ERC=!errorlevel!"
type "%OUTDIR%\e18.log"
if not "!ERC!"=="0" (
    set /a EPASS+=1
    echo E18 PASS
    call :ROW EDGE E18 "!PRIMARY_COVER!" "!PRIMARY_MSG!" 0 0 PASS "cli=!ERC!" "-m rejected in extract mode"
) else (
    set /a EFAIL+=1
    echo E18 FAIL
    call :ROW EDGE E18 "!PRIMARY_COVER!" "!PRIMARY_MSG!" 0 0 FAIL "cli=!ERC!" "-m accepted in extract mode"
)

echo.
echo === E19: duplicate -o rejected ===
"%STEGO%" -hide -m "!PRIMARY_MSG!" -c "!PRIMARY_COVER!" -o "%OUTDIR%\e19a.wav" -o "%OUTDIR%\e19b.wav" >"%OUTDIR%\e19.log" 2>&1
set "ERC=!errorlevel!"
type "%OUTDIR%\e19.log"
if not "!ERC!"=="0" (
    set /a EPASS+=1
    echo E19 PASS
    call :ROW EDGE E19 "!PRIMARY_COVER!" "!PRIMARY_MSG!" 0 0 PASS "cli=!ERC!" "duplicate -o rejected"
) else (
    set /a EFAIL+=1
    echo E19 FAIL
    call :ROW EDGE E19 "!PRIMARY_COVER!" "!PRIMARY_MSG!" 0 0 FAIL "cli=!ERC!" "duplicate -o accepted"
)

REM E20 default hide output: <cover>_stego.wav
set "DEFAULT_COVER=%OUTDIR%\e20_default_cover.wav"
set "DEFAULT_SW=%OUTDIR%\e20_default_cover_stego.wav"
copy /y "!PRIMARY_COVER!" "!DEFAULT_COVER!" >nul
call :DELETE "!DEFAULT_SW!"
echo.
echo === E20: default hide output name ===
"%STEGO%" -hide -m "%OUTDIR%\e6_one_byte.bin" -c "!DEFAULT_COVER!" >"%OUTDIR%\e20.log" 2>&1
set "ERC=!errorlevel!"
type "%OUTDIR%\e20.log"
set "EDGE_OK=0"
if "!ERC!"=="0" if exist "!DEFAULT_SW!" set "EDGE_OK=1"
if "!EDGE_OK!"=="1" (
    set /a EPASS+=1
    echo E20 PASS
    call :ROW EDGE E20 "!DEFAULT_COVER!" "%OUTDIR%\e6_one_byte.bin" 1 1 PASS "hide=!ERC! extract=-" "default hide name created"
) else (
    set /a EFAIL+=1
    echo E20 FAIL
    call :ROW EDGE E20 "!DEFAULT_COVER!" "%OUTDIR%\e6_one_byte.bin" 1 0 FAIL "hide=!ERC! extract=-" "default hide output missing"
)

REM E21 default extraction output: extracted_message.bin in current directory
set "DEFAULT_EX=%OUTDIR%\extracted_message.bin"
call :DELETE "!DEFAULT_EX!"
echo.
echo === E21: default extract output name ===
pushd "%OUTDIR%" >nul
"%STEGO%" -extract -s "!DEFAULT_SW!" >"%OUTDIR%\e21.log" 2>&1
set "ERC=!errorlevel!"
popd >nul
type "%OUTDIR%\e21.log"
set "ESZ=0"
if exist "!DEFAULT_EX!" for %%A in ("!DEFAULT_EX!") do set "ESZ=%%~zA"
set "CMPRES=NOT_RUN"
if exist "!DEFAULT_EX!" call :COMPARE_PREFIX "%OUTDIR%\e6_one_byte.bin" "!DEFAULT_EX!"
set "EDGE_OK=0"
if "!ERC!"=="0" if "!ESZ!"=="1" if "!CMPRES!"=="MATCH" set "EDGE_OK=1"
if "!EDGE_OK!"=="1" (
    set /a EPASS+=1
    echo E21 PASS
    call :ROW EDGE E21 "!DEFAULT_SW!" "%OUTDIR%\e6_one_byte.bin" 1 1 PASS "hide=- extract=!ERC!" "default extract name created"
) else (
    set /a EFAIL+=1
    echo E21 FAIL
    call :ROW EDGE E21 "!DEFAULT_SW!" "%OUTDIR%\e6_one_byte.bin" 1 !ESZ! FAIL "hide=- extract=!ERC!" "default extract output incorrect: !CMPRES!"
)

echo.
echo ============================================================
echo Round-trip tests run : !RUN!
echo   Exact pass         : !PASS!
echo   Pass ^(partial^)  : !TRUNC!
echo   Failed             : !FAILED!
echo   Skipped            : !SKIPPED!
echo Edge-case tests      : !EPASS! passed, !EFAIL! failed
echo ============================================================
call :ROW SUMMARY SUMMARY "-" "-" 0 0 SUMMARY "hide=- extract=-" "pass=!PASS! trunc=!TRUNC! fail=!FAILED! skip=!SKIPPED! edge_pass=!EPASS! edge_fail=!EFAIL!"
set "FINAL_RC=0"
if !FAILED! GTR 0 set "FINAL_RC=1"
if !EFAIL! GTR 0 set "FINAL_RC=1"
set "OVERALL=PASS"
if not "!FINAL_RC!"=="0" set "OVERALL=FAIL"
call :WRITE_SUMMARY
echo Summary row appended to: "%STEGO_CSV%"
echo Test Run Summary written to: "%STEGO_RESULTS_TXT%"
set "RETURN_RC=!FINAL_RC!"
endlocal & exit /b %RETURN_RC%

REM ---- helpers ---------------------------------------------------------------
:WRITE_SUMMARY
REM Write UTF-8 with a byte-order marker so Windows editors detect the encoding.
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$lines=@('Echo Hiding Audio - Latest Test Summary','=======================================',('Run stamp            : '+$env:STEGO_STAMP),('Overall result       : '+$env:OVERALL),('Executable           : '+$env:STEGO),('Test data directory  : '+$env:DATADIR),('Latest full log      : '+$env:STEGO_LATEST_LOG),('Archived transcript  : '+$env:STEGO_RUNLOG),('Results history      : '+$env:STEGO_CSV),'','Round-trip tests',('  Tests run           : '+$env:RUN),('  Exact passes        : '+$env:PASS),('  Partial passes      : '+$env:TRUNC),('  Failures            : '+$env:FAILED),('  Skipped             : '+$env:SKIPPED),'','Edge-case tests',('  Passed              : '+$env:EPASS),('  Failed              : '+$env:EFAIL),'','PASS-PARTIAL means the complete message was attempted, the cover','was exhausted, and every recoverable prefix byte matched exactly.'); $lines | Set-Content -LiteralPath $env:STEGO_RESULTS_TXT -Encoding UTF8"
if errorlevel 1 (
    echo ERROR: Could not write Test Run Summary.txt.
    exit /b 1
)
exit /b 0

:DELETE
if exist "%~1" del /q "%~1" >nul 2>&1
exit /b 0

:CLASSIFY
set "KIND=FAIL"
set "WHY=hide error"
findstr /i /c:"unsupported" "%~1" >nul && (set "KIND=SKIP" & set "WHY=cover format not supported")
findstr /i /c:"too small to hold" "%~1" >nul && (set "KIND=SKIP" & set "WHY=cover too short for the 64-bit header")
exit /b 0

:COMPARE_PREFIX
setlocal EnableExtensions DisableDelayedExpansion
set "CMP_ORIGINAL=%~1"
set "CMP_EXTRACTED=%~2"
set "RESULT="
for /f "usebackq delims=" %%R in (`powershell -NoProfile -Command "$o=[IO.File]::ReadAllBytes($env:CMP_ORIGINAL); $e=[IO.File]::ReadAllBytes($env:CMP_EXTRACTED); if($e.Length -gt $o.Length){'EXTRACTED_LONGER'; exit}; for($i=0;$i -lt $e.Length;$i++){if($o[$i] -ne $e[$i]){'MISMATCH_AT_BYTE_'+$i; exit}}; 'MATCH'"`) do set "RESULT=%%R"
if not defined RESULT set "RESULT=COMPARATOR_ERROR"
endlocal & set "CMPRES=%RESULT%" & exit /b 0

:ROW
REM Args: 1=record_type 2=test 3=cover 4=message 5=message_bytes
REM       6=embedded_bytes 7=result 8=return_codes 9=detail
>>"%STEGO_CSV%" echo %STEGO_STAMP%,%~1,%~2,"%~3","%~4",%~5,%~6,%~7,"%~8","%~9"
exit /b 0
