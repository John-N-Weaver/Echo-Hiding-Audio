# PG-14 Statistical Analysis Tool

## Purpose

`pg14_analysis.py` implements the basic statistical and detector-oriented
analysis required by PG-14. Its CSV output is also the planned data table for
the later PG-23 controlled testing matrix.

The analysis tool does **not** change the C++ hiding or extraction program. It
analyzes files produced by that program.

## Requirements

- Windows, Linux, or macOS
- Python 3
- No third-party packages

The parser supports the same audio formats as the project:

- Uncompressed 8-bit PCM WAV
- Uncompressed 16-bit PCM WAV
- Mono or stereo
- Classic PCM or PCM WAVE_FORMAT_EXTENSIBLE

## Metrics

### Capacity

The script calculates:

- Protected logical capacity
- Payload capacity after the 64-bit header
- Complete recoverable payload bytes
- Requested-to-capacity ratio
- Expected embedded bits
- Capacity utilization
- Cover bytes required per payload byte

The defaults match the current program:

```text
Segment length: 2,048 frames
Echo delays:    150 and 200 samples
Echo decay:     0.4
Repetition:     7
Header:         64 bits
```

### Payload recovery

When both the original and extracted payloads are supplied, the script
calculates:

- Exact-prefix status
- Byte error rate within the recovered prefix
- Bit error rate (BER) within the recovered prefix
- Missing and extra bytes
- Recovery fraction
- End-to-end BER that counts missing and extra data

The separate BER fields are important because capacity-limited output can have
a perfect recovered prefix while still omitting the remainder of the requested
message.

### Cover/stego distortion

The script calculates:

- Modified-sample rate
- Mean error
- Mean absolute error
- MSE
- RMSE and normalized RMSE
- Peak absolute error
- SNR
- PSNR
- Pearson correlation

### Detectability

Two types of detectability evidence are produced:

1. **Sample-distribution measures**
   - Histogram total-variation distance
   - Histogram Jensen-Shannon divergence

2. **Echo-oriented measures**
   - Normalized autocorrelation at delays 150 and 200
   - Cover-to-stego change at each delay
   - Delay-bias change
   - Combined echo-detector delta

These measures do not prove that a stego file is undetectable. They provide
repeatable quantitative evidence that can be compared across the PG-23 matrix.


## Output overwrite rules

The output behavior depends on the analysis mode.

### `single` mode

Without `--append`, the output CSV is opened in write mode and an existing file
with the same name is **overwritten**. The program now prints an explicit
warning before doing so.

```powershell
# Replaces pg14_results.csv if it already exists
& ".\Analysis\run_pg14_analysis.bat" single `
    --cover ".\cover.wav" `
    --stego ".\stego.wav" `
    --output ".\Analysis\pg14_results.csv"
```

Use `--append` to retain the existing rows and add the new single-pair result:

```powershell
& ".\Analysis\run_pg14_analysis.bat" single `
    --cover ".\cover.wav" `
    --stego ".\stego.wav" `
    --output ".\Analysis\pg14_results.csv" `
    --append
```

Appending is permitted only when the existing CSV has the same PG-14 output
schema.

### `matrix` mode

Matrix mode always writes a complete matrix snapshot. Therefore, the named
output file is **overwritten** when it already exists.

The automated workflow avoids losing evidence by writing timestamped matrix,
results, and summary files under `Analysis\results\history`. Only the convenient
files beginning with `Latest PG14` are replaced.

## Analyze one pair

From the project root:

```powershell
& ".\Analysis\run_pg14_analysis.bat" single `
    --cover ".\TestData\Corpus\music_blues_16bit_stereo.wav" `
    --stego ".\Tests\out\t1_stego.wav" `
    --payload ".\TestData\messages\fifths.wav" `
    --extracted ".\Tests\out\t1_extracted.bin" `
    --test-id "PG14-001" `
    --cover-category "music" `
    --payload-category "audio" `
    --target-payload-fraction "over-capacity" `
    --output ".\Analysis\pg14_results.csv"
```

Add `--append` to preserve prior rows in the same compatible results file.


## Automate PG-14 against the actual regression pairs

After `Tests\run_tests.bat` completes, the harness has already created the
actual T1-T10 files:

```text
Tests\out\t1_stego.wav
Tests\out\t1_extracted.bin
...
Tests\out\t10_stego.wav
Tests\out\t10_extracted.bin
```

`pg14_analyze_latest_tests.py` reads `Tests\Latest Test Run.log` to recover the
corresponding original cover and payload paths. It then creates a ten-row
PG-23-compatible matrix and analyzes all ten rows in one operation.

From the project root:

```powershell
& ".\Analysis\run_pg14_latest_tests.bat"
```

Outputs:

```text
Analysis\results\
├── Latest PG14 Matrix.csv
├── Latest PG14 Results.csv
├── Latest PG14 Summary.txt
└── history\
    ├── pg14_matrix_<run-stamp>.csv
    ├── pg14_results_<run-stamp>.csv
    └── pg14_summary_<run-stamp>.txt
```

The timestamped history files are retained. Reanalyzing the same regression run
creates a `_rerun01`, `_rerun02`, and so forth rather than replacing the prior
archived result.

### Run tests and PG-14 together

The complete automated workflow is:

```powershell
& ".\Analysis\run_tests_then_pg14.bat"
```

This performs:

1. `Tests\run_tests.bat`
2. PG-14 analysis of the resulting T1-T10 pairs

PG-14 runs only when the regression harness returns success.

## Analyze a PG-23 matrix

1. Copy `pg23_matrix_template.csv`.
2. Add one row per cover/stego/payload/extracted combination.
3. Paths may be absolute or relative to the matrix CSV.
4. Run:

```powershell
& ".\Analysis\run_pg14_analysis.bat" matrix `
    --matrix ".\Analysis\pg23_matrix.csv" `
    --output ".\Analysis\pg14_pg23_results.csv"
```

Every row is attempted. Failed rows are retained with `status=FAIL` and an
explanation in `error_message`.

## Planned PG-23 factors

The matrix fields support controlled variation of:

- Cover category: music, speech, near silence, sparse/quiet, noise
- Bit depth: 8-bit and 16-bit
- Channel count: mono and stereo
- Payload type: text, image, audio, compressed, encrypted, arbitrary binary
- Requested payload fraction: 0%, 25%, 50%, 75%, 100%, and above capacity
- Parameter set: fixed current configuration or future approved variants

The output also reserves PG-24 listening fields:

- `auditory_rating`
- `listener_id`
- `listening_original_available`
- `playback_device`
- `listening_environment`
- `listening_notes`

The required rating values should be standardized as:

```text
obvious
apparent_close_listening
undetectable_without_original
```

## Output-field reference

See `pg14_output_data_dictionary.csv` for the complete field list, units, and
definitions.


## PG-23 controlled limit testing

PG-14 supplies the measurement layer used by the completed PG-23 workflow.

Preview the representative matrix:

```powershell
& ".\Analysis\run_pg23_matrix.bat" --dry-run
```

Run the controlled matrix:

```powershell
& ".\Analysis\run_pg23_matrix.bat"
```

See `README_PG23.md` for the lower-limit frame tests, payload fractions,
archived output structure, pass criteria, exhaustive-cover option, and storage
controls.
