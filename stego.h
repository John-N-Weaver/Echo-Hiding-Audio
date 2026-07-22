// ============================================================================
// stego.h
// Project:     Echo Hiding Audio
//  Authors:     John N. Weaver
//                       Alex W. Bryant 
 //  GitHub:      https://github.com/John-N-Weaver/Echo-Hiding-Audio
 //  Created:     July 21, 2026
 //  Last Updated: July 21, 2026
// 
// Public interface for the echo-hiding steganography engine.
//
// Step (a) -- this deliverable -- gives you: CLI parsing, capacity math,
// message-file I/O (including -m random), a fixed payload header format,
// and STUB implementations of embed_echo / extract_echo so the whole
// program compiles, runs, and does its argument validation end-to-end.
//
// Steps (b) the actual echo mixer and (c) the cepstrum-based detector
// will fill in embed_echo() and extract_echo() in stego_echo.cpp without
// touching this header or the CLI.
// ============================================================================
#pragma once

#include "wave_io.h"

// ----------------------------------------------------------------------------
// Payload header format (written into the audio as the first bits of the
// hidden bitstream). Extract reads this first to know how many payload
// bits to pull out of the audio.
//
//   offset  size  field
//   0       4     magic  = 'E','C','H','O'
//   4       4     payloadBytes (uint32 little-endian)
//   -----   8     total header bytes  ==> STEGO_HEADER_BITS = 64
//
// The magic acts as a sanity check on extract: if the four bytes we recover
// are not "ECHO" we tell the user this file probably has nothing hidden
// (or the parameters used to hide it don't match), instead of dumping
// garbage to a file.
// ----------------------------------------------------------------------------
#define STEGO_MAGIC0 'E'
#define STEGO_MAGIC1 'C'
#define STEGO_MAGIC2 'H'
#define STEGO_MAGIC3 'O'
#define STEGO_HEADER_BYTES 8
#define STEGO_HEADER_BITS  (STEGO_HEADER_BYTES * 8)

// ----------------------------------------------------------------------------
// Echo-hiding parameters. Centralised here so the extractor uses the same
// values as the embedder -- mismatched params guarantee garbage output.
// These values are placeholders sized for step (a); step (b) will tune
// them against real WAV files.
//
//   ECHO_SEGMENT_LEN  audio frames used to carry ONE payload bit.
//                     Bigger  = more robust, less capacity.
//                     Smaller = more capacity, more audible.
//   ECHO_DELAY_ZERO   echo offset (in samples) representing a 0 bit.
//   ECHO_DELAY_ONE    echo offset (in samples) representing a 1 bit.
//   ECHO_DECAY        echo amplitude (0..1). Louder = easier to detect
//                     both by our extractor AND by the human ear.
// ----------------------------------------------------------------------------
#define ECHO_SEGMENT_LEN 8192
#define ECHO_DELAY_ZERO  150
#define ECHO_DELAY_ONE   200
#define ECHO_DECAY       0.5

// ----------------------------------------------------------------------------
// stego_capacity_bits
//
// How many payload bits (INCLUDING the 64-bit header) this cover WAV can
// carry with the current segment length. Used by the CLI to warn when a
// message will be truncated.
// ----------------------------------------------------------------------------
DWORD stego_capacity_bits(const WaveFile* cover);

// ----------------------------------------------------------------------------
// embed_echo   [stubbed in step (a), implemented in step (b)]
//
// Hide the first `bitCount` bits of `bits` (LSB-first within each byte)
// inside `cover`'s PCM samples by applying an echo whose delay encodes
// each bit. Modifies cover->samples8/samples16 in place.
//
// Returns the number of bits actually embedded (may be < bitCount if the
// audio ran out).
// ----------------------------------------------------------------------------
DWORD embed_echo(WaveFile* cover, const BYTE* bits, DWORD bitCount);

// ----------------------------------------------------------------------------
// extract_echo   [stubbed in step (a), implemented in step (c)]
//
// Recover up to `maxBits` bits from `stego`'s PCM samples and write them
// (LSB-first within each byte) into `bits`. Returns the number of bits
// actually recovered.
// ----------------------------------------------------------------------------
DWORD extract_echo(const WaveFile* stego, BYTE* bits, DWORD maxBits);

// ----------------------------------------------------------------------------
// stego_hide / stego_extract
//
// High-level operations invoked by the CLI. They own the header format,
// message-file I/O, capacity check + truncation warning, and call
// embed_echo / extract_echo for the heavy lifting.
//
// messagePath == "random" (case-insensitive) means "fill with random bits
// up to capacity", per the assignment spec.
//
// Return 0 on success, non-zero on any error (bad file, bad WAV, etc.).
// ----------------------------------------------------------------------------
int stego_hide   (const char* messagePath,
                  const char* coverPath,
                  const char* stegoPath);

int stego_extract(const char* stegoPath,
                  const char* outMessagePath);
