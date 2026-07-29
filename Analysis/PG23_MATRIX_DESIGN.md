# PG-23 Final Controlled Test Design

## Research question

At what cover size and requested payload fraction does the current fixed
Echo Hiding Audio implementation stop providing complete, reliable recovery,
and how do distortion and detector metrics change across those conditions?

## Independent variables

### Cover characteristics

- Content category
- Sample rate
- 8-bit or 16-bit PCM
- Mono or stereo
- Duration and resulting payload capacity

### Requested payload fraction

```text
0%, 25%, 50%, 75%, 100%, 125%
```

### Fixed implementation parameters

```text
Segment length: 2,048 frames
Echo delays:    150 and 200 samples
Echo decay:     0.4
Repetition:     7
Header:         64 bits
```

These parameters are held constant because they define the current approved
implementation rather than an unimplemented runtime parameter sweep.

## Dependent variables

### Functional

- Hide and extract exit status
- Complete versus partial recovery
- Exact recovered prefix
- Compared-prefix BER
- End-to-end BER
- Missing and extra bytes

### Capacity and limits

- Logical capacity
- Complete payload-byte capacity
- Highest complete-recovery fraction
- First capacity-exhausted fraction
- First reliability failure
- Minimum header frames
- Minimum one-byte frames

### Distortion and detectability

- Modified-sample rate
- Normalized RMSE
- SNR and PSNR
- Histogram total variation
- Histogram Jensen-Shannon divergence
- Echo-delay autocorrelation change

## Test groups

1. Representative payload-fraction matrix
2. Exact lower frame-boundary matrix
3. Optional exhaustive all-cover replication
4. Later PG-24 auditory classification using retained representative stego WAVs

## Reproducibility

Each row records:

- Run and test ID
- Absolute input/output paths
- Cover format metadata
- Exact requested bytes and capacity
- Process exit codes
- Direct byte comparison
- PG-14 metrics
- Per-command transcript path

Each run is stored in a unique timestamped directory.


## Exact RIFF boundary construction

Derived lower-limit covers are emitted as classic PCM RIFF/WAVE files. The
writer adds the required physical pad byte when the data chunk length is odd.
The `data` chunk size field continues to record only the logical sample bytes,
while the RIFF container size includes the pad byte. This is required for the
8-bit mono `L03_ONE_BYTE_MINUS` case.


## Auditory follow-on

Successful payload-fraction rows feed the PG-13/PG-24 blinded listening study.
The study retains test IDs, cover categories, payload fractions, and PG-14
statistics while adding human perception ratings and per-cover thresholds.
