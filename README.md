# Echo Hiding Audio Steganography

**Course:** CS 4463 / CS 5173<br>
**Team:** 21<br>
**Authors:** John N. Weaver and Alex W. Bryant<br>
**Repository:** <https://github.com/John-N-Weaver/Echo-Hiding-Audio>

## Quick Start: Run Without Visual Studio

A compiled Windows executable is included in the project ZIP. Visual Studio is not required to run the program.

1. Extract the complete ZIP file to a new folder. Do not run the executable from inside the ZIP.
2. Open the extracted project folder in File Explorer.
3. Right-click an empty area in the folder and select **Open in Terminal**.
4. At the PowerShell prompt, run:

   ```powershell
   & ".\x64\Release\Echo Hiding Audio.exe"
   ```

5. The program will display its command-line usage instructions.

The executable can also be started from its own folder:

```powershell
Set-Location ".\x64\Release"
& ".\Echo Hiding Audio.exe"
```

Because the executable name contains spaces, PowerShell requires the call operator `&` before the quoted path.

A Windows command-line application that hides and extracts arbitrary binary data in uncompressed PCM WAV audio using time-domain echo hiding. The current implementation uses fixed, empirically tested echo parameters, repetition coding, majority-vote extraction, bounded partial recovery, strict WAV validation, and automated statistical and limit testing.

## Current implementation status

The current implementation supports complete and capacity-limited hide/extract operations without crashing on tested faulty input.

Verified project results:

- Regression suite: **PASS**
  - 10/10 round-trip cases recovered an exact byte prefix
  - 21/21 command-line, file-format, capacity, and error-handling edge cases passed
- PG-14 statistical analysis: **PASS**
  - 10/10 regression cover/stego pairs passed analysis
  - Compared-prefix BER was zero for all analyzed pairs
- PG-23 controlled limit matrix: **PASS**
  - 28/28 controlled cases passed
  - 24 payload-fraction cases passed
  - Four lower-limit boundary cases passed
  - 27/27 generated stego files passed the attached PG-14 analysis; the below-header case intentionally creates no stego WAV
- PG-13 / PG-24 auditory perception: **GO**
  - Study ID: `20260729_142717`
  - Listener: Alex Bryant
  - 24/24 rating rows validated
  - 0 validation errors
  - 20 samples rated `undetectable_without_original`
  - Four samples rated `apparent_close_listening`
  - Zero samples rated `obvious`

The completed auditory study used one listener and 24 blinded cover/stego pairs. Its results document the tested perceptual behavior but do not establish a universal detectability threshold.

## Supported inputs

The WAV parser accepts:

- Classic `WAVE_FORMAT_PCM`
- `WAVE_FORMAT_EXTENSIBLE` when its subtype is PCM
- 8-bit unsigned PCM
- 16-bit signed PCM
- Mono
- Stereo

The program rejects compressed WAV audio, non-PCM extensible subtypes, unsupported packed valid-bit layouts, unsupported bit depths such as 24-bit PCM, unsupported channel counts, malformed RIFF/WAVE structures, truncated chunks, missing files, and non-WAV files.

The hidden message may be any binary file, including text, images, archives, encrypted files, or audio. The program does not interpret the message format.

## Current echo-hiding design

The implementation uses these fixed parameters from `stego.h`:

| Parameter | Current value | Purpose |
|---|---:|---|
| Segment length | 2,048 frames | Analysis and embedding window for one physical echo observation |
| Bit-0 delay | 150 samples | Echo delay representing logical 0 |
| Bit-1 delay | 200 samples | Echo delay representing logical 1 |
| Echo decay | 0.4 | Delayed-signal amplitude relative to the dry signal |
| Repetition count | 7 | Seven interleaved physical copies per logical bit |
| Majority vote | 4 of 7 | Recovers a logical bit despite as many as three disagreeing copies |
| Header size | 64 logical bits | Four-byte `ECHO` magic plus a four-byte requested payload length |
| Magic tolerance | 4 bit errors | Permits limited corruption in the 32-bit magic before rejecting the WAV |

The parameters are intentionally not exposed as command-line options. Embedding and extraction therefore cannot silently use different settings.

### Embedding

For each logical bit, the program writes seven interleaved physical copies. Each copy selects either the 150-sample or 200-sample echo path. Crossfading prevents clicks at bit transitions. A headroom factor of `1 / (1 + decay)` prevents clipping, and the same gain is applied after the payload region to prevent an audible level step.

### Extraction

The extractor computes a real cepstrum for each physical segment, compares local peak scores near the two candidate delays, and recovers the physical bit. It then majority-votes the seven interleaved copies to recover each logical bit.

The original cover WAV is not required for extraction.

## Payload format

The embedded logical stream is:

```text
Bytes 0-3: ASCII magic "ECHO"
Bytes 4-7: requested payload length, uint32 little-endian
Bytes 8+:  requested payload bytes
```

The header records the complete requested payload length, even when the cover becomes full. This allows extraction to distinguish complete recovery from a capacity-limited exact prefix.

## Capacity

One logical bit consumes:

```text
ECHO_SEGMENT_LEN * ECHO_REPEAT
= 2048 * 7
= 14,336 audio frames
```

Capacity is based on frame count, not file size:

```text
logical_bits = floor(frame_count / 14,336)
payload_bytes = floor(max(0, logical_bits - 64) / 8)
```

Important consequences:

- Mono and stereo files with the same frame count have the same payload capacity.
- The 64-bit header requires at least **917,504 frames**.
- At 44.1 kHz, a cover must be approximately **20.8 seconds** long to hold the header.
- One complete payload byte requires at least **1,032,192 frames**, approximately **23.4 seconds** at 44.1 kHz.
- The asymptotic raw rate at 44.1 kHz is approximately **3.08 logical bits/second**, or approximately **23 payload bytes/minute before subtracting the fixed header**.
- A 60-second, 44.1 kHz cover holds approximately **15 complete payload bytes after the header**.

The program prints the calculated capacity during hiding.

## Oversized messages and partial recovery

For an ordinary message file, the program does **not** reject or pre-truncate the message because it exceeds capacity.

Instead, it:

1. Reads the complete requested message.
2. Builds the complete header and payload bitstream.
3. Embeds until the cover is exhausted.
4. Saves the valid partial stego WAV when the complete header was stored.
5. Reports requested bits, embedded complete bytes, any partial trailing byte bits, and missing bits.

Extraction safely bounds the declared length against physical capacity and writes every complete recoverable prefix byte. The hide and extract summaries clearly identify complete versus partial recovery.

The special command `-m random` intentionally generates exactly enough complete random bytes to fill the cover's reported payload capacity.

## Project files

| Path | Purpose |
|---|---|
| `main.cpp` | CLI parsing, help text, default output naming, command dispatch, and direct-command logging |
| `command_log.h` | Public logging declarations and console-mirroring macros |
| `stego.h` | Fixed parameters, payload constants, and public hide/extract interfaces |
| `stego.cpp` | Message I/O, capacity calculation, header handling, partial embedding/recovery, and summaries |
| `stego_echo.cpp` | Echo mixing, headroom, crossfading, cepstral detection, interleaving, and majority voting |
| `wave.h` | Windows-compatible RIFF/WAVE structures retained from course-provided material |
| `wave_io.h` | Validated `WaveFile` interface |
| `wave_io.cpp` | RIFF/WAVE loading, validation, chunk preservation, memory management, and saving |
| `WaveReader.cpp` | Original course reader retained for reference; exclude it from the build if it contains another `main()` |
| `Tests/run_tests.bat` | Regression and edge-case harness |
| `Analysis/` | PG-14 statistical analysis, PG-23 controlled limit testing, and PG-13/PG-24 listening-study tools |
| `TestData/` | WAV covers, binary/text messages, manifests, and representative fixtures |

Generated folders such as `.vs`, `x64`, `CommandLogs`, `Tests/out`, `Analysis/pg23_results`, and `Analysis/pg24_results` are build or temporary analysis outputs rather than source files. Curated PG-24 evidence is committed under `Analysis/pg24_evidence`.

## Visual Studio build instructions

1. Open `Echo Hiding Audio.sln` in **Visual Studio 2022**.
2. Confirm that the `.cpp` and `.h` files listed above are included in the project.
3. If `WaveReader.cpp` is present and contains its own `main()`, right-click it in Solution Explorer, select **Properties**, and set **Excluded From Build** to **Yes** for all configurations.
4. Select the desired platform and configuration. The final grading build should use:

   ```text
   Configuration: Release
   Platform:      x64
   ```

5. Select **Build > Rebuild Solution**.
6. Confirm that the build completes with zero errors.
7. The final executable is normally created at:

   ```text
   x64\Release\Echo Hiding Audio.exe
   ```

The C++ executable does not require Python. Python 3 is required only for the supplemental analysis scripts.

## Command-line usage

Running the executable with no parameters prints the complete usage guide.

```text
Echo Hiding Audio.exe -hide -m <message file | random> -c <cover.wav> [-o <stego.wav>]
Echo Hiding Audio.exe -extract -s <stego.wav> [-o <message file>]
```

| Option | Meaning |
|---|---|
| `-hide` | Hide a message in a cover WAV |
| `-extract` | Extract a message from a stego WAV |
| `-m <path>` | Message file, or the literal word `random` |
| `-c <path>` | Cover WAV for hiding |
| `-s <path>` | Stego WAV for extraction |
| `-o <path>` | Optional output path |
| `-h`, `--help` | Display help and exit successfully |

Defaults:

- Hide output: `<cover-name>_stego.wav`
- Extract output: `extracted_message.bin`

Unknown, duplicated, incomplete, and mode-inappropriate options are rejected with a diagnostic and a nonzero exit code.

## PowerShell examples

From the project root using the included Release x64 build:

```powershell
& ".\x64\Release\Echo Hiding Audio.exe"

& ".\x64\Release\Echo Hiding Audio.exe" `
    -hide `
    -m ".\TestData\sample_message.txt" `
    -c ".\TestData\cover.wav" `
    -o ".\hidden.wav"

& ".\x64\Release\Echo Hiding Audio.exe" `
    -hide `
    -m random `
    -c ".\TestData\cover.wav"

& ".\x64\Release\Echo Hiding Audio.exe" `
    -extract `
    -s ".\hidden.wav" `
    -o ".\recovered.bin"
```

Because the executable name contains spaces, PowerShell requires the call operator `&` before the quoted path.

## Command logging

Direct invocations are mirrored to:

```text
CommandLogs\Latest Command.log
CommandLogs\history\command_<timestamp>_<process-id>.log
```

The logs include the command, working directory, console output, warnings/errors, and final exit code. Logging failure does not prevent the hide/extract operation from running.

The test harness disables direct-command logging because the harness already records complete test transcripts.

## Automated verification

Run commands from the project root in PowerShell.

### Regression and edge cases

```powershell
& ".\Tests\run_tests.bat"
```

Expected overall result:

```text
Overall result: PASS
```

### PG-14 statistical detectability analysis

```powershell
& ".\Analysis\run_tests_then_pg14.bat"
```

The analysis reports exact-prefix BER, MSE, normalized RMSE, SNR, histogram total variation, and an echo-detector-oriented delta metric.

### PG-23 controlled limit matrix

```powershell
& ".\Analysis\run_pg23_matrix.bat"
```

The matrix tests four representative cover categories at requested payload fractions of 0%, 25%, 50%, 75%, 100%, and 125%, plus the below-header, header-exact, one-byte-minus, and one-byte-exact lower boundaries.

### PG-13 / PG-24 auditory study

Prepare the blinded study:

```powershell
& ".\Analysis\run_pg24_prepare.bat"
```

Open the exact timestamped `PG24 Listening Study.html` path printed by the command. Do not open the study key until all ratings are completed.

After the browser downloads the completed ratings CSV:

```powershell
& ".\Analysis\run_pg24_summarize.bat" `
    --ratings "C:\path\to\PG24_Ratings_<study-id>.csv"
```

Successful validation reports:

```text
Validation errors : 0
Overall result    : PASS
```

## Auditory perception status (PG-13 / PG-24)

**Current status:** Complete for the documented one-listener, 24-pair blinded study.

| Item | Result |
|---|---|
| Study ID | `20260729_142717` |
| Listener | Alex Bryant |
| Validated rating rows | 24 |
| Validation errors | 0 |
| PG-24 three-category completion | **GO** |
| PG-13 threshold documented | **GO** |
| `obvious` | 0 |
| `apparent_close_listening` | 4 |
| `undetectable_without_original` | 20 |

Results by requested capacity fraction:

| Fraction | Ratings | Obvious | Apparent with close listening | Undetectable without original |
|---:|---:|---:|---:|---:|
| 0 | 4 | 0 | 0 | 4 |
| 0.25 | 4 | 0 | 1 | 3 |
| 0.50 | 4 | 0 | 1 | 3 |
| 0.75 | 4 | 0 | 1 | 3 |
| 1.00 | 4 | 0 | 1 | 3 |
| 1.25 | 4 | 0 | 0 | 4 |

Per-cover results:

- `music_blues_16bit_stereo.wav`: no apparent or obvious threshold observed; highest tested undetectable fraction was 1.25.
- `near_silence_16bit_mono.wav`: no apparent or obvious threshold observed; highest tested undetectable fraction was 1.25.
- `sparse_quiet_16bit_mono.wav`: first apparent threshold occurred at 0.25; no obvious threshold was observed; the highest tested undetectable fraction was 1.25.
- `speech_8bit_mono.wav`: no apparent or obvious threshold observed; highest tested undetectable fraction was 1.25.

The study used paired original/stego excerpts. Ratings were cover-specific, and no sample was rated obvious. Because the study used one listener, these results should be interpreted as documented project evidence rather than a universal human-perception threshold.

Committed evidence is located in:

```text
Analysis\pg24_evidence\20260729_142717\PG13 PG24 Auditory Summary.txt
Analysis\pg24_evidence\20260729_142717\PG24 Combined Ratings.csv
Analysis\pg24_evidence\20260729_142717\PG24 Per-Cover Thresholds.csv
Analysis\pg24_evidence\20260729_142717\PG24 Study Key.csv
Analysis\pg24_evidence\20260729_142717\PG24 Study Manifest.txt
Analysis\pg24_evidence\20260729_142717\PG24_Ratings_20260729_142717.csv
```

## Error handling and output safety

The program has tested handling for:

- No parameters
- Unknown or duplicate options
- Missing option values
- Missing required options
- Hide/extract option conflicts
- Missing message, cover, or stego files
- Non-WAV input
- Malformed or truncated RIFF/WAVE input
- Unsupported compression, bit depth, channel count, or extensible subtype
- Cover too small to contain the header
- Extraction from an untouched WAV
- Corrupted or implausible payload length
- Attempts to use an input path as the output path
- Existing output files
- Capacity exhaustion

Existing output files are overwritten only after a warning. Input/output path collisions are rejected.

## Known limitations

- Capacity is low because each logical bit uses seven 2,048-frame observations.
- Covers shorter than the 64-bit header requirement cannot be used.
- Low-sample-rate pure tones are a measured failure boundary because a sinusoid and its delayed copy can remain another sinusoid with insufficient broadband structure for cepstral discrimination.
- Statistical metrics and human perception are cover-dependent; no single SNR or payload fraction is a universal detectability threshold.
- The current fixed parameters prioritize extraction reliability over capacity.
- The completed PG-13/PG-24 auditory study used one listener; its findings are cover-specific and do not establish a universal detectability threshold.

## Documentation

Primary supporting documents are located in:

```text
Analysis\README_PG14.md
Analysis\README_PG23.md
Analysis\README_PG24.md
Analysis\PG23_MATRIX_DESIGN.md
Analysis\PG13_PG24_STUDY_DESIGN.md
Analysis\pg24_evidence\20260729_142717\PG13 PG24 Auditory Summary.txt
TESTING_REPORT.md
```

The analysis output folders contain generated evidence and should be curated before creating the final submission ZIP.
