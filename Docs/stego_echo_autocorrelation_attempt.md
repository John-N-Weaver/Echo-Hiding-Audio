# Why the Current Extractor Uses Direct Cepstral Peak Comparison

**Document type:** Historical implementation decision and current-status note<br>
**Project:** Echo Hiding Audio<br>
**Course:** CS 4463 / CS 5173, Team 21<br>
**Authors:** John N. Weaver and Alex W. Bryant<br>
**Updated:** July 29, 2026

## 1. Current status

The current extractor does **not** autocorrelate the cepstrum. It directly
compares baseline-corrected local cepstral peak scores near the two fixed echo
delays:

```text
Bit 0 delay: 150 samples
Bit 1 delay: 200 samples
```

The current implementation is located primarily in these functions in
`stego_echo.cpp`:

```text
fft_radix2()
next_pow2()
cepstrum_peak_score()
extract_echo_raw()
extract_echo()
```

The current operating configuration is:

```text
Segment length: 2,048 frames
Echo decay:     0.4
Repetition:     7 interleaved copies
Decision:       Four-of-seven majority vote
```

This document supersedes the earlier version that described the old runtime
parameter block and implied that the earlier 4,096-frame measurements were the
current configuration.

## 2. What the earlier design proposed

The Milestone 1 extraction pseudocode proposed an additional
autocorrelation-of-the-cepstrum step:

```text
cepstrum = IFFT(log(abs(FFT(segment))))
autocorrelation = IFFT(abs(FFT(cepstrum))^2)

val_d0 = autocorrelation at delay_zero
val_d1 = autocorrelation at delay_one
```

The rationale was that an echo produces repeated cepstral structure and that
autocorrelation might reinforce the delay-related pattern while suppressing
unrelated cover content.

That was a reasonable hypothesis and was implemented rather than dismissed
without testing.

## 3. Historical comparison experiment

### 3.1 Scope

During the earlier detector-development phase, four approaches were compared on
a 56-bit known-pattern round trip across covers including:

- Dense music
- Speech
- Sparse or quiet material
- Synthetic tones
- Near silence
- 8-bit and 16-bit examples

The experiment used an earlier operating point, including a 4,096-frame segment
configuration and delay values derived from the earlier runtime-parameter
design. It was **not** the final 2,048-frame, fixed 150/200-sample,
seven-repetition implementation.

### 3.2 Approaches tested

1. **Direct cepstrum comparison**
   Compare cepstral responses near the two candidate delays.

2. **Full FFT autocorrelation**
   Compute `IFFT(|FFT(cepstrum)|^2)` over the complete cepstrum and compare the
   two delay locations.

3. **Liftered FFT autocorrelation**
   Remove the low-quefrency envelope region before applying the FFT
   autocorrelation.

4. **Restricted linear autocorrelation**
   Compute a non-circular autocorrelation over a selected mid-quefrency range
   to avoid the spectral-envelope region and circular wraparound.

### 3.3 Historical bit-error observations

Bit errors out of the 56-bit known pattern:

| Cover category | Direct cepstrum | Full FFT autocorrelation | Liftered autocorrelation | Restricted linear autocorrelation |
|---|---:|---:|---:|---:|
| Near silence, 16-bit | 0 | 56 | 44 | 45 |
| Dense blues music | 2 | 31 | 35 | 36 |
| Dense rock music, stereo | 0 | Not run | Not run | Not run |
| Synthetic tone | 20 | 33 | 33 | 27 |
| Speech | 3 | Not run | Not run | Not run |
| Sparse or quiet material | 0 | Not run | Not run | Not run |
| Near silence, 8-bit | 25 | Not run | Not run | Not run |

These results showed that all tested autocorrelation variants substantially
degraded the covers on which direct cepstrum comparison already worked.

## 4. Why autocorrelation performed worse in that experiment

The most plausible explanation is that the cepstrum already contained a usable
delay response at the tested operating point. Autocorrelation therefore did not
create new information. Instead, it combined the echo response with other
cepstral structure, including:

- Pitch periodicity
- Speech formants
- Harmonic structure
- Room coloration
- Synthetic-tone periodicity
- Quantization structure in low-level 8-bit audio

Autocorrelating that complete structure can create many competing peaks and can
bury the delay feature that direct comparison can read more cleanly.

The synthetic-tone and 8-bit near-silence failures did not show that
autocorrelation was required. Autocorrelation also performed poorly on the
tested synthetic-tone case, and 8-bit near silence contains little useful signal
above quantization structure.

## 5. How the current direct detector differs from the historical version

The present implementation is not merely the old direct comparison copied
unchanged. It includes several additional reliability measures.

### 5.1 Center-segment analysis

`extract_echo_raw()` emphasizes the center of each segment so the decision is
less affected by crossfade ramps at segment boundaries.

### 5.2 Local peak search

`cepstrum_peak_score()` searches a small neighborhood around each nominal delay
instead of requiring the maximum to land in exactly one FFT bin.

### 5.3 Local baseline subtraction

Each candidate score is calculated as a local peak minus a surrounding
baseline. This reduces bias from the cover's natural cepstral envelope.

### 5.4 Channel accumulation

For stereo covers, evidence is accumulated across channels. The embedded echo
pattern is shared, while unrelated channel structure is less coherent.

### 5.5 Headroom during embedding

The encoder scales the dry-plus-echo mixture to prevent clipping. Avoiding
clipping preserves the cepstral structure that the detector needs.

### 5.6 Interleaved repetition coding

Every logical bit is written seven times in widely separated regions of the
cover. `extract_echo()` uses a four-of-seven vote. This prevents one difficult
local passage from controlling the complete logical result.

## 6. Current validation evidence

The historical table above should not be presented as a current-configuration
ablation. The autocorrelation variants have not been rerun against the final
2,048-frame, 150/200-sample, 0.4-decay, seven-copy configuration.

The current direct detector is nevertheless supported by independent
end-to-end validation:

| Validation | Current result |
|---|---:|
| Regression round trips | 10/10 exact recovered prefixes |
| Regression edge cases | 21/21 passed |
| PG-14 regression-pair analyses | 10/10 passed |
| PG-14 compared-prefix BER | Zero in 10/10 pairs |
| PG-23 controlled cases | 28/28 passed |
| PG-23 generated stego PG-14 analyses | 27/27 passed |

The missing twenty-eighth statistical pair is expected: the below-header
boundary case intentionally produces no stego WAV.

## 7. Current implementation decision

The accepted detector path is:

```text
PCM segment
    -> normalized samples
    -> FFT
    -> log magnitude
    -> inverse FFT
    -> real cepstrum
    -> local baseline-corrected score near 150 samples
    -> local baseline-corrected score near 200 samples
    -> physical-bit decision
    -> four-of-seven logical-bit vote
```

The rejected path is:

```text
real cepstrum
    -> autocorrelation of the cepstrum
    -> delay comparison
```

The rejection is based on measured historical degradation, not solely on
implementation convenience.

## 8. What would justify revisiting autocorrelation

Autocorrelation should be reconsidered only as a controlled experiment, not as
an undocumented code substitution. A valid new comparison should:

1. Use the current fixed configuration as the baseline.
2. Hold cover files and payloads constant.
3. Compare direct and autocorrelated detectors on the same physical segments.
4. Report raw physical-bit BER before majority voting.
5. Report logical-bit and byte recovery after voting.
6. Include music, speech, sparse material, near silence, broadband noise,
   sweeps, and low-sample-rate tones.
7. Rerun regression, PG-14, PG-23, and PG-24 if the accepted detector changes.

Until such an experiment shows a repeatable improvement, the direct
baseline-corrected cepstral detector remains the supported implementation.

## 9. Relationship to PG-13 and PG-24

The PG-13/PG-24 listening study measures human audibility. It does not directly
compare detector algorithms.

An audible result does not establish that autocorrelation would improve
extraction or reduce audibility. Changing the detector alone may affect
recovery, while changing the echo decay or delays changes the embedded signal
and requires a complete new validation cycle.

## 10. Conclusion

Autocorrelation of the cepstrum was implemented and measured during an earlier
development phase. In those tests, every evaluated autocorrelation variant
performed worse than direct cepstrum comparison on covers where direct
comparison was already useful.

The final implementation therefore uses local, baseline-corrected direct
cepstral peak comparison combined with clipping prevention, interleaving, and
seven-copy majority voting. Current end-to-end testing supports that decision,
while the historical table remains explicitly labeled as evidence from an
older configuration rather than a current-parameter ablation.
