// ============================================================================
// stego.cpp
//
//  Project:     Echo Hiding Audio
//  Authors:     John N. Weaver
//                       Alex W. Bryant
//  GitHub:      https://github.com/John-N-Weaver/Echo-Hiding-Audio
//  Created:     July 21, 2026
//  Last Updated: July 26, 2026
//
// High-level hide/extract orchestration: the 56-bit parameter block, the
// 32-bit length header, message-file I/O, capacity reporting, and the
// summaries printed at the end. The echo mixer and cepstrum-based detector
// live in stego_echo.cpp; this file only calls
// embed_echo_region()/extract_echo_region() and owns the surrounding format.
//
// Everything here is designed to NEVER crash on bad input: every file open
// is checked, every allocation is checked, and a message larger than the
// cover produces a warning (never a truncated/rewritten length header,
// never an abort) rather than a crash -- per the assignment's "do not do
// capacity checks to determine if a message will fit" instruction and the
// M1 report's explicit rationale for keeping the declared length at the
// full requested size even on a short embed.
// ============================================================================
#define _CRT_SECURE_NO_WARNINGS
#include "stego.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

// ----------------------------------------------------------------------------
// round_to_word -- ms-to-samples conversions and 16-bit parameter-block
// fields all funnel through here so out-of-range values clamp instead of
// wrapping silently.
// ----------------------------------------------------------------------------
static WORD round_to_word(double v)
{
    if (v < 0.0) v = 0.0;
    double r = floor(v + 0.5);
    if (r > 65535.0) r = 65535.0;
    return (WORD)r;
}

// ----------------------------------------------------------------------------
// bytes_to_bits_msb / bits_to_bytes_msb  (internal)
//
// MSB-first within each byte, per the M1 report's pseudocode ("Convert
// message bytes to a stream of individual bits (MSB first per byte)"). Must
// match between embed and extract or every recovered byte comes out
// bit-reversed.
// ----------------------------------------------------------------------------
static void bytes_to_bits_msb(const BYTE* bytes, DWORD nBytes, BYTE* bits)
{
    for (DWORD i = 0; i < nBytes; ++i)
        for (int b = 0; b < 8; ++b)
            bits[i * 8 + b] = (bytes[i] >> (7 - b)) & 1;
}
static void bits_to_bytes_msb(const BYTE* bits, DWORD nBytes, BYTE* bytes)
{
    for (DWORD i = 0; i < nBytes; ++i)
    {
        BYTE v = 0;
        for (int b = 0; b < 8; ++b)
            v |= (bits[i * 8 + b] & 1) << (7 - b);
        bytes[i] = v;
    }
}

// ----------------------------------------------------------------------------
// read_file_all / write_file_all  (internal)
//
// Return SUCCESS/FAILURE explicitly rather than signalling failure through
// a NULL buffer pointer -- a genuinely empty (0-byte) message file is valid
// input, not an error, and NULL was ambiguous between the two.
// ----------------------------------------------------------------------------
static int read_file_all(const char* path, BYTE** outBuf, DWORD* outLen)
{
    *outBuf = NULL; *outLen = 0;
    FILE* f = fopen(path, "rb");
    if (f == NULL)
    {
        fprintf(stderr, "Error: could not open message file '%s'\n", path);
        return FAILURE;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return FAILURE; }
    if (sz == 0) { fclose(f); return SUCCESS; }   // valid empty message

    BYTE* buf = (BYTE*)malloc((size_t)sz);
    if (buf == NULL) { fclose(f); return FAILURE; }

    if ((long)fread(buf, 1, (size_t)sz, f) != sz)
    {
        fprintf(stderr, "Error: short read on '%s'\n", path);
        free(buf); fclose(f); return FAILURE;
    }
    fclose(f);
    *outBuf = buf; *outLen = (DWORD)sz;
    return SUCCESS;
}
static int write_file_all(const char* path, const BYTE* buf, DWORD len)
{
    FILE* f = fopen(path, "wb");
    if (f == NULL)
    {
        fprintf(stderr, "Error: could not open '%s' for writing\n", path);
        return FAILURE;
    }
    if (len > 0 && fwrite(buf, 1, len, f) != len)
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

// ----------------------------------------------------------------------------
// warn_if_exists  (internal)
//
// The M1 report lists "output filename ... does not unintentionally
// overwrite an existing file unless overwriting is allowed" as something
// the program validates. There's no interactive confirmation in a
// command-line tool meant to run unattended through parameter sweeps, so
// "overwriting is allowed" here means: warn loudly, then proceed.
// ----------------------------------------------------------------------------
static void warn_if_exists(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (f != NULL)
    {
        fclose(f);
        fprintf(stderr, "Warning: '%s' already exists and will be overwritten.\n", path);
    }
}

// ============================================================================
// stego_hide
// ============================================================================
int stego_hide(const char* messagePath, const char* coverPath, const char* stegoPath,
               long segmentLenArg, double delayZeroMsArg, double delayOneMsArg,
               double amplitudeArg, long maxBitsArg)
{
    // -- 1. Load cover --------------------------------------------------------
    WaveFile cover;
    if (wave_load(coverPath, &cover) != SUCCESS) return 1;

    // -- 2. Resolve echo parameters (defaults for anything not supplied) -----
    double segArg = (segmentLenArg > 0) ? (double)segmentLenArg : (double)DEFAULT_SEGMENT_LEN;
    double dzMs   = (delayZeroMsArg > 0.0) ? delayZeroMsArg : DEFAULT_DELAY_ZERO_MS;
    double d1Ms   = (delayOneMsArg  > 0.0) ? delayOneMsArg  : DEFAULT_DELAY_ONE_MS;
    double amp    = (amplitudeArg   >= 0.0) ? amplitudeArg  : DEFAULT_ECHO_AMPLITUDE;

    DWORD sampleRate = cover.format.sampleRate;
    WORD  d0 = round_to_word(dzMs / 1000.0 * sampleRate);
    WORD  d1 = round_to_word(d1Ms / 1000.0 * sampleRate);

    if (segArg < MIN_SEGMENT_LEN || segArg > MAX_SEGMENT_LEN)
    {
        fprintf(stderr, "Error: -seg must be between %d and %d samples (got %.0f)\n",
                MIN_SEGMENT_LEN, MAX_SEGMENT_LEN, segArg);
        wave_free(&cover); return 2;
    }
    WORD segLen = (WORD)segArg;
    if (d1 <= d0)
    {
        fprintf(stderr, "Error: -d1 (%.3fms = %u samples) must be greater than "
                        "-d0 (%.3fms = %u samples)\n", d1Ms, d1, dzMs, d0);
        wave_free(&cover); return 2;
    }
    if (segLen <= d1)
    {
        fprintf(stderr, "Error: -seg (%u samples) must be greater than -d1 (%u samples)\n",
                segLen, d1);
        wave_free(&cover); return 2;
    }

    // Fixed bootstrap delays -- ALWAYS 1.0ms/1.3ms, regardless of the
    // message body's -d0/-d1, so extract() has a known starting point.
    WORD bootD0 = round_to_word(BOOTSTRAP_DELAY_ZERO_MS / 1000.0 * sampleRate);
    WORD bootD1 = round_to_word(BOOTSTRAP_DELAY_ONE_MS  / 1000.0 * sampleRate);

    // -- 3. Capacity (reported, never enforced) -------------------------------
    DWORD bodyStart   = (DWORD)PARAM_BITS * BOOTSTRAP_SEG;
    DWORD maxBodyBits = echo_region_capacity_bits(&cover, bodyStart, segLen);
    DWORD maxMsgBytes = (maxBodyBits > LENGTH_HEADER_BITS) ? (maxBodyBits - LENGTH_HEADER_BITS) / 8 : 0;

    // -- 4. Message bytes (never truncated in memory) -------------------------
    BYTE* msg = NULL;
    DWORD msgLen = 0;
    if (iequals(messagePath, "random"))
    {
        msgLen = maxMsgBytes;
        msg = (BYTE*)malloc(msgLen ? msgLen : 1);
        if (msg == NULL)
        {
            fprintf(stderr, "Error: out of memory allocating %u random bytes\n", msgLen);
            wave_free(&cover); return 3;
        }
        srand((unsigned)time(NULL));
        for (DWORD i = 0; i < msgLen; ++i) msg[i] = (BYTE)(rand() & 0xFF);
        printf("Generated %u random message bytes (full reported capacity)\n", msgLen);
    }
    else
    {
        if (read_file_all(messagePath, &msg, &msgLen) != SUCCESS) { wave_free(&cover); return 4; }
    }

    // -- 5. Capacity report (informational only -- never trims the message) --
    DWORD totalBitsRequested = (msgLen + 4) * 8;   // 32-bit length header + full message
    if (totalBitsRequested > maxBodyBits)
    {
        fprintf(stderr,
            "WARNING: Message exceeds cover capacity: only %u of %u bits will fit\n",
            maxBodyBits, totalBitsRequested);
    }

    // -maxbits caps the MESSAGE portion only; the length header is always
    // attempted in full since it's fixed-size structural overhead.
    DWORD messageBitsRequested = msgLen * 8;
    DWORD messageBitsAllowed   = messageBitsRequested;
    if (maxBitsArg >= 0 && (DWORD)maxBitsArg < messageBitsAllowed)
        messageBitsAllowed = (DWORD)maxBitsArg;

    // -- 6. Write the 56-bit parameter block (fixed bootstrap segmentation) --
    BYTE paramBytes[PARAM_BLOCK_BYTES];
    paramBytes[0] = STEGO_VERSION;
    paramBytes[1] = (BYTE)((segLen >> 8) & 0xFF); paramBytes[2] = (BYTE)(segLen & 0xFF);
    paramBytes[3] = (BYTE)((d0     >> 8) & 0xFF); paramBytes[4] = (BYTE)(d0     & 0xFF);
    paramBytes[5] = (BYTE)((d1     >> 8) & 0xFF); paramBytes[6] = (BYTE)(d1     & 0xFF);
    BYTE paramBits[PARAM_BITS];
    bytes_to_bits_msb(paramBytes, PARAM_BLOCK_BYTES, paramBits);

    DWORD paramEmbedded = embed_echo_region(&cover, 0, paramBits, PARAM_BITS, BOOTSTRAP_SEG, bootD0, bootD1, amp);

    DWORD bodyBitsAttempted = 0, bodyBitsEmbedded = 0, messageBitsEmbedded = 0;
    if (paramEmbedded < PARAM_BITS)
    {
        // Smallest failure case: the cover can't even hold the parameter
        // block. Report it and skip the body -- there is nowhere to put it.
        fprintf(stderr,
            "WARNING: Cover too small to hold the parameter block (embedded %u of %u bits); "
            "no message body was written.\n", paramEmbedded, (unsigned)PARAM_BITS);
    }
    else
    {
        // -- 7. Length header (full requested msgLen, never the truncated
        //       embedded count -- rewriting it would make a partial embed
        //       look complete, which conceals exactly the failure mode this
        //       analysis is meant to characterize) + message bits ----------
        BYTE lenHeader[4];
        lenHeader[0] = (BYTE)((msgLen >> 24) & 0xFF);
        lenHeader[1] = (BYTE)((msgLen >> 16) & 0xFF);
        lenHeader[2] = (BYTE)((msgLen >>  8) & 0xFF);
        lenHeader[3] = (BYTE)( msgLen        & 0xFF);

        bodyBitsAttempted = LENGTH_HEADER_BITS + messageBitsAllowed;
        BYTE* bodyBits = (BYTE*)malloc(bodyBitsAttempted ? bodyBitsAttempted : 1);
        BYTE* allMsgBits = (BYTE*)malloc(messageBitsRequested ? messageBitsRequested : 1);
        if (bodyBits == NULL || allMsgBits == NULL)
        {
            fprintf(stderr, "Error: out of memory allocating bit buffer\n");
            free(bodyBits); free(allMsgBits); free(msg); wave_free(&cover); return 5;
        }
        bytes_to_bits_msb(lenHeader, 4, bodyBits);
        bytes_to_bits_msb(msg, msgLen, allMsgBits);
        memcpy(bodyBits + LENGTH_HEADER_BITS, allMsgBits, messageBitsAllowed);
        free(allMsgBits);

        bodyBitsEmbedded = embed_echo_region(&cover, bodyStart, bodyBits, bodyBitsAttempted, segLen, d0, d1, amp);
        free(bodyBits);

        messageBitsEmbedded = (bodyBitsEmbedded > LENGTH_HEADER_BITS) ? (bodyBitsEmbedded - LENGTH_HEADER_BITS) : 0;
        if (bodyBitsEmbedded < bodyBitsAttempted)
        {
            fprintf(stderr,
                "WARNING: Cover exhausted: embedded %u of %u message bits\n",
                messageBitsEmbedded, messageBitsAllowed);
        }
    }

    // -- 8. Save ---------------------------------------------------------------
    warn_if_exists(stegoPath);
    int rc = 0;
    if (wave_save(stegoPath, &cover) != SUCCESS)
    {
        fprintf(stderr, "Error: failed to write stego file '%s'\n", stegoPath);
        rc = 6;
    }
    else
    {
        // -- 9. Summary ----------------------------------------------------
        double reqPct = (maxBodyBits > 0) ? (100.0 * totalBitsRequested / maxBodyBits) : 0.0;
        double achPct = (maxBodyBits > 0) ? (100.0 * (double)(PARAM_BITS + bodyBitsEmbedded) / (PARAM_BITS + maxBodyBits)) : 0.0;
        printf("\n--- Hide summary ---\n");
        printf("Stego file:            %s\n", stegoPath);
        printf("Parameters used:       segment_len=%u samples, d0=%.3fms (%u samples), "
               "d1=%.3fms (%u samples), amplitude=%.3f\n", segLen, dzMs, d0, d1Ms, d1, amp);
        if (maxBitsArg >= 0) printf("Message bit cap:       -maxbits %ld\n", maxBitsArg);
        printf("Parameter block:       %u of %u bits embedded\n", paramEmbedded, (unsigned)PARAM_BITS);
        printf("Message bits:          requested %u, allowed (after -maxbits) %u, embedded %u\n",
               messageBitsRequested, messageBitsAllowed, messageBitsEmbedded);
        printf("Body capacity:         %u bits (%u bytes payload capacity)\n", maxBodyBits, maxMsgBytes);
        printf("Capacity used:         %.1f%% requested, %.1f%% achieved\n", reqPct, achPct);
        printf("Status:                %s\n",
               (paramEmbedded == PARAM_BITS && bodyBitsEmbedded == bodyBitsAttempted)
               ? "completed" : "stopped early -- cover exhausted");
    }

    free(msg);
    wave_free(&cover);
    return rc;
}

// ============================================================================
// stego_extract
// ============================================================================
int stego_extract(const char* stegoPath, const char* outMessagePath)
{
    WaveFile stego;
    if (wave_load(stegoPath, &stego) != SUCCESS) return 1;

    DWORD sampleRate = stego.format.sampleRate;
    WORD  bootD0 = round_to_word(BOOTSTRAP_DELAY_ZERO_MS / 1000.0 * sampleRate);
    WORD  bootD1 = round_to_word(BOOTSTRAP_DELAY_ONE_MS  / 1000.0 * sampleRate);

    // -- 1. Read the 56-bit parameter block (fixed bootstrap segmentation) ---
    BYTE paramBits[PARAM_BITS];
    DWORD gotParam = extract_echo_region(&stego, 0, paramBits, PARAM_BITS, BOOTSTRAP_SEG, bootD0, bootD1);
    if (gotParam < PARAM_BITS)
    {
        fprintf(stderr, "Error: No hidden data found or file corrupted "
                        "('%s' is too small to contain a parameter block)\n", stegoPath);
        wave_free(&stego); return 2;
    }
    BYTE paramBytes[PARAM_BLOCK_BYTES];
    bits_to_bytes_msb(paramBits, PARAM_BLOCK_BYTES, paramBytes);

    EchoParams p;
    p.version    = paramBytes[0];
    p.segmentLen = (WORD)((paramBytes[1] << 8) | paramBytes[2]);
    p.delayZero  = (WORD)((paramBytes[3] << 8) | paramBytes[4]);
    p.delayOne   = (WORD)((paramBytes[5] << 8) | paramBytes[6]);

    int paramsValid = (p.version == STEGO_VERSION)
                    && (p.segmentLen >= MIN_SEGMENT_LEN && p.segmentLen <= MAX_SEGMENT_LEN)
                    && (p.delayOne > p.delayZero)
                    && (p.segmentLen > p.delayOne);
    if (!paramsValid)
    {
        fprintf(stderr, "Error: No hidden data found or file corrupted "
                        "(parameter block failed validation)\n");
        wave_free(&stego); return 3;
    }

    // -- 2. Length header (first 32 body bits) --------------------------------
    DWORD bodyStart = (DWORD)PARAM_BITS * BOOTSTRAP_SEG;
    BYTE headerBits[LENGTH_HEADER_BITS];
    DWORD gotHeader = extract_echo_region(&stego, bodyStart, headerBits, LENGTH_HEADER_BITS,
                                           p.segmentLen, p.delayZero, p.delayOne);
    if (gotHeader < LENGTH_HEADER_BITS)
    {
        fprintf(stderr, "Error: No hidden data found or file corrupted "
                        "(stego file too small to contain a length header)\n");
        wave_free(&stego); return 4;
    }
    BYTE lenBytes[4];
    bits_to_bytes_msb(headerBits, 4, lenBytes);
    DWORD messageLength = ((DWORD)lenBytes[0] << 24) | ((DWORD)lenBytes[1] << 16)
                         | ((DWORD)lenBytes[2] <<  8) |  (DWORD)lenBytes[3];

    if (messageLength == 0)
    {
        fprintf(stderr, "Error: No hidden data found or file corrupted "
                        "(declared message length is zero)\n");
        wave_free(&stego); return 5;
    }

    // -- 3. Message body -------------------------------------------------------
    DWORD totalBitsExpected = LENGTH_HEADER_BITS + messageLength * 8;
    DWORD bodyCapacity = echo_region_capacity_bits(&stego, bodyStart, p.segmentLen);
    DWORD totalBitsToExtract = totalBitsExpected;
    if (totalBitsExpected > bodyCapacity)
    {
        fprintf(stderr,
            "WARNING: Declared length exceeds available segments: recovering %u of %u bits\n",
            bodyCapacity, totalBitsExpected);
        totalBitsToExtract = bodyCapacity;
    }

    BYTE* bodyBits = (BYTE*)malloc(totalBitsToExtract ? totalBitsToExtract : 1);
    if (bodyBits == NULL)
    {
        fprintf(stderr, "Error: out of memory allocating extract buffer\n");
        wave_free(&stego); return 6;
    }
    DWORD gotBody = extract_echo_region(&stego, bodyStart, bodyBits, totalBitsToExtract,
                                         p.segmentLen, p.delayZero, p.delayOne);
    if (gotBody < totalBitsToExtract)
    {
        fprintf(stderr, "WARNING: Stego file exhausted: recovered %u of %u expected bits\n",
                gotBody, totalBitsToExtract);
    }

    DWORD messageBitsRecovered = (gotBody > LENGTH_HEADER_BITS) ? (gotBody - LENGTH_HEADER_BITS) : 0;
    DWORD payloadBytes = messageBitsRecovered / 8;   // partial trailing bits (<8) can't form a byte

    BYTE* payload = (BYTE*)malloc(payloadBytes ? payloadBytes : 1);
    if (payload == NULL)
    {
        fprintf(stderr, "Error: out of memory allocating payload buffer\n");
        free(bodyBits); wave_free(&stego); return 7;
    }
    bits_to_bytes_msb(bodyBits + LENGTH_HEADER_BITS, payloadBytes, payload);
    free(bodyBits);

    // -- 4. Write output + summary ----------------------------------------------
    warn_if_exists(outMessagePath);
    int rc = 0;
    if (write_file_all(outMessagePath, payload, payloadBytes) != SUCCESS) rc = 8;
    else
    {
        printf("\n--- Extract summary ---\n");
        printf("Output file:            %s (%u bytes)\n", outMessagePath, payloadBytes);
        printf("Version:                %u\n", p.version);
        printf("Recovered parameters:   segment_len=%u samples, d0=%u samples (%.3fms), "
               "d1=%u samples (%.3fms)\n", p.segmentLen, p.delayZero,
               1000.0 * p.delayZero / sampleRate, p.delayOne, 1000.0 * p.delayOne / sampleRate);
        printf("Declared message length: %u bytes\n", messageLength);
        printf("Bits expected/recovered: %u / %u (message bits: %u / %u)\n",
               totalBitsExpected, gotBody, messageLength * 8, messageBitsRecovered);
        printf("Status:                  %s\n",
               (gotHeader == LENGTH_HEADER_BITS && gotBody == totalBitsExpected && totalBitsExpected == totalBitsToExtract)
               ? "completed" : "stopped early -- stego file exhausted or declared length exceeded capacity");
    }

    free(payload);
    wave_free(&stego);
    return rc;
}
