# Echo Hiding Audio: Milestone 2 Testing Report

**Course:** CS 4463 / CS 5173<br>
**Team:** 21<br>
**Authors:** John N. Weaver and Alex W. Bryant<br>
**Repository:** <https://github.com/John-N-Weaver/Echo-Hiding-Audio><br>
**Report status:** Current Milestone 2 implementation, July 29, 2026

## 1. Purpose and scope

This report documents functional correctness, capacity, lower and upper operating
limits, statistical detectability, error handling, and the pending human auditory
evaluation for the current Echo Hiding Audio implementation.

The report describes the implementation currently defined by `stego.h` and
`stego_echo.cpp`. It supersedes testing notes that referred to the earlier
runtime-parameter design, 4,096- or 8,192-frame configurations, a 56-bit parameter
block, or decay 0.5.

## 2. Current fixed implementation

The current implementation uses time-domain echo hiding in uncompressed PCM WAV
audio.

| Parameter | Current value |
|---|---:|
| Segment length | 2,048 audio frames |
| Bit-0 echo delay | 150 samples |
| Bit-1 echo delay | 200 samples |
| Echo decay | 0.4 |
| Repetition count | 7 physical copies per logical bit |
| Logical-bit vote | Four of seven |
| Header | 64 logical bits |
| Header contents | ASCII `ECHO` plus uint32 requested payload length |
| Magic tolerance | Up to four bit differences in the 32-bit magic |
| Supported PCM | 8-bit unsigned or 16-bit signed |
| Supported channels | Mono or stereo |

The fixed parameters are intentionally shared by the encoder and decoder rather
than supplied independently at the command line.

### 2.1 Embedding

Each logical bit is replicated seven times. The copies are interleaved across
widely separated portions of the cover rather than placed in consecutive
segments. This reduces correlated failures when one local passage is difficult
for the detector.

The mixer uses:

- A 150-sample echo for logical 0
- A 200-sample echo for logical 1
- A crossfade near segment boundaries
- A headroom factor of `1 / (1 + decay)` to prevent clipping
- Matching attenuation outside the active payload region to avoid an abrupt
  level change

### 2.2 Extraction

For each physical segment, the extractor:

1. Reads normalized PCM samples.
2. Uses the center of the segment to reduce boundary-ramp contamination.
3. Computes a real cepstrum with an FFT, log-magnitude operation, and inverse
   FFT.
4. Calculates local peak-minus-baseline scores near 150 and 200 samples.
5. Selects the stronger score as the physical bit.
6. Majority-votes the seven interleaved copies to recover the logical bit.

The original cover is not required for extraction.

### 2.3 Payload and partial-recovery semantics

The hidden stream begins with:

```text
Bytes 0-3: ASCII "ECHO"
Bytes 4-7: requested payload length, uint32 little-endian
Bytes 8+: requested payload bytes
```

The header stores the full requested length. When a message exceeds cover
capacity, the program does not reject or pre-truncate the request. It embeds the
complete protected prefix that fits, saves the resulting stego WAV when the
header is complete, and reports the shortfall.

Extraction bounds the declared length against physical capacity and writes every
complete recovered prefix byte. A partial trailing byte is not written.

## 3. Build and execution environment

The project is a Windows command-line C++ application built with Visual Studio
2022.

The final grading package should use:

```text
Configuration: Release
Platform:      x64
```

The current regression and analysis evidence was generated from a successfully
rebuilt development executable. The project has also been built in the Visual
Studio Debug/Release and x64/Win32 configurations during development. A final
x64 Release rebuild and extracted-ZIP smoke test remain packaging tasks.

The C++ program does not require Python. Python 3 is required only for the
supplemental PG-14, PG-23, and PG-24 analysis scripts.

## 4. Command-line interface

```text
Echo Hiding Audio.exe -hide -m <message file | random> -c <cover.wav> [-o <stego.wav>]
Echo Hiding Audio.exe -extract -s <stego.wav> [-o <message file>]
```

Supported options:

| Option | Purpose |
|---|---|
| `-hide` | Hide a payload |
| `-extract` | Extract a payload |
| `-m <path>` | Payload file, or literal `random` |
| `-c <path>` | Cover WAV |
| `-s <path>` | Stego WAV |
| `-o <path>` | Optional output path |
| `-h`, `--help` | Print usage and exit successfully |

Running with no parameters prints usage. Unknown, duplicated, incomplete, and
mode-inappropriate options are rejected cleanly.

## 5. Functional regression testing

### 5.1 Reproduction command

Run from the project root in PowerShell:

```powershell
& ".\Tests\run_tests.bat"
```

### 5.2 Latest verified regression result

Latest post-move combined run:

```text
Run stamp: 20260728_235124
Overall result: PASS
```

Results:

| Test group | Result |
|---|---:|
| Round-trip cases | 10 run |
| Exact complete recoveries | 0 |
| Exact-prefix partial recoveries | 10 |
| Round-trip failures | 0 |
| Edge cases | 21 passed |
| Edge-case failures | 0 |

The ten round-trip fixtures deliberately request more data than their selected
covers can hold under the low-capacity, seven-copy design. Each case therefore
tests bounded partial recovery. All ten recovered byte prefixes matched their
source files exactly.

The edge-case suite covers command-line errors, missing and malformed inputs,
unsupported formats, capacity boundaries, output naming, and extraction from
non-stego audio.

### 5.3 Interpretation

The regression result supports these claims:

- The program embeds until protected cover capacity is exhausted.
- Extraction returns the exact complete-byte prefix that was physically stored.
- Capacity exhaustion is reported rather than hidden.
- Tested faulty inputs terminate cleanly rather than crashing.
- Default output names work for hiding and extraction.

It does not establish that every arbitrary malformed binary file is safe. It
establishes clean handling for all 21 documented edge cases and the validated WAV
parser checks described below.

## 6. Capacity

One logical bit consumes:

```text
2,048 frames/physical observation × 7 copies = 14,336 frames/logical bit
```

For a cover containing `frame_count` audio frames:

```text
logical_bits = floor(frame_count / 14,336)
payload_bytes = floor(max(0, logical_bits - 64) / 8)
```

Channel count does not increase capacity because all channels carry the same
logical stream.

### 6.1 Fixed lower boundaries

| Boundary | Frames | Expected behavior |
|---|---:|---|
| One frame below complete header | 917,503 | Hide fails cleanly |
| Header exact | 917,504 | Empty payload completes |
| One frame below one complete byte | 1,032,191 | Header plus seven payload bits; zero complete payload bytes recover |
| One byte exact | 1,032,192 | One payload byte completes |

At 44.1 kHz:

- Header-only minimum: approximately 20.8 seconds
- One-byte minimum: approximately 23.4 seconds
- Asymptotic rate: approximately 3.08 logical bits/second
- Approximate payload rate before subtracting the header: 23 bytes/minute
- Approximate payload capacity of a 60-second cover: 15 complete bytes after
  the header

### 6.2 Special `-m random` behavior

The literal payload argument `random` generates exactly the number of complete
random payload bytes reported by the cover-capacity calculation. It is intended
for capacity-filling tests and differs from ordinary oversized-message behavior.

## 7. PG-23 controlled limit matrix

### 7.1 Reproduction command

```powershell
& ".\Analysis\run_pg23_matrix.bat"
```

### 7.2 Matrix design

The default matrix selects four representative supported covers, prioritizing:

1. Music
2. Speech
3. Sparse or quiet material
4. Near silence

Each cover is tested at requested payload fractions:

```text
0%, 25%, 50%, 75%, 100%, 125%
```

This creates 24 payload-fraction cases. Four exact lower-boundary cases are
added, producing 28 controlled cases.

### 7.3 Latest verified PG-23 result

Corrected run:

```text
Run stamp: 20260729_001404
Overall result: PASS
```

Results:

| Result | Count |
|---|---:|
| All controlled cases | 28/28 passed |
| Payload-fraction cases | 24/24 passed |
| Lower-boundary cases | 4/4 passed |
| Successful stego WAVs sent to PG-14 | 27 |
| PG-14 results for generated stego WAVs | 27/27 passed |

There are 27 statistical rows rather than 28 because the
`L01_BELOW_HEADER` case intentionally fails before a valid stego WAV can be
created.

### 7.4 Lower-boundary classifications

| ID | Boundary | Verified classification |
|---|---|---|
| L01 | Below header | Clean hide failure |
| L02 | Header exact | Complete empty payload |
| L03 | One byte minus one frame | Exact zero-byte prefix; incomplete byte omitted |
| L04 | One byte exact | Complete one-byte recovery |

The L03 generator writes a valid RIFF pad byte when an 8-bit mono data chunk has
an odd logical size. This prevents a malformed test fixture from being
misclassified as an algorithmic failure.

### 7.5 Limit conclusion

Within the tested matrix:

- Requests through 100% of complete-byte capacity recover completely.
- The 125% requests exhaust capacity but recover the exact available prefix.
- The exact header and one-byte frame thresholds behave as predicted.
- No compared-prefix corruption was observed.
- No unexpected reliability failure was observed in the representative matrix.

These findings are limited to the tested cover corpus and fixed implementation
parameters.

## 8. PG-14 statistical detectability analysis

### 8.1 Reproduction command

To run regression testing followed by PG-14 analysis:

```powershell
& ".\Analysis\run_tests_then_pg14.bat"
```

### 8.2 Metrics

The analysis records:

- Exact compared-prefix BER
- End-to-end BER, including omitted bytes after capacity exhaustion
- Modified-sample rate
- Mean squared error
- Normalized RMSE
- Signal-to-noise ratio
- Peak signal-to-noise ratio
- Histogram total variation
- Histogram Jensen-Shannon divergence
- Echo-detector-oriented delta

Raw MSE should not be compared directly across 8-bit and 16-bit files. Use
normalized RMSE, SNR, PSNR, distribution metrics, and detector-oriented metrics
for cross-format comparisons.

### 8.3 Latest verified regression-pair analysis

| Measure | Result |
|---|---:|
| Rows analyzed | 10 |
| Rows passed | 10 |
| Rows failed | 0 |
| Exact recovered prefixes | 10 |
| Zero compared-prefix BER | 10 |

Observed ranges:

| Metric | Minimum | Mean | Maximum |
|---|---:|---:|---:|
| MSE | 20.161 | 6.08031e+06 | 2.42657e+07 |
| SNR, dB | 7.31712 | 7.99912 | 8.75849 |
| Histogram total variation | 0.0049001 | 0.0733112 | 0.156593 |
| Echo-detector delta L1 | 0.101105 | 0.280573 | 0.382152 |

### 8.4 Interpretation

- Compared-prefix BER is zero because every physically recovered complete-byte
  prefix matched its source.
- End-to-end BER is larger for oversized requests because bytes that cannot fit
  are counted as missing.
- Distortion and detector metrics vary materially by cover content and payload
  level.
- A low or high value from one WAV does not establish a universal human or
  machine-detectability threshold.
- The targeted echo metric is more relevant to this technique than an
  LSB-specific detector.

## 9. PG-13 and PG-24 auditory evaluation

### 9.1 Status

```text
Status: PENDING HUMAN LISTENING STUDY
```

The study-generation and validation tools are installed and have passed
synthetic tooling validation. Actual perception results must come from a human
listener and must not be fabricated.

### 9.2 Preparation

```powershell
& ".\Analysis\run_pg24_prepare.bat"
```

The current prepared study used:

```text
Study ID: 20260729_005447
Pairs:    24
```

The listener must open the exact timestamped `PG24 Listening Study.html` file
printed by the preparation command. The listener must not open
`PG24 Study Key.csv` before completing all ratings.

### 9.3 Required categories

Each pair must receive exactly one rating:

- `obvious`
- `apparent_close_listening`
- `undetectable_without_original`

The listener must also record:

- Listener ID
- Playback device
- Listening environment

### 9.4 Summarization

After the browser downloads the completed CSV:

```powershell
& ".\Analysis\run_pg24_summarize.bat" `
    --ratings "C:\path\to\PG24_Ratings_<study-id>.csv"
```

A valid run must report:

```text
Validation errors : 0
Overall result    : PASS
```

The summary calculates, for each cover:

- First apparent-or-obvious payload fraction
- First obvious payload fraction
- Highest undetectable payload fraction

### 9.5 Change-control rule

An audible result is a valid PG-13/PG-24 finding and is not automatically a
software defect. Do not change `ECHO_DECAY`, the echo delays, segment length, or
repetition count solely because one or more cases are audible.

Any parameter change defines a new configuration and requires:

1. C++ rebuild
2. Full regression suite
3. PG-14 analysis
4. PG-23 controlled matrix
5. A new blinded PG-24 listening study
6. Documentation updates that identify the new configuration

## 10. WAV and input validation

The current WAV parser validates:

- RIFF and WAVE identifiers
- Chunk bounds and file-size consistency
- Required `fmt ` and `data` chunks
- Classic PCM
- Extensible PCM only when the subtype is PCM
- 8-bit unsigned or 16-bit signed samples
- Mono or stereo
- Block alignment and byte-rate consistency
- Truncated or malformed chunks
- Odd-sized RIFF chunks and required pad bytes
- Supported valid-bit layouts

The program rejects:

- Missing files
- Non-WAV files
- Compressed WAV files
- Unsupported extensible subtypes
- Unsupported bit depths, including 24-bit PCM
- Unsupported channel counts
- Malformed RIFF/WAVE data
- Covers too short to contain the complete header
- Untouched WAV files without a valid hidden header
- Implausible or capacity-inconsistent payload lengths
- Input/output path collisions

Existing output files are overwritten only after a warning.

## 11. Direct-command logging

Normal direct invocations are mirrored to:

```text
CommandLogs\Latest Command.log
CommandLogs\history\command_<timestamp>_<process-id>.log
```

The log records:

- Command line
- Working directory
- Console output
- Warnings and errors
- Final exit code

The automated test harness disables direct-command logging because the harness
already captures complete transcripts.

## 12. Direct cepstrum comparison versus autocorrelation

The current extractor compares local baseline-corrected cepstral peak scores
near the two candidate delays. It does not autocorrelate the cepstrum.

Earlier development experiments tested full FFT autocorrelation, liftered
autocorrelation, and a restricted linear autocorrelation under an older
configuration. Each variant degraded recovery relative to direct cepstrum
comparison. That experiment is preserved as a historical engineering record in:

```text
Docs\stego_echo_autocorrelation_attempt.md
```

The historical measurements were not rerun as an autocorrelation-versus-direct
ablation under the current 2,048-frame, 150/200-sample, seven-copy
configuration. The current implementation is supported independently by the
regression, PG-14, and PG-23 results documented above.

## 13. Known limits and limitations

- Capacity is low because seven 2,048-frame observations are used per logical
  bit.
- Covers shorter than 917,504 frames cannot contain the complete header.
- Stereo does not increase payload capacity.
- Low-sample-rate pure tones are a measured reliability boundary. A delayed
  sinusoid can remain another sinusoid with too little broadband structure for
  reliable cepstral discrimination.
- Results from broadband music, speech, noise, sparse material, and near-silence
  do not imply that every possible WAV will decode successfully.
- Statistical detectability and auditory detectability are cover-dependent.
- The human auditory threshold remains pending until the real blinded study is
  complete.
- The fixed configuration prioritizes recovery reliability over payload
  capacity.
- The header contains no cryptographic authentication or encryption. Payload
  confidentiality depends on encrypting the message before hiding it.
- Magic tolerance reduces false rejection from isolated detector errors but is
  not an integrity guarantee.

## 14. Evidence locations

Primary scripts and documentation:

```text
Tests\run_tests.bat
Analysis\README_PG14.md
Analysis\README_PG23.md
Analysis\README_PG24.md
Analysis\PG23_MATRIX_DESIGN.md
Analysis\PG13_PG24_STUDY_DESIGN.md
Analysis\pg14_analysis.py
Analysis\pg23_run_matrix.py
Analysis\pg24_listening_study.py
```

Generated evidence:

```text
Tests\Test Run Summary.txt
Tests\Latest Test Run.log
Analysis\results\
Analysis\pg23_results\
Analysis\pg24_results\
```

Generated histories can be large. The final grading ZIP should include compact
representative summaries and selected evidence rather than every generated WAV
and historical run directory.

## 15. Current conclusion

The current implementation is functionally stable under the documented
regression and edge-case suite. Capacity and exact lower limits have been
measured. The controlled PG-23 matrix passed all 28 cases, and all available
PG-14 statistical analyses passed with zero compared-prefix BER.

PG-13 and PG-24 remain incomplete until a human listener completes the blinded
24-pair study and the summarizer validates the returned ratings. No further C++
change is required merely to record an audible threshold. Parameter retuning,
when chosen, would require a complete new validation cycle.
