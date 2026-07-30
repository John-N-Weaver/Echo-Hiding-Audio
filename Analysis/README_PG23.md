# PG-23 Controlled Limit Testing

## Requirement addressed

PG-23 requires testing to be thorough and documented, to use many tests, and to
identify upper and lower limits.

`pg23_run_matrix.py` performs that work against the compiled Echo Hiding Audio
program. It does not simulate or reimplement the steganography algorithm.

## Default controlled matrix

The default run selects up to four representative supported WAV files,
prioritizing these categories:

1. Music
2. Speech
3. Sparse or quiet audio
4. Near silence

Selection also favors diversity in bit depth and channel count.

Each selected cover is tested at these requested payload fractions:

```text
0.00
0.25
0.50
0.75
1.00
1.25
```

With four covers, this produces 24 payload-fraction cases. Four exact
frame-boundary tests are added, for a default total of 28 controlled cases.

## Limits measured

### Lower limit

The script generates derived PCM covers at exact frame boundaries:

| Test | Frames | Expected result |
|---|---:|---|
| One frame below header | 917,503 | Hide fails cleanly |
| Header exact | 917,504 | Empty payload completes |
| One frame below one byte | 1,032,191 | Exact empty prefix; incomplete byte omitted |
| One byte exact | 1,032,192 | One byte completes exactly |

These values follow from:

```text
frames per logical bit = 2,048 × 7 = 14,336
header frames          = 64 × 14,336 = 917,504
one-byte frames        = 72 × 14,336 = 1,032,192
```

### Upper complete-recovery limit

For each ordinary cover, the script identifies:

- Highest tested fraction recovered completely and byte-exactly
- First fraction that exhausts capacity but retains an exact prefix
- First reliability failure, when present

The expected boundary is complete recovery through 100% of complete-byte
capacity and exact-prefix partial recovery above capacity.

### Reliability limit

A reliability failure is recorded when any of these occurs unexpectedly:

- Hide failure
- Extract failure
- Header failure
- Corrupted recovered prefix
- Nonzero compared-prefix BER
- Failed PG-14 analysis


## L03 odd-length 8-bit PCM boundary handling

`L03_ONE_BYTE_MINUS` uses exactly 1,032,191 frames. When the selected source is
8-bit mono, that creates an odd-length PCM data chunk. RIFF/WAVE requires a
physical pad byte after an odd-length chunk, although the chunk's recorded
logical length remains unpadded.

The PG-23 generator writes this boundary file directly as valid classic PCM,
including the required RIFF pad byte. This prevents the test generator from
mistaking a malformed derived WAV for an algorithmic hide failure.

## Statistical evidence

Every successful stego pair is passed automatically to `pg14_analysis.py`.
The combined PG-23 results include:

- Capacity and requested-to-capacity ratio
- Exact complete or partial-prefix recovery
- Compared-prefix BER
- End-to-end BER
- Modified-sample rate
- MSE and normalized RMSE
- SNR and PSNR
- Histogram total variation
- Histogram Jensen-Shannon divergence
- Echo-detector delta

Raw MSE should not be averaged across 8-bit and 16-bit covers. Use normalized
RMSE, SNR, PSNR, distribution metrics, and echo metrics for cross-format
comparison.

## Install

Add:

```text
Analysis\pg23_run_matrix.py
Analysis\run_pg23_matrix.bat
Analysis\README_PG23.md
Analysis\pg23_output_data_dictionary.csv
```

Replace:

```text
Analysis\PG23_MATRIX_DESIGN.md
Analysis\README_PG14.md
```

No C++ source file changes are required.

## Preview the matrix

From the project root:

```powershell
& ".\Analysis\run_pg23_matrix.bat" --dry-run
```

This reports:

- Executable selected
- Eligible covers
- Representative covers selected
- Planned case count
- Estimated generated stego storage

## Run the representative matrix

```powershell
& ".\Analysis\run_pg23_matrix.bat"
```

The runner first uses the executable recorded in:

```text
Tests\Test Run Summary.txt
```

This keeps PG-23 aligned with the exact binary that most recently passed the
regression suite. When that record is unavailable, the fallback search order is:

```text
x64\Debug\Echo Hiding Audio.exe
x64\Release\Echo Hiding Audio.exe
Debug\Echo Hiding Audio.exe
Release\Echo Hiding Audio.exe
Echo Hiding Audio.exe
stego.exe
```

Use an explicit executable when needed:

```powershell
& ".\Analysis\run_pg23_matrix.bat" `
    --exe ".\x64\Debug\Echo Hiding Audio.exe"
```

## Run every eligible cover

```powershell
& ".\Analysis\run_pg23_matrix.bat" --all-covers
```

This can produce substantially more WAV data and take much longer. The default
representative matrix is the appropriate first PG-23 run.

## Customize the fractions

```powershell
& ".\Analysis\run_pg23_matrix.bat" `
    --fractions "0,0.10,0.25,0.50,0.75,1.00,1.10,1.25"
```

## Output preservation

Each run receives a unique directory:

```text
Analysis\pg23_results\history\<run-stamp>\
```

A rerun with the same stamp receives `_rerun01`, `_rerun02`, and so forth.

The run directory contains:

```text
PG23 Execution Manifest.csv
PG23 PG14 Matrix.csv
PG23 PG14 Detailed Results.csv
PG23 Combined Results.csv
PG23 Summary.txt
PG23 Cover Discovery.txt
logs\
generated\
```

Convenient newest-run copies are written to:

```text
Analysis\pg23_results\Latest PG23 Execution Manifest.csv
Analysis\pg23_results\Latest PG23 PG14 Matrix.csv
Analysis\pg23_results\Latest PG23 PG14 Detailed Results.csv
Analysis\pg23_results\Latest PG23 Combined Results.csv
Analysis\pg23_results\Latest PG23 Summary.txt
Analysis\pg23_results\Latest PG23 Cover Discovery.txt
```

The `Latest` files are replaced. Timestamped run directories are retained.

## Storage control

Generated stego WAVs are retained by default because they are useful evidence
and can later support PG-24 listening tests.

To retain metrics and logs but delete generated WAV/payload/extracted files
after analysis:

```powershell
& ".\Analysis\run_pg23_matrix.bat" --cleanup-generated
```

Do not include the complete `Analysis\pg23_results` history in the final grading
ZIP unless specifically required. Include selected evidence and representative
files instead.

## Pass criteria

The overall run passes when:

- Every controlled case produces its expected outcome
- Every generated stego pair completes PG-14 analysis
- Complete-capacity cases recover exactly
- Above-capacity cases recover the exact available prefix
- Lower frame-boundary cases behave as specified


## Continue with PG-13 and PG-24

After a successful PG-23 run:

```powershell
& ".\Analysis\run_pg24_prepare.bat"
```

See `README_PG24.md`.
