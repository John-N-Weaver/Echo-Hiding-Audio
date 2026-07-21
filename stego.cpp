// ============================================================================
// stego.cpp
//
// High-level hide/extract orchestration + message-file I/O + the payload
// header format. The actual echo mixer and cepstrum detector live in
// stego_echo.cpp (created in steps b/c) -- this file only calls the
// embed_echo / extract_echo entry points declared in stego.h.
//
// Everything in this file is designed to NEVER crash on bad input: every
// file open is checked, every allocation is checked, every capacity math
// path prints a warning and continues with whatever data fits.
// ============================================================================
#define _CRT_SECURE_NO_WARNINGS
#include "stego.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ----------------------------------------------------------------------------
// stego_capacity_bits
//
// Capacity = one bit per ECHO_SEGMENT_LEN sample FRAMES (a frame is one
// sample per channel; echo hiding operates on the time-domain waveform,
// which is naturally per-frame). Total bits INCLUDES the 64-bit header
// -- callers subtracting that get "payload capacity".
// ----------------------------------------------------------------------------
DWORD stego_capacity_bits(const WaveFile* cover)
{
    if (cover == NULL) return 0;
    return wave_frame_count(cover) / ECHO_SEGMENT_LEN;
}

// ============================================================================
// STUB IMPLEMENTATIONS (step a).
//
// Both leave the audio untouched / return zero-filled bits, but they DO
// respect the bitCount/maxBits contract so the CLI can be tested end-to-
// end. Step (b) replaces embed_echo; step (c) replaces extract_echo.
// ============================================================================
DWORD embed_echo(WaveFile* /*cover*/, const BYTE* /*bits*/, DWORD bitCount)
{
    fprintf(stderr, "[stub] embed_echo: would embed %u bits "
                    "(implemented in step b)\n", bitCount);
    return bitCount;   // pretend we succeeded so CLI flow can be exercised
}

DWORD extract_echo(const WaveFile* /*stego*/, BYTE* bits, DWORD maxBits)
{
    fprintf(stderr, "[stub] extract_echo: would recover up to %u bits "
                    "(implemented in step c)\n", maxBits);
    // Zero the buffer so the header check below fails loudly and predictably
    // rather than reading whatever was on the stack.
    memset(bits, 0, (maxBits + 7) / 8);
    return 0;
}

// ----------------------------------------------------------------------------
// bytes_to_bits / bits_to_bytes  (internal)
//
// LSB-first within each byte. This convention MUST match between embed
// and extract, otherwise the message comes out bit-reversed per byte.
// ----------------------------------------------------------------------------
static void bytes_to_bits(const BYTE* bytes, DWORD nBytes, BYTE* bits)
{
    for (DWORD i = 0; i < nBytes; ++i)
        for (int b = 0; b < 8; ++b)
            bits[i * 8 + b] = (bytes[i] >> b) & 1;
}
static void bits_to_bytes(const BYTE* bits, DWORD nBytes, BYTE* bytes)
{
    for (DWORD i = 0; i < nBytes; ++i)
    {
        BYTE v = 0;
        for (int b = 0; b < 8; ++b)
            v |= (bits[i * 8 + b] & 1) << b;
        bytes[i] = v;
    }
}

// ----------------------------------------------------------------------------
// read_file_all / write_file_all  (internal)
//
// Slurp / dump a whole file. Returns malloc'd buffer + length via out params,
// or NULL/0 on any error. Kept tiny because payloads are bounded by cover
// audio capacity, which is small enough to fit comfortably in memory.
// ----------------------------------------------------------------------------
static BYTE* read_file_all(const char* path, DWORD* outLen)
{
    *outLen = 0;
    FILE* f = fopen(path, "rb");
    if (f == NULL)
    {
        fprintf(stderr, "Error: could not open message file '%s'\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return NULL; }

    BYTE* buf = (BYTE*)malloc((size_t)sz);
    if (buf == NULL) { fclose(f); return NULL; }

    if ((long)fread(buf, 1, (size_t)sz, f) != sz)
    {
        fprintf(stderr, "Error: short read on '%s'\n", path);
        free(buf); fclose(f); return NULL;
    }
    fclose(f);
    *outLen = (DWORD)sz;
    return buf;
}
static int write_file_all(const char* path, const BYTE* buf, DWORD len)
{
    FILE* f = fopen(path, "wb");
    if (f == NULL)
    {
        fprintf(stderr, "Error: could not open '%s' for writing\n", path);
        return FAILURE;
    }
    if (fwrite(buf, 1, len, f) != len)
    {
        fprintf(stderr, "Error: short write on '%s'\n", path);
        fclose(f); return FAILURE;
    }
    fclose(f);
    return SUCCESS;
}

// ----------------------------------------------------------------------------
// iequals  (internal)  -- case-insensitive ASCII compare for "-m random".
// ----------------------------------------------------------------------------
static int iequals(const char* a, const char* b)
{
    if (a == NULL || b == NULL) return 0;
    while (*a && *b)
    {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
        ++a; ++b;
    }
    return *a == 0 && *b == 0;
}

// ============================================================================
// stego_hide
//
// 1. Load cover WAV (fail cleanly if bad).
// 2. Compute capacity in bits; subtract 64 for the header.
// 3. Load message bytes (or generate random bytes filling the whole capacity
//    if the user said "-m random").
// 4. If message > capacity: warn, truncate to capacity (per spec: "embed as
//    much as possible and then issue a warning if all the data is not hidden").
// 5. Build the bit-stream: [ECHO magic][payloadBytes LE][payload...].
// 6. Call embed_echo() and save the stego WAV.
// ============================================================================
int stego_hide(const char* messagePath, const char* coverPath, const char* stegoPath)
{
    // -- 1. Load cover -------------------------------------------------------
    WaveFile cover;
    if (wave_load(coverPath, &cover) != SUCCESS) return 1;

    // -- 2. Capacity ---------------------------------------------------------
    DWORD capBits = stego_capacity_bits(&cover);
    if (capBits <= STEGO_HEADER_BITS)
    {
        fprintf(stderr, "Error: cover '%s' is too small to hold even the header "
                        "(capacity %u bits, header needs %u)\n",
                coverPath, capBits, STEGO_HEADER_BITS);
        wave_free(&cover); return 2;
    }
    DWORD payloadCapBytes = (capBits - STEGO_HEADER_BITS) / 8;

    // -- 3. Message bytes ----------------------------------------------------
    BYTE* msg = NULL;
    DWORD msgLen = 0;
    if (iequals(messagePath, "random"))
    {
        // Fill the entire payload capacity with random bytes -- the spec
        // says "-m random" means the message IS just random bits.
        msgLen = payloadCapBytes;
        msg = (BYTE*)malloc(msgLen ? msgLen : 1);
        if (msg == NULL)
        {
            fprintf(stderr, "Error: out of memory allocating %u random bytes\n", msgLen);
            wave_free(&cover); return 3;
        }
        srand((unsigned)time(NULL));
        for (DWORD i = 0; i < msgLen; ++i) msg[i] = (BYTE)(rand() & 0xFF);
        printf("Generated %u random message bytes\n", msgLen);
    }
    else
    {
        msg = read_file_all(messagePath, &msgLen);
        if (msg == NULL) { wave_free(&cover); return 4; }
    }

    // -- 4. Truncate on overflow --------------------------------------------
    DWORD embedBytes = msgLen;
    if (embedBytes > payloadCapBytes)
    {
        fprintf(stderr,
            "WARNING: message is %u bytes but cover only fits %u bytes; "
            "truncating (%u bytes will NOT be hidden).\n",
            msgLen, payloadCapBytes, msgLen - payloadCapBytes);
        embedBytes = payloadCapBytes;
    }

    // -- 5. Build header + bit-stream ---------------------------------------
    // Header layout matches STEGO_MAGIC / STEGO_HEADER_BYTES in stego.h.
    BYTE header[STEGO_HEADER_BYTES];
    header[0] = STEGO_MAGIC0; header[1] = STEGO_MAGIC1;
    header[2] = STEGO_MAGIC2; header[3] = STEGO_MAGIC3;
    header[4] = (BYTE)( embedBytes        & 0xFF);
    header[5] = (BYTE)((embedBytes >>  8) & 0xFF);
    header[6] = (BYTE)((embedBytes >> 16) & 0xFF);
    header[7] = (BYTE)((embedBytes >> 24) & 0xFF);

    DWORD totalBits = (STEGO_HEADER_BYTES + embedBytes) * 8;
    BYTE* bits = (BYTE*)malloc(totalBits ? totalBits : 1);
    if (bits == NULL)
    {
        fprintf(stderr, "Error: out of memory allocating bit buffer\n");
        free(msg); wave_free(&cover); return 5;
    }
    bytes_to_bits(header, STEGO_HEADER_BYTES, bits);
    bytes_to_bits(msg,    embedBytes,         bits + STEGO_HEADER_BITS);

    // -- 6. Embed + save -----------------------------------------------------
    DWORD embedded = embed_echo(&cover, bits, totalBits);
    if (embedded < totalBits)
    {
        // Should not happen if capacity math is right, but be loud if it does
        // -- silent short-embed would corrupt every extraction from this file.
        fprintf(stderr, "WARNING: embed_echo only stored %u of %u bits\n",
                embedded, totalBits);
    }

    int rc = 0;
    if (wave_save(stegoPath, &cover) != SUCCESS)
    {
        fprintf(stderr, "Error: failed to write stego file '%s'\n", stegoPath);
        rc = 6;
    }
    else
    {
        printf("Wrote stego file '%s' (%u payload bytes embedded, capacity %u)\n",
               stegoPath, embedBytes, payloadCapBytes);
    }

    free(bits);
    free(msg);
    wave_free(&cover);
    return rc;
}

// ============================================================================
// stego_extract
//
// 1. Load stego WAV.
// 2. Pull the header bits (STEGO_HEADER_BITS) and validate the magic.
// 3. Read payloadBytes from the header; sanity-check against remaining
//    capacity so a corrupted header can't make us allocate gigabytes.
// 4. Pull payload bits, pack to bytes, write to disk.
// ============================================================================
int stego_extract(const char* stegoPath, const char* outMessagePath)
{
    WaveFile stego;
    if (wave_load(stegoPath, &stego) != SUCCESS) return 1;

    DWORD capBits = stego_capacity_bits(&stego);
    if (capBits < STEGO_HEADER_BITS)
    {
        fprintf(stderr, "Error: '%s' is too small to contain a payload header\n",
                stegoPath);
        wave_free(&stego); return 2;
    }

    // Pull header bits.
    BYTE headerBits[STEGO_HEADER_BITS];
    DWORD got = extract_echo(&stego, headerBits, STEGO_HEADER_BITS);
    if (got < STEGO_HEADER_BITS)
    {
        fprintf(stderr, "Error: could not recover payload header from '%s'\n",
                stegoPath);
        wave_free(&stego); return 3;
    }
    BYTE headerBytes[STEGO_HEADER_BYTES];
    bits_to_bytes(headerBits, STEGO_HEADER_BYTES, headerBytes);

    if (headerBytes[0] != STEGO_MAGIC0 || headerBytes[1] != STEGO_MAGIC1 ||
        headerBytes[2] != STEGO_MAGIC2 || headerBytes[3] != STEGO_MAGIC3)
    {
        fprintf(stderr,
            "Error: '%s' does not appear to contain a hidden payload "
            "(header magic mismatch). It may be a plain WAV, may have been "
            "hidden with different parameters, or may have been altered.\n",
            stegoPath);
        wave_free(&stego); return 4;
    }

    DWORD payloadBytes =  (DWORD)headerBytes[4]
                       | ((DWORD)headerBytes[5] <<  8)
                       | ((DWORD)headerBytes[6] << 16)
                       | ((DWORD)headerBytes[7] << 24);

    DWORD payloadCapBytes = (capBits - STEGO_HEADER_BITS) / 8;
    if (payloadBytes > payloadCapBytes)
    {
        fprintf(stderr,
            "Error: header claims %u payload bytes but the file only fits "
            "%u -- header is likely corrupt.\n",
            payloadBytes, payloadCapBytes);
        wave_free(&stego); return 5;
    }

    // Pull payload bits. We ask for header + payload together so extract_echo
    // sees the exact same segment layout the embedder used.
    DWORD totalBits = STEGO_HEADER_BITS + payloadBytes * 8;
    BYTE* bits = (BYTE*)malloc(totalBits ? totalBits : 1);
    if (bits == NULL)
    {
        fprintf(stderr, "Error: out of memory allocating extract buffer\n");
        wave_free(&stego); return 6;
    }
    DWORD got2 = extract_echo(&stego, bits, totalBits);
    if (got2 < totalBits)
    {
        fprintf(stderr, "Error: only recovered %u of %u expected bits\n",
                got2, totalBits);
        free(bits); wave_free(&stego); return 7;
    }

    BYTE* payload = (BYTE*)malloc(payloadBytes ? payloadBytes : 1);
    if (payload == NULL)
    {
        fprintf(stderr, "Error: out of memory allocating payload buffer\n");
        free(bits); wave_free(&stego); return 8;
    }
    bits_to_bytes(bits + STEGO_HEADER_BITS, payloadBytes, payload);

    int rc = 0;
    if (write_file_all(outMessagePath, payload, payloadBytes) != SUCCESS) rc = 9;
    else printf("Recovered %u message bytes to '%s'\n", payloadBytes, outMessagePath);

    free(payload);
    free(bits);
    wave_free(&stego);
    return rc;
}
