// ============================================================================
// wave_io.h
//
// Course:      CS 4463 / CS 5173 - Team 21
// Project:      Echo Hiding Audio
// Authors:      John N. Weaver and Alex W. Bryant
// GitHub:       https://github.com/John-N-Weaver/Echo-Hiding-Audio
// Created:      July 21, 2026
// Last updated: July 28, 2026
//
// Public WaveFile representation and WAV loading, saving, cleanup, and
// frame-count interfaces.
// Source basis: WaveReader.cpp provided by John A. Ortiz for the UTSA
// Steganography course.
// ============================================================================
#pragma once

// Section: Import on-disk WAV field definitions
// WaveFile retains the instructor-compatible primitive types and RIFF
// structures used by the implementation.
#include "wave.h"  // BYTE/WORD/DWORD, W_CHUNK, W_FORMAT, SUCCESS/FAILURE

// ----------------------------------------------------------------------------
// WaveFile
//
// Holds the parsed format, all retained RIFF chunks, and typed views of the
// PCM data. Keeping each supported chunk lets wave_save preserve the original
// chunk order, payloads, and padding while replacing only modified samples.
// ----------------------------------------------------------------------------
// Section: WaveFile ownership model
// The object owns every retained chunk buffer and exposes typed views into
// the data chunk so sample processing does not duplicate audio memory.
typedef struct
{
    // Parsed format information copied from the first 16 bytes of the fmt
    // chunk. WAVE_FORMAT_EXTENSIBLE PCM is normalized to compCode == 1 in this
    // convenience copy; the original raw fmt chunk remains unchanged.
    W_FORMAT format;

    // Raw chunk table, excluding the outer RIFF header. Index 0 is the first
    // chunk after the four-byte WAVE identifier.
    int      chunkCount;
    W_CHUNK  chunks[MAX_CHUNKS];
    BYTE*    chunkData[MAX_CHUNKS];  // malloc'd; released by wave_free()

    // Indices into chunks[] for the fmt and data chunks; -1 when absent.
    int      fmtIndex;
    int      dataIndex;

    // Convenience views into chunkData[dataIndex]. Exactly one is non-NULL.
    // sampleCount is the total number of samples across all channels.
    unsigned char* samples8;   // non-NULL iff bitsPerSample == 8
    short*         samples16;  // non-NULL iff bitsPerSample == 16
    DWORD          sampleCount;
} WaveFile;

// Section: WAV lifecycle and frame interface
// Loading establishes ownership, saving preserves retained chunks, freeing
// is safe after partial failure, and frame count supplies echo capacity.
// Reads path into out. Returns SUCCESS on success and FAILURE on any error.
// On failure, out is left in a safe-to-free state.
// ============================================================================
// Function: wave_load
// Purpose: Load and validate a supported PCM WAV.
// Inputs:
//   path - Input WAV path.
//   out - Destination WaveFile.
// Outputs:
//   Allocates retained chunk data and populates sample views.
// Returns:
//   SUCCESS on success; FAILURE on error.
// Rationale:
//   Callers need a safe reusable WAV parser instead of direct unchecked
//   binary access.
// ============================================================================
int wave_load(const char* path, WaveFile* out);

// Writes wf as a valid RIFF/WAVE file. In-place changes to samples8/samples16
// are persisted. A partially written output file is removed on failure.
// ============================================================================
// Function: wave_save
// Purpose: Save a retained WaveFile as a valid RIFF/WAVE file.
// Inputs:
//   path - Output path.
//   wf - WaveFile to write.
// Outputs:
//   Creates/overwrites the WAV and removes partial output on failure.
// Returns:
//   SUCCESS on success; FAILURE on error.
// Rationale:
//   A shared writer preserves chunk layout and commits modified PCM samples
//   safely.
// ============================================================================
int wave_save(const char* path, const WaveFile* wf);

// Releases all memory owned by wf. Safe for zero-initialized or partially
// loaded WaveFile objects.
// ============================================================================
// Function: wave_free
// Purpose: Release all memory owned by a WaveFile.
// Inputs:
//   wf - WaveFile to release; NULL and partial states are accepted.
// Outputs:
//   Frees allocations and resets the structure.
// Returns:
//   Nothing.
// Rationale:
//   Every load path requires one safe cleanup routine.
// ============================================================================
void wave_free(WaveFile* wf);

// Returns the number of sample frames, where one frame contains one sample per
// channel. Echo-hiding capacity is based on frames, not total samples.
// ============================================================================
// Function: wave_frame_count
// Purpose: Return the number of time-aligned audio frames in a WaveFile.
// Inputs:
//   wf - WaveFile to inspect.
// Outputs:
//   No data is modified.
// Returns:
//   Frame count, or zero for invalid input.
// Rationale:
//   Echo-hiding capacity depends on frames rather than aggregate channel
//   samples.
// ============================================================================
DWORD wave_frame_count(const WaveFile* wf);
