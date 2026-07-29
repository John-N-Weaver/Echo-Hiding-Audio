// ============================================================================
// stego.h
//
// Course:      CS 4463 / CS 5173 - Team 21
// Project:      Echo Hiding Audio
// Authors:      John N. Weaver and Alex W. Bryant
// GitHub:       https://github.com/John-N-Weaver/Echo-Hiding-Audio
// Created:      July 21, 2026
// Last updated: July 28, 2026
//
// Public constants and interfaces for capacity reporting, echo embedding,
// echo extraction, and high-level hide/extract operations.
// ============================================================================
#pragma once

#pragma once

// Section: Share the validated WAV representation
// Public steganography interfaces operate on WaveFile objects and fixed-
// width Windows-compatible sample types defined by the I/O layer.
#include "wave_io.h"

// ----------------------------------------------------------------------------
// Payload header format (written into the audio as the first bits of the
// hidden bitstream). Extract reads this first to know how many payload
// bits to pull out of the audio.
//
//   offset  size  field
//   0       4     magic  = 'E','C','H','O'
//   4       4     requestedPayloadBytes (uint32 little-endian)
//   -----   8     total header bytes  ==> STEGO_HEADER_BITS = 64
//
// requestedPayloadBytes preserves the complete requested message length. If
// the cover runs out, the embedder stores the prefix that fits and warns. The
// extractor compares the declared length with the WAV's available capacity,
// warns about partial recovery, and writes the complete-byte prefix.
//
// The magic acts as a sanity check on extract: if the four bytes we recover
// are not "ECHO" we tell the user this file probably has nothing hidden
// (or the parameters used to hide it don't match), instead of dumping
// garbage to a file.
// ----------------------------------------------------------------------------
// Section: Self-identifying payload header constants
// The extractor uses the magic to reject ordinary WAV files and the little-
// endian length to bound complete or partial recovery.
#define STEGO_MAGIC0 'E'
#define STEGO_MAGIC1 'C'
#define STEGO_MAGIC2 'H'
#define STEGO_MAGIC3 'O'
#define STEGO_HEADER_BYTES 8
#define STEGO_HEADER_BITS  (STEGO_HEADER_BYTES * 8)

// ----------------------------------------------------------------------------
// Echo-hiding parameters. Centralised here so the extractor uses the same
// values as the embedder -- mismatched parameters guarantee garbage output.
// These values were selected through the tuning and field tests documented
// below; change them only with corresponding round-trip regression testing.
//
//   ECHO_SEGMENT_LEN  audio frames used to carry ONE payload bit.
//                     Bigger  = more robust, less capacity.
//                     Smaller = more capacity, more audible.
//   ECHO_DELAY_ZERO   echo offset (in samples) representing a 0 bit.
//   ECHO_DELAY_ONE    echo offset (in samples) representing a 1 bit.
//   ECHO_DECAY        echo amplitude (0..1). Louder = easier to detect
//                     both by our extractor AND by the human ear.
// ----------------------------------------------------------------------------
// TUNING LOG (keep this - the assignment asks about capacity vs detectability):
//   8192 / 0.5  -> ~5 bytes per minute of audio. Too low: 5-second clips
//                  could not even hold the 64-bit header.
//   1024 / 0.5  -> correct capacity (~330 B/min at 44.1 kHz) but 7-42%
//                  bit errors on tonal/synthetic covers. A 64-bit header
//                  cannot survive that, so extraction aborted outright.
//
//   Two independent causes were measured, not guessed:
//
//   (1) CLIPPING. An echo is additive, so on a cover already mastered near
//       full scale (our sweeps are -3 dBFS) the sum exceeded +/-1.0 and the
//       sample writer clamped it. Clamping is a hard nonlinearity and it
//       destroys the cepstral structure the detector reads. The tell was
//       that RAISING ECHO_DECAY made accuracy WORSE (0.5 -> 10.7%,
//       0.8 -> 27.7% on the sweep). Fixed by ECHO_HEADROOM in
//       stego_echo.cpp, which pre-scales by 1/(1+decay) so the worst case
//       cannot clip.
//
//   (2) SEGMENT LENGTH. 1024 frames is simply too short a window for the
//       cepstrum to resolve a 150 vs 200 sample delay reliably. Measured
//       bit error rate on a 30 s sweep, with headroom applied:
//            1024 -> 17.0%      2048 -> 1.5%      4096 -> 0.0%
//       We also tried sub-window averaging and the autocepstrum; both were
//       markedly worse (26-77% BER), so they were dropped.
//
//   (3) NO ERROR PROTECTION. Even at 4096 a residual 0.7-10% bit error
//       rate remained on the harshest covers, and because the first 64
//       bits are the header, ONE flip there aborted the entire extraction
//       with "header magic mismatch" while the rest of the message was
//       intact. Fixed with ECHO_REPEAT majority voting (stego_echo.cpp).
//
//   2048 / 0.4 + headroom + 7x repetition was the first setting that gave
//       0.00% bit error everywhere, but it was chosen conservatively rather
//       than measured against the alternatives. It cost 14336 frames per
//       logical bit (~23 bytes per minute at 44.1 kHz), and in harness runs
//       that meant most real test clips could not even hold the 64-bit
//       header. So the whole grid was re-measured.
//
//   RE-TUNING RUN (13 synthetic covers x 60 s at 44.1 kHz: log sweep, 440 Hz
//   tone, white noise, multi-tone "music", quiet -34 dBFS material and an
//   amplitude-modulated speech-like signal, in 16-bit mono, 16-bit stereo,
//   8-bit mono and 8-bit stereo). Worst-case bit error rate over all 13:
//
//        SEG \ REPEAT      R=3       R=5       R=7
//        1024             11.03%     3.49%     2.44%
//        1536              8.19%     0.29%     0.00%
//        2048              0.23%     0.00%     0.00%
//
//   Three settings reach a clean 0.00%: 2048/R=5 (10240 frames per bit),
//   1536/R=7 (10752) and 2048/R=7 (14336). 2048/R=5 is the cheapest of the
//   three AND keeps the longer 2048-frame analysis window, which is what
//   makes the cepstrum able to separate a 150-sample from a 200-sample
//   delay in the first place. A shorter segment bought back by more
//   repetition was measurably worse at equal cost (1024/R=5 = 3.49% at
//   10240 frames per bit, the same budget 2048/R=5 spends for 0.00%).
//
//   FIELD RESULT / REVERSION (this run). R=5 passed every synthetic cover
//   here, but on the real test set it produced 3 wrong bytes out of 24 on
//   audiocheck's 1 Hz->44 kHz HD sweep -- a 30 s full-band log sweep that
//   spends most of its length below 1 kHz, where a 150 vs 200 sample delay
//   is hardest for the cepstrum to separate. R=7 decoded that same file
//   cleanly in the previous run. Measured reliability on REAL covers beats
//   the 40% capacity gain, so ECHO_REPEAT is back to 7 (vote 4-of-7, i.e.
//   three detector slips absorbed per logical bit instead of two).
//
//   CURRENT SETTING: 2048 / 0.4 / headroom / 7x repetition.
//   Verified at 0.00% bit error on all 13 synthetic covers, on 30 s log
//   sweeps at 44.1/48/96 kHz, and end-to-end through the CLI on 16-bit
//   mono, 16-bit stereo and 8-bit covers.
//
//   KNOWN LIMIT (measured, not a regression -- R=7 behaves the same): a PURE
//   sine tone in a LOW sample-rate cover (440 Hz at 8 kHz) decodes at chance,
//   ~50% bit error. At 8 kHz the 150/200-sample delays are 19/25 ms, and a
//   single sinusoid plus a delayed copy of itself is just another sinusoid
//   of the same frequency -- there is no spectral structure for the cepstrum
//   to read. Broadband material at the same rate (noise, music, speech)
//   decoded at 0.00%. Real audio is broadband, so this affects test tones
//   rather than practical covers, but it is a genuine boundary of the
//   technique and is reported here rather than hidden.
//
//   CAPACITY CONSEQUENCE. One logical bit costs
//   ECHO_SEGMENT_LEN * ECHO_REPEAT = 14336 sample frames, i.e. about
//   3.1 bits/sec (~23 bytes/minute) at 44.1 kHz. The 64-bit header alone
//   needs 917,504 frames, so a cover shorter than about 21 seconds at
//   44.1 kHz (or any cover with fewer frames than that, whatever its file
//   size) cannot hold even an empty payload, and the program says so
//   instead of producing a broken file. Low capacity is a known property of
//   echo hiding -- the published range is single-digit to a few tens of bits
//   per second -- and it is the direct trade for surviving detection.
// Number of consecutive segments each LOGICAL bit is written into. The
// decoder majority-votes them, which is what keeps a single detector slip
// from corrupting the 64-bit header and aborting the whole extraction.
// MUST be odd so the vote can never tie. Capacity scales as 1/ECHO_REPEAT.
// Tried at 5 (see the grid above) and reverted to 7 after a real full-band
// sweep cover mis-decoded at 5. 7 votes 4-of-7: three slips per bit absorbed.
// How many bit flips we tolerate in the 32-bit magic before declaring a
// file "not a stego file". Non-zero because the detector is statistical;
// see the tolerant compare in stego.cpp for the reasoning and the
// false-positive math.
// Section: Statistical header tolerance
// A small Hamming-distance allowance keeps isolated detector errors from
// discarding an otherwise recoverable message while retaining a low false-
// positive probability.
#define STEGO_MAGIC_TOLERANCE 4

// Section: Reliability versus capacity setting
// Seven interleaved copies permit a four-of-seven vote and survived real
// full-band sweep tests that defeated lower redundancy.
#define ECHO_REPEAT      7

// Section: Fixed acoustic encoding parameters
// Segment length, candidate delays, and decay are centralized so embedding
// and extraction cannot silently use incompatible settings.
#define ECHO_SEGMENT_LEN 2048
#define ECHO_DELAY_ZERO  150
#define ECHO_DELAY_ONE   200
#define ECHO_DECAY       0.4

// Compile-time guards keep invalid tuning combinations from producing
// undecodable or unsafe output.
// Section: Reject invalid tuning at compile time
// These relationships are structural requirements for unambiguous voting,
// bounded delay access, and nonzero stable echo gain.
static_assert((ECHO_REPEAT % 2) == 1,
              "ECHO_REPEAT must be odd for majority voting.");
static_assert(ECHO_REPEAT >= 3,
              "ECHO_REPEAT must provide meaningful redundancy.");
static_assert(ECHO_DELAY_ZERO > 0,
              "ECHO_DELAY_ZERO must be greater than zero.");
static_assert(ECHO_DELAY_ZERO < ECHO_DELAY_ONE,
              "The bit-one delay must exceed the bit-zero delay.");
static_assert(ECHO_DELAY_ONE < ECHO_SEGMENT_LEN,
              "Echo delays must fit inside one segment.");
static_assert(ECHO_DECAY > 0.0 && ECHO_DECAY < 1.0,
              "ECHO_DECAY must be between zero and one.");

// Section: Low-level capacity and signal-processing interface
// These declarations separate physical echo operations from high-level file
// and payload orchestration.
// ----------------------------------------------------------------------------
// stego_capacity_bits
//
// How many logical bits (INCLUDING the 64-bit header) this cover WAV can
// carry with the current segment length. Used for reporting, minimum-header
// validation, extraction bounds, and sizing the special -m random payload.
// Ordinary message files are not rejected or pre-trimmed using this value.
// ----------------------------------------------------------------------------
// ============================================================================
// Function: stego_capacity_bits
// Purpose: Report logical-bit capacity for the current echo and repetition settings.
// Inputs:
//   cover - Parsed cover WAV.
// Outputs:
//   No data is modified.
// Returns:
//   Capacity including the mandatory header.
// Rationale:
//   One shared capacity calculation prevents disagreement among hide,
//   extract, and reporting.
// ============================================================================
DWORD stego_capacity_bits(const WaveFile* cover);

// ----------------------------------------------------------------------------
// embed_echo   [implemented in stego_echo.cpp]
//
// Hide the first `bitCount` bits of `bits` (LSB-first within each byte)
// inside `cover`'s PCM samples by applying an echo whose delay encodes
// each bit. Modifies cover->samples8/samples16 in place.
//
// Returns the number of bits actually embedded. The caller may request the
// complete stream even when it exceeds the cover; this function safely stops
// at the physical limit and returns a value smaller than bitCount.
// ----------------------------------------------------------------------------
// ============================================================================
// Function: embed_echo
// Purpose: Embed as many requested logical bits as fit using protected echo coding.
// Inputs:
//   cover - Mutable cover WAV.
//   bits - Logical bit array.
//   bitCount - Requested logical-bit count.
// Outputs:
//   Modifies cover PCM samples.
// Returns:
//   Number of logical bits fully embedded.
// Rationale:
//   The public signal-processing boundary must stop safely at the physical
//   limit.
// ============================================================================
DWORD embed_echo(WaveFile* cover, const BYTE* bits, DWORD bitCount);

// ----------------------------------------------------------------------------
// extract_echo   [implemented in stego_echo.cpp]
//
// Recover up to `maxBits` bits from `stego`'s PCM samples and write them
// (LSB-first within each byte) into `bits`. Returns the number of bits
// actually recovered.
// ----------------------------------------------------------------------------
// ============================================================================
// Function: extract_echo
// Purpose: Recover protected logical bits from a stego WAV.
// Inputs:
//   stego - Input stego WAV.
//   bits - Destination logical-bit array.
//   maxBits - Maximum logical bits requested.
// Outputs:
//   Writes recovered bits into bits.
// Returns:
//   Number of logical bits recovered.
// Rationale:
//   A bounded public decoder supports separate header and payload recovery.
// ============================================================================
DWORD extract_echo(const WaveFile* stego, BYTE* bits, DWORD maxBits);

// Section: CLI-facing orchestration interface
// The high-level operations own paths, payload headers, warnings, I/O, and
// cleanup so main.cpp remains a strict dispatcher.
// ----------------------------------------------------------------------------
// stego_hide / stego_extract
//
// High-level operations invoked by the CLI. They own the header format,
// message-file I/O, capacity reporting, cover-exhaustion warnings, partial
// recovery behavior, and calls to embed_echo / extract_echo.
//
// messagePath == "random" (case-insensitive) means "fill with random bits
// up to capacity", per the assignment spec.
//
// Return 0 on success, non-zero on any error (bad file, bad WAV, etc.).
// ----------------------------------------------------------------------------
// ============================================================================
// Function: stego_hide
// Purpose: Perform the complete high-level hide workflow.
// Inputs:
//   messagePath - Payload file or random keyword.
//   coverPath - Input cover WAV.
//   stegoPath - Output stego WAV.
// Outputs:
//   Creates a complete or valid partial stego WAV and prints status.
// Returns:
//   0 on successful save; nonzero on error.
// Rationale:
//   The CLI needs one audited orchestration entry point for all hide
//   resources and checks.
// ============================================================================
int stego_hide   (const char* messagePath,
                  const char* coverPath,
                  const char* stegoPath);

// ============================================================================
// Function: stego_extract
// Purpose: Perform the complete high-level extraction workflow.
// Inputs:
//   stegoPath - Input stego WAV.
//   outMessagePath - Recovered-message output path.
// Outputs:
//   Creates the recovered message file and prints status.
// Returns:
//   0 on successful complete or partial recovery; nonzero on error.
// Rationale:
//   The CLI needs one audited orchestration entry point for header
//   validation and bounded recovery.
// ============================================================================
int stego_extract(const char* stegoPath,
                  const char* outMessagePath);
