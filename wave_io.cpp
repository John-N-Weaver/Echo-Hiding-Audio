// ============================================================================
// wave_io.cpp
//
// Course:      CS 4463 / CS 5173 - Team 21
// Project:      Echo Hiding Audio
// Authors:      John N. Weaver and Alex W. Bryant
// GitHub:       https://github.com/John-N-Weaver/Echo-Hiding-Audio
// Created:      July 21, 2026
// Last updated: July 28, 2026
//
// RIFF/WAVE loading, validation, memory management, chunk preservation, and
// output writing for supported PCM cover and stego files.
// Source basis: WaveReader.cpp provided by John A. Ortiz for the UTSA
// Steganography course.
// ============================================================================
#define _CRT_SECURE_NO_WARNINGS
#include "wave_io.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "command_log.h"

// Section: Verify binary structure layout at compile time
// RIFF fields are read directly into fixed structures, so unexpected
// compiler padding would misinterpret every chunk.
static_assert(sizeof(W_CHUNK) == 8,
              "W_CHUNK must match the 8-byte RIFF chunk header.");
static_assert(sizeof(W_FORMAT) == 16,
              "W_FORMAT must match the 16-byte PCM format record.");

// KSDATAFORMAT_SUBTYPE_PCM represented in the byte order stored in a
// WAVE_FORMAT_EXTENSIBLE fmt chunk.
// Section: Identify extensible PCM precisely
// WAVE_FORMAT_EXTENSIBLE shares one container code among many subtypes, so
// the stored GUID must confirm ordinary PCM before sample editing.
static const BYTE PCM_SUBFORMAT_GUID[16] =
{
    0x01, 0x00, 0x00, 0x00,
    0x00, 0x00,
    0x10, 0x00,
    0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71
};

// ----------------------------------------------------------------------------
// read_chunk_header
// ----------------------------------------------------------------------------
// ============================================================================
// Function: read_chunk_header
// Purpose: Read one fixed-size RIFF chunk header from the current file position.
// Inputs:
//   fptr - Open binary WAV stream.
//   chunk - Destination W_CHUNK structure.
// Outputs:
//   Advances the stream and fills chunk on a complete read.
// Returns:
//   SUCCESS on an exact header read; otherwise FAILURE.
// Rationale:
//   Centralized exact-size checking simplifies safe RIFF traversal.
// ============================================================================
static int read_chunk_header(FILE* fptr, W_CHUNK* chunk)
{
    // Section: Require an exact eight-byte header
    // A partial chunk header cannot safely provide either an identifier or
    // size and therefore terminates RIFF traversal.
    if (fptr == NULL || chunk == NULL) return FAILURE;
    return (fread(chunk, 1, sizeof(W_CHUNK), fptr) == sizeof(W_CHUNK))
        ? SUCCESS
        : FAILURE;
}

// ----------------------------------------------------------------------------
// read_chunk_data
//
// Reads the declared chunk payload plus the optional RIFF pad byte. A non-NULL
// allocation is returned even for a legal zero-length chunk so NULL remains an
// unambiguous failure indicator to callers.
// ----------------------------------------------------------------------------
// ============================================================================
// Function: read_chunk_data
// Purpose: Read a RIFF chunk payload together with its optional pad byte.
// Inputs:
//   fptr - Open binary WAV stream.
//   size - Declared unpadded payload size.
//   outData - Receives an allocated buffer.
// Outputs:
//   Advances the stream and allocates caller-owned chunk storage.
// Returns:
//   SUCCESS on a complete read; otherwise FAILURE.
// Rationale:
//   Preserving pad bytes and distinguishing zero-length chunks supports
//   byte-stable WAV round trips.
// ============================================================================
static int read_chunk_data(FILE* fptr, DWORD size, BYTE** outData)
{
    // Section: Initialize ownership before size calculation
    // A null output pointer consistently means no allocation was transferred
    // after any failure.
    if (fptr == NULL || outData == NULL) return FAILURE;
    *outData = NULL;

    // Section: Include the RIFF alignment byte safely
    // Odd-sized chunks are followed by one pad byte, and 64-bit arithmetic
    // prevents overflow before conversion to process-sized allocation.
    const uint64_t padded64 = (uint64_t)size + (uint64_t)(size & 1u);
    if (padded64 > (uint64_t)SIZE_MAX)
    {
        fprintf(stderr, "Error: WAV chunk is too large for this process\n");
        return FAILURE;
    }

    const size_t padded = (size_t)padded64;
    // Section: Allocate retained raw chunk storage
    // Keeping payload and pad bytes allows wave_save to preserve unknown
    // chunks and original ordering instead of rebuilding a reduced WAV.
    BYTE* data = (BYTE*)malloc(padded > 0 ? padded : 1);
    if (data == NULL) return FAILURE;

    // Section: Read the complete padded region
    // Short reads indicate truncation, and partial chunk data must never be
    // exposed to later format or sample parsing.
    if (padded > 0 && fread(data, 1, padded, fptr) != padded)
    {
        free(data);
        return FAILURE;
    }

    *outData = data;
    return SUCCESS;
}

// ----------------------------------------------------------------------------
// wave_free
// ----------------------------------------------------------------------------
// ============================================================================
// Function: wave_free
// Purpose: Release every allocation owned by a WaveFile and reset it to a safe empty state.
// Inputs:
//   wf - WaveFile to release; NULL is accepted.
// Outputs:
//   Frees chunk buffers and clears format, indices, sample views, and
//   counts.
// Returns:
//   Nothing.
// Rationale:
//   Idempotent cleanup supports both successful loads and partially
//   initialized failure paths.
// ============================================================================
void wave_free(WaveFile* wf)
{
    // Section: Bound cleanup to the valid chunk table
    // Clamping a damaged or partially initialized count prevents cleanup
    // itself from walking beyond the fixed allocation array.
    if (wf == NULL) return;

    int count = wf->chunkCount;
    if (count < 0) count = 0;
    if (count > MAX_CHUNKS) count = MAX_CHUNKS;

    // Section: Release every retained chunk buffer
    // Each chunk is independently allocated during loading, so ownership
    // must be discharged one entry at a time.
    for (int i = 0; i < count; ++i)
    {
        free(wf->chunkData[i]);
        wf->chunkData[i] = NULL;
    }

    // Section: Return the object to a reusable empty state
    // Clearing pointers, indices, and counts makes wave_free idempotent and
    // safe after both full and partial loads.
    memset(&wf->format, 0, sizeof(wf->format));
    memset(wf->chunks, 0, sizeof(wf->chunks));
    memset(wf->chunkData, 0, sizeof(wf->chunkData));
    wf->chunkCount = 0;
    wf->fmtIndex = -1;
    wf->dataIndex = -1;
    wf->samples8 = NULL;
    wf->samples16 = NULL;
    wf->sampleCount = 0;
}

// ----------------------------------------------------------------------------
// validate_extensible_pcm
// ----------------------------------------------------------------------------
// ============================================================================
// Function: validate_extensible_pcm
// Purpose: Validate the extension fields of a WAVE_FORMAT_EXTENSIBLE PCM fmt chunk.
// Inputs:
//   path - Source filename for diagnostics.
//   fmtBytes - Raw fmt chunk bytes.
//   fmtSize - Raw fmt chunk size.
//   bitsPerSample - PCM container bit depth.
// Outputs:
//   Writes a specific diagnostic when the extension is unsupported or
//   malformed.
// Returns:
//   SUCCESS for a supported PCM subtype with matching valid/container bits;
//   otherwise FAILURE.
// Rationale:
//   The signal processor must reject non-PCM subtypes and packed formats it
//   cannot modify safely.
// ============================================================================
static int validate_extensible_pcm(const char* path,
                                   const BYTE* fmtBytes,
                                   DWORD fmtSize,
                                   WORD bitsPerSample)
{
    // Section: Verify the extensible record is complete
    // The subtype GUID and valid-bit fields occur beyond the classic 16-byte
    // format record and cannot be read from a short chunk.
    if (fmtBytes == NULL || fmtSize < 40)
    {
        fprintf(stderr,
                "Error: '%s' declares WAVE_FORMAT_EXTENSIBLE but its 'fmt ' "
                "chunk is only %u bytes (at least 40 are required)\n",
                path, fmtSize);
        return FAILURE;
    }

    // Section: Decode extension lengths explicitly
    // Little-endian byte assembly avoids alignment assumptions when reading
    // raw retained chunk bytes.
    const WORD cbSize = (WORD)(fmtBytes[16] | ((WORD)fmtBytes[17] << 8));
    const WORD validBits = (WORD)(fmtBytes[18] | ((WORD)fmtBytes[19] << 8));

    if (cbSize < 22)
    {
        fprintf(stderr,
                "Error: '%s' has an invalid extensible fmt extension size %u "
                "(at least 22 is required)\n",
                path, cbSize);
        return FAILURE;
    }

    // Section: Reject non-PCM extensible subtypes
    // Floating-point and compressed subtype samples require different
    // interpretation and must not be processed as integer PCM.
    if (memcmp(fmtBytes + 24, PCM_SUBFORMAT_GUID,
               sizeof(PCM_SUBFORMAT_GUID)) != 0)
    {
        fprintf(stderr,
                "Error: '%s' is WAVE_FORMAT_EXTENSIBLE but its subtype is not "
                "PCM\n",
                path);
        return FAILURE;
    }

    // Section: Reject packed valid-bit layouts
    // Modifying container padding as though it were audio would corrupt
    // unsupported formats silently.
    // The sample-processing code assumes every stored bit is an audio bit.
    // Reject packed containers with fewer valid bits until they are handled
    // explicitly rather than silently modifying padding bits.
    if (validBits != bitsPerSample)
    {
        fprintf(stderr,
                "Error: '%s' uses %u valid bits in a %u-bit PCM container; "
                "only equal valid/container bit depths are supported\n",
                path, validBits, bitsPerSample);
        return FAILURE;
    }

    return SUCCESS;
}

// ----------------------------------------------------------------------------
// wave_load
// ----------------------------------------------------------------------------
// ============================================================================
// Function: wave_load
// Purpose: Parse, retain, and validate a supported RIFF/WAVE PCM file.
// Inputs:
//   path - WAV file path.
//   out - Destination WaveFile.
// Outputs:
//   Allocates retained chunk buffers, populates format/sample views, and
//   reports validation errors.
// Returns:
//   SUCCESS for a fully validated WAV; otherwise FAILURE with out left safe
//   for wave_free.
// Rationale:
//   Strict parsing prevents malformed metadata from causing unsafe sample
//   access or corrupt output.
// ============================================================================
int wave_load(const char* path, WaveFile* out)
{
    // Section: Validate arguments and establish a failure-safe object
    // The destination is zeroed with sentinel indices before file access so
    // every subsequent error path can call wave_free safely.
    if (path == NULL || out == NULL)
    {
        fprintf(stderr, "Error: invalid argument passed to wave_load\n");
        return FAILURE;
    }

    memset(out, 0, sizeof(*out));
    out->fmtIndex = -1;
    out->dataIndex = -1;

    // Section: Open the WAV without text translation
    // RIFF is a byte-oriented binary format, and newline conversion would
    // invalidate chunk sizes and sample data.
    FILE* fptr = fopen(path, "rb");
    if (fptr == NULL)
    {
        fprintf(stderr, "Error: could not open WAV file '%s'\n", path);
        return FAILURE;
    }

    // Section: Validate the outer RIFF container
    // The loader confirms the RIFF identifier, declared size, and WAVE form
    // type before trusting any nested chunk metadata.
    W_CHUNK riff;
    if (read_chunk_header(fptr, &riff) != SUCCESS)
    {
        fprintf(stderr, "Error: '%s' is too short to be a WAV file\n", path);
        fclose(fptr);
        return FAILURE;
    }

    if (memcmp(&riff.chunkID, "RIFF", 4) != 0)
    {
        fprintf(stderr, "Error: '%s' is not a RIFF file\n", path);
        fclose(fptr);
        return FAILURE;
    }

    if (riff.chunkSize < 4)
    {
        fprintf(stderr, "Error: '%s' has an invalid RIFF size %u\n",
                path, riff.chunkSize);
        fclose(fptr);
        return FAILURE;
    }

    char waveTag[4];
    if (fread(waveTag, 1, sizeof(waveTag), fptr) != sizeof(waveTag) ||
        memcmp(waveTag, "WAVE", 4) != 0)
    {
        fprintf(stderr, "Error: '%s' is not a WAVE file\n", path);
        fclose(fptr);
        return FAILURE;
    }

    // Section: Traverse only the declared RIFF payload
    // Tracking remaining bytes prevents a malformed chunk size from reading
    // beyond the container or silently accepting trailing truncation.
    uint64_t remaining = (uint64_t)riff.chunkSize - 4u;

    // Section: Retain each chunk in source order
    // Unknown chunks are preserved rather than discarded, while the first
    // fmt and data chunks are indexed for processing.
    while (remaining > 0)
    {
        if (remaining < sizeof(W_CHUNK))
        {
            fprintf(stderr,
                    "Error: '%s' ends with a partial RIFF chunk header\n",
                    path);
            fclose(fptr);
            wave_free(out);
            return FAILURE;
        }

        if (out->chunkCount >= MAX_CHUNKS)
        {
            fprintf(stderr,
                    "Error: '%s' contains more than %d RIFF chunks; increase "
                    "MAX_CHUNKS or use a dynamic chunk table\n",
                    path, MAX_CHUNKS);
            fclose(fptr);
            wave_free(out);
            return FAILURE;
        }

        // Section: Validate each chunk before allocation
        // The declared payload plus optional pad byte must fit inside the
        // RIFF bytes that remain.
        const int index = out->chunkCount;
        if (read_chunk_header(fptr, &out->chunks[index]) != SUCCESS)
        {
            fprintf(stderr, "Error: truncated chunk header in '%s'\n", path);
            fclose(fptr);
            wave_free(out);
            return FAILURE;
        }
        remaining -= sizeof(W_CHUNK);

        const DWORD chunkSize = out->chunks[index].chunkSize;
        const uint64_t padded = (uint64_t)chunkSize + (uint64_t)(chunkSize & 1u);
        if (padded > remaining)
        {
            fprintf(stderr,
                    "Error: chunk %d in '%s' extends beyond the declared RIFF "
                    "container\n",
                    index, path);
            fclose(fptr);
            wave_free(out);
            return FAILURE;
        }

        if (read_chunk_data(fptr, chunkSize,
                            &out->chunkData[index]) != SUCCESS)
        {
            fprintf(stderr, "Error: failed to read chunk %d in '%s'\n",
                    index, path);
            fclose(fptr);
            wave_free(out);
            return FAILURE;
        }

        // Section: Record the processing chunks without losing others
        // Only the first format and data chunks drive sample access, but
        // every retained chunk remains available for byte-stable output.
        if (memcmp(&out->chunks[index].chunkID, "fmt ", 4) == 0 &&
            out->fmtIndex < 0)
        {
            out->fmtIndex = index;
        }
        if (memcmp(&out->chunks[index].chunkID, "data", 4) == 0 &&
            out->dataIndex < 0)
        {
            out->dataIndex = index;
        }

        ++out->chunkCount;
        remaining -= padded;
    }

    // Section: Finalize input before semantic validation
    // A close failure is reported before ownership proceeds to format
    // parsing, and all retained memory is released.
    // Section: Finalize atomically from the caller's perspective
    // If buffered output fails during close, the partial file is removed
    // rather than left looking like a valid stego WAV.
    if (fclose(fptr) != 0)
    {
        fprintf(stderr, "Error: failed to close WAV file '%s' after reading\n",
                path);
        wave_free(out);
        return FAILURE;
    }

    // Section: Require the minimum semantic chunk set
    // Without both fmt and data chunks the byte container may be RIFF/WAVE
    // but cannot supply interpretable PCM audio.
    if (out->fmtIndex < 0)
    {
        fprintf(stderr, "Error: no 'fmt ' chunk in '%s'\n", path);
        wave_free(out);
        return FAILURE;
    }
    if (out->dataIndex < 0)
    {
        fprintf(stderr, "Error: no 'data' chunk in '%s'\n", path);
        wave_free(out);
        return FAILURE;
    }

    // Section: Parse the format record from retained bytes
    // The classic fields are copied only after verifying the chunk is large
    // enough for the fixed W_FORMAT layout.
    const DWORD fmtSize = out->chunks[out->fmtIndex].chunkSize;
    if (fmtSize < sizeof(W_FORMAT))
    {
        fprintf(stderr,
                "Error: the 'fmt ' chunk in '%s' is only %u bytes; at least "
                "%u are required\n",
                path, fmtSize, (unsigned)sizeof(W_FORMAT));
        wave_free(out);
        return FAILURE;
    }

    const BYTE* fmtBytes = out->chunkData[out->fmtIndex];
    memcpy(&out->format, fmtBytes, sizeof(W_FORMAT));

    // Section: Normalize supported PCM encodings
    // Classic PCM and extensible PCM with the PCM subtype share the same
    // sample-processing path, while the raw chunk remains unchanged for
    // saving.
    if (out->format.compCode == 0xFFFE)
    {
        if (validate_extensible_pcm(path, fmtBytes, fmtSize,
                                    out->format.bitsPerSample) != SUCCESS)
        {
            wave_free(out);
            return FAILURE;
        }

        // Normalize only the parsed convenience copy. The raw fmt chunk is
        // retained byte-for-byte and written back unchanged.
        out->format.compCode = 1;
    }
    else if (out->format.compCode != 1)
    {
        fprintf(stderr,
                "Error: '%s' is not uncompressed PCM (compCode=%u)\n",
                path, out->format.compCode);
        wave_free(out);
        return FAILURE;
    }

    // Section: Enforce the implemented sample model
    // The echo code has explicit normalized accessors only for 8-bit
    // unsigned and 16-bit signed integer PCM.
    if (out->format.bitsPerSample != 8 &&
        out->format.bitsPerSample != 16)
    {
        fprintf(stderr,
                "Error: unsupported bit depth %u in '%s' "
                "(only 8 and 16 are supported)\n",
                out->format.bitsPerSample, path);
        wave_free(out);
        return FAILURE;
    }

    // Section: Enforce supported channel layouts
    // Mono and stereo are processed explicitly; rejecting other layouts
    // avoids incorrect interleaved indexing.
    if (out->format.numChannels != 1 &&
        out->format.numChannels != 2)
    {
        fprintf(stderr,
                "Error: unsupported channel count %u in '%s' "
                "(only mono and stereo are supported)\n",
                out->format.numChannels, path);
        wave_free(out);
        return FAILURE;
    }

    if (out->format.sampleRate == 0)
    {
        fprintf(stderr, "Error: '%s' has a zero sample rate\n", path);
        wave_free(out);
        return FAILURE;
    }

    // Section: Cross-check derived PCM metadata
    // blockAlign controls frame boundaries and must equal channels times
    // bytes per sample before any frame count is trusted.
    const DWORD bytesPerSample = out->format.bitsPerSample / 8u;
    const DWORD expectedBlockAlign =
        (DWORD)out->format.numChannels * bytesPerSample;

    if (out->format.blockAlign != expectedBlockAlign)
    {
        fprintf(stderr,
                "Error: malformed PCM metadata in '%s': blockAlign is %u but "
                "%u is required\n",
                path, out->format.blockAlign, expectedBlockAlign);
        wave_free(out);
        return FAILURE;
    }

    // Section: Warn on noncritical rate inconsistency
    // avgBytesPerSec is useful metadata but does not control indexing, so a
    // mismatch is visible without rejecting otherwise valid PCM.
    const uint64_t expectedBytesPerSec =
        (uint64_t)out->format.sampleRate * expectedBlockAlign;
    if (expectedBytesPerSec <= UINT32_MAX &&
        out->format.avgBytesPerSec != (DWORD)expectedBytesPerSec)
    {
        fprintf(stderr,
                "Warning: '%s' reports avgBytesPerSec=%u; expected %u from "
                "sampleRate and blockAlign\n",
                path, out->format.avgBytesPerSec,
                (DWORD)expectedBytesPerSec);
    }

    // Section: Derive complete frame and sample counts
    // The data chunk must end on a blockAlign boundary, and 64-bit
    // multiplication prevents sample-count overflow.
    const DWORD dataBytes = out->chunks[out->dataIndex].chunkSize;
    if (dataBytes % out->format.blockAlign != 0)
    {
        fprintf(stderr,
                "Error: the data chunk in '%s' contains an incomplete audio "
                "frame (%u bytes is not divisible by blockAlign %u)\n",
                path, dataBytes, out->format.blockAlign);
        wave_free(out);
        return FAILURE;
    }

    const DWORD frameCount = dataBytes / out->format.blockAlign;
    const uint64_t sampleCount64 =
        (uint64_t)frameCount * out->format.numChannels;
    if (sampleCount64 > UINT32_MAX)
    {
        fprintf(stderr, "Error: '%s' contains too many PCM samples\n", path);
        wave_free(out);
        return FAILURE;
    }
    out->sampleCount = (DWORD)sampleCount64;

    // Section: Expose one typed sample view
    // Pointing into the retained data chunk avoids copying PCM while
    // ensuring only the accessor matching the bit depth is non-null.
    if (out->format.bitsPerSample == 8)
        out->samples8 = (unsigned char*)out->chunkData[out->dataIndex];
    else
        out->samples16 = (short*)out->chunkData[out->dataIndex];

    return SUCCESS;
}

// ----------------------------------------------------------------------------
// wave_save
// ----------------------------------------------------------------------------
// ============================================================================
// Function: wave_save
// Purpose: Write a retained WaveFile structure as a complete RIFF/WAVE file.
// Inputs:
//   path - Output WAV path.
//   wf - Valid WaveFile containing retained chunks and modified samples.
// Outputs:
//   Creates/overwrites path; removes a partially written file on failure.
// Returns:
//   SUCCESS after a complete finalized write; otherwise FAILURE.
// Rationale:
//   Writing retained chunks preserves source structure while committing only
//   intended sample changes.
// ============================================================================
int wave_save(const char* path, const WaveFile* wf)
{
    // Section: Validate the retained object before creating output
    // Rejecting invalid indices and chunk counts first prevents partial
    // files caused by malformed in-memory state.
    if (path == NULL || wf == NULL)
    {
        fprintf(stderr, "Error: invalid argument passed to wave_save\n");
        return FAILURE;
    }

    if (wf->chunkCount <= 0 || wf->chunkCount > MAX_CHUNKS ||
        wf->fmtIndex < 0 || wf->fmtIndex >= wf->chunkCount ||
        wf->dataIndex < 0 || wf->dataIndex >= wf->chunkCount)
    {
        fprintf(stderr, "Error: invalid WaveFile structure passed to wave_save\n");
        return FAILURE;
    }

    // Section: Precompute the exact RIFF payload size
    // RIFF stores a 32-bit container size, so every chunk header, payload,
    // and pad byte is checked before the output file is opened.
    uint64_t payload64 = 4u;  // Four bytes for the WAVE identifier.
    // Section: Write retained chunks in their original order
    // Preserving unknown metadata and pad bytes minimizes unintended
    // differences between cover and stego files.
    for (int i = 0; i < wf->chunkCount; ++i)
    {
        const DWORD size = wf->chunks[i].chunkSize;
        const uint64_t padded = (uint64_t)size + (uint64_t)(size & 1u);

        if (padded > 0 && wf->chunkData[i] == NULL)
        {
            fprintf(stderr, "Error: chunk %d has no data buffer\n", i);
            return FAILURE;
        }

        payload64 += sizeof(W_CHUNK) + padded;
        if (payload64 > UINT32_MAX)
        {
            fprintf(stderr, "Error: WAV output exceeds the RIFF size limit\n");
            return FAILURE;
        }
    }

    // Section: Open output only after validation succeeds
    // Delaying creation avoids truncating an existing file when the WaveFile
    // structure cannot be serialized safely.
    FILE* fptr = fopen(path, "wb");
    if (fptr == NULL)
    {
        fprintf(stderr, "Error: could not open '%s' for writing\n", path);
        return FAILURE;
    }

    // Section: Write the outer RIFF/WAVE header
    // The precomputed payload size describes all following bytes and allows
    // standard WAV readers to locate the container boundary.
    static const char RIFF_ID[4] = {'R', 'I', 'F', 'F'};
    static const char WAVE_ID[4] = {'W', 'A', 'V', 'E'};
    const DWORD payload = (DWORD)payload64;

    if (fwrite(RIFF_ID, 1, sizeof(RIFF_ID), fptr) != sizeof(RIFF_ID) ||
        fwrite(&payload, sizeof(payload), 1, fptr) != 1 ||
        fwrite(WAVE_ID, 1, sizeof(WAVE_ID), fptr) != sizeof(WAVE_ID))
    {
        fprintf(stderr, "Error: failed to write RIFF header to '%s'\n", path);
        fclose(fptr);
        remove(path);
        return FAILURE;
    }

    for (int i = 0; i < wf->chunkCount; ++i)
    {
        const DWORD size = wf->chunks[i].chunkSize;
        const size_t padded = (size_t)((uint64_t)size + (size & 1u));

        if (fwrite(&wf->chunks[i], 1, sizeof(W_CHUNK), fptr) !=
            sizeof(W_CHUNK))
        {
            fprintf(stderr, "Error: failed to write chunk %d header to '%s'\n",
                    i, path);
            fclose(fptr);
            remove(path);
            return FAILURE;
        }

        if (padded > 0 &&
            fwrite(wf->chunkData[i], 1, padded, fptr) != padded)
        {
            fprintf(stderr, "Error: failed to write chunk %d data to '%s'\n",
                    i, path);
            fclose(fptr);
            remove(path);
            return FAILURE;
        }
    }

    if (fclose(fptr) != 0)
    {
        fprintf(stderr, "Error: failed to finalize '%s'\n", path);
        remove(path);
        return FAILURE;
    }

    return SUCCESS;
}

// ----------------------------------------------------------------------------
// wave_frame_count
// ----------------------------------------------------------------------------
// ============================================================================
// Function: wave_frame_count
// Purpose: Convert total interleaved sample count into audio-frame count.
// Inputs:
//   wf - WaveFile whose channel count and sample count are inspected.
// Outputs:
//   No state is modified.
// Returns:
//   sampleCount divided by numChannels, or zero for NULL/zero-channel input.
// Rationale:
//   Echo capacity is measured per time frame, not independently per channel.
// ============================================================================
DWORD wave_frame_count(const WaveFile* wf)
{
    // Section: Convert interleaved samples to time frames
    // One frame contains one sample per channel, which is the time-domain
    // unit consumed by each echo segment.
    if (wf == NULL || wf->format.numChannels == 0) return 0;
    return wf->sampleCount / wf->format.numChannels;
}
