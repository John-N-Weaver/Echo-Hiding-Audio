# PG-13 / PG-24 Auditory-Perception Evaluation

## Requirements addressed

PG-13 requires a documented point at which echo hiding becomes noticeable.

PG-24 requires exactly these auditory categories:

```text
obvious
apparent_close_listening
undetectable_without_original
```

## Prepare the study

From the project root:

```powershell
& ".\Analysis\run_pg24_prepare.bat"
```

The default successful PG-23 matrix provides four covers at six capacity
fractions, creating 24 blinded A/B pairs. Each pair contains matching 12-second
cover and stego excerpts. A/B identity and presentation order are randomized.

Outputs are written under:

```text
Analysis\pg24_results\history\<study-id>\
```

The command prints the path to `PG24 Listening Study.html`. Open that file in a
current browser. Do not open `PG24 Study Key.csv` before completing ratings.

## Complete the browser form

For every pair:

1. Listen to both files at the same volume.
2. Replay as needed.
3. Select exactly one required category.
4. Record optional notes.
5. Save and continue.

The form requires listener ID, playback device, and listening environment.

At the end, select **Download Completed Ratings CSV**.

## Summarize the ratings

Example:

```powershell
& ".\Analysis\run_pg24_summarize.bat" `
    --ratings "C:\Users\johnn\Downloads\PG24_Ratings_20260729_010000.csv"
```

For multiple listeners, repeat `--ratings`.

## Output

The summary creates:

```text
Latest PG24 Combined Ratings.csv
Latest PG24 Per-Cover Thresholds.csv
Latest PG13 PG24 Auditory Summary.txt
```

## Thresholds

For each cover:

- First `apparent_close_listening` or `obvious` fraction = PG-13 threshold
- First `obvious` fraction = immediate-noticeability threshold
- Highest `undetectable_without_original` fraction = upper undetectable result

## GO criteria

PG-13 and PG-24 become GO when every prepared pair has a valid rating, listener
metadata are complete, only the required categories are used, and the summary
reports zero validation errors.

The software prepares and validates the study. Actual auditory judgments must
come from a human listener.
