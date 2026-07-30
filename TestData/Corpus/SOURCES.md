# Corpus sources and licensing

This document applies to files placed under `TestData/Corpus/`. It records
provenance and licensing only. Functional success or failure must be measured
with the current merged implementation and reported in `TESTING_REPORT.md`.

Do not retain claims based on the partner prototype's 56-bit parameter block,
4,096-frame segments, or unprotected exact-bin detector as current results.

## Real recordings converted to WAV

### Blues and rock source files

Files named `music_blues_*.wav` and `music_rock_*.wav` were derived from:

- `TestData/alec_koff-blues-ballad-487408.mp3`
- `TestData/alex-morgan-rock-rock-music-545498.mp3`

The filenames indicate Pixabay downloads. Before public redistribution:

1. Preserve the original download filenames.
2. Record the source page or creator page.
3. Record the license terms that applied on the download date.
4. Record conversion commands, trimming, sample rate, channel count, and bit
   depth used to create each WAV.

MP3 files cannot be used directly by this application because it accepts
uncompressed PCM WAV input only.

### Speech source

Files named `speech_*.wav` were described as derived from Richard Nixon's
Apollo 11 congratulatory telephone call, from the Internet Archive collection
*Greatest Speeches of the 20th Century*. The source was identified as a U.S.
federal-government recording and public domain in the United States.

Record the exact Internet Archive item, source filename, downloaded date,
trim interval, and conversion command before distributing the derived files.

## Synthesized fixtures

- `near_silence_*.wav` — near-zero-amplitude dither/noise generated for tests.
- `sparse_quiet_*.wav` — low-amplitude decaying synthesized notes separated by
  long quiet gaps.
- `synth_tone_*.wav` — synthetic tones, if retained.

For generated files, preserve the script or command used to produce them and
record:

- sample rate,
- channel count,
- bit depth,
- duration,
- amplitude or signal parameters, and
- generation date.

## Measurement status

Earlier partner documentation reported that a trimmed subset round-tripped
under a different implementation. Those results are historical and must not be
presented as evidence for the merged version.

After the final executable is built:

1. Run `Tests\update_manifest.ps1`.
2. Run `Tests\run_tests.bat`.
3. Record per-file results in `TESTING_REPORT.md`.
4. Keep failed or unsupported fixtures when they provide useful negative tests;
   label them clearly in the manifest rather than deleting them solely because
   they fail.
