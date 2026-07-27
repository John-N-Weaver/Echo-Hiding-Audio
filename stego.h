// ============================================================================
// stego.h
//
//  Project:     Echo Hiding Audio
//  Authors:     John N. Weaver
//                       Alex W. Bryant
//  GitHub:      https://github.com/John-N-Weaver/Echo-Hiding-Audio
//  Created:     July 21, 2026
//  Last Updated: July 26, 2026
//
// Public interface for the echo-hiding steganography engine.
//
// This revision replaces the earlier fixed-#define parameter set with the
// design committed to in the Milestone 1 report: a 56-bit parameter block
// embedded ahead of the message body (so segment length and both echo
// delays are recovered from the stego file itself instead of being
// hardcoded), sample-rate-derived default delays, and a cepstrum-based
// extraction path. See stego.cpp / stego_echo.cpp for the implementation,
// the M1 report for the pseudocode this mirrors, and
// stego_echo_autocorrelation_attempt.md for the one place implementation
// deviates from that pseudocode and why.
// ============================================================================
#pragma once

#include "wave_io.h"

// ----------------------------------------------------------------------------
// Format version written into the parameter block. Bumped only if the block
// layout itself changes -- lets extract() refuse a stego file produced by an
// incompatible future version instead of silently misreading it.
// ----------------------------------------------------------------------------
#define STEGO_VERSION 1

// ----------------------------------------------------------------------------
// Parameter block layout (M1 report, "Write Parameter Block"):
//   byte 0      = format version
//   bytes 1..2  = segment_len (16-bit big-endian, in samples)
//   bytes 3..4  = delay_zero  (16-bit big-endian, in samples)
//   bytes 5..6  = delay_one   (16-bit big-endian, in samples)
//   -> 7 bytes = 56 bits = PARAM_BITS
//
// The block is embedded one bit per segment using a FIXED bootstrap segment
// length (BOOTSTRAP_SEG), regardless of the user's chosen -seg, so extract()
// can read segment_len itself before it knows what segment_len is.
// ----------------------------------------------------------------------------
#define PARAM_BLOCK_BYTES 7
#define PARAM_BITS        (PARAM_BLOCK_BYTES * 8)   // 56
#define BOOTSTRAP_SEG      4096

// ----------------------------------------------------------------------------
// Message-body length header: a 32-bit big-endian byte count, embedded
// immediately after the parameter-block region using the (recovered)
// message-body segment_len/delays. An explicit length beats an end-of-file
// sentinel because the payload is arbitrary binary and could legitimately
// contain any marker byte sequence.
// ----------------------------------------------------------------------------
#define LENGTH_HEADER_BITS 32

// ----------------------------------------------------------------------------
// Defaults, used whenever the corresponding CLI flag is omitted.
// ----------------------------------------------------------------------------
#define DEFAULT_SEGMENT_LEN    4096      // samples per bit
#define DEFAULT_DELAY_ZERO_MS  1.0       // encodes bit 0
#define DEFAULT_DELAY_ONE_MS   1.3       // encodes bit 1
#define DEFAULT_ECHO_AMPLITUDE 0.4       // fraction of original sample amplitude

// Fixed bootstrap delays used ONLY to decode the parameter block itself.
// Always 1.0ms / 1.3ms regardless of the message body's -d0/-d1, so extract()
// has a known, unambiguous starting point before it has recovered anything.
#define BOOTSTRAP_DELAY_ZERO_MS 1.0
#define BOOTSTRAP_DELAY_ONE_MS  1.3

// segment_len is stored in a 16-bit field, so it must fit in a WORD. The
// lower bound keeps the 64-sample ramp (see stego_echo.cpp) from consuming
// the entire segment, which would leave no stable "carrier" region for the
// cepstrum detector to lock onto.
#define MIN_SEGMENT_LEN 128
#define MAX_SEGMENT_LEN 65535

// Width, in samples, of the linear fade the echo mixer ramps through at the
// start and end of every segment (0->1 then 1->0), so the echo never
// switches on/off abruptly at a segment boundary.
#define ECHO_RAMP_LEN 64

// ----------------------------------------------------------------------------
// EchoParams
//
// The three values carried in the parameter block, plus the version byte
// that guards them. Used both when WRITING the block (hide, from resolved
// CLI values) and when READING it back (extract, recovered from the file).
// ----------------------------------------------------------------------------
typedef struct
{
    BYTE  version;
    WORD  segmentLen;   // samples per bit, message body
    WORD  delayZero;    // samples, encodes bit 0, message body
    WORD  delayOne;     // samples, encodes bit 1, message body
} EchoParams;

// ----------------------------------------------------------------------------
// echo_region_capacity_bits
//
// How many whole segments of length `segmentLen` fit in the cover starting
// at frame `regionStart`. Used for both the fixed PARAM_BITS bootstrap
// region (regionStart = 0, segmentLen = BOOTSTRAP_SEG) and the message body
// (regionStart = body start, segmentLen = the resolved -seg value).
// ----------------------------------------------------------------------------
DWORD echo_region_capacity_bits(const WaveFile* wf, DWORD regionStart, WORD segmentLen);

// ----------------------------------------------------------------------------
// embed_echo_region   [stego_echo.cpp]
//
// Embeds up to `bitCount` bits of `bits` into `wf`, one bit per segment of
// `segmentLen` samples, starting at frame `regionStart`. Bit 0 -> echo at
// `delayZero`, bit 1 -> echo at `delayOne`, scaled by `amplitude` and
// ramped by ECHO_RAMP_LEN at each segment's start/end (see stego_echo.cpp
// for the mixing math). Modifies wf->samples8/samples16 in place.
//
// Returns the number of bits actually embedded -- less than bitCount if the
// cover runs out of samples first. Never reads or writes past the buffer.
// ----------------------------------------------------------------------------
DWORD embed_echo_region(WaveFile* wf, DWORD regionStart, const BYTE* bits, DWORD bitCount,
                         WORD segmentLen, WORD delayZero, WORD delayOne, double amplitude);

// ----------------------------------------------------------------------------
// extract_echo_region   [stego_echo.cpp]
//
// Recovers up to `maxBits` bits from `wf`, one bit per segment of
// `segmentLen` samples starting at frame `regionStart`. Each bit is decided
// by computing the segment's real cepstrum and comparing its value at
// `delayZero` vs `delayOne` (see stego_echo.cpp's file header for why this
// compares the cepstrum directly rather than autocorrelating it first, as
// the M1 report's pseudocode specifies).
//
// Returns the number of bits actually recovered -- less than maxBits if the
// file runs out of samples first.
// ----------------------------------------------------------------------------
DWORD extract_echo_region(const WaveFile* wf, DWORD regionStart, BYTE* bits, DWORD maxBits,
                           WORD segmentLen, WORD delayZero, WORD delayOne);

// ----------------------------------------------------------------------------
// stego_hide / stego_extract   [stego.cpp]
//
// High-level operations invoked by the CLI. Own the parameter block, the
// length header, message-file I/O, capacity reporting, and the summary
// printed at the end -- the echo mixing/detection itself lives in
// stego_echo.cpp.
//
// A negative/zero value for segmentLen, delayZeroMs, delayOneMs, or
// amplitude means "use the default"; maxBits < 0 means "no cap". This lets
// main.cpp forward "flag not supplied" without stego_hide needing to know
// about argv.
//
// Return 0 on success, non-zero on any error (bad file, bad WAV, etc.).
// ----------------------------------------------------------------------------
int stego_hide(const char* messagePath, const char* coverPath, const char* stegoPath,
               long segmentLen, double delayZeroMs, double delayOneMs,
               double amplitude, long maxBits);

int stego_extract(const char* stegoPath, const char* outMessagePath);
