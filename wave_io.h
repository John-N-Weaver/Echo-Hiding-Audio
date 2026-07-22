// ============================================================================
// wave_io.h
//
//  Project:     Echo Hiding Audio
//  Authors:     John N. Weaver
//                      Alex W. Bryant 
 //  GitHub:      https://github.com/John-N-Weaver/Echo-Hiding-Audio
 //  Created:     July 21, 2026
 //  Last Updated: July 21, 2026


// Reusable WAV file I/O for the echo-hiding steganography project.
//
// Wraps the chunk-based reader pattern from the provided WaveReader.cpp and
// adds a matching writer, so hide/extract code can load a cover WAV, modify
// its PCM samples, and write a valid RIFF/WAVE file back out.
//
// Only uncompressed PCM (compCode == 1), 8-bit unsigned or 16-bit signed,
// mono or stereo is supported -- these are the formats the assignment
// targets and the only ones WaveReader.cpp handles.
// ============================================================================
#pragma once

#include "wave.h"   // BYTE/WORD/DWORD, W_CHUNK, W_FORMAT, SUCCESS/FAILURE

// ----------------------------------------------------------------------------
// WaveFile
//
// Holds everything we need to (a) round-trip a WAV file back to disk and
// (b) read/write individual PCM samples for the echo-hiding algorithm.
//
// We keep every chunk we read (not just fmt/data) so the writer can emit an
// identical file when the payload is unchanged. The `data` chunk's payload
// is exposed through samples8 / samples16 (only one is non-NULL depending
// on bitsPerSample) for the embed/extract code to work on in place.
// ----------------------------------------------------------------------------
typedef struct
{
    // Parsed format info (copied out of the fmt chunk for convenience).
    W_FORMAT format;

    // Raw chunk table, exactly as read from disk (excluding the RIFF header
    // itself, which we synthesise on write). Index 0 is the first chunk
    // AFTER the "WAVE" fourCC.
    int      chunkCount;
    W_CHUNK  chunks[MAX_CHUNKS];
    BYTE*    chunkData[MAX_CHUNKS];  // malloc'd; freed by wave_free()

    // Indices into chunks[] for the fmt and data chunks. -1 if missing.
    int      fmtIndex;
    int      dataIndex;

    // Convenience views into chunkData[dataIndex]. Exactly one is non-NULL.
    // sampleCount is the TOTAL number of samples across all channels
    // (i.e. numFrames * numChannels).
    unsigned char* samples8;    // non-NULL iff bitsPerSample == 8
    short*         samples16;   // non-NULL iff bitsPerSample == 16
    DWORD          sampleCount;
} WaveFile;

// ----------------------------------------------------------------------------
// wave_load
//
// Reads `path` from disk into `out`. Returns SUCCESS on success, FAILURE on
// any error (file missing, not a RIFF/WAVE, unsupported bit depth, etc.).
// On failure `out` is left in a safe-to-free state.
//
// This function does NOT call exit() -- unlike the original WaveReader.cpp
// helpers -- because the stego program must degrade gracefully on bad input.
// ----------------------------------------------------------------------------
int  wave_load(const char* path, WaveFile* out);

// ----------------------------------------------------------------------------
// wave_save
//
// Writes `wf` back out to `path` as a valid RIFF/WAVE file. Any in-place
// edits to samples8/samples16 are persisted. Returns SUCCESS/FAILURE.
// ----------------------------------------------------------------------------
int  wave_save(const char* path, const WaveFile* wf);

// ----------------------------------------------------------------------------
// wave_free
//
// Releases all memory owned by `wf`. Safe to call on a zero-initialised or
// partially-loaded WaveFile.
// ----------------------------------------------------------------------------
void wave_free(WaveFile* wf);

// ----------------------------------------------------------------------------
// wave_frame_count
//
// Returns the number of sample FRAMES (one frame == one sample per channel).
// This is the value the echo-hiding capacity math is based on, not the raw
// sample count.
// ----------------------------------------------------------------------------
DWORD wave_frame_count(const WaveFile* wf);
