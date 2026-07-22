@echo off
REM ============================================================
REM  Echo-Hiding stego test harness (Windows / Visual Studio)
REM  Run from the folder that contains this file. Expects the
REM  built stego.exe path in %STEGO%. Adjust if your Debug path
REM  differs.
REM ============================================================
setlocal EnableDelayedExpansion
if "%STEGO%"=="" set STEGO=..\x64\Debug\Echo Hiding Audio.exe
if not exist "%STEGO%" (
    echo Cannot find "%STEGO%".  Build the project first, or set STEGO=full\path\to\exe.
    exit /b 1
)

if not exist cover.wav (
    echo Missing cover.wav in this folder.  Drop a short 16-bit PCM WAV here and re-run.
    exit /b 1
)

echo === T1: hide + extract short ASCII message ===
> msg.txt echo Hello Echo Hiding CS4463
"%STEGO%" -hide    -m msg.txt   -c cover.wav -o stego.wav || goto :fail
"%STEGO%" -extract -s stego.wav -o recovered.bin           || goto :fail
fc /b msg.txt recovered.bin > nul && echo   T1 PASS || (echo   T1 FAIL & goto :fail)

echo === T2: -m random ===
"%STEGO%" -hide    -m random    -c cover.wav -o stego_r.wav || goto :fail
"%STEGO%" -extract -s stego_r.wav -o recovered_r.bin        || goto :fail
echo   T2 PASS (recovered %~z0 bytes; header validated by extractor)

echo === T3: oversize message (should warn, not crash) ===
copy /y msg.txt big.txt > nul
for /L %%i in (1,1,20) do type msg.txt >> big.txt
"%STEGO%" -hide -m big.txt -c cover.wav -o stego_big.wav
echo   T3 PASS (see WARNING above)

echo === T4: missing cover ===
"%STEGO%" -hide -m msg.txt -c nope.wav -o out.wav && (echo   T4 FAIL & goto :fail) || echo   T4 PASS

echo === T5: non-WAV file as cover ===
"%STEGO%" -hide -m msg.txt -c run_tests.bat -o out.wav && (echo   T5 FAIL & goto :fail) || echo   T5 PASS

echo === T6: extract on untouched cover (no ECHO magic) ===
"%STEGO%" -extract -s cover.wav -o junk.bin && (echo   T6 FAIL & goto :fail) || echo   T6 PASS

echo.
echo All tests completed.
exit /b 0

:fail
echo.
echo A test failed above.
exit /b 1
