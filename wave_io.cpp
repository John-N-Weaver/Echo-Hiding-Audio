// ============================================================================
// wave_io.cpp
//
// Implementation of the reusable WAV reader/writer declared in wave_io.h.
// Modeled directly on the provided WaveReader.cpp chunk-walking pattern,
// but:
//   * returns error codes instead of calling exit() (the assignment forbids
//     crashing on bad input),
//   * keeps every chunk so we can round-trip the file on write,
//   * exposes typed sample views (unsigned char* for 8-bit, short* for 16-bit)
//     that the echo-hiding embed/extract code will mutate in place.
// ============================================================================
#define _CRT_SECURE_NO_WARNINGS
#include "wave_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ----------------------------------------------------------------------------
// read_chunk_header (internal)
//
// Reads the 8-byte chunk header (4-byte ID + 4-byte little-endian size) from
// `fptr` into `pChunk`. Returns SUCCESS on a full read, FAILURE otherwise
// (typically EOF -- used as the loop terminator).
// ----------------------------------------------------------------------------
static int read_chunk_header(FILE* fptr, W_CHUNK* pChunk)
{
    // fread returns element count; asking for 8 elements of size 1.
    size_t n = fread(pChunk, 1, 8, fptr);
    return (n == 8) ? SUCCESS : FAILURE;
}

// ----------------------------------------------------------------------------
// read_chunk_data (internal)
//
// Allocates a buffer of the requested size (padded up to WORD alignment as
// the RIFF spec requires) and reads `size` bytes into it. Returns NULL on
// allocation or read failure -- caller is responsible for cleanup on error.
// ----------------------------------------------------------------------------
static BYTE* read_chunk_data(FILE* fptr, int size)
{
    // RIFF pads odd-sized chunks with a single zero byte so the next chunk
    // header starts on an even boundary. We must consume that pad byte too,
    // otherwise the following readChunkHeader lands mid-chunk.
    int padded = (size % 2) ? (size + 1) : size;

    BYTE* ptr = (BYTE*)malloc(padded);
    if (ptr == NULL) return NULL;

    size_t n = fread(ptr, 1, padded, fptr);
    if ((int)n != padded) { free(ptr); return NULL; }

    return ptr;
}

// ----------------------------------------------------------------------------
// wave_free -- release all owned memory. Zero-safe.
// ----------------------------------------------------------------------------
void wave_free(WaveFile* wf)
{
    if (wf == NULL) return;
    for (int i = 0; i < wf->chunkCount; ++i)
    {
        if (wf->chunkData[i] != NULL)
        {
            free(wf->chunkData[i]);
            wf->chunkData[i] = NULL;
        }
    }
    wf->chunkCount = 0;
    wf->fmtIndex = -1;
    wf->dataIndex = -1;
    wf->samples8 = NULL;
    wf->samples16 = NULL;
    wf->sampleCount = 0;
}

// ----------------------------------------------------------------------------
// wave_load
//
// See header for contract. Structure mirrors WaveReader.cpp's main() but:
//   * every early exit unwinds allocations and returns FAILURE,
//   * we KEEP all chunks (needed for wave_save to reproduce the file),
//   * we validate bit depth and channel count against what our echo-hiding
//     code can actually work on.
// ----------------------------------------------------------------------------
int wave_load(const char* path, WaveFile* out)
{
    // Defensive: caller may pass an uninitialised struct. Zero it so the
    // failure-path free is safe even if we bail before touching fields.
    memset(out, 0, sizeof(*out));
    out->fmtIndex = -1;
    out->dataIndex = -1;

    FILE* fptr = fopen(path, "rb");
    if (fptr == NULL)
    {
        fprintf(stderr, "Error: could not open WAV file '%s'\n", path);
        return FAILURE;
    }

    // --- RIFF header ---------------------------------------------------------
    // First chunk is the RIFF container: "RIFF" + size + "WAVE" fourCC. We
    // read it separately from the chunk table since it's not a real payload
    // chunk -- it wraps everything else.
    W_CHUNK riff;
    if (read_chunk_header(fptr, &riff) != SUCCESS)
    {
        fprintf(stderr, "Error: '%s' is too short to be a WAV file\n", path);
        fclose(fptr); return FAILURE;
    }
    if (memcmp(&riff.chunkID, "RIFF", 4) != 0)
    {
        fprintf(stderr, "Error: '%s' is not a RIFF file\n", path);
        fclose(fptr); return FAILURE;
    }

    // The 4-byte "WAVE" tag lives inside the RIFF payload -- read + verify.
    char wave_tag[4];
    if (fread(wave_tag, 1, 4, fptr) != 4 ||
        memcmp(wave_tag, "WAVE", 4) != 0)
    {
        fprintf(stderr, "Error: '%s' is not a WAVE file\n", path);
        fclose(fptr); return FAILURE;
    }

    // --- Chunk table ---------------------------------------------------------
    // Read every remaining chunk into our table so we can rewrite the file
    // verbatim. Stop when we run out of chunks (EOF) or hit MAX_CHUNKS.
    int cnt = 0;
    while (cnt < MAX_CHUNKS)
    {
        if (read_chunk_header(fptr, &out->chunks[cnt]) != SUCCESS) break;

        out->chunkData[cnt] = read_chunk_data(fptr, out->chunks[cnt].chunkSize);
        if (out->chunkData[cnt] == NULL)
        {
            fprintf(stderr, "Error: failed to read chunk %d in '%s'\n", cnt, path);
            fclose(fptr); wave_free(out); return FAILURE;
        }

        if (memcmp(&out->chunks[cnt].chunkID, "fmt ", 4) == 0) out->fmtIndex  = cnt;
        if (memcmp(&out->chunks[cnt].chunkID, "data", 4) == 0) out->dataIndex = cnt;

        cnt++;
    }
    out->chunkCount = cnt;
    fclose(fptr);

    // --- Required chunks present? -------------------------------------------
    if (out->fmtIndex < 0)
    {
        fprintf(stderr, "Error: no 'fmt ' chunk in '%s'\n", path);
        wave_free(out); return FAILURE;
    }
    if (out->dataIndex < 0)
    {
        fprintf(stderr, "Error: no 'data' chunk in '%s'\n", path);
        wave_free(out); return FAILURE;
    }

    // Copy the first 16 bytes of the fmt chunk into our typed struct. The
    // "extra" fields present in some WAV variants are preserved inside the
    // raw chunk buffer, so wave_save will still emit them.
    memcpy(&out->format, out->chunkData[out->fmtIndex], sizeof(W_FORMAT));

    // --- Validate format against what echo hiding can handle ----------------
    if (out->format.compCode != 1)
    {
        fprintf(stderr, "Error: '%s' is not uncompressed PCM (compCode=%u)\n",
                path, out->format.compCode);
        wave_free(out); return FAILURE;
    }
    if (out->format.bitsPerSample != 8 && out->format.bitsPerSample != 16)
    {
        fprintf(stderr, "Error: unsupported bit depth %u in '%s' "
                        "(only 8 and 16 are supported)\n",
                out->format.bitsPerSample, path);
        wave_free(out); return FAILURE;
    }
    if (out->format.numChannels != 1 && out->format.numChannels != 2)
    {
        fprintf(stderr, "Error: unsupported channel count %u in '%s' "
                        "(only mono and stereo are supported)\n",
                out->format.numChannels, path);
        wave_free(out); return FAILURE;
    }

    // --- Set up sample views -------------------------------------------------
    // 8-bit WAV samples are unsigned (0..255); 16-bit are signed little-endian
    // shorts. On MSVC/x86/x64 short IS a 16-bit little-endian type so the
    // reinterpret cast is safe. We compute sample count from the data chunk
    // size (bytes) divided by the sample width in bytes.
    DWORD dataBytes = out->chunks[out->dataIndex].chunkSize;
    DWORD bytesPerSample = out->format.bitsPerSample / 8;
    out->sampleCount = dataBytes / bytesPerSample;

    if (out->format.bitsPerSample == 8)
        out->samples8  = (unsigned char*)out->chunkData[out->dataIndex];
    else
        out->samples16 = (short*)out->chunkData[out->dataIndex];

    return SUCCESS;
}

// ----------------------------------------------------------------------------
// wave_save
//
// Reassembles the RIFF/WAVE container from the parsed chunk table. We
// recompute the RIFF size from scratch rather than trusting the loaded
// value in case the caller added/removed data (future-proofing; the
// echo-hiding embed path only mutates samples in place, so size is
// unchanged there).
// ----------------------------------------------------------------------------
int wave_save(const char* path, const WaveFile* wf)
{
    FILE* fptr = fopen(path, "wb");
    if (fptr == NULL)
    {
        fprintf(stderr, "Error: could not open '%s' for writing\n", path);
        return FAILURE;
    }

    // Total payload size = 4 bytes for "WAVE" + sum of (8-byte header +
    // padded chunk size) for every stored chunk.
    DWORD payload = 4;
    for (int i = 0; i < wf->chunkCount; ++i)
    {
        DWORD sz = wf->chunks[i].chunkSize;
        if (sz % 2) sz++;                 // WORD alignment pad
        payload += 8 + sz;
    }

    // Write RIFF header.
    DWORD riffID = 0x46464952; // 'RIFF' little-endian
    DWORD waveID = 0x45564157; // 'WAVE' little-endian
    if (fwrite(&riffID, 4, 1, fptr) != 1 ||
        fwrite(&payload, 4, 1, fptr) != 1 ||
        fwrite(&waveID, 4, 1, fptr) != 1)
    {
        fprintf(stderr, "Error: failed to write RIFF header to '%s'\n", path);
        fclose(fptr); return FAILURE;
    }

    // Write each chunk verbatim, honouring WORD-alignment padding.
    for (int i = 0; i < wf->chunkCount; ++i)
    {
        DWORD sz = wf->chunks[i].chunkSize;
        DWORD padded = (sz % 2) ? (sz + 1) : sz;

        if (fwrite(&wf->chunks[i], 8, 1, fptr) != 1 ||
            fwrite(wf->chunkData[i], 1, padded, fptr) != padded)
        {
            fprintf(stderr, "Error: failed to write chunk %d to '%s'\n", i, path);
            fclose(fptr); return FAILURE;
        }
    }

    fclose(fptr);
    return SUCCESS;
}

// ----------------------------------------------------------------------------
// wave_frame_count -- convert total samples to per-channel frames.
// ----------------------------------------------------------------------------
DWORD wave_frame_count(const WaveFile* wf)
{
    if (wf == NULL || wf->format.numChannels == 0) return 0;
    return wf->sampleCount / wf->format.numChannels;
}
