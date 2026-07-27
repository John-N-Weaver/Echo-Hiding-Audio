# Echo-Hiding Stego: Testing & Detectability Report
CS 4463 Team 21 — Milestone 2

All numbers below were measured against the actual `Echo Hiding Audio.exe`
(x64 Release build), run through the real CLI, on a corpus of 19 WAV files
spanning dense music, speech, sparse/quiet, synthetic tones, and
near-silence. Nothing here is estimated or hand-calculated; every table is
parsed from actual program output. This supersedes the Milestone 1 draft of
this report, which was written against an earlier, non-final CLI/header
design.

**Repo note**: `TestData/Corpus/` has since been trimmed to the 8 files
that round-trip successfully. The two source MP3s and the seven 5-second
test WAVs are still kept in `TestData/` for reference, but neither works as
a cover -- MP3 can never work since the tool only accepts uncompressed PCM
WAV, and the 5-second files can't hold even the 56-bit parameter block (at
most 53 of 56 bits fit before the file runs out). The tables below
reference all 19 original corpus files and both bad-format cases, since
that's the actual data this report documents; `SOURCES.md` explains the
corpus trim in more detail.

A full 4-parameter sweep (segment length, both delays, amplitude, message
cap) across the whole corpus, as described in the M1 report's Analysis
Plan, is deferred to the Milestone 3 final report per the assignment's own
staging (M2 requires working code; the statistical analysis deliverable is
listed under M3). What follows is enough to demonstrate the tool works
end-to-end and to characterize its current reliability honestly.

## 1. Functional tests

| Test | Result |
|------|--------|
| Run with no arguments | Usage message printed, exit code 1, no crash |
| `-hide` with a missing cover file | Clean error, exit code 1, no crash |
| `-hide` with a non-WAV file (`.mp3`) as cover | Clean "not a RIFF file" error, exit code 1, no crash |
| `-hide` with `-seg` below the minimum (10) | Clean validation error, exit code 2, no crash |
| `-extract` on a plain WAV that was never hidden into | **Did not reject cleanly -- see Section 5** |

All five paths ran without crashing; the last one is a real correctness
issue, not a crash, and is discussed below rather than hidden.

## 2. Capacity

Capacity is one bit per `segment_len` frames in the message body, which
starts after a fixed 229,376-frame (56 segments x 4096-frame bootstrap)
region reserved for the parameter block. At the default `segment_len=4096`:

| File | Duration | Sample rate | Channels | Body capacity (bits) | Payload capacity (bytes) |
|------|---------:|------------:|---------:|----------------------:|--------------------------:|
| music_rock_16bit_stereo | 43s | 44100 | 2 | 410 | 47 |
| music_blues_16bit_mono | 90s | 44100 | 1 | 912 | 110 |
| near_silence / sparse_quiet / synth_tone (all variants) | 90s | 44100 | 1 or 2 | 912 | 110 |
| music_blues_16bit_stereo / 8bit_mono | 152s | 44100 | 1 or 2 | 1585 | 197 |
| speech_16bit_mono / 8bit_mono | 90s | 44100 | 1 | 912 / 2774* | 110 / 342* |
| speech_16bit_stereo | 262s | 44100 | 2 | 2774 | 342 |

\* `speech_16bit_mono` and `speech_8bit_mono` were both generated from the
same 90s trim, but capacity depends only on frame count, not bit depth or
channel count -- channel count does not change capacity because the same
bit stream is embedded identically on every channel, and the discrepancy in
the table for `speech_8bit_mono` (2774 bits, matching the 262s stereo file's
frame count rather than the 90s mono trim) is because the `-t 90` ffmpeg
flag only applied to the first output stream during corpus generation (see
`SOURCES.md`); the 8-bit file inherited the full untrimmed length.

Capacity is *reported*, never enforced: every hide run against
`sample_message.txt` (4193 bytes) intentionally exceeds every cover's
capacity, and the program consistently responds with the documented
warning-and-truncate behavior (Section 4) rather than aborting.

## 3. Parameter-block flexibility (the M1-spec'd `-seg`/`-d0`/`-d1`/`-a`)

This is the headline capability the M2 rewrite added: `-seg`, `-d0`, `-d1`,
and `-a` are no longer compile-time constants -- they're recorded in a
56-bit parameter block and recovered automatically at extraction. Tested by
hiding with three different configurations and extracting with **zero**
`-seg`/`-d0`/`-d1` flags:

| Config used at `-hide` | Recovered at `-extract` (no flags given) | Correct? | Message BER |
|---|---|:---:|---:|
| `-seg 2048 -d0 0.7 -d1 1.1 -a 0.35` | segment_len=2048, d0=31 samples, d1=49 samples | Yes | 3.80% |
| `-seg 8192 -d0 1.0 -d1 1.3 -a 0.4` | segment_len=8192, d0=44 samples, d1=57 samples | Yes | 1.89% |
| `-seg 1024 -d0 0.5 -d1 0.9 -a 0.3` | segment_len=1024, d0=22 samples, d1=40 samples | Yes | 3.84% |

All three recovered their parameters correctly with no user re-entry, and
the trend matches the M1 report's own prediction: smaller segments raise
capacity but leave the detector less to work with, so BER rises as
`segment_len` shrinks (1024 -> 3.84% vs 8192 -> 1.89%).

## 4. Recovery accuracy (BER) and perceptual distortion (SNR)

Every corpus file was hidden into with `sample_message.txt` at default
parameters, then extracted, then compared bit-for-bit against the portion
of the original message that fit in that cover's capacity.

| File | Hide: bits embedded | Extract | Message BER | Time-domain SNR (dB) |
|---|---:|:---:|---:|---:|
| music_blues_16bit_mono | 880 | OK | 1.02% | 8.20 |
| music_blues_16bit_stereo | 1553 | OK | 0.26% | 8.22 |
| music_blues_8bit_mono | 1553 | **failed** (param block) | -- | 8.19 |
| music_rock_16bit_stereo | 378 | OK | 0.00% | 8.06 |
| near_silence_16bit_mono | 880 | OK | 0.00% | 8.02 |
| near_silence_16bit_stereo | 880 | OK | 0.00% | 8.02 |
| near_silence_8bit_mono | 880 | **failed** (param block) | -- | n/a\*\* |
| near_silence_8bit_stereo | 880 | **failed** (param block) | -- | n/a\*\* |
| sparse_quiet_16bit_mono | 880 | OK | 0.64% | 8.06 |
| sparse_quiet_16bit_stereo | 880 | OK | 0.34% | 8.06 |
| sparse_quiet_8bit_mono | 880 | **failed** (param block) | -- | 8.28 |
| sparse_quiet_8bit_stereo | 880 | **failed** (param block) | -- | 8.28 |
| speech_16bit_mono | 880 | **failed** (param block) | -- | 8.07 |
| speech_16bit_stereo | 2742 | **failed** (param block) | -- | 8.06 |
| speech_8bit_mono | 2742 | OK | 1.17% | 8.05 |
| synth_tone_16bit_mono | 880 | **failed** (param block) | -- | 8.07 |
| synth_tone_16bit_stereo | 880 | **failed** (param block) | -- | 8.07 |
| synth_tone_8bit_mono | 880 | **failed** (param block) | -- | 8.08 |
| synth_tone_8bit_stereo | 880 | **failed** (param block) | -- | 8.08 |

\*\* `near_silence_8bit` has ~zero signal-to-noise ratio to measure against:
see Section 5.

**8 of 19 files (42%) extracted successfully end-to-end; 11 failed at
parameter-block validation** ("No hidden data found or file corrupted"),
even though `-hide` reported successfully embedding the full 56-bit
parameter block in every case. Among the files that *did* extract
successfully, message-body BER is low and consistent (0.00%-1.17%, mean
~0.43%), and SNR is stable around 8.0-8.3 dB regardless of content type
(expected: the mixer's echo strength is a fixed fraction of the original
sample regardless of what the sample contains).

The pattern in which files fail is itself informative: every 8-bit file
failed except `speech_8bit_mono`; among 16-bit files, only the two
`speech_16bit` variants and all four `synth_tone` variants failed. This is
consistent with the message-body BER numbers -- a 1-3% per-bit error rate,
applied to a 56-bit parameter block with **no error correction**, produces
a substantial chance of a single flipped bit landing in `segment_len`,
`delay_zero`, or `delay_one` and failing the range/ordering validation
outright. See Section 5.

## 5. Known limitations (found through this testing, not designed around)

**Parameter-block fragility.** The 56-bit parameter block is embedded and
read back with the same detector as the message body, and that detector
has a nonzero, content-dependent BER (Section 4). Because the block carries
raw numeric fields with no redundancy, a single flipped bit can corrupt
`segment_len` or a delay enough to fail validation, discarding an otherwise
mostly-correct extraction. This is a direct, expected consequence of the
M1 report's own parameter-block design (which specifies exactly this
validation and no redundancy scheme) -- not a deviation introduced here,
but worth naming plainly since it fails on 11 of 19 corpus files.

**False-accept on a plain (never-hidden) cover.** `-extract` run on
`music_blues_16bit_mono.wav`, an unmodified cover, did not report "no
hidden data" -- it read a parameter block that happened to satisfy the
validation checks (version byte, segment length range, delay ordering) by
chance from the file's own natural cepstral content, then proceeded to
"recover" 14 bytes of garbage under a nonsensical declared length of
1,850,743,349 bytes (caught and safely bounded by the existing
"declared length exceeds available segments" guard, so it did not crash or
runaway-allocate -- it just didn't refuse). This is inherent to the M1
report's validation design (version + range + ordering checks only, no
checksum or magic value), and is worth flagging for the M3 report's
"technical difficulties" section.

**8-bit near-silence degenerates to true silence.** `near_silence_8bit_*`
measured an SNR of 0/0 (cover and stego are byte-identical) because the
source near-silence signal (a few LSBs of dither noise in the 16-bit
version) quantizes to a constant value at 8-bit depth -- there is no AC
content left for an echo to attach to, so embedding has no effect at all.
This is exactly the degenerate case the M1 report's Test Corpus section
predicted near-silence would expose ("an echo has almost no signal to
attach to"), just realized one bit depth earlier than expected.

**Autocorrelation step omitted from extraction**, in favor of comparing the
cepstrum directly. See `stego_echo_autocorrelation_attempt.md` for the full
data -- every autocorrelation variant tried was measurably worse (40-100%
BER) than direct comparison (0-3.8% BER) at this implementation's
parameters.

## 6. Reproducing these numbers

All results above come from a Python harness that drives the real
`x64\Release\Echo Hiding Audio.exe` through hide/extract on every file in
`TestData/Corpus/`, parses the printed summaries, and compares recovered
bytes bit-for-bit against `TestData/sample_message.txt`. SNR is computed
directly from the cover/stego PCM samples (time-domain, dB). Any of the
individual runs can be reproduced manually, e.g.:

```cmd
"x64\Release\Echo Hiding Audio.exe" -hide -m TestData\sample_message.txt -c TestData\Corpus\music_blues_16bit_mono.wav -o stego.wav
"x64\Release\Echo Hiding Audio.exe" -extract -s stego.wav -o recovered.bin
```
