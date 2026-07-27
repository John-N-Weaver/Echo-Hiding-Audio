# Why extraction compares the cepstrum directly, not its autocorrelation

The M1 report's extraction pseudocode adds an autocorrelation step between
computing the cepstrum and comparing it at the two candidate delays:

    val_d0 = autocorrelation(cepstrum) at delay_zero
    val_d1 = autocorrelation(cepstrum) at delay_one

The stated reasoning (citing Gruhl, Lu, & Bender 1996) is that the raw
cepstral impulse is small relative to the cover signal and unreliable to
compare directly, and that autocorrelation reinforces the faint train of
impulses the echo produces at multiples of the delay into one clear peak.

That step was implemented and tested -- three variants -- against a 56-bit
known-pattern embed/extract round trip run across the project's test
corpus (`TestData/Corpus/`, covering dense music, speech, sparse/quiet,
synthetic tones, and near-silence). Results, bit errors out of 56:

| Content              | Direct cepstrum compare | Full FFT autocorrelation | Liftered autocorrelation | Linear restricted-band autocorrelation |
|-----------------------|:---:|:---:|:---:|:---:|
| near-silence (16-bit)  | 0  | 56 | 44 | 45 |
| dense music (blues)    | 2  | 31 | 35 | 36 |
| dense music (rock, stereo) | 0 | -- | -- | -- |
| synthetic tone          | 20 | 33 | 33 | 27 |
| speech                  | 3  | -- | -- | -- |
| sparse/quiet             | 0  | -- | -- | -- |
| near-silence (8-bit)     | 25 | -- | -- | -- |

Variants tried:
1. **Full FFT autocorrelation**: `R = IFFT(|FFT(cepstrum)|^2)` over the
   whole N-point cepstrum, compare R at the two delays.
2. **Liftered**: same, but the low-quefrency band (quefrency < d0/2) is
   zeroed first, since that band reflects the smooth spectral envelope
   rather than the echo and was suspected of dominating the result.
3. **Linear restricted-band**: a direct (non-circular) autocorrelation sum
   restricted to a mid-quefrency window, avoiding both the envelope region
   and the circular wraparound from the mirrored upper half of the FFT-based
   approach.

All three landed at 40-100% bit error even on content the direct comparison
recovers perfectly or near-perfectly. The most likely explanation: at this
implementation's operating point (segment length 4096, echo amplitude 0.4,
delays on the order of 44-57 samples), the cepstral impulse from the echo is
*not* small relative to the cover -- direct comparison shows a clean, large
separation (e.g. ~0.20 vs ~0.003 on near-silence). Autocorrelating in that
regime doesn't add signal; it convolves the cepstrum's other structure
(pitch/formant periodicities in speech and music, the sharp harmonic
cepstrum of a pure tone) into itself and buries the echo peak under content
that direct comparison never had to contend with in the first place.

The synthetic-tone and 8-bit-near-silence failures on direct comparison
(20/56 and 25/56) aren't a regression from this decision -- the M1 report's
own Test Corpus section names synthetic tones as "expected to be the
hardest case for the detector" precisely because of their highly regular
cepstra, and 8-bit near-silence is dominated by quantization noise at that
bit depth. Both are consistent with direct comparison behaving correctly,
not with a bug.

**Decision**: `extract_echo_region()` in `stego_echo.cpp` compares the
summed cepstrum directly at `delayZero`/`delayOne`, omitting the
autocorrelation step. This is the one deliberate remaining deviation from
the M1 report's pseudocode; everything else (parameter block, sample-rate
derived bootstrap delays, per-segment ramp, MSB-first bit packing, capacity
formula, -seg/-d0/-d1/-a/-maxbits) follows the report as written.
