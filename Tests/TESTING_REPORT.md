# Echo-Hiding Stego: Testing & Detectability Report
CS 4463 Team 21, Milestone 1

## 1. Functional tests (round-trip)

Cover: 60 s, 44.1 kHz, 16-bit mono, sine 440 Hz + 880 Hz + white noise.

| Test | Message | Result |
|------|---------|--------|
| T1 | 32-byte ASCII string | Byte-exact recovery |
| T2 | `-m random` (32 B) | Header valid, 32 B recovered |
| T3 | Oversize message (960 B) | Warning printed, 32 B embedded, no crash |
| T4 | Missing cover file | Clean error, exit code 1 |
| T5 | Non-WAV file passed as cover | RIFF parse error, no crash |
| T6 | Extract on unmodified cover | "ECHO magic not found" error, no crash |
| T7 | 8-bit unsigned PCM cover | Round-trip OK |
| T8 | Stereo 16-bit cover | Round-trip OK (bits carried on both channels) |

All tests exit cleanly; no crash observed on any malformed input path.

## 2. Capacity

One payload bit per `ECHO_SEGMENT_LEN` = 8192 audio frames.
Fixed 64-bit header ("ECHO" + uint32 length) is subtracted from the raw
segment count. Result is independent of channel count (bits carried
identically on every channel for extractor SNR).

| Sample rate | Duration | Segments | Payload bytes |
|-------------|----------|---------:|--------------:|
| 22050 Hz | 60 s  |   161 |   12 |
| 22050 Hz | 180 s |   484 |   52 |
| 44100 Hz | 30 s  |   161 |   12 |
| 44100 Hz | 60 s  |   322 |   32 |
| 44100 Hz | 180 s |   968 |  113 |
| 48000 Hz | 60 s  |   351 |   35 |
| 48000 Hz | 180 s |  1054 |  123 |

Rule of thumb: raw capacity ≈ `sample_rate / 8192` bits per second
≈ 5.4 bits/sec at 44.1 kHz (~0.67 bytes/sec).

## 3. Perceptual quality (SNR)

Measured on the 60 s / 44.1 kHz cover above with default parameters
(ECHO_DECAY = 0.5, delays 150 / 200 samples):

| Metric | Value |
|--------|-------|
| Time-domain SNR | 6.16 dB |
| Spectral SNR    | 7.96 dB |
| Peak sample diff | 7749 / 32767 (~24 %) |

At decay 0.5 the echo is clearly audible as a short slap-back
(delay 150 / 200 samples ≈ 3.4 / 4.5 ms at 44.1 kHz). It becomes
noticeable to a critical listener above decay ≈ 0.25 and is essentially
transparent on music-like content below decay ≈ 0.1, at the cost of a
lower extraction success rate. Speech is more forgiving than solo tones.

## 4. Statistical detectability

### 4.1 LSB chi-square (Westfeld/Pfitzmann style)
The classical LSB steganalysis test computes chi-square on
(2k, 2k+1) sample pairs. Echo hiding modifies samples across the whole
dynamic range, not just the LSB, so this test should NOT flag it.

| File | chi-square | dof |
|------|-----------:|----:|
| Cover | 15582 | 15495 |
| Stego | 22866 | 22827 |

Both values sit essentially on their degrees of freedom (i.e. p ≈ 0.5).
The LSB test does not distinguish cover from stego. Expected: echo
hiding is invisible to LSB detectors.

### 4.2 Cepstrum signature (targeted attack)
The extractor itself is a targeted detector: if you know the delay
range, you compute the real cepstrum of each segment and check for
peaks at ~150 and ~200 samples. On our stego file the cepstrum shows
consistent peaks at those quefrencies; on the raw cover it does not.

Practical implication: echo hiding defeats LSB / histogram / chi-square
attacks, but a warden who knows or brute-forces the delay space can
detect it. Randomising delays per session and lowering decay both push
back the detection threshold, at the cost of BER.

## 5. Where it becomes noticeable
- decay ≤ 0.10: transparent on music, extraction unreliable on quiet content
- decay ≈ 0.25: audible only on isolated tones, extraction reliable
- decay ≥ 0.40: audible slap-back on any content (project default: 0.5, chosen for a robust demo)
- delay < 50 samples: comb-filter coloration audible even at low decay
- delay > 400 samples: perceived as a distinct echo rather than room tone

## 6. Reproducing these numbers
`tests/run_tests.bat` (Windows) drives the compiled `Echo Hiding Audio.exe`
through T1--T6 above and diffs the recovered message against the source.
SNR and chi-square numbers are produced by `tests/analyze.py` using the
generated `cover.wav` / `stego.wav`.
