# Corpus sources and licensing

Originally generated as a 19-file set to satisfy the M1 report's Analysis
Plan requirement of >=12 WAV files spanning dense music, speech,
sparse/quiet, synthetic tones, and near-silence, each with 8-bit/16-bit and
mono/stereo variants. All files are 44.1kHz, ~90s (some longer where a
source conversion ran long).

**This folder is trimmed to the 8 files that round-trip successfully** (see
`../../TESTING_REPORT.md` Sections 4-5 for the measured BER/failure data on
the full original 19-file set, including *why* the excluded ones fail --
mostly 8-bit quantization crushing quiet passages to literal silence, which
the parameter block has no tolerance for). The two source MP3s
(`alec_koff-blues-ballad-487408.mp3`, `alex-morgan-rock-rock-music-545498.mp3`)
are kept one level up in `TestData/` for reference, but can never work as a
cover -- the tool only accepts uncompressed PCM WAV; their WAV conversions
(`music_blues_*`, `music_rock_*` below) are the usable form.

## Real recordings (converted to WAV with ffmpeg)

- `music_blues_*.wav`, `music_rock_*.wav` -- from the pre-existing
  `TestData/alec_koff-blues-ballad-487408.mp3` and
  `TestData/alex-morgan-rock-rock-music-545498.mp3` (already part of this
  repo prior to this corpus addition; filenames match Pixabay's royalty-free
  download naming convention).
- `speech_*.wav` -- Richard Nixon's Apollo 11 congratulatory phone call
  ("The Moon Landing"), from the Internet Archive collection *Greatest
  Speeches of the 20th Century* (archive.org/details/Greatest_Speeches_of_the_20th_Century,
  listed public domain). A U.S. federal government recording; public domain
  in the United States. Source file: `TheMoonLanding.mp3`, trimmed to the
  first ~90s.

NOTE: an Archive.org item called "Calm Relaxing Piano - Collection (2020)"
was considered for the sparse/quiet category and rejected -- despite being
labeled "Public Domain" on Archive.org, its actual contents are commercially
released tracks by named artists (Ludovico Einaudi, Nils Frahm, etc.), i.e.
still under copyright. Do not use that source.

## Synthesized (no licensing concerns)

- `near_silence_*.wav` -- near-zero amplitude dither noise only.
- `sparse_quiet_*.wav` -- low-amplitude decaying plucked-note synth with
  long silent gaps between notes. Stands in for "sparse/quiet solo
  instrument" material.

Both are inherently synthetic test signals even in a from-scratch analysis
(silence isn't something you'd "record"), so generating them directly
carries no licensing question.

`synth_tone_*.wav` (pure two-tone sine mix, the M1 "synthetic tones"
category) was part of the original 19-file set but removed here -- all
four variants failed parameter-block validation (see
`../../TESTING_REPORT.md`), consistent with the M1 report's own prediction
that synthetic tones would be the hardest case for the detector.
