// ============================================================================
// wave.h
//
// Course:      CS 4463 / CS 5173 - Team 21
// Project:      Echo Hiding Audio
// Authors:      John N. Weaver and Alex W. Bryant
// GitHub:       https://github.com/John-N-Weaver/Echo-Hiding-Audio
// Created:      July 21, 2026
// Last updated: July 28, 2026
//
// Windows-compatible RIFF/WAVE structures retained from course-provided
// source material and used by the project's WAV I/O layer.
// Source: Provided by John A. Ortiz for the UTSA Steganography course.
// ============================================================================
#pragma once

#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>

// Section: Shared status and storage limits
// Consistent success/failure values simplify C-style cleanup paths, while
// the fixed chunk limit bounds memory and malformed-file traversal.
#define SUCCESS 0
#define FAILURE -1
#define MAX_CHUNKS 16

// Section: RIFF chunk header layout
// The structure mirrors the on-disk eight-byte identifier/size pair so chunk
// traversal can use exact fixed-size reads.
typedef struct
{
    DWORD chunkID;
    DWORD chunkSize;
} W_CHUNK;

// Section: Classic PCM format layout
// These first sixteen fmt bytes define how interleaved sample data must be
// interpreted and validated.
typedef struct
{
    WORD  compCode;
    WORD  numChannels;
    DWORD sampleRate;
    DWORD avgBytesPerSec;  // sampleRate * blockAlign
    WORD  blockAlign;      // numChannels * (bitsPerSample / 8)
    WORD  bitsPerSample;
} W_FORMAT;

// Section: Instructor-source compatibility placeholder
// W_DATA remains defined even though this implementation stores raw chunks
// in WaveFile, avoiding unnecessary divergence from provided course
// structures.
// Retained from the instructor-provided header for source compatibility.
typedef struct
{
} W_DATA;
