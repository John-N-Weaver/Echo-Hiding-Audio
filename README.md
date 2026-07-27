# Echo Hiding Audio Steganography

A Windows command-line tool that hides and extracts arbitrary data inside 8-bit or 16-bit PCM WAV files using echo-hiding steganography.

## Project Files

Place all files in the same folder as your Visual Studio project files (`.sln`, `.vcxproj`, `main.cpp`):

| File | Type | Purpose |
|------|------|---------|
| `main.cpp` | Source | Program entry point and CLI argument parser |
| `stego.h` | Header | Public interface for embed/extract functions and constants |
| `wave.h` | Header | WAV file format structures (from the original WaveReader) |
| `stego.cpp` | Source | High-level hide/extract orchestration, parameter block, length header, capacity math |
| `stego_echo.cpp` | Source | Echo mixer (embed) and cepstrum-based detector (extract) |
| `wave_io.cpp` | Source | Robust WAV reader/writer that round-trips all RIFF chunks |
| `WaveReader.cpp` | Source | Original reader (must be **Excluded From Build**) |
| `TESTING_REPORT.md` | Document | Functional test matrix, capacity table, BER/SNR results, known limitations |
| `stego_echo_autocorrelation_attempt.md` | Document | Why extraction compares the cepstrum directly instead of autocorrelating it |
| `TestData/Corpus/` | Test data | 8 WAV files (dense music, speech, sparse/quiet, near-silence) confirmed to round-trip reliably -- see `SOURCES.md` there for provenance and for what was trimmed out and why |
| `TestData/*.wav`, `TestData/*.mp3` | Test data | The original 5-second WAV test pack and source MP3s, kept for reference/format-variety -- **none work as a cover**, see "Trying it against the included test corpus" below |
| `Tests/run_tests.bat` | Script | Windows test harness (needs a `cover.wav` dropped into `Tests/`; copy one from `TestData/Corpus/`) |

## Visual Studio Build Instructions

1. Open the solution in **Visual Studio 2022** (Community or Build Tools).
2. Ensure all source and header files above are in the same folder as `main.cpp`.
3. In **Solution Explorer**, right-click the project and choose **Add > Existing Item...** to add any missing files.
4. Right-click `WaveReader.cpp` and select **Properties**.
5. Set **Configuration Properties > General > Excluded From Build** to **Yes**.
   - This keeps the original file in the project but prevents a duplicate `main()` link error.
6. Select **Build > Build Solution** (or press **Ctrl+Shift+B**). Both `x64` and `Win32` platforms, Debug and Release, all build cleanly.
7. The executable is produced at:
   ```
   x64\Release\Echo Hiding Audio.exe
   ```
   (or `x64\Debug\...` for a Debug build).

Runtime Library is set to a non-DLL option (`/MT` Release, `/MTd` Debug) per
the course's Visual Studio setup slides, so the built `.exe` has no
dependency on the VC++ Redistributable -- verified with `dumpbin
/dependents`, which shows only `KERNEL32.dll`.

## Running Outside Visual Studio

```cmd
cd "path\to\Echo-Hiding-Audio-master\x64\Release"
"Echo Hiding Audio.exe" -hide -m message.txt -c cover.wav
```

Because the file name contains spaces, quote it (or the full path) when
running from `cmd`/PowerShell.

## Command-Line Usage

```
Echo Hiding Audio.exe -hide -m <message file|random> -c <cover.wav>
         [-seg <samples>] [-d0 <ms>] [-d1 <ms>] [-a <amplitude>]
         [-maxbits <bits>] [-o <stego.wav>]

Echo Hiding Audio.exe -extract -s <stego.wav> [-o <message file>]
```

| Flag | Meaning | Default |
|------|---------|---------|
| `-hide` / `-extract` | Mode select (exactly one required) | -- |
| `-m <path\|random>` | Message file to hide, or the literal word `random` to fill the cover's reported capacity with random bits | required for `-hide` |
| `-c <path>` | Cover WAV (8-bit unsigned or 16-bit signed PCM, mono/stereo) | required for `-hide` |
| `-s <path>` | Stego WAV to extract from | required for `-extract` |
| `-seg <samples>` | Segment length -- samples per embedded bit | 4096 |
| `-d0 <ms>` | Echo delay encoding bit 0 | 1.0 |
| `-d1 <ms>` | Echo delay encoding bit 1 (must be > `-d0`, and `-seg` must be > `-d1`) | 1.3 |
| `-a <0..1>` | Echo amplitude, as a fraction of the original sample | 0.4 |
| `-maxbits <n>` | Cap the number of message bits embedded, regardless of capacity | no cap |
| `-o <path>` | Output file | `<cover>_stego.wav` (hide) / `extracted_message.bin` (extract) |

`-seg`/`-d0`/`-d1` are **not needed at extraction** -- they're written into a
56-bit parameter block ahead of the message body at hide time and read back
automatically. `-a`/`-maxbits` affect embedding only and are not recorded.

### Examples

```cmd
"Echo Hiding Audio.exe" -hide -m secret.txt -c song.wav -o hidden.wav
"Echo Hiding Audio.exe" -hide -m random -c song.wav
"Echo Hiding Audio.exe" -hide -m secret.txt -c song.wav -seg 2048 -d0 0.7 -d1 1.1 -a 0.3
"Echo Hiding Audio.exe" -extract -s hidden.wav -o recovered.txt
"Echo Hiding Audio.exe"                      REM prints usage
"Echo Hiding Audio.exe" --help               REM same, exits 0
```

### Trying it against the included test corpus

`TestData/` also has the original 5-second WAV test pack (`01_...` through
`07_...`) and the two source MP3s, kept for reference/format-variety, but
**none of them work as a cover** -- the 5-second WAVs are too short to hold
even the 56-bit parameter block at any segment length (confirmed: at most
53 of 56 bits fit before the file runs out), the 24-bit WAV is rejected
outright as an unsupported bit depth, and MP3 can never work since the tool
only accepts uncompressed PCM WAV. Use `TestData/60s_test_cover.wav` or
anything in `TestData/Corpus/` instead -- those are long enough to actually
hold a message. A reliable first try:

```cmd
"Echo Hiding Audio.exe" -hide -m TestData\sample_message.txt -c TestData\Corpus\near_silence_16bit_mono.wav -o stego.wav
"Echo Hiding Audio.exe" -extract -s stego.wav -o recovered.bin
```

**Note on reliability**: the parameter block currently has no error
correction, so extraction can fail cleanly (a "No hidden data found or file
corrupted" error, not a crash) on some content/bit-depth combinations --
see `TESTING_REPORT.md` Section 4-5 for the full data, including the 11
files (mostly 8-bit, plus all synthetic-tone variants) that were pulled
from `TestData/Corpus/` for exactly this reason. Every file still in the
repo -- `music_blues_16bit_mono/stereo`, `music_rock_16bit_stereo`,
`near_silence_16bit_mono/stereo`, `sparse_quiet_16bit_mono/stereo`,
`speech_8bit_mono`, and `60s_test_cover.wav` -- round-trips reliably.

## How to Run the Test Harness

```cmd
copy TestData\Corpus\near_silence_16bit_mono.wav Tests\cover.wav
cd Tests
run_tests.bat
```

Drives the compiled `Echo Hiding Audio.exe` through a short-message
round-trip, `-m random`, an oversize-message truncation check, and three
error-handling paths (missing cover, non-WAV cover, extract on an untouched
cover), diffing recovered output against the source where applicable.

For the fuller measured results (capacity table, BER, SNR, parameter-block
flexibility, known limitations), see `TESTING_REPORT.md` -- its
"Reproducing these numbers" section has the exact commands.

## Notes

- The program accepts `-hide` or `-extract` mode.
- `-m` accepts either a file path or the literal `random`.
- `-o` is optional for both hide and extract; see the defaults table above.
- `--help` (or `-h`) prints the full usage and exits successfully.
- Output files are overwritten with a warning, not silently.
- Capacity is reported but never enforced -- an oversized message triggers a
  warning and embeds as much as fits, per the assignment spec ("do not do
  capacity checks to determine if a message will fit").
- Do not remove `WaveReader.cpp` from the project; exclude it from the
  build instead.
